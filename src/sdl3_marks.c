/*
 * File: sdl3_marks.c
 *
 * The marks: what the game *declares* and the frontend draws over the finished
 * grid.
 *
 * Damage numbers, the lunge a melee blow makes, the glyph a corpse leaves, the
 * cells of an explosion, the rule under a page's heading, the arrows that say
 * a page has more above or below it, and the portrait box. Seven things with
 * nothing in common except their shape:
 *
 *     a little state, set by one case of the semantic channel;
 *     a draw call at present time;
 *     and nothing else in the port touches either.
 *
 * That shape is why they are here. Term cannot carry any of them -- a cell
 * holds one character and one of thirty-two colours, and none of these is a
 * character -- so they are held rather than painted when the event arrives,
 * and drawn over the top after Term has finished. Nothing here ever writes
 * into the grid texture, so Term's idea of what is on screen stays true.
 *
 * Lifted out of main-sdl3.c on 7 September 2026, which was 4,586 lines. It had
 * been split once before -- sdl3_map.c, sdl3_anim.c and sdl3_look.c came out
 * of it -- and had grown back, and the review that prompted this said the
 * thing worth saying: a file that regrows after a split is a file with no rule
 * about what belongs in it. The rule for this one is the three lines above.
 *
 * Copyright (c) 2026 John Horton.
 *
 * Added since the fork and offered on the same terms as the rest of the
 * tree -- the Angband licence in LICENSE.
 */

#include "sdl3_local.h"

/**** Sprites ****/

/*
 * Sprites: short-lived things drawn on top of the grid.
 *
 * Damage numbers and melee lunges must not block the game -- a hundred
 * milliseconds stolen from every blow would make combat feel like wading. So
 * they go in this list with a birth time, drift, and lifetime, and get drawn
 * over the grid each time we present. Nothing here ever touches the grid
 * texture, so Term's idea of the screen stays correct.
 */
#define SDL3_MAX_SPRITES 24

typedef struct
{
    char   text[12];
    char   tile_name[160];
    bool   map_actor;
    float  tile_w, tile_h;
    float  x, y;         /* pixel position at birth       */
    float  vx, vy;       /* pixels per millisecond         */
    byte   attr;
    Uint64 born;
    int    life;         /* milliseconds                   */
} sdl3_sprite;

static sdl3_sprite sdl3_sprites[SDL3_MAX_SPRITES];
static int         sdl3_sprite_ct;

void sdl3_sprite_add(cptr text, float x, float y,
                            float vx, float vy, byte attr, int life)
{
    sdl3_sprite *s;

    /* Full: drop the oldest. Losing the tail of a flurry is better than
     * refusing to show the newest hit. */
    if (sdl3_sprite_ct >= SDL3_MAX_SPRITES)
    {
        int i;
        for (i = 1; i < sdl3_sprite_ct; i++) sdl3_sprites[i - 1] = sdl3_sprites[i];
        sdl3_sprite_ct--;
    }

    s = &sdl3_sprites[sdl3_sprite_ct++];

    memset(s, 0, sizeof(*s));
    my_strcpy(s->text, text, sizeof(s->text));
    s->x    = x;
    s->y    = y;
    s->vx   = vx;
    s->vy   = vy;
    s->attr = attr;
    s->born = SDL_GetTicks();
    s->life = life;
}

/* Drop expired sprites; returns how many are still alive. */
int sdl3_sprites_reap(void)
{
    Uint64 now = SDL_GetTicks();
    int i = 0;

    while (i < sdl3_sprite_ct)
    {
        if ((int)(now - sdl3_sprites[i].born) >= sdl3_sprites[i].life)
        {
            int j;
            for (j = i + 1; j < sdl3_sprite_ct; j++)
                sdl3_sprites[j - 1] = sdl3_sprites[j];
            sdl3_sprite_ct--;
        }
        else i++;
    }

    return sdl3_sprite_ct;
}


/**** The blast ****/

/*
 * The explosion currently on screen.
 *
 * The game hands us the blast one cell at a time as it expands ring by ring,
 * already clipped to walls and line of sight, and tells us when it is over.
 * We hold the cells and draw them over the grid, which means the game never
 * has to poke them into Term cells and rub them out again -- that erase pass
 * is where the old flicker came from.
 */
#define SDL3_MAX_BLAST 512

typedef struct { short x, y; byte attr; char ch; } sdl3_blast_cell;

static sdl3_blast_cell sdl3_blast[SDL3_MAX_BLAST];
static int             sdl3_blast_ct;
static Uint8           sdl3_blast_alpha = SDL_ALPHA_OPAQUE;


/**** The rule, the arrows and the portrait ****/

/*
 * The rule under the current page's heading, and whether that page has more
 * above or below what is showing.
 *
 * Both are chrome the frontend draws over the finished grid, like the panel's
 * border: the document leaves the rule's row blank and says where it is, and
 * the arrows sit on the frame, which is ours. Both are re-declared on every
 * paint and torn down with the panel.
 */
static int  sdl3_rule_x = -1, sdl3_rule_y = -1, sdl3_rule_w;
static bool sdl3_scroll_up, sdl3_scroll_down;

static bool sdl3_art_active = FALSE;
static int  sdl3_art_x, sdl3_art_y, sdl3_art_w, sdl3_art_h;
static int  sdl3_art_collection;
static byte sdl3_art_attr;
static char sdl3_art_ch;
static char sdl3_art_name[80];
static char sdl3_art_alt[80];   /* the less specific name, tried second */


/**** Drawing, at present time ****/

/*
 * The rule under a heading.
 *
 * Drawn as characters rather than as a hairline rectangle, which was the other
 * option and would have been easier. A rule is typography: it should carry the
 * typeface's own stroke weight and sit on the same baseline as the text above
 * it, and a rectangle carries neither -- it reads as a graphic laid across the
 * page. U+2500 is drawn to join across cell boundaries, so a run of them is a
 * continuous line and not a row of dashes.
 *
 * In the border colour, not the text colour: it is furniture.
 */
void sdl3_draw_rule(void)
{
    theme_color_t c;
    int           i;

    if (sdl3_rule_y < 0 || sdl3_rule_w <= 0) return;
    if (sdl3_atlas_slot_of(0x2500) < 0) return;   /* face has no such glyph */

    c = theme_ui(THEME_UI_BORDER);
    SDL_SetTextureColorMod(sdl3.atlas, c.r, c.g, c.b);

    for (i = 0; i < sdl3_rule_w; i++)
    {
        sdl3_draw_glyph_cp((float)((sdl3_rule_x + i) * sdl3.cell_w),
                           (float)(sdl3_rule_y * sdl3.cell_h), 0x2500);
    }

    SDL_SetTextureColorMod(sdl3.atlas, 255, 255, 255);
}

/*
 * There is more of this page above or below.
 *
 * On the frame rather than in the text, at its right-hand edge, because that is
 * where a reader looks for it and because the text has no room to spare. Only
 * drawn while a frame is up: without one there is nothing for them to sit on
 * and they would be two triangles floating on the dungeon.
 */
void sdl3_draw_scroll(void)
{
    theme_color_t c;
    float         x;

    if (!sdl3_panel_active) return;
    if (!sdl3_scroll_up && !sdl3_scroll_down) return;

    c = theme_ui(THEME_UI_ACCENT);
    SDL_SetTextureColorMod(sdl3.atlas, c.r, c.g, c.b);

    x = (float)((sdl3_panel_x + sdl3_panel_w - 1) * sdl3.cell_w);

    if (sdl3_scroll_up)
        sdl3_draw_glyph_cp(x, (float)(sdl3_panel_y * sdl3.cell_h), 0x25B2);

    if (sdl3_scroll_down)
    {
        sdl3_draw_glyph_cp(x,
            (float)((sdl3_panel_y + sdl3_panel_h - 1) * sdl3.cell_h), 0x25BC);
    }

    SDL_SetTextureColorMod(sdl3.atlas, 255, 255, 255);
}

/*
 * The portrait, over the finished grid.
 *
 * Two ways this can go, and both have to look deliberate, because most of the
 * six hundred races in this game have no painting and never will:
 *
 *   - There is art: every cell is a character in its own colour, laid on the
 *     grid the conversion chose. The grid is centred in the box it was given,
 *     because the conversion keeps the picture's proportions and so almost
 *     never fills a box exactly.
 *   - There is not: the monster's own letter, drawn very large in its own
 *     colour. Not a placeholder -- a giant D in dragon-red is a perfectly good
 *     answer to "what is this", and it is the answer this game has always
 *     given, just said loudly enough to be part of the page.
 */
void sdl3_draw_portrait(void)
{
    const ui_art_t *art;
    int             bw, bh;

    if (!sdl3_art_active) return;

    /* Convert the requested character-cell box to pixels before sizing art.
     * This gives recall pages the same detailed sub-tiles as the look card and map. */
    bw = sdl3_art_w * sdl3.cell_w;
    bh = sdl3_art_h * sdl3.cell_h;

    art = ui_art_get_px(sdl3_art_collection, sdl3_art_name, bw, bh);

    /* Then the general answer. A shop whose keeper has no portrait still has
     * a picture of the shop, which is what it has always shown. */
    if (!art && sdl3_art_alt[0])
        art = ui_art_get_px(sdl3_art_collection, sdl3_art_alt, bw, bh);

    /* Once per subject, not once per frame: this is drawn continuously while
     * the page is up, and a line a second is not a diagnostic. */
    if (sdl3_verbose)
    {
        static char said[80] = "";

        if (strcmp(said, sdl3_art_name))
        {
            my_strcpy(said, sdl3_art_name, sizeof(said));
            printf("portrait: collection %d, \"%s\"%s%s%s -> %s\n",
                   sdl3_art_collection, sdl3_art_name,
                   sdl3_art_alt[0] ? " (or \"" : "",
                   sdl3_art_alt[0] ? sdl3_art_alt : "",
                   sdl3_art_alt[0] ? "\")" : "",
                   art ? "drawn" : "no plate, drawing the letter");
            fflush(stdout);
        }
    }

    if (!art || art->cols <= 0 || art->rows <= 0)
    {
        /* The classic glyph, as big as the box allows. Squared off against the
         * cell aspect so the letter is not stretched into a different letter. */
        float side = (float)(sdl3_art_h * sdl3.cell_h);
        float wide = (float)(sdl3_art_w * sdl3.cell_w);
        float w, h;
        sdl3_atlas_entry *atlas;

        if (!sdl3_art_ch) return;

        h = side * 0.9f;
        w = h * (float)sdl3.cell_w / (float)sdl3.cell_h;
        if (w > wide * 0.9f)
        {
            w = wide * 0.9f;
            h = w * (float)sdl3.cell_h / (float)sdl3.cell_w;
        }

        atlas = sdl3_atlas_for(h);

        SDL_SetTextureColorMod(atlas ? atlas->tex : sdl3.atlas,
                               ormeq_color_table[sdl3_art_attr][1],
                               ormeq_color_table[sdl3_art_attr][2],
                               ormeq_color_table[sdl3_art_attr][3]);

        sdl3_draw_glyph_scaled(atlas,
            (float)(sdl3_art_x * sdl3.cell_w) + (wide - w) / 2.0f,
            (float)(sdl3_art_y * sdl3.cell_h) + (side - h) / 2.0f,
            w, h, sdl3_art_ch, ATLAS_MAP);

        SDL_SetTextureColorMod(atlas ? atlas->tex : sdl3.atlas, 255, 255, 255);
        return;
    }

    sdl3_draw_art_box(art,
                      (float)(sdl3_art_x * sdl3.cell_w),
                      (float)(sdl3_art_y * sdl3.cell_h),
                      (float)bw, (float)bh, SDL_ALPHA_OPAQUE);
}

void sdl3_draw_sprites(void)
{
    Uint64 now = SDL_GetTicks();
    int i, j;

    if (!ui_screen_map_owns()) return;

    for (i = 0; i < sdl3_sprite_ct; i++)
    {
        sdl3_sprite *s = &sdl3_sprites[i];
        float age = (float)(int)(now - s->born);
        float px  = s->x + s->vx * age;
        float py  = s->y + s->vy * age;
        float remaining = 1.0f - age / (float)s->life;
        Uint8 alpha;

        if (remaining < 0.0f) remaining = 0.0f;

        /* Hold full opacity for the first half, then fade out. */
        alpha = (Uint8)(255.0f * (remaining > 0.5f ? 1.0f : remaining * 2.0f));

        if (s->map_actor)
        {
            SDL_FRect cell = {px,py,s->tile_w,s->tile_h}, source;
            SDL_Texture *texture;
            if (sdl3_tiles_on() && ui_tiles_find("monster",s->tile_name,&texture,&source))
            {
                SDL_SetTextureColorMod(texture,255,255,255);
                SDL_SetTextureAlphaMod(texture,alpha);
                SDL_RenderTexture(sdl3.renderer,texture,&source,&cell);
                SDL_SetTextureAlphaMod(texture,255);
            }
            else
            {
                sdl3_atlas_entry *atlas = sdl3_atlas_for(sdl3_map_quad(0,0,cell.w,cell.h).h);
                texture = atlas ? atlas->tex : sdl3.atlas;
                SDL_SetTextureColorMod(texture,ormeq_color_table[s->attr][1],ormeq_color_table[s->attr][2],ormeq_color_table[s->attr][3]);
                SDL_SetTextureAlphaMod(texture,alpha);
                sdl3_draw_map_glyph(atlas,cell,s->text[0],FALSE);
                SDL_SetTextureAlphaMod(texture,255);
            }
            continue;
        }
        SDL_SetTextureColorMod(sdl3.atlas,
                               ormeq_color_table[s->attr][1],
                               ormeq_color_table[s->attr][2],
                               ormeq_color_table[s->attr][3]);
        SDL_SetTextureAlphaMod(sdl3.atlas, alpha);

        for (j = 0; s->text[j]; j++)
            sdl3_draw_glyph_at(px + (float)(j * sdl3.cell_w), py, s->text[j]);
    }

    SDL_SetTextureAlphaMod(sdl3.atlas, SDL_ALPHA_OPAQUE);
}

void sdl3_draw_blast(void)
{
    int i;

    if (!sdl3_blast_ct) return;

    SDL_SetTextureAlphaMod(sdl3.atlas, sdl3_blast_alpha);

    for (i = 0; i < sdl3_blast_ct; i++)
    {
        sdl3_blast_cell *b = &sdl3_blast[i];

        SDL_SetTextureColorMod(sdl3.atlas,
                               ormeq_color_table[b->attr][1],
                               ormeq_color_table[b->attr][2],
                               ormeq_color_table[b->attr][3]);
        {
            float bx, by;

            sdl3_cell_to_px(b->x, b->y, &bx, &by);
            sdl3_draw_glyph_at(bx, by, b->ch);
        }
    }

    SDL_SetTextureAlphaMod(sdl3.atlas, SDL_ALPHA_OPAQUE);
}


/**** What the game says ****/

/*
 * One function per event kind, and the switch in main-sdl3.c is a table of
 * which kinds this port answers.
 *
 * Each returns TRUE, which in the semantic channel means "handled, do not draw
 * it the old way" -- see ui_event.h. None of these has a fallback worth
 * declining to: the game had no picture of any of them.
 */

bool sdl3_marks_damage(const ui_event_t *e)
{
    char  buf[12];
    float tx, ty;

    /* Drift up and slightly right, so a flurry on one square fans out
     * instead of stacking into an unreadable smear. */
    sdl3_cell_to_px(e->to.x, e->to.y, &tx, &ty);
    strnfmt(buf, sizeof(buf), "%d", e->amount);
    sdl3_sprite_add(buf, tx, ty,
                    0.004f, -0.020f,
                    e->amount >= 50 ? TERM_YELLOW : TERM_WHITE,
                    750);
    return TRUE;
}

static void sprite_actor(point_t point, SDL_FRect box)
{
    sdl3_sprite *s = &sdl3_sprites[sdl3_sprite_ct-1];
    point_t cave = ui_xy_to_cave_pt(point.x,point.y);
    cptr name = ui_map_monster(cave.x,cave.y);
    s->map_actor = TRUE;
    s->x = box.x; s->y = box.y; s->tile_w = box.w; s->tile_h = box.h;
    if (player_bold(cave.y,cave.x)) s->attr = ui_player_health_attr(s->attr);
    else if (name) my_strcpy(s->tile_name,name,sizeof(s->tile_name));
}

bool sdl3_marks_bump(const ui_event_t *e)
{
    char  buf[2];
    float fx, fy, tx, ty;

    /* A lunge: the attacker's glyph, a third of the way across, gone again
     * almost immediately. */
    sdl3_cell_to_px(e->from.x, e->from.y, &fx, &fy);
    sdl3_cell_to_px(e->to.x,   e->to.y,   &tx, &ty);

    buf[0] = e->ch;
    buf[1] = '\0';

    sdl3_sprite_add(buf,
                    fx + (tx - fx) * 0.35f,
                    fy + (ty - fy) * 0.35f,
                    0.0f, 0.0f, e->attr, 110);
    {
        SDL_FRect from = sdl3_cell_rect(e->from.x,e->from.y);
        SDL_FRect to = sdl3_cell_rect(e->to.x,e->to.y);
        from.x += (to.x-from.x)*.35f; from.y += (to.y-from.y)*.35f;
        sprite_actor(e->from,from);
    }
    return TRUE;
}

bool sdl3_marks_death(const ui_event_t *e)
{
    char  buf[2];
    float tx, ty;

    buf[0] = e->ch;
    buf[1] = '\0';

    /* The corpse glyph lingers a moment and drifts down as it fades, so a kill
     * in a crowd is visible rather than just one character quietly
     * vanishing. */
    sdl3_cell_to_px(e->to.x, e->to.y, &tx, &ty);
    sdl3_sprite_add(buf, tx, ty, 0.0f, 0.006f, e->attr, 320);
    sprite_actor(e->to,sdl3_cell_rect(e->to.x,e->to.y));
    return TRUE;
}

bool sdl3_marks_blast_cell(const ui_event_t *e)
{
    if (sdl3_blast_ct < SDL3_MAX_BLAST)
    {
        sdl3_blast_cell *b = &sdl3_blast[sdl3_blast_ct++];

        b->x    = (short)e->to.x;
        b->y    = (short)e->to.y;
        b->attr = e->attr;
        b->ch   = e->ch;
    }
    sdl3_blast_alpha = SDL_ALPHA_OPAQUE;
    return TRUE;
}

bool sdl3_marks_blast_end(void)
{
    /* Fade rather than cut. The game's version blinked the whole blast out in
     * one frame, which is the flicker you notice on a big fireball. */
    const int steps = 5;
    int i;

    for (i = steps - 1; i >= 0 && sdl3_blast_ct; i--)
    {
        sdl3_blast_alpha = (Uint8)(255 * i / steps);
        sdl3_present();
        SDL_Delay(12);
    }

    sdl3_blast_ct = 0;
    sdl3_blast_alpha = SDL_ALPHA_OPAQUE;
    return TRUE;
}

bool sdl3_marks_rule(const ui_event_t *e)
{
    sdl3_rule_x = e->from.x;
    sdl3_rule_y = e->from.y;
    sdl3_rule_w = e->amount;
    return TRUE;
}

bool sdl3_marks_scroll(const ui_event_t *e)
{
    sdl3_scroll_up   = BOOL(e->to.x);
    sdl3_scroll_down = BOOL(e->to.y);
    return TRUE;
}

bool sdl3_marks_portrait(const ui_event_t *e)
{
    int x = e->from.x, y = e->from.y;
    int w = e->amount,  h = e->msec;

    /* No box, or nobody to draw: the page that asked for a portrait has gone.
     * Nothing else says so -- the screens drawn with prt() rather than as
     * documents announce nothing at all, which is how a monster's portrait
     * ended up sitting behind the list you pressed Escape back to. */
    if (w <= 0 || h <= 0 || !e->text || !e->text[0])
    {
        sdl3_art_active = FALSE;
        return TRUE;
    }

    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > sdl3.cols) w = sdl3.cols - x;
    if (y + h > sdl3.rows) h = sdl3.rows - y;
    if (w <= 0 || h <= 0) return TRUE;

    sdl3_art_x = x;
    sdl3_art_y = y;
    sdl3_art_w = w;
    sdl3_art_h = h;
    sdl3_art_collection = e->to.x;
    sdl3_art_attr = e->attr;
    sdl3_art_ch   = e->ch;
    my_strcpy(sdl3_art_name, e->text ? e->text : "", sizeof(sdl3_art_name));
    my_strcpy(sdl3_art_alt, e->text2 ? e->text2 : "", sizeof(sdl3_art_alt));
    sdl3_art_active = TRUE;

    /* Nothing to mark dirty: this is drawn over the finished grid at present
     * time, not into it, so the next present has it. */
    return TRUE;
}

/*
 * Forget the ones a menu owns.
 *
 * A page that wants a portrait or a rule says so again on every draw, the same
 * way it re-declares its clickable rows, so forgetting them when the rows go
 * costs nothing -- and paging from a monster to one without a painting cannot
 * leave the first one hanging.
 */
void sdl3_marks_forget(void)
{
    sdl3_art_active = FALSE;
    sdl3_rule_y     = -1;
}

/*
 * And the arrows with them, when the frame itself comes down.
 *
 * A menu clear does not do this: the rows going away does not mean the page
 * has, and a page re-declares its arrows on every draw. A frame coming down
 * does mean it -- there is nothing left for them to sit on.
 */
void sdl3_marks_drop(void)
{
    sdl3_marks_forget();

    sdl3_scroll_up   = FALSE;
    sdl3_scroll_down = FALSE;
}
