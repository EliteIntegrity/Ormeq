/*
 * File: sdl3_map.c
 *
 * The SDL3 frontend's map layer: the dungeon drawn at a scale of the
 * player's choosing, and the arithmetic that says where a pixel is.
 *
 * Those two things live in one file because they have to agree exactly. A
 * map drawn with one transform and clicked with another is a map that lies
 * about where things are, and the bug looks like bad aim rather than bad
 * arithmetic. sdl3_map_view_get() is the only transform; the drawing and the
 * mouse both go through it, in both directions:
 *
 *     cave grid -> pixel:  origin + grid * scale
 *     pixel -> cave grid:  (pixel - origin) / scale
 *
 * All SDL3 map modes use this transform, including rectangular ASCII at 1x.
 * The camera keeps the player or browsing focus clear of the fixed HUD;
 * rendering chooses glyphs, tiles or portraits at the requested scale.
 *
 * Lifted out of main-sdl3.c, which was 6,452 lines. See sdl3_local.h for
 * what the port shares and what it deliberately does not.
 *
 * Copyright (c) 2026 John Horton.
 *
 * Added since the fork and offered on the same terms as the rest of the
 * tree -- the Angband licence in LICENSE.
 */

#include "sdl3_local.h"

/* Ammerow's warning style: a narrow, static fade at the window edge.
 * Ormeq uses its own health and status model; no flashing or extra timer. */
void sdl3_draw_edge_warning(void)
{
    ui_player_warning_t warning;
    SDL_Rect viewport;
    SDL_BlendMode blend;
    Uint8 old_r, old_g, old_b, old_a;
    Uint8 r = 220, g = 40, b = 40;
    int w, h, band, i;

    if (!display_edge_warnings_get() || !ui_screen_map_owns()) return;
    warning = ui_player_warning();
    if (warning == UI_WARNING_NONE) return;
    switch (warning)
    {
    case UI_WARNING_POISON:   r = 80;  g = 190; b = 65;  break;
    case UI_WARNING_BLEEDING: r = 220; g = 75;  b = 40;  break;
    case UI_WARNING_IMPAIRED: r = 170; g = 95;  b = 220; break;
    case UI_WARNING_HUNGER:   r = 190; g = 110; b = 35;  break;
    default: break;
    }
    if (!SDL_GetCurrentRenderOutputSize(sdl3.renderer, &w, &h)) return;
    band = MIN(40, MIN(w, h) / 28);
    if (band < 1) return;

    SDL_GetRenderViewport(sdl3.renderer, &viewport);
    SDL_GetRenderDrawBlendMode(sdl3.renderer, &blend);
    SDL_GetRenderDrawColor(sdl3.renderer, &old_r, &old_g, &old_b, &old_a);
    SDL_SetRenderViewport(sdl3.renderer, NULL);
    SDL_SetRenderDrawBlendMode(sdl3.renderer, SDL_BLENDMODE_BLEND);
    for (i = 0; i < band; ++i)
    {
        SDL_FRect edges[4] = {
            {(float)i, (float)i, (float)(w - 2*i), 1.0f},
            {(float)i, (float)(h - i - 1), (float)(w - 2*i), 1.0f},
            {(float)i, (float)(i + 1), 1.0f, (float)(h - 2*i - 2)},
            {(float)(w - i - 1), (float)(i + 1), 1.0f, (float)(h - 2*i - 2)}
        };
        Uint8 alpha = (Uint8)(64 * (band-i) * (band-i) / (band*band));
        SDL_SetRenderDrawColor(sdl3.renderer, r, g, b, alpha);
        SDL_RenderFillRects(sdl3.renderer, edges, 4);
    }
    SDL_SetRenderDrawColor(sdl3.renderer, old_r, old_g, old_b, old_a);
    SDL_SetRenderDrawBlendMode(sdl3.renderer, blend);
    SDL_SetRenderViewport(sdl3.renderer, &viewport);
}

bool sdl3_map_showing(void)
{
    /* The game models this now -- see ui_screen.h. The doc flag stays as a
     * belt-and-braces check while the layers are retrofitted, since eight
     * files still take the screen with a bare Term_save().
     *
     * character_icky used to be ANDed on here as well, and that was the wrong
     * place for it: it is a rule about whether the map is on screen, not about
     * how this frontend draws, and keeping it here meant the frontend knew
     * something ui_screen_map_owns() did not. It is in there now. */
    return BOOL(ui_screen_map_owns() && !sdl3_doc_covered);
}

/*
 * The map is drawn by us, always.
 *
 * It used to be drawn by us only when zoomed, and by Term at zoom one --
 * which was fine while a tile and a terminal cell were the same thing. They
 * are not any more: a tile is square. Term can only ever put one character in
 * one cell, so the moment the map stopped being a grid of cells it stopped
 * being something Term could draw.
 *
 * A function rather than nothing at all, because the three places that care
 * -- the drawing, the look cursor, and what a click means -- have to agree,
 * and because it is the honest place to put the reason.
 */
bool sdl3_map_is_ours(void)
{
    /* A single camera keeps drawing and input aligned in every map mode. */
    return TRUE;
}

/*
 * Whether the level is being drawn behind the terminal rather than into it.
 *
 * When this is true the map fills the whole window and the grid is composited
 * over it: transparent everywhere the level shows, and a translucent ground
 * under the three strips of chrome. That is the arrangement the rest of this
 * file means by "overlay".
 */
bool sdl3_map_overlay(void)
{
    return BOOL(sdl3_map_is_ours() && sdl3_map_showing());
}

/*
 * Where a terminal cell actually is on the screen.
 *
 * Everything the game says about a *position* -- who hit whom, where a bolt
 * flew, which square took how much damage -- it says in terminal cells,
 * because that is the only language it has. It used to be enough to multiply
 * by the cell size, and for anything that is genuinely text it still is.
 *
 * But a square on the map is not at its terminal cell any more. The map is
 * ours, drawn on its own square tiles at its own origin, and the two disagree
 * by a couple of hundred pixels: the terminal centres the level in the hole
 * between the panels, we centre it in the window. So a damage number appeared
 * a long way from the blow, and the lunge that goes with a melee hit drew the
 * attacker's own glyph out there with it -- which is the "ghost @".
 */
SDL_FRect sdl3_cell_rect(int cx, int cy)
{
    SDL_FRect r;

    if (sdl3_map_overlay() && sdl3_in_map(cx, cy))
    {
        sdl3_map_view v;
        point_t       cp = ui_xy_to_cave_pt(cx, cy);

        sdl3_map_view_get(&v);

        r.x = v.origin_x + cp.x * v.scale_x;
        r.y = v.origin_y + cp.y * v.scale_y;
        r.w = v.scale_x;
        r.h = v.scale_y;
        return r;
    }

    r.x = (float)(cx * sdl3.cell_w);
    r.y = (float)(cy * sdl3.cell_h);
    r.w = (float)sdl3.cell_w;
    r.h = (float)sdl3.cell_h;
    return r;
}

/* And the same thing again, as the corner to hang a cell-sized *glyph* off:
 * the middle of the tile, because a tile is twice the width of a cell and a
 * number pinned to the left edge of one leans towards the square next door. */
void sdl3_cell_to_px(int cx, int cy, float *px, float *py)
{
    SDL_FRect r = sdl3_cell_rect(cx, cy);

    *px = r.x + (r.w - (float)sdl3.cell_w) * 0.5f;
    *py = r.y + (r.h - (float)sdl3.cell_h) * 0.5f;
}

/*
 * How far out the wheel is allowed to go.
 *
 * **As far as the player likes, until a grid stops being anything.**
 *
 * This used to stop at the point where the level exactly filled the screen,
 * on the reasoning that "a dungeon has no outside, and dragging the world
 * into a smaller stamp in the middle of a black field is a worse picture of
 * it, not a wider one". That is an opinion about how somebody should want to
 * look at their own map, and it was wrong twice over: it also measured the
 * *height* alone, which in square mode -- what tile mode runs in -- left the
 * floor at exactly twice the value it should have had. On a measured 1144x714
 * window with a 198x66 level it stopped at 0.515 where fitting the width
 * needed 0.259. You could zoom until the top and bottom touched the frame and
 * still not see the ends.
 *
 * So the fit is not the limit any more. The limit is the point where a grid
 * has shrunk to a couple of pixels and the map has stopped carrying
 * information -- which is the only "silly" there actually is. Everything
 * between that and fitting is the player's business.
 *
 * Expressed in pixels rather than as a zoom number so it holds whatever the
 * typeface, the density or the square-cells setting are doing to the cell.
 */
float sdl3_zoom_floor(void)
{
    int   lvl_w, lvl_h;
    float sx, sy, view_w, view_h, fit;

    if (!sdl3_map_is_ours()) return SDL3_ZOOM_MIN;

    ui_map_size(&lvl_w, &lvl_h);
    if (lvl_w <= 0 || lvl_h <= 0) return SDL3_ZOOM_MIN;

    /*
     * The scale one unit of zoom buys, on each axis.
     *
     * The *unsnapped* tile, deliberately. sdl3_tile_px() rounds to a whole
     * number of tile pixels when magnifying, and it does that by consulting
     * the current zoom -- so asking it here, while working out what the zoom
     * should be, is a circle. It answered with the scale for the zoom we
     * already had rather than the one being computed, and the fit came out
     * wrong: a world map a hundred and ninety-eight grids across was
     * declared to fit in eleven hundred pixels when it needed eighteen.
     *
     * Fitting a whole level on screen is always minification, and the
     * snapping does not apply there anyway.
     */
    if (display_square_get())
    {
        sx = (float)(sdl3.cell_h > 0 ? sdl3.cell_h : 1);
        sy = sx;
    }
    else
    {
        sx = (float)sdl3.cell_w;
        sy = (float)sdl3.cell_h;
    }

    view_w = (float)(sdl3.cols * sdl3.cell_w);
    view_h = (float)(sdl3.rows * sdl3.cell_h);

    if (sx <= 0.0f || sy <= 0.0f) return SDL3_ZOOM_MIN;

    /*
     * A grid must stay at least SDL3_MIN_GRID_PX across on its narrower axis.
     * Below that the map is a texture rather than a plan and there is nothing
     * left to read; above it, how far out to go is the player's choice.
     */
    fit = (float)SDL3_MIN_GRID_PX / ((sx < sy) ? sx : sy);

    if (sdl3_verbose)
    {
        printf("SDL3: zoom floor %.3f (%d px per grid) for a %dx%d level in "
               "%.0fx%.0f px -- it would *fit* at %.3f\n",
               fit, SDL3_MIN_GRID_PX, lvl_w, lvl_h, view_w, view_h,
               0.94f * MIN(view_w / ((float)lvl_w * sx),
                           view_h / ((float)lvl_h * sy)));
        fflush(stdout);
    }

    if (fit < SDL3_ZOOM_MIN) fit = SDL3_ZOOM_MIN;

    /* Never a floor that would force a zoom *in*: a level smaller than the
     * window is already entirely visible and the wheel should still work. */
    if (fit > 1.0f) fit = 1.0f;

    return fit;
}

/*
 * One grid of the map, drawn as tiles.
 *
 * The ground first and whatever is standing on it second, which is the
 * distinction Term_pict has drawn between cp and tcp since before any of this
 * was written: a creature tile is mostly transparent and needs a floor behind
 * it, and the floor is not what ui_map_cell() reports once something walks
 * onto it.
 *
 * Anything without a tile falls through to its character, and that is the
 * ordinary case rather than the failure case -- there are 1395 creatures and
 * there will never be 1395 tiles. FALSE means nothing was drawn at all and
 * the caller should do what it always did.
 */
/*
 * How far to knock a grid back because it is remembered rather than seen.
 * 255 means the player can see it now.
 *
 * **One place, because three paths draw the map and all three have to agree.**
 * The glyphs, the solid blocks at low zoom, and the tiles are three different
 * ways of putting the same grid on screen, and until 7 September 2026 only the
 * tiles dimmed at all -- and even they did it by a ratio that was silently
 * wrong for half the palette. Everything the player had ever walked past
 * stayed at full brightness for the rest of the game.
 */
/* -v only: how many grids the last frame drew seen vs remembered. The
 * question "is the dimming firing at all" wants a number, not a lucky
 * screenshot -- six attempts at one is what prompted these. */
static long _dim_seen, _dim_remembered;

int sdl3_seen_dim(int cave_x, int cave_y)
{
    /* ...except on the world map, which is a map and not a view. Knocking
     * back the parts of a country you are not currently looking at is not a
     * lighting effect. The game decides this, not us: see ui_map_is_world()
     * and the note above ui_map_cell(). */
    if (ui_map_is_world()) return 255;

    if (ui_map_seen(cave_x, cave_y)) return 255;
    return 255 * SDL3_DIM_REMEMBERED_PCT / 100;
}


/* White snow and bright acid must not swallow @ or an unpainted creature.
 * Square glyphs already have a one-pixel inset, keeping this outline within
 * the cell. It carries no extra information and keeps health colours intact. */
static void sdl3_tile_glyph_outline(const sdl3_atlas_entry *atlas, SDL_FRect cell, char ch)
{
    int dx, dy;
    SDL_SetTextureColorMod(atlas ? atlas->tex : sdl3.atlas,0,0,0);
    for (dy = -1; dy <= 1; ++dy) for (dx = -1; dx <= 1; ++dx)
    {
        SDL_FRect shifted = cell;
        if (!dx && !dy) continue;
        shifted.x += dx; shifted.y += dy;
        sdl3_draw_map_glyph(atlas,shifted,ch,FALSE);
    }
}

static bool sdl3_draw_grid_tiles(int cave_x, int cave_y,
                                 float px, float py, float w, float h,
                                 byte attr, char ch, const byte fore[3], bool *ground_drawn)
{
    SDL_Texture *tex;
    SDL_FRect    src, dst;
    cptr         name, known_name;
    byte         ta, tlit;
    char         tc;
    bool         drew = FALSE, underlay = FALSE;
    int          dim = 255;

    dst.x = px;
    dst.y = py;
    dst.w = w;
    dst.h = h;

    /**** the ground ****/
    name = ui_map_terrain(cave_x, cave_y, &ta, &tc, &tlit);
    known_name = name;

    /*
     * How dark this square is, as one number.
     *
     * A letter carries the lighting in its colour, so the character path gets
     * this for free and never had to think about it. A tile is drawn in its
     * own colours and would look identical in a corridor you cannot see into,
     * which is a map that has quietly stopped answering the only question it
     * is really being asked. So: compare the brightness the game chose for
     * this ground against the brightness it would choose with the light on,
     * and scale the tile by the ratio.
     *
     * Scaled, not tinted -- the same factor on all three channels. The tile
     * keeps its own colours; it only sits further back in the dark.
     *
     * **The ratio alone was not enough, and it failed silently.** cave.c maps
     * each colour to a darkened form through `lighting_colours`, and for red,
     * green and blue that form is *the same colour*. Those grids came back at
     * a ratio of exactly one and were drawn at full brightness whether you
     * could see them or not, so a level you had walked once stayed lit for
     * the rest of the game -- everything you had ever seen, permanently in
     * view, with only the floors showing any difference at all because white
     * is one of the colours that does darken.
     *
     * So the question is asked directly now. A grid you cannot currently see
     * is dimmed by a fixed amount, whatever colour the game chose for it; the
     * ratio still applies on top, so a torch-lit grid still reads brighter
     * than an ambient one.
     */
    if (name)
    {
        int now = sdl3_luma(ta);
        int max = sdl3_luma(tlit);

        int seen = sdl3_seen_dim(cave_x, cave_y);

        if (max > 0 && now < max)
            dim = 255 * now / max;

        if (dim > seen) dim = seen;

        /* Never quite black: it is remembered, and remembered is not gone.
         * Tied to the remembered level rather than fixed, because a fixed 60
         * sat directly under the 63 that 25% works out to and flattened the
         * whole range the ratio was supposed to move through. */
        {
            int least = 255 * SDL3_DIM_REMEMBERED_PCT / 100 * 2 / 3;

            if (least < 24) least = 24;
            if (dim < least) dim = least;
        }
    }

    if (display_map_mode_get() == DISPLAY_MAP_FULL && name)
    {
        cptr edges[4];
        bool vary = ui_map_terrain_edges(cave_x,cave_y,name,edges);
        unsigned links = ui_map_terrain_links(cave_x,cave_y,name);
        cptr ground = ui_map_terrain_ground(name);
        if (ground && !(display_ascii_floors_get() && ui_map_plain_floor(ground)))
            underlay = ui_tiles_draw_terrain(ground,dst,dim,
                ui_map_terrain_seed(cave_x,cave_y),TRUE,NULL,0);
        name = ui_map_terrain_tile(cave_x,cave_y,name);
        if (!(display_ascii_floors_get() && ui_map_plain_floor(name)))
            drew = ui_tiles_draw_terrain(name,dst,dim,
                ui_map_terrain_seed(cave_x,cave_y),vary,edges,links);
    }
    /* A background alone must never suppress an unpainted feature's glyph. */
    *ground_drawn = drew || underlay;

    /* Draw one top occupant. Never infer occupancy from matching glyphs. */
    if (player_bold(cave_y, cave_x)) return FALSE;
    name = ui_map_monster(cave_x, cave_y);
    if (name)
    {
        if (!ui_tiles_find("monster", name, &tex, &src)) return FALSE;
        SDL_SetTextureColorMod(tex, 255, 255, 255);
        SDL_RenderTexture(sdl3.renderer, tex, &src, &dst);
        return TRUE;
    }
    if (ui_map_monster_present(cave_x,cave_y)) return FALSE;
    {
        char specific[128], generic[128];
        if (ui_map_object_art(cave_x,cave_y,specific,generic,sizeof(specific),NULL,NULL))
        {
            if (!ui_tiles_find("object",specific,&tex,&src) &&
                !ui_tiles_find("object",generic,&tex,&src)) return FALSE;
            SDL_SetTextureColorMod(tex, (Uint8)dim, (Uint8)dim, (Uint8)dim);
            SDL_RenderTexture(sdl3.renderer, tex, &src, &dst);
            return TRUE;
        }
    }
    if (ui_map_occupied(cave_x,cave_y)) return FALSE;
    if (drew && ch == tc && attr == ta)
    {
        cptr emblem = ui_map_terrain_emblem(known_name);
        /* Decorations belong to bare terrain only: never paint a number or
         * trap mark over the player, a creature, or a found item. */
        if (emblem && ui_tiles_find("object",emblem,&tex,&src))
        {
            SDL_FRect sign = {px+w*.18f,py+h*.28f,w*.56f,h*.58f};
            SDL_SetTextureColorMod(tex,(Uint8)dim,(Uint8)dim,(Uint8)dim);
            SDL_RenderTexture(sdl3.renderer,tex,&src,&sign);
        }
        if (ui_map_terrain_label(known_name))
        {
            SDL_FRect label = {px+w*.45f,py+h*.35f,w*.55f,h*.65f};
            const sdl3_atlas_entry *atlas;
            /* At small zoom use the normal full-size symbol over the art.
             * The label keeps exactly the glyph and colour already visible. */
            if (w < 24 || h < 24) return FALSE;
            atlas = sdl3_atlas_for(label.h);
            sdl3_tile_glyph_outline(atlas,label,tc);
            SDL_SetTextureColorMod(atlas ? atlas->tex : sdl3.atlas,fore[0],fore[1],fore[2]);
            sdl3_draw_map_glyph(atlas,label,tc,FALSE);
        }
        return TRUE;
    }

    return FALSE;
}

/*
 * One grid of the zoomed-in map, drawn as a portrait of what is on it --
 * whatever is standing there, or failing that whatever is lying there.
 *
 * TRUE if it drew something, FALSE to let the caller draw the letter as usual
 * -- which is what happens for an empty grid, for a creature with no plate, and
 * for anything the player cannot properly see.
 *
 * The characters are sized to the grid rather than to the terminal's cell:
 * this is a picture squeezed into a rectangle, and the rectangle is whatever
 * the zoom has made of one square of dungeon floor.
 */
bool sdl3_draw_grid_portrait(int cave_x, int cave_y,
                                    float px, float py, float w, float h)
{
    const ui_art_t         *art  = NULL;
    const sdl3_atlas_entry *atlas;
    cptr                    name = ui_map_monster(cave_x, cave_y);
    float                   cw, ch, ox, oy;
    int                     row, col;

    if (name)
        art = ui_art_get_px(UI_ART_MONSTER, name, (int)w, (int)h);
    else
    {
        /*
         * Then whatever is lying there, under the same two keys the look card
         * uses and in the same order -- its own name if the player knows the
         * kind, its kind if not. The map and the card should never disagree
         * about which picture belongs to a thing.
         */
        char specific[64], generic[64];

        if (ui_map_object_art(cave_x, cave_y, specific, generic,
                              sizeof(specific), NULL, NULL))
        {
            if (specific[0])
                art = ui_art_get_px(UI_ART_OBJECT, specific, (int)w, (int)h);
            if (!art && generic[0])
                art = ui_art_get_px(UI_ART_OBJECT, generic, (int)w, (int)h);
        }
    }

    if (!art || art->cols <= 0 || art->rows <= 0) return FALSE;

    /*
     * The picture is made of square sub-tiles.
     *
     * Filling the rectangle exactly was the first version and it stretched
     * every character in the picture. Drawing each cell at the *glyph's* shape
     * was the second, and left the picture a narrow column, because a glyph
     * cell is twice as tall as it is wide and a portrait is thirty of them
     * across. Square sub-tiles are the same answer as square map tiles, for
     * the same reason: it is the geometry that should be honest, and the
     * glyph that should bend to it.
     */
    cw = w / (float)art->cols;
    ch = h / (float)art->rows;

    if (display_square_get())
    {
        /* Square, fitted to the tighter axis. */
        if (ch < cw) cw = ch; else ch = cw;
    }
    else
    {
        /* The glyph's own shape, letterboxed. */
        ch = cw * (float)sdl3.cell_h / (float)sdl3.cell_w;
        if (ch * (float)art->rows > h)
        {
            ch = h / (float)art->rows;
            cw = ch * (float)sdl3.cell_w / (float)sdl3.cell_h;
        }
    }

    ox = px + (w - cw * (float)art->cols) / 2.0f;
    oy = py + (h - ch * (float)art->rows) / 2.0f;

    /*
     * A baking of the face at roughly the size these cells will be drawn.
     *
     * The base atlas is sampled nearest-neighbour, which is right for text --
     * a hard edge at one texel to one pixel. Squeezing a twenty-by-forty cell
     * into a five-pixel square that way keeps one texel in forty and throws
     * the rest away, so a cell that should have been half-inked came out
     * either solid or empty at random and the picture crawled with noise. The
     * ladder's bakings are sampled smoothly, and a smooth average of a cell is
     * exactly the quantity a portrait cell is meant to carry: how much of it
     * is ink. One lookup for the whole picture -- every cell is the same size.
     */
    atlas = sdl3_atlas_for(ch);

    /*
     * A dim solid under every cell, in that cell's own colour.
     *
     * This is the terminal's background attribute, and it is what gives a
     * small picture its mass. A glyph five pixels across inks perhaps a third
     * of its cell however heavy it is, so a picture made only of glyphs is two
     * thirds black however bright its colours -- the creature reads as a
     * smudge of embers rather than a body. Half-strength under-fill plus the
     * glyph at full strength keeps the texture of the characters, which is the
     * whole point of the thing, while letting each cell actually show what
     * colour it is.
     *
     * Its own pass, before the glyphs: an under-fill drawn immediately before
     * its glyph would have the next cell's fill painted over the last one's
     * ink wherever the rectangles round onto each other.
     */
    for (row = 0; row < art->rows; row++)
    {
        for (col = 0; col < art->cols; col++)
        {
            const ui_art_cell *c = &art->cells[row * art->cols + col];
            SDL_FRect          q;

            if (!c->ink) continue;

            q.x = ox + col * cw;
            q.y = oy + row * ch;
            q.w = cw;
            q.h = ch;

            SDL_SetRenderDrawColor(sdl3.renderer, (Uint8)(c->r / 2),
                                   (Uint8)(c->g / 2), (Uint8)(c->b / 2),
                                   SDL_ALPHA_OPAQUE);
            SDL_RenderFillRect(sdl3.renderer, &q);
        }
    }

    for (row = 0; row < art->rows; row++)
    {
        for (col = 0; col < art->cols; col++)
        {
            const ui_art_cell *c = &art->cells[row * art->cols + col];
            int                slot;

            if (!c->ink) continue;

            slot = sdl3_atlas_slot_of(c->cp);
            if (slot < 0) continue;

            /*
             * The sub-tile is filled, not fitted.
             *
             * On the map proper a glyph is scaled to its ink so that nothing
             * leaves its tile and walls keep their gaps. Here the opposite is
             * wanted. A sub-tile is five or six pixels across; no glyph is
             * legible as itself at that size, and what carries the picture is
             * how much of the tile is inked and what colour it is. Running it
             * through the map's fit shrank every cell to little over half the
             * width of its tile, so a solid flank came out as a row of
             * stripes with the background showing between them -- detail
             * thrown away in the name of a rule that belongs to lettering.
             */
            SDL_SetTextureColorMod(atlas ? atlas->tex : sdl3.atlas,
                                   c->r, c->g, c->b);
            sdl3_draw_slot_scaled(atlas, slot, ox + col * cw, oy + row * ch,
                                  cw, ch, ATLAS_MAP);
        }
    }

    SDL_SetTextureColorMod(atlas ? atlas->tex : sdl3.atlas, 255, 255, 255);

    return TRUE;
}

/* Estimate the atlas size needed for square-map ink. Actual glyph placement
 * uses its individual ink bounds in sdl3_draw_map_glyph(). */
SDL_FRect sdl3_map_quad(float px, float py, float tw, float th)
{
    float cw = (float)sdl3.cell_w;
    float ch = (float)sdl3.cell_h;
    float sx, sy, dw, dh;
    SDL_FRect q;

    /* With the font's own cell, a glyph fills its cell and there is nothing
     * to fit: the tile *is* the cell. */
    if (cw <= 0.0f || ch <= 0.0f || !display_square_get())
    {
        q.x = px; q.y = py; q.w = tw; q.h = th;
        return q;
    }

    {
        float s;

        sx = tw / (float)(sdl3_ink_w > 0 ? sdl3_ink_w : 1);
        sy = th / (float)(sdl3_ink_h > 0 ? sdl3_ink_h : 1);
        s  = (sx < sy) ? sx : sy;

        sx = s;
        sy = s;
    }

    dw = cw * sx;
    dh = ch * sy;

    q.x = px + (tw - dw) * 0.5f;
    q.y = py + (th - dh) * 0.5f;
    q.w = dw;
    q.h = dh;

    return q;
}

/* Follow the focus while zoomed in; blend towards the level centre as the
 * whole level fits. The HUD clearance pass below overrides edge composition. */
static float _axis_origin(float view_pos, float view_len,
                          float player, float scale, float level_len)
{
    float follow = view_pos + view_len / 2.0f - (player + 0.5f) * scale;
    float centre = view_pos + (view_len - level_len) / 2.0f;
    float t;

    if (level_len <= 0.0f) return follow;
    if (level_len <= view_len) return centre;

    t = (view_len * 1.6f - level_len) / (view_len * 0.6f);
    if (t <= 0.0f) return follow;
    if (t > 1.0f) t = 1.0f;

    t = t * t * (3.0f - 2.0f * t);   /* ease, so the handover is not a corner */

    return follow + (centre - follow) * t;
}

static point_t _focus={-1,-1};
void sdl3_map_focus(int x, int y)
{
    _focus=point(x,y);
    sdl3_hover_dirty=TRUE;
}

/* A stable play area inside the fixed HUD. Keep the full calculation height
 * reserved even in its empty state, so the first hit does not move the camera. */
SDL_FRect sdl3_map_safe_rect(void)
{
    float pad=(float)sdl3.cell_w;
    float right=panel_sidebar ? (ui_char_info_rect().x-1)*pad : sdl3.grid_w-pad;
    float top=2.0f*sdl3.cell_h;
    float bottom=sdl3.grid_h-(panel_status ? 1.5f : .5f)*sdl3.cell_h;
    float left=pad, card=sdl3_look_reserved_width();
    SDL_FRect r;
    if(character_generated && (panel_message || msg_line_waiting()))
        top=MAX(top,(msg_line_rect().cy+1.0f)*sdl3.cell_h);
    if(display_calculations_get()!=DISPLAY_CALC_OFF && !sdl3_look_recall_active())
    {
        bottom=MIN(bottom,sdl3_calc_bounds(TRUE).y-pad);
    }
    if(card>0) left=card+pad;
    r.x=left; r.y=top; r.w=MAX(pad,right-left); r.h=MAX((float)sdl3.cell_h,bottom-top);
    return r;
}

bool sdl3_map_input_clear(float x, float y)
{
    float top=2.0f*sdl3.cell_h;
    float right=panel_sidebar ? ui_char_info_rect().x*sdl3.cell_w : sdl3.grid_w;
    float bottom=sdl3.grid_h-(panel_status ? sdl3.cell_h : 0);
    if(character_generated && (panel_message || msg_line_waiting()))
        top=MAX(top,msg_line_rect().cy*sdl3.cell_h);
    /* Camera clearance is deliberately rectangular and stable. Input only
     * excludes actual HUD surfaces, not the visible map beside a panel. */
    return x>=0 && x<right && y>=top && y<bottom
        && !sdl3_calc_contains(x,y) && !sdl3_look_recall_contains(x,y);
}

void sdl3_map_view_get(sdl3_map_view *v)
{
    int lvl_w, lvl_h, plx, ply;
    float view_w, view_h;

    /*
     * The whole window, not the game's map rectangle.
     *
     * The rectangle is still the right answer for the *game* -- it is where
     * the terminal's own map goes, and what shrinks when a panel is switched
     * on. But we are not drawing into the terminal. The level goes behind it,
     * edge to edge, and the three strips of chrome float on top; so the level
     * is centred on the window the player is looking at rather than on the
     * hole left between the furniture, and switching a panel off no longer
     * shifts the whole dungeon sideways to fill the gap.
     */
    v->map = rect(0, 0, sdl3.cols, sdl3.rows);

    ui_map_size(&lvl_w, &lvl_h);
    ui_map_player(&plx, &ply);

    /*
     * Square tiles.
     *
     * A dungeon grid is a square of floor and should look like one. Drawn at
     * the font's cell it is twice as tall as it is wide, which compresses
     * north-south distance to about half: something ten grids away above you
     * sits half as far up the screen as something ten grids away beside you.
     * Every picture drawn on the map inherits that -- which is what made the
     * zoomed portraits look stretched, the same distortion showing up on one
     * glyph instead of on the whole level.
     *
     * The side is the geometric mean of the cell, so a grid covers about the
     * same area of screen it did before: things stay the size they were and
     * only the shape changes. It follows the typeface and the interface size,
     * and the zoom multiplies it, so nothing here decides how much dungeon you
     * can see -- the wheel does.
     */
    if (display_square_get())
    {
        v->scale_x = sdl3_zoom * sdl3_tile_px();
        v->scale_y = v->scale_x;
    }
    else
    {
        v->scale_x = sdl3_zoom * (float)sdl3.cell_w;
        v->scale_y = sdl3_zoom * (float)sdl3.cell_h;
    }

    /*
     * Correct the zoom here as well as at the wheel, so that whatever set it
     * -- the wheel, the -z option, a level change that made the old value
     * meaningless -- ends up at the same answer. One source of truth, and the
     * write-back means the wheel does not then have to climb back through a
     * range that draws nothing.
     */
    {
        float floor = sdl3_zoom_floor();

        if (sdl3_zoom < floor)
        {
            sdl3_zoom = floor;
            v->scale_x = sdl3_zoom * (display_square_get()
                                      ? sdl3_tile_px() : (float)sdl3.cell_w);
            v->scale_y = display_square_get()
                       ? v->scale_x : sdl3_zoom * (float)sdl3.cell_h;
        }
    }

    {
        SDL_FRect safe=sdl3_map_safe_rect();
        float fit=MIN(1.0f,MIN(safe.w/v->scale_x,safe.h/v->scale_y));
        /* Extreme zoom on a small display must still fit the complete focus
         * cell. Keep the requested zoom; apply the same effective scale to
         * drawing, markers and input until more space is available. */
        v->scale_x*=fit; v->scale_y*=fit;
    }
    view_w = (float)(v->map.cx * sdl3.cell_w);
    view_h = (float)(v->map.cy * sdl3.cell_h);
    if(_focus.x>=0) { plx=_focus.x; ply=_focus.y; }

    if (sdl3_verbose)
    {
        static float said_zoom = -1.0f;
        static int   said_world = -1;
        int          world = ui_map_is_world() ? 1 : 0;

        if (sdl3_zoom != said_zoom || world != said_world)
        {
            said_zoom = sdl3_zoom;
            said_world = world;
            printf("map: %s %dx%d, scale %.1fx%.1f -> %.0fx%.0f px,"
                   " view %.0fx%.0f, zoom %.3f (floor %.3f)\n",
                   ui_map_is_world() ? "WORLD" : "dungeon",
                   lvl_w, lvl_h, (double)v->scale_x, (double)v->scale_y,
                   (double)(lvl_w * v->scale_x), (double)(lvl_h * v->scale_y),
                   (double)view_w, (double)view_h,
                   (double)sdl3_zoom, (double)sdl3_zoom_floor());
        }
    }

    v->origin_x = _axis_origin((float)(v->map.x * sdl3.cell_w), view_w,
                               (float)plx, v->scale_x,
                               (float)lvl_w * v->scale_x);
    v->origin_y = _axis_origin((float)(v->map.y * sdl3.cell_h), view_h,
                               (float)ply, v->scale_y,
                               (float)lvl_h * v->scale_y);
    {
        SDL_FRect safe=sdl3_map_safe_rect();
        /* The world can extend beyond the viewport at an edge. Never clamp the
         * player back underneath a panel merely to hide blank space. */
        v->origin_x=ui_map_safe_axis(v->origin_x,plx,v->scale_x,safe.x,safe.x+safe.w);
        v->origin_y=ui_map_safe_axis(v->origin_y,ply,v->scale_y,safe.y,safe.y+safe.h);
    }
}

/*
 * Draw the map at the current zoom, in the space the map normally occupies.
 *
 * This is the thing Term cannot do. It carries the window onto the level, so
 * anything other than one-grid-per-cell has to come from somewhere else --
 * ui_map.h, where the frontend asks the game what the level looks like rather
 * than being told.
 *
 * Zoomed in, cells are glyphs like anywhere else, just bigger. Zoomed far
 * enough out they become coloured blocks, because a letter two pixels across
 * is not a letter. Unexplored ground is simply absent, so what you see is what
 * you have walked.
 */
void sdl3_draw_zoomed_map(void)
{
    sdl3_map_view v;
    const sdl3_atlas_entry *atlas = NULL;
    int lvl_w, lvl_h, plx, ply;
    bool glyphs, portraits, tiles, player_tile_ground = FALSE;
    SDL_FRect r;
    int x, y;

    sdl3_map_view_get(&v);
    ui_map_size(&lvl_w, &lvl_h);
    if (lvl_w <= 0 || lvl_h <= 0) return;

    glyphs    = BOOL(sdl3_zoom >= SDL3_ZOOM_GLYPH);
    tiles     = sdl3_tiles_on();

    /* Map presentation stays consistent at every zoom. ASCII illustrations
     * remain available in inspection cards. */
    portraits = FALSE;

    /* One lookup per frame, not per cell: every glyph here is the same size. */
    /* The quad is taller than the tile -- see sdl3_map_quad -- so ask for the
     * size actually drawn, or every glyph on the map is scaled up from an
     * atlas rendered too small. */
    if (glyphs)
        atlas = sdl3_atlas_for(sdl3_map_quad(0.0f, 0.0f,
                                             v.scale_x, v.scale_y).h);

    /*
     * No blanking, and nothing to keep clear of.
     *
     * Both used to happen here: the map region was painted over first, and its
     * top edge was pushed down past whatever the message line was occupying,
     * because a message painted over by the map is a message the player never
     * sees -- and a "-more-" nobody can see is a game that appears to have
     * stopped. Neither is needed now. The level is drawn on the cleared canvas
     * and the terminal is composited over it, so the message line is simply
     * one of the things in front.
     */
    if (v.map.cy <= 0) return;

    r.x = (float)(v.map.x * sdl3.cell_w);
    r.y = (float)(v.map.y * sdl3.cell_h);
    r.w = (float)(v.map.cx * sdl3.cell_w);
    r.h = (float)(v.map.cy * sdl3.cell_h);

    /* Clip to it as well. Skipping cells whose *origin* falls outside is not
     * enough: zoomed in, a cell starting just inside the edge is drawn many
     * pixels wide and spills over the sidebar and the message line. */
    {
        SDL_Rect clip;

        clip.x = (int)r.x;
        clip.y = (int)r.y;
        clip.w = (int)r.w;
        clip.h = (int)r.h;
        SDL_SetRenderClipRect(sdl3.renderer, &clip);
    }

    /*
     * The hour and the terrain, worked out here too.
     *
     * They used to live only in the Term drawing path, which is the unzoomed
     * one -- so the moment you touched the wheel the ground went flat and the
     * light went out, and came back when you returned to where you started. A
     * map that changes its character with the zoom is worse than one that
     * never had any.
     */
    {
        ui_daylight_t sky;
        bool          lit   = ui_daylight(&sky);
        int           style = display_terrain_get();
        theme_color_t canvas = theme_ui(THEME_UI_CANVAS);
        byte          ground[3];

        ground[0] = canvas.r;
        ground[1] = canvas.g;
        ground[2] = canvas.b;

    for (y = 0; y < lvl_h; y++)
    {
        float py_ = v.origin_y + y * v.scale_y;

        /* Skip whole rows that fall outside the view. */
        if (py_ + v.scale_y < r.y || py_ > r.y + r.h) continue;

        for (x = 0; x < lvl_w; x++)
        {
            float px_ = v.origin_x + x * v.scale_x;
            byte a;
            char c;
            byte fore[3];
            ui_terrain_t t;
            bool         dressed;

            if (px_ + v.scale_x < r.x || px_ > r.x + r.w) continue;
            if (!ui_map_cell(x, y, &a, &c)) continue;
            if (c == ' ') continue;

            fore[0] = ormeq_color_table[a][1];
            fore[1] = ormeq_color_table[a][2];
            fore[2] = ormeq_color_table[a][3];

            if (lit)
            {
                byte sky_rgb[3];

                sky_rgb[0] = sky.r;
                sky_rgb[1] = sky.g;
                sky_rgb[2] = sky.b;
                sdl3_blend(fore, sky_rgb, sky.strength * 64 / 100, fore);
            }

            dressed = BOOL(style > UI_TERRAIN_PLAIN
                        && ui_terrain_at(x, y, style, &t));

            if (dressed) sdl3_tone(fore, t.tone, fore);

            /*
             * And knocked back if it is remembered rather than seen. Here,
             * above the branch, so the letters and the solid blocks get it
             * too -- they had it from nowhere before, and the letters are how
             * most of this game is actually played.
             */
            {
                int d = sdl3_seen_dim(x, y);

                if (d < 255)
                {
                    fore[0] = (byte)(fore[0] * d / 255);
                    fore[1] = (byte)(fore[1] * d / 255);
                    fore[2] = (byte)(fore[2] * d / 255);
                    _dim_remembered++;
                }
                else _dim_seen++;
            }

            /* Keep the player's warning colour clear in darkness and daylight. */
            if (player_bold(y, x) && c == '@')
            {
                fore[0] = ormeq_color_table[a][1];
                fore[1] = ormeq_color_table[a][2];
                fore[2] = ormeq_color_table[a][3];
            }

            if (glyphs)
            {
                bool tiled_ground = FALSE;
                /*
                 * Close enough in, a grid is big enough to hold a picture of
                 * whatever is standing on it, and a letter is no longer the
                 * most it can say. Only past the threshold: at anything less
                 * the portrait would be a few specks and the letter is both
                 * clearer and faster to read.
                 *
                 * The plates win over the tiles when you lean right in. A
                 * sixteen pixel tile blown up eight times is a decision about
                 * how it should look; a plate at that size is a picture. The
                 * one place tile mode does not get the last word.
                 */
                if (portraits && sdl3_draw_grid_portrait(x, y, px_, py_,
                                                         v.scale_x, v.scale_y))
                {
                    continue;
                }

                if (tiles && sdl3_draw_grid_tiles(x, y, px_, py_,
                                                  v.scale_x, v.scale_y, a, c, fore, &tiled_ground))
                {
                    continue;
                }

                SDL_FRect q = {px_, py_, v.scale_x, v.scale_y};

                if (player_bold(y,x)) player_tile_ground = tiled_ground;
                if (tiled_ground)
                    sdl3_tile_glyph_outline(atlas,q,c);
                SDL_SetTextureColorMod(atlas ? atlas->tex : sdl3.atlas,
                                       fore[0], fore[1], fore[2]);
                sdl3_draw_map_glyph(atlas, q, c, !tiles && display_enhanced_square_get() && ui_map_wall(x,y));
            }
            else
            {
                SDL_FRect b;

                b.x = px_;
                b.y = py_;
                b.w = v.scale_x < 1.0f ? 1.0f : v.scale_x;
                b.h = v.scale_y < 1.0f ? 1.0f : v.scale_y;

                SDL_SetRenderDrawColor(sdl3.renderer,
                                       fore[0], fore[1], fore[2],
                                       SDL_ALPHA_OPAQUE);
                SDL_RenderFillRect(sdl3.renderer, &b);
            }
        }
    }
    }

    if (sdl3_verbose && (_dim_seen + _dim_remembered))
    {
        printf("SDL3: grids drawn -- %ld seen, %ld remembered (dimmed to %d%%)\n",
               _dim_seen, _dim_remembered, SDL3_DIM_REMEMBERED_PCT);
        fflush(stdout);
    }
    _dim_seen = _dim_remembered = 0;

    /* The player, last and never smaller than a few pixels, so they cannot
     * disappear into the floor at low zoom. */
    ui_map_player(&plx, &ply);

    if (glyphs)
    {
        SDL_FRect q = {v.origin_x + plx * v.scale_x,
                        v.origin_y + ply * v.scale_y, v.scale_x, v.scale_y};

        byte a = ui_player_health_attr(TERM_WHITE);
        if (player_tile_ground)
            sdl3_tile_glyph_outline(atlas,q,'@');
        SDL_SetTextureColorMod(atlas ? atlas->tex : sdl3.atlas,
            ormeq_color_table[a][1], ormeq_color_table[a][2], ormeq_color_table[a][3]);
        sdl3_draw_map_glyph(atlas, q, '@', FALSE);
    }
    else
    {
        SDL_FRect b;

        b.w = v.scale_x < 3.0f ? 3.0f : v.scale_x;
        b.h = v.scale_y < 3.0f ? 3.0f : v.scale_y;
        b.x = v.origin_x + plx * v.scale_x;
        b.y = v.origin_y + ply * v.scale_y;

        byte a = ui_player_health_attr(TERM_WHITE);
        SDL_SetRenderDrawColor(sdl3.renderer,
            ormeq_color_table[a][1], ormeq_color_table[a][2], ormeq_color_table[a][3], SDL_ALPHA_OPAQUE);
        SDL_RenderFillRect(sdl3.renderer, &b);
    }

    SDL_SetRenderClipRect(sdl3.renderer, NULL);
}

/*
 * Is this Term cell part of the map region at all?
 *
 * The message line lives *inside* that region -- it is an overlay drawn over
 * the top-left of the map, and it grows downwards as a message runs to more
 * than one line. prt_map() knows to skip those cells; this has to as well, or
 * the text of a message gets terrain decoration and the light of dawn, which
 * is both wrong and, once a message is two or three lines deep, hard to read.
 */
bool sdl3_in_map(int x, int y)
{
    rect_t map = ui_map_rect();

    if (x < map.x || x >= map.x + map.cx) return FALSE;
    if (y < map.y || y >= map.y + map.cy) return FALSE;

    return BOOL(!msg_line_contains(y, x));
}
