/*
 * File: sdl3_look.c
 *
 * Look-on-hover: point at something on the map and a card says what it is.
 *
 * The game already has a look command -- l, cycle to the target, read a line,
 * escape back out. This answers the same question by pointing at the thing,
 * and it shows exactly what that command shows. Nothing the player could not
 * already read, four keystrokes sooner.
 *
 * Three things make it a card rather than a tooltip:
 *
 *   - It fades IN and leaves at once. The answer to "what is that" stops
 *     being true the moment you point somewhere else, so there is nothing to
 *     fade out of.
 *   - It goes in the corner of the map furthest from the pointer, because a
 *     card that covers the thing it describes is worse than no card.
 *   - It draws the creature's plate at the size the card has room for, and
 *     falls back to a very large letter when nobody has painted that one yet
 *     -- the same fallback the map itself uses, at a different scale.
 *
 * sdl3_draw_hover(), which highlights the menu row under the pointer, is NOT
 * here. It shares the word and nothing else: it wants sdl3_menu[] and this
 * wants the map.
 *
 * Lifted out of main-sdl3.c. See sdl3_local.h for what the port shares.
 *
 * Copyright (c) 2026 John Horton.
 *
 * Added since the fork and offered on the same terms as the rest of the
 * tree -- the Angband licence in LICENSE.
 */

#include "sdl3_local.h"
#include <math.h>

/*
 * Look-on-hover: the dungeon grid under the pointer, and where the pointer is.
 *
 * The game has a look command -- l, then cycle targets, then read a line and
 * escape back out. This answers the same question by pointing at the thing,
 * and it shows exactly what that command shows: the creature's name and how
 * hurt it is. Nothing the player could not already read, four keystrokes
 * sooner.
 */
static int    sdl3_look_x = -1, sdl3_look_y = -1;
static float  sdl3_look_px, sdl3_look_py;
static Uint64 sdl3_look_since;


/* The full Look document is independent of Term's saved-screen/menu layers.
 * Its owner dismisses it before freeing the document. */
static ui_look_card_t *_recall;

typedef struct {
    SDL_FRect panel;
    float cw, ch, x, y;
    int rows, top;
} _recall_layout_t;

typedef struct { SDL_FRect rect; cptr label; int key; } _recall_action_t;

/* One quiet row for everyday navigation. Less-used actions remain clickable
 * under More, and all original keyboard commands remain valid throughout. */
static int _recall_actions(_recall_layout_t v, _recall_action_t actions[13], float *height)
{
    static cptr labels[]={"- Prev","+ Next","R Full","Tab More","Esc","? Help",
        "[ Up","] Down","p Player","t Target","o Position","x Objects","j Travel"};
    static const char keys[]="-+r\tq?[]ptox(";
    int i,n=0,row=0;
    float x=0,width=doc_width(_recall->doc)*v.cw;
    for(i=0;i<(_recall->more ? 13:6);++i)
    {
        int key=keys[i];
        cptr label=labels[i];
        float w;
        if(i==2 && _recall->fullscreen) label="R Card";
        if(i==3 && _recall->more) label="Tab Less";
        if(i==10 && strchr(_recall->commands,'m')) { key='m'; label="m Subjects"; }
        if(i>=8 && !strchr(_recall->commands,key) && !(key=='(' && strchr(_recall->commands,'j'))) continue;
        if(i==12 && rogue_like_commands) label="( Travel";
        w=(strlen(label)+1)*v.cw;
        if(i==6 || (x>0 && x+w>width)) { ++row; x=0; }
        actions[n].rect=(SDL_FRect){v.x+x,v.y+(v.rows+1.3f+row*1.5f)*v.ch,w,1.4f*v.ch};
        actions[n].label=label; actions[n++].key=key;
        x+=w+v.cw;
    }
    *height=(row*1.5f+1.4f)*v.ch;
    return n;
}

static _recall_layout_t _recall_layout(void)
{
    _recall_layout_t v={0};
    _recall_action_t actions[13];
    float actions_height;
    float pad=(float)sdl3.cell_w;
    int lines=doc_cursor(_recall->doc).y+1;
    float width=_recall->fullscreen ? sdl3.grid_w-2*pad : sdl3.grid_w*.46f-2*pad;
    float height=_recall->fullscreen ? sdl3.grid_h : sdl3.grid_h-3*sdl3.cell_h;
    v.cw=MIN(sdl3.cell_w*(_recall->fullscreen ? 1.25f : 1.0f),width/doc_width(_recall->doc));
    v.ch=v.cw*sdl3.cell_h/sdl3.cell_w;
    _recall_actions(v,actions,&actions_height);
    v.rows=MAX(1,(int)((height-2*pad-actions_height)/v.ch-1.3f));
    v.rows=MIN(v.rows,lines);
    v.top=MAX(0,MIN(_recall->top,lines-v.rows));
    v.panel.x=pad; v.panel.y=2.0f*sdl3.cell_h;
    v.panel.w=doc_width(_recall->doc)*v.cw+2*pad;
    v.panel.h=(v.rows+1.3f)*v.ch+actions_height+2*pad;
    v.x=v.panel.x+pad; v.y=v.panel.y+pad;
    if(_recall->fullscreen)
    {
        v.panel.x=v.panel.y=0; v.panel.w=(float)sdl3.grid_w; v.panel.h=(float)sdl3.grid_h;
        v.x=(sdl3.grid_w-doc_width(_recall->doc)*v.cw)*.5f; v.y=pad;
    }
    return v;
}

/* Kept independent of the camera, so reserving the card cannot recurse. */
float sdl3_look_reserved_width(void)
{
    _recall_layout_t v;
    if(!sdl3_look_recall_active() || _recall->fullscreen) return 0;
    v=_recall_layout();
    return v.panel.x+v.panel.w;
}

bool sdl3_look_recall(ui_look_card_t *card)
{
    if (card && !sdl3_map_showing()) return FALSE;
    if (card)
    {
        _recall = card;
        _recall_layout_t layout=_recall_layout();
        _recall->rows=layout.rows;
        _recall->top=MAX(0,MIN(_recall->top,doc_cursor(_recall->doc).y+1-layout.rows));
        if (sdl3_verbose)
        {
            _recall_action_t actions[13];
            float height;
            int i,n=_recall_actions(layout,actions,&height);
            printf("look-card %.2f %.2f %.2f %.2f %d %d %d %d %d %s\n",
                layout.panel.x,layout.panel.y,layout.panel.w,layout.panel.h,
                layout.top,layout.rows,doc_cursor(_recall->doc).y+1,
                _recall->fullscreen,_recall->more,_recall->doc->portrait.name);
            for(i=0;i<n;++i)
                printf("look-action %d %.2f %.2f %.2f %.2f\n",actions[i].key,
                    actions[i].rect.x,actions[i].rect.y,actions[i].rect.w,actions[i].rect.h);
        }
    }
    else
    {
        _recall = NULL;
        sdl3_look_x = sdl3_look_y = -1;
    }
    sdl3_hover_dirty = TRUE;
    return TRUE;
}

bool sdl3_look_recall_active(void)
{
    return _recall && sdl3_map_showing();
}

bool sdl3_look_recall_contains(float x, float y)
{
    _recall_layout_t v;
    if (!sdl3_look_recall_active()) return FALSE;
    v = _recall_layout();
    return x >= v.panel.x && x < v.panel.x + v.panel.w
        && y >= v.panel.y && y < v.panel.y + v.panel.h;
}

bool sdl3_look_recall_click(float x, float y, int button)
{
    _recall_layout_t v;
    _recall_action_t actions[13];
    float height;
    int i,n;
    if(!sdl3_look_recall_active()) return FALSE;
    v=_recall_layout();
    n=_recall_actions(v,actions,&height);
    if(button==SDL_BUTTON_LEFT) for(i=0;i<n;++i)
    {
        SDL_FRect r=actions[i].rect;
        if(x>=r.x && x<r.x+r.w && y>=r.y && y<r.y+r.h)
        { Term_keypress(actions[i].key); return TRUE; }
    }
    /* This modal browser owns input; never turn a card tap into movement. */
    return TRUE;
}

bool sdl3_look_recall_wheel(float amount)
{
    if(!sdl3_look_recall_active()) return FALSE;
    if(amount) Term_keypress(amount>0 ? '[' : ']');
    return TRUE;
}

static void _draw_recall(void)
{
    _recall_layout_t v = _recall_layout();
    sdl3_atlas_entry *atlas = sdl3_atlas_for(v.ch);
    SDL_Texture *tex = atlas ? atlas->tex : sdl3.atlas;
    theme_color_t bg = theme_ui(THEME_UI_PANEL);
    theme_color_t edge = theme_ui(THEME_UI_BORDER);
    theme_color_t muted = theme_ui(THEME_UI_MUTED);
    SDL_BlendMode blend;
    SDL_Rect old_clip, clip;
    bool clipped = SDL_RenderClipEnabled(sdl3.renderer);
    int row, col;
    sdl3_map_view map;
    SDL_FRect target;
    char footer[180];
    _recall_action_t actions[13];
    float actions_height;
    int action_count=_recall_actions(v,actions,&actions_height);
    sdl3_map_view_get(&map);
    target.x=map.origin_x+_recall->target.x*map.scale_x;
    target.y=map.origin_y+_recall->target.y*map.scale_y;
    target.w=map.scale_x; target.h=map.scale_y;
    if (v.rows < doc_cursor(_recall->doc).y+1)
        strnfmt(footer,sizeof(footer),"%s | %d-%d/%d",_recall->status,
            v.top+1,v.top+v.rows,doc_cursor(_recall->doc).y+1);
    else strnfmt(footer,sizeof(footer),"%s",_recall->status);

    SDL_GetRenderClipRect(sdl3.renderer, &old_clip);
    SDL_GetRenderDrawBlendMode(sdl3.renderer, &blend);
    SDL_SetRenderDrawBlendMode(sdl3.renderer, SDL_BLENDMODE_BLEND);
    /* Derive the marker from the creature, even after a window resize has
     * reset Term's cursor. The ordinary cursor is hidden while this is up. */
    SDL_SetRenderDrawColor(sdl3.renderer, ormeq_color_table[TERM_YELLOW][1],
        ormeq_color_table[TERM_YELLOW][2], ormeq_color_table[TERM_YELLOW][3], 255);
    SDL_RenderRect(sdl3.renderer, &target);
    SDL_SetRenderDrawColor(sdl3.renderer, bg.r, bg.g, bg.b, 255);
    SDL_RenderFillRect(sdl3.renderer, &v.panel);
    SDL_SetRenderDrawColor(sdl3.renderer, edge.r, edge.g, edge.b, 255);
    SDL_RenderRect(sdl3.renderer, &v.panel);
    SDL_SetTextureAlphaMod(tex, 255);

    clip.x = (int)v.x; clip.y = (int)v.y;
    clip.w = (int)(doc_width(_recall->doc)*v.cw);
    clip.h = (int)(v.rows*v.ch);
    SDL_SetRenderClipRect(sdl3.renderer, &clip);
    for (row = 0; row < v.rows; ++row)
    {
        doc_char_ptr cells = doc_char(_recall->doc, doc_pos_create(0, v.top + row));
        for (col = 0; col < doc_width(_recall->doc); ++col)
        {
            byte a = cells[col].a;
            if (!cells[col].c || cells[col].c == ' ') continue;
            SDL_SetTextureColorMod(tex, ormeq_color_table[a][1],
                                  ormeq_color_table[a][2], ormeq_color_table[a][3]);
            sdl3_draw_glyph_scaled(atlas, v.x + col*v.cw, v.y + row*v.ch,
                                  v.cw, v.ch, cells[col].c, ATLAS_TEXT);
        }
    }
    if (_recall->doc->portrait.live)
    {
        doc_portrait_t *p = &_recall->doc->portrait;
        float x = v.x + p->col*v.cw, y = v.y + (p->row - v.top)*v.ch;
        float w = p->cols*v.cw, h = p->rows*v.ch;
        if (y+h > v.y && y < v.y+clip.h)
        {
            const ui_art_t *art = ui_art_get_px(p->collection, p->name, (int)w, (int)h);
            if (art) sdl3_draw_art_box(art, x, y, w, h, 255);
            else
            {
                float gh = MIN(h*0.8f, w*0.8f*sdl3.cell_h/sdl3.cell_w);
                float gw = gh*sdl3.cell_w/sdl3.cell_h;
                sdl3_atlas_entry *glyph = sdl3_atlas_for(gh);
                SDL_SetTextureColorMod(glyph ? glyph->tex : sdl3.atlas,
                    ormeq_color_table[p->attr][1], ormeq_color_table[p->attr][2], ormeq_color_table[p->attr][3]);
                sdl3_draw_glyph_scaled(glyph, x+(w-gw)/2, y+(h-gh)/2, gw, gh, p->ch, ATLAS_MAP);
            }
        }
    }
    SDL_SetRenderClipRect(sdl3.renderer, clipped ? &old_clip : NULL);
    /* Reacquire: drawing a large portrait may have used the atlas cache. */
    atlas = sdl3_atlas_for(v.ch);
    tex = atlas ? atlas->tex : sdl3.atlas;
    SDL_SetTextureColorMod(tex, muted.r, muted.g, muted.b);
    SDL_SetTextureAlphaMod(tex, 255);
    for (col = 0; footer[col] && col < doc_width(_recall->doc); ++col)
        sdl3_draw_glyph_scaled(atlas, v.x+col*v.cw, v.y+v.rows*v.ch,
                              v.cw, v.ch, footer[col], ATLAS_TEXT);
    for(row=0;row<action_count;++row)
    {
        SDL_FRect button=actions[row].rect;
        cptr text=actions[row].label;
        bool key=TRUE;
        for(col=0;text[col];++col)
        {
            theme_color_t ink=theme_ui(key ? THEME_UI_ACCENT : THEME_UI_TEXT);
            if(text[col]==' ') key=FALSE;
            SDL_SetTextureColorMod(tex,ink.r,ink.g,ink.b);
            sdl3_draw_glyph_scaled(atlas,button.x+(col+.3f)*v.cw,button.y+.2f*v.ch,
                v.cw,v.ch,text[col],ATLAS_TEXT);
        }
    }
    SDL_SetRenderDrawBlendMode(sdl3.renderer, blend);
}

/*
 * How long the card takes to arrive.
 *
 * Long enough that sweeping the pointer across a room does not strobe a panel
 * at you once per square, short enough that pointing at something and waiting
 * never feels like waiting. It fades in only: on the way out it goes at once,
 * because the answer to "what is that" stops being true the moment you point
 * somewhere else.
 */
#define SDL3_LOOK_FADE_MS 110

/* Widest a figures row can be. Four columns of label and value. */
#define SDL3_LOOK_W 32


/*
 * Which dungeon grid the pointer is over, or -1.
 *
 * Inverted from the same transform the renderer used, via the same function,
 * for the same reason the click path does it: two answers to "where is that"
 * is how you describe one square and walk to another.
 */
void sdl3_look_at(float wx, float wy)
{
    sdl3_map_view v;
    int           gx, gy, lvl_w, lvl_h, ox, oy;
    float         px, py;

    /* The grid is centred in the window and everything downstream works in
     * grid coordinates, so take the origin off exactly as the click path
     * does. Leaving it on put the pointer a whole margin away from the square
     * it was over, and the card simply never appeared. */
    sdl3_grid_origin(&ox, &oy);
    px = wx - (float)ox;
    py = wy - (float)oy;

    gx = gy = -1;

    /* Hover uses the same map transform as drawing and clicking. */
    if (sdl3_map_showing() && px >= 0.0f && py >= 0.0f
        && sdl3.cell_w > 0 && sdl3.cell_h > 0
        && !sdl3_look_recall_active() && sdl3_map_input_clear(px,py))
    {
        ui_map_size(&lvl_w, &lvl_h);

        sdl3_map_view_get(&v);
        gx = (int)floorf((px - v.origin_x) / v.scale_x);
        gy = (int)floorf((py - v.origin_y) / v.scale_y);

        if (gx < 0 || gy < 0 || gx >= lvl_w || gy >= lvl_h) gx = gy = -1;
    }

    if (gx != sdl3_look_x || gy != sdl3_look_y)
    {
        sdl3_look_x = gx;
        sdl3_look_y = gy;
        sdl3_look_since = SDL_GetTicks();
        sdl3_hover_dirty = TRUE;
    }

    /* Where the pointer is, so the card can sit on the other side of the
     * screen from it. A card that covers the thing you are pointing at is
     * a card that answers a question by hiding it. */
    sdl3_look_px = px;
    sdl3_look_py = py;
}

/*
 * The surface the look card stands on.
 *
 * Heavier than sdl3_chrome_slab(), which is what the permanent strips use.
 * Those sit in the same place every frame and the eye stops seeing them; this
 * one appears because you asked a question and has to read as an answer, over
 * whatever happens to be underneath. So: near-solid, and a border to say where
 * it ends.
 */
static void sdl3_look_slab(rect_t r, float fade)
{
    theme_color_t bg = theme_ui(THEME_UI_PANEL);
    theme_color_t ed = theme_ui(THEME_UI_BORDER);
    SDL_FRect     q;
    float         pad_x = (float)sdl3.cell_w / 2.0f;
    float         pad_y = (float)sdl3.cell_h / 2.0f;

    q.x = (float)(r.x * sdl3.cell_w) - pad_x;
    q.y = (float)(r.y * sdl3.cell_h) - pad_y;
    q.w = (float)(r.cx * sdl3.cell_w) + pad_x * 2.0f;
    q.h = (float)(r.cy * sdl3.cell_h) + pad_y * 2.0f;

    SDL_SetRenderDrawBlendMode(sdl3.renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(sdl3.renderer, bg.r, bg.g, bg.b,
                           (Uint8)(244.0f * fade));
    SDL_RenderFillRect(sdl3.renderer, &q);
    SDL_SetRenderDrawColor(sdl3.renderer, ed.r, ed.g, ed.b,
                           (Uint8)(200.0f * fade));
    SDL_RenderRect(sdl3.renderer, &q);
    SDL_SetRenderDrawBlendMode(sdl3.renderer, SDL_BLENDMODE_NONE);
}

/* A line of text at a cell position, in a chrome colour, part-faded. */
static void sdl3_draw_line(int cx, int cy, cptr s, int role, float fade)
{
    theme_color_t c = theme_ui(role);
    int           i;

    if (!s) return;

    /* Alpha, not a darker colour. Scaling the colour towards black is not a
     * fade -- it is a dimmer switch, and on a panel that is already dark it
     * arrives as text you cannot read rather than text that is not there
     * yet. */
    SDL_SetTextureColorMod(sdl3.atlas, c.r, c.g, c.b);
    SDL_SetTextureAlphaMod(sdl3.atlas, (Uint8)(255.0f * fade));

    for (i = 0; s[i] && cx + i < sdl3.cols; i++)
    {
        sdl3_draw_glyph_at((float)((cx + i) * sdl3.cell_w),
                           (float)(cy * sdl3.cell_h), s[i]);
    }

    SDL_SetTextureColorMod(sdl3.atlas, 255, 255, 255);
    SDL_SetTextureAlphaMod(sdl3.atlas, 255);
}

/* Milliseconds until the fading card next wants redrawing, or -1. */
int sdl3_look_due_ms(void)
{
    Uint64 since;

    if (_recall || sdl3_look_x < 0 || sdl3_look_y < 0) return -1;

    since = SDL_GetTicks() - sdl3_look_since;
    if (since >= SDL3_LOOK_FADE_MS) return -1;      /* arrived; nothing due */

    return 16;
}

void sdl3_draw_look_card(void)
{
    ui_look_t mon;
    cptr      what;
    char      specific[64], generic[64];
    char      line[128];
    char      rows_txt[4][SDL3_LOOK_W];
    int       rows, cols, cx, cy, i, text_top, lines;
    float     side = 0.0f, fade;
    const ui_art_t *art = NULL;
    cptr            art_key = NULL;

    if (_recall)
    {
        if (sdl3_map_showing()) _draw_recall();
        return;
    }
    if (sdl3_look_x < 0 || sdl3_look_y < 0) return;
    /* Whether the map is on screen, not whether we drew it: the card belongs
     * over Term's map just as much as over ours. Asking the wrong one of
     * those two questions is what kept this card off the screen entirely in
     * ordinary play. */
    if (!sdl3_map_showing()) return;
    if (sdl3.cell_w <= 0 || sdl3.cell_h <= 0) return;

    /*
     * A creature first, then whatever is lying there. Deliberately not the
     * ground underneath: terrain is under the pointer everywhere, so naming
     * it means the card never goes away, and a card that is always on screen
     * has stopped being an answer and become wallpaper. "Patch of grass" is
     * not worth a panel.
     *
     * The cost is that stairs, doors and traps go unnamed too. If that turns
     * out to matter, the fix is a notable-terrain test here rather than
     * bringing all of it back.
     */
    ui_map_look(sdl3_look_x, sdl3_look_y, &mon);

    /*
     * Asked here rather than beside the picture because the answer names the
     * card as well as choosing what goes on it: an object the player knows is
     * captioned with its own name, and only one they do not falls back to the
     * kind. mon is empty when there is no creature, so the object's colour and
     * letter go straight into it -- the glyph fallback further down reads
     * those two fields and does not care which of the two filled them.
     */
    specific[0] = generic[0] = '\0';
    if (!mon.name)
        (void)ui_map_object_art(sdl3_look_x, sdl3_look_y,
                                specific, generic, sizeof(specific),
                                &mon.attr, &mon.ch);

    what = mon.name    ? mon.name
         : specific[0] ? specific
         : ui_map_object(sdl3_look_x, sdl3_look_y);

    /*
     * The picture, fetched here rather than beside the drawing so that the
     * verbose line below can report what came back. A diagnostic that runs
     * before the lookup can only say what was wanted.
     */
    {
        int px = (sdl3.cols * sdl3.cell_w < sdl3.rows * sdl3.cell_h)
               ? sdl3.cols * sdl3.cell_w : sdl3.rows * sdl3.cell_h;

        side = (float)(px / 3);

        if (mon.name)
        {
            art = ui_art_get_px(UI_ART_MONSTER, mon.name, (int)side, (int)side);
            if (art) art_key = mon.name;
        }
        else
        {
            /*
             * A thing on the floor gets the same card, at the same size, from
             * its own collection, under the two keys worked out above: its own
             * name if the player knows the kind, and "unidentified potion" if
             * they do not. Tried in that order, because twenty-one objects are
             * painted and five hundred and thirty-six exist -- so most of the
             * time the specific name misses and the kind is the honest answer,
             * and when both miss the letter is.
             */
            if (specific[0])
            {
                art = ui_art_get_px(UI_ART_OBJECT, specific,
                                    (int)side, (int)side);
                if (art) art_key = specific;
            }
            if (!art && generic[0])
            {
                art = ui_art_get_px(UI_ART_OBJECT, generic,
                                    (int)side, (int)side);
                if (art) art_key = generic;
            }
        }
    }

    /* Under -v, say what the pointer found, once per square. Verifying this
     * from screenshots meant guessing which corner the card went to and
     * cropping for it; a line of text says the same thing and can be read
     * by something that is not a pair of eyes. */
    if (sdl3_verbose)
    {
        static int said_x = -1, said_y = -1;

        if (sdl3_look_x != said_x || sdl3_look_y != said_y)
        {
            int plx, ply;

            said_x = sdl3_look_x;
            said_y = sdl3_look_y;
            /* The player's own square too. Without it a coordinate is only
             * plausible; with it the transform can be checked, because
             * pointing at the @ has exactly one right answer. */
            ui_map_player(&plx, &ply);

            /* And where a nameable thing actually is, so a test can aim at
             * one instead of sweeping and hoping. Sweeping and hoping is how
             * this card came to be shipped without ever being seen. */
            {
                int m, shown = 0;

                for (m = 1; m < m_max && shown < 3; m++)
                {
                    monster_type *mp = &m_list[m];

                    if (!mp->r_idx || !mp->ml) continue;
                    printf("      visible: %s at %d,%d\n",
                           r_name + r_info[mp->ap_r_idx].name, mp->fx, mp->fy);
                    shown++;
                }
                if (!shown) printf("      visible: none\n");
            }
            printf("look: %d,%d (player %d,%d) -> %s%s%s\n",
                   sdl3_look_x, sdl3_look_y, plx, ply,
                   what ? what : "(nothing named)",
                   mon.health ? ", " : "", mon.health ? mon.health : "");

            /*
             * And the picture. Whether a plate was found, under which key,
             * and how big a box was asked for -- because the box is worked
             * out from the window, and the only difference between a card
             * that shows a picture here and one that does not on somebody
             * else's screen may well be how large that window is.
             */
            if (art)
                printf("      art: \"%s\" -> %dx%d cells in a %d px box\n",
                       art_key ? art_key : "?", art->cols, art->rows,
                       (int)side);
            else if (mon.name)
                printf("      art: none for \"%s\" in a %d px box\n",
                       mon.name, (int)side);
            else if (specific[0] || generic[0])
                printf("      art: none for \"%s\" or \"%s\" in a %d px box\n",
                       specific, generic, (int)side);

            fflush(stdout);
        }
    }

    if (!what) return;

    /*
     * The picture: this creature's plate, or its letter at the same size.
     *
     * The letter is not a placeholder for a missing plate -- it is what this
     * game is drawn in, and forty-one creatures out of fourteen hundred have
     * a plate. A card that shrinks to two words when there is no picture
     * looks like something went wrong; the same card with a great green w in
     * it looks like the answer. Same size either way, so the card does not
     * change shape as the pointer crosses the room.
     */
    /*
     * The figures, laid out as the recall page lays them out.
     *
     * Two columns, because six one-per-line rows make a card taller than the
     * picture above it and the whole thing stops reading as a card. Much of
     * this says "?" until the player has killed one of these, and that is not
     * a gap to be filled in later -- a row of question marks is the game
     * telling you that you do not know yet, which is worth seeing.
     */
    lines = 0;
    if (mon.name)
    {
        strnfmt(rows_txt[lines++], SDL3_LOOK_W, "%-8s%-9s%-6s%s",
                "Level", mon.level, "AC", mon.ac);
        strnfmt(rows_txt[lines++], SDL3_LOOK_W, "%-8s%-9s%-6s%s",
                "Speed", mon.speed, "HP", mon.hp);
        strnfmt(rows_txt[lines++], SDL3_LOOK_W, "%-8s%-9s%-6s%d",
                "Attacks", mon.attacks, "Kills", mon.kills);
        strnfmt(rows_txt[lines++], SDL3_LOOK_W, "%-8s%s", "Exp", mon.exp);
    }

    strnfmt(line, sizeof(line), "%s", what);

    /* Size the card round the widest line and the picture above them. */
    cols = (int)strlen(line);
    if (mon.health && (int)strlen(mon.health) > cols)
        cols = (int)strlen(mon.health);
    for (i = 0; i < lines; i++)
        if ((int)strlen(rows_txt[i]) > cols) cols = (int)strlen(rows_txt[i]);
    if (side > 0.0f)
    {
        int art_cols = (int)(side / (float)sdl3.cell_w) + 1;
        if (art_cols > cols) cols = art_cols;
    }
    cols += 2;                                    /* a space either side */

    rows = 1 + (mon.health ? 1 : 0) + (lines ? lines + 1 : 0) + 1;
    if (side > 0.0f) rows += (int)(side / (float)sdl3.cell_h) + 1;

    /*
     * Inside the map, in the corner furthest from the pointer.
     *
     * The map's own rectangle rather than the window's, because the window
     * also holds the message line, the sidebar and the status bar, and a card
     * that lands on the messages is a card sitting on the one part of the
     * screen that was already saying something.
     */
    {
        rect_t m = ui_map_rect();

        if (m.cx <= 0 || m.cy <= 0) return;
        if (cols > m.cx - 2 || rows > m.cy - 2) return;

        cx = (sdl3_look_px > (float)((m.x + m.cx / 2) * sdl3.cell_w))
           ? m.x + 1 : m.x + m.cx - cols - 1;
        cy = (sdl3_look_py > (float)((m.y + m.cy / 2) * sdl3.cell_h))
           ? m.y + 1 : m.y + m.cy - rows - 1;
    }

    /* How far in it is. Everything below is drawn through this. */
    {
        Uint64 age = SDL_GetTicks() - sdl3_look_since;

        fade = (age >= SDL3_LOOK_FADE_MS)
             ? 1.0f : (float)age / (float)SDL3_LOOK_FADE_MS;
    }

    sdl3_look_slab(rect(cx, cy, cols, rows), fade);

    text_top = cy;
    if (side > 0.0f)
    {
        float ox = (float)(cx * sdl3.cell_w)
                 + ((float)(cols * sdl3.cell_w) - side) / 2.0f;
        float oy = (float)(cy * sdl3.cell_h);

        if (art)
        {
            sdl3_draw_art_rect(art, ox, oy, side, (Uint8)(255.0f * fade));
        }
        else
        {
            /* No plate for this one: its own letter, filling the same space,
             * in its own colour. The glyph keeps the cell's proportions --
             * stretching a letter to a square is the one thing that would
             * make it look like a mistake rather than a choice. */
            float h = side;
            float w = h * (float)sdl3.cell_w / (float)sdl3.cell_h;
            const sdl3_atlas_entry *atlas = sdl3_atlas_for(h);
            SDL_Texture *tex = atlas ? atlas->tex : sdl3.atlas;

            SDL_SetTextureColorMod(tex,
                                   ormeq_color_table[mon.attr][1],
                                   ormeq_color_table[mon.attr][2],
                                   ormeq_color_table[mon.attr][3]);
            SDL_SetTextureAlphaMod(tex, (Uint8)(255.0f * fade));

            sdl3_draw_glyph_scaled(atlas, ox + (side - w) / 2.0f, oy,
                                   w, h, mon.ch, ATLAS_MAP);

            SDL_SetTextureColorMod(tex, 255, 255, 255);
            SDL_SetTextureAlphaMod(tex, 255);
        }

        text_top += (int)(side / (float)sdl3.cell_h) + 1;
    }

    /*
     * The bare name, with no article.
     *
     * "a Green jelly" is right in a sentence, which is where the look command
     * puts it. A card is a label, and a label that starts with an article
     * reads as a fragment of something else -- and capitalising the article
     * to fix that gives "A Green jelly", which is worse than either.
     */
    i = cx + (cols - (int)strlen(line)) / 2;
    sdl3_draw_line(i < cx ? cx : i, text_top, line, THEME_UI_TITLE, fade);
    text_top++;

    if (mon.health)
    {
        i = cx + (cols - (int)strlen(mon.health)) / 2;
        sdl3_draw_line(i < cx ? cx : i, text_top, mon.health,
                       THEME_UI_MUTED, fade);
        text_top++;
    }

    /* The figures block, left-aligned as a table wants to be, and inset by
     * the same space the card is padded with. */
    if (lines)
    {
        text_top++;
        for (i = 0; i < lines; i++)
            sdl3_draw_line(cx + 1, text_top + i, rows_txt[i],
                           THEME_UI_TEXT, fade);
    }

    /*
     * Still arriving: ask for another frame.
     *
     * Without this the last frame drawn is whichever one the fade clock
     * happened to stop on, and the card sits there at two thirds opacity for
     * as long as the pointer does not move -- which reads as a card that is
     * simply too dark, not as a fade that did not finish.
     */
    if (fade < 1.0f) sdl3_hover_dirty = TRUE;
}
