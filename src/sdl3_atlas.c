/*
 * File: sdl3_atlas.c
 *
 * The typeface, and the texture baked from it.
 *
 * A frontend for this game needs one thing from a font: a strip of pixels in
 * which character N sits at exactly x = N * cell_w. Because the face is
 * monospaced that strip can be produced by rendering one string, which is why
 * there is no glyph-metric arithmetic anywhere in this port -- and why the
 * whole subject fits in one file with a single question in it.
 *
 * Three things live here and they are the same subject seen from three sides:
 *
 *   **Choosing a face.** The setting, the -F override, the built-in
 *   candidates, and a size worked out from the display's own content scale so
 *   that 16pt does not become a squint on a 4K panel.
 *
 *   **Baking it.** sdl3_render_atlas() renders the printable ASCII range plus
 *   six decorative code points into one texture and measures what came out.
 *   Nothing about a cell's size is assumed; all of it is read back off the
 *   pixels.
 *
 *   **Keeping several.** The map zooms continuously, and one atlas scaled to
 *   any zoom looks like one atlas scaled to any zoom. A short ladder of sizes
 *   with an eight per cent ratio between rungs keeps the residual scale under
 *   four per cent, which the eye does not catch.
 *
 * Lifted out of main-sdl3.c on 7 September 2026, the second cut of that day --
 * sdl3_marks.c was the first. It was the obvious next one because none of this
 * knows anything about the rest of the port: it needs the renderer, the font
 * and the tile size, and hands back a texture and the cell measurements.
 *
 * Copyright (c) 2026 John Horton.
 *
 * Added since the fork and offered on the same terms as the rest of the
 * tree -- the Angband licence in LICENSE.
 */

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include "sdl3_local.h"
#include "ui_square.h"

SDL_Rect sdl3_base_map_ink[95];
static SDL_Rect last_map_ink[95];

/**** The face ****/

/*
 * Candidate fonts, tried in order. Consolas ships with every supported
 * Windows; Courier New is the belt-and-braces fallback.
 */
static cptr sdl3_font_candidates[] =
{
    "C:\\Windows\\Fonts\\consola.ttf",
    "C:\\Windows\\Fonts\\cour.ttf",
    NULL
};

/*
 * Set from the command line; see sdl3_parse_args().
 */
cptr  sdl3_font_override = NULL;   /* -F<path>  */
float sdl3_font_size = 0.0f;       /* -f<size>, 0 means "work it out" */

/*
 * Whether the face now open is the one the setting names.
 *
 * Falling back to the built-in when a chosen face cannot be opened is right --
 * far better than refusing to start -- but doing it silently is not: the
 * setting then says one thing and the screen shows another, which reads as
 * "sometimes the settings do not change".
 */
bool  sdl3_font_got_choice = FALSE;

/*
 * Pick a font size that suits the display.
 *
 * A fixed point size is a fixed number of *pixels*, so 16pt that reads well on
 * a 1080p monitor is a squint on a 4K one. The display's content scale is the
 * factor the desktop itself is using, so following it lands us at roughly the
 * same apparent size everywhere.
 */
static float sdl3_auto_font_size(void)
{
    float scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());

    if (scale <= 0.0f) scale = 1.0f;

    return SDL3_FONT_SIZE * scale;
}

/*
 * Where the shipped typefaces live: a "font" directory beside the executable.
 *
 * Not under lib/, deliberately. lib/xtra/font holds the Win32 port's .fon
 * bitmap fonts, which are a different thing entirely and are indexed by the
 * size baked into their filename; putting scalable faces in with them would
 * make two unrelated collections look like one.
 */
static bool sdl3_font_path(int font, char *buf, int len)
{
    cptr file = display_font_file(font);
    const char *base;

    if (!file) return FALSE;

    base = SDL_GetBasePath();
    if (!base) base = "";

    strnfmt(buf, len, "%sfont%s%s", base, PATH_SEP, file);
    return TRUE;
}

errr sdl3_open_font(void)
{
    int  i;
    char path[1024];

    if (sdl3_font_size <= 0.0f) sdl3_font_size = sdl3_auto_font_size();

    /*
     * "Default" *is* the built-in face, so reaching the fallback list below
     * is success and not failure. Treating it as failure meant the setting
     * could never be cycled off the last named typeface: it would open the
     * built-in, decide it had not got what was asked for, and put the
     * preference back. Stuck on VT323, and only a restart would move it.
     */
    sdl3_font_got_choice = BOOL(sdl3_font_override != NULL
                             || !display_font_file(display_font_get()));

    /* The player's choice comes before our own list, and -F before either. */
    if (!sdl3_font_override && sdl3_font_path(display_font_get(), path, sizeof(path)))
    {
        sdl3.font = TTF_OpenFont(path, sdl3_font_size);

        if (sdl3.font)
        {
            sdl3_font_got_choice = TRUE;
            return 0;
        }

        /* Missing or unreadable is not fatal -- fall through to the default
         * rather than refusing to start over a typeface. */
        plog_fmt("SDL3: could not open font '%s': %s", path, SDL_GetError());
    }

    if (sdl3_font_override)
    {
        sdl3.font = TTF_OpenFont(sdl3_font_override, sdl3_font_size);
        if (sdl3.font) return 0;

        plog_fmt("SDL3: could not open font '%s': %s",
                 sdl3_font_override, SDL_GetError());
        return -1;
    }

    for (i = 0; sdl3_font_candidates[i]; i++)
    {
        sdl3.font = TTF_OpenFont(sdl3_font_candidates[i], sdl3_font_size);
        if (sdl3.font) return 0;
    }

    plog_fmt("SDL3: could not open a font: %s", SDL_GetError());
    return -1;
}


/**** The atlas ****/

/*
 * Build the glyph atlas.
 *
 * One string, rendered once. The font is monospaced, so the result is a strip
 * in which character (c - GLYPH_FIRST) occupies exactly one cell width. That
 * also gives us our cell size, baseline-aligned, without touching a single
 * glyph metric.
 */
/*
 * Glyph atlases at several sizes.
 *
 * Scaling one atlas to any zoom is what a first cut does, and it looks it:
 * blown up, the glyphs are visibly blocky; shrunk, they turn to mush. So we
 * keep a few atlases rendered at different point sizes and pick the nearest,
 * which leaves only a small residual scale to apply.
 *
 * Sizes are quantised onto a geometric ladder rather than following the zoom
 * exactly. Zoom is continuous, so tracking it precisely would mean re-rendering
 * ninety-five glyphs on every notch of the wheel; a ratio of 1.25 between rungs
 * means the residual scale is never worse than about 12%, which the eye does
 * not catch.
 *
 * (SDL_ttf can render signed distance fields, which would stay crisp at any
 * scale from a single atlas -- but sampling an SDF needs a shader, and
 * SDL_Renderer has nowhere to put one.)
 */
/*
 * Finer rungs than the first cut, and more of them.
 *
 * A ratio of 1.25 leaves up to twelve per cent of residual scaling, which is
 * invisible on a letter of text and very visible on a map glyph blown up to
 * fill a tile -- soft edges and uneven stems, where the whole point of this
 * is crispness. Eight per cent rungs put the residual under four, and an
 * atlas is only ninety-odd glyphs to render.
 */
#define SDL3_ATLAS_CACHE  10
#define SDL3_ATLAS_RATIO  1.08f


static sdl3_atlas_entry sdl3_atlas_cache[SDL3_ATLAS_CACHE];
static Uint64           sdl3_atlas_clock;


/*
 * Render the printable ASCII range at the font's current size and upload it.
 * Reports the cell size it came out as.
 */
/* Additional shading blocks supply solid coverage for the portrait ramp.
 * Keep them outside printable ASCII, whose glyphs already identify game objects.
 * These blocks fill cells and are not used as new map symbols. */
#define SDL3_DECO_MAX 8

static const u32b sdl3_deco_want[] =
{
    0x2591,  /* light shade  -- the portrait ramp's dark end */
    0x2592,  /* medium shade */
    0x2593,  /* dark shade   */
    0x2500,  /* horizontal   -- the rule under a heading      */
    0x25B2,  /* up triangle  -- there is more of this page    */
    0x25BC   /* down triangle                                 */
};

/*
 * U+2588, the full block, is deliberately absent. It is the densest thing a
 * font has and so the obvious choice for the dark end of a portrait, but it is
 * *solid*: a cell of it is a filled rectangle with no texture at all, and in a
 * picture made of characters that reads as a hole rather than as shadow. The
 * dark shade is denser than any letter and still stippled, which is what was
 * wanted. If some later piece of chrome needs a solid block, add it back here
 * -- the ramp only ever takes the heaviest, so it would take this instead.
 */

static u32b sdl3_deco_cp[SDL3_DECO_MAX];
static int  sdl3_deco_ct;   /* how many this face actually carries */

/*
 * The largest ink box any printable glyph occupies, in atlas pixels.
 *
 * This is what a map tile is fitted against: no glyph is ever drawn larger
 * than this, so bounding it to the tile means nothing can leave its tile --
 * by construction, not by tuning.
 *
 * Measured, never assumed: faces vary, and a constant that happened to suit
 * one of ours would quietly be wrong for the next.
 *
 * Two pairs, and the distinction matters. Every atlas built measures itself
 * into `sdl3_last_ink`; only the *base* atlas publishes into `sdl3_ink`. The
 * zoom ladder bakes the same face at a dozen sizes, and while it did so it was
 * republishing -- so the fit that decides how big a glyph is drawn on the map
 * was quietly re-measured against whichever rung had been baked last, and
 * every glyph on a zoomed map came out at a fraction of its proper size.
 */
int sdl3_ink_w = 1, sdl3_ink_h = 1;
static int sdl3_last_ink_w = 1, sdl3_last_ink_h = 1;

/* The base atlas has been rebuilt: what it measured is now the truth. */
void sdl3_publish_ink(void)
{
    sdl3_ink_w = sdl3_last_ink_w;
    sdl3_ink_h = sdl3_last_ink_h;
    memcpy(sdl3_base_map_ink, last_map_ink, sizeof(last_map_ink));
    /* Both initial builds and font refits come here. Portrait shading must
     * follow the current font, not change only when a renderer reset happens. */
    ui_art_set_font(sdl3.font, sdl3.cell_w, sdl3.cell_h,
                    sdl3_deco_cp, sdl3_deco_ct);
}

static int sdl3_atlas_slots(void)
{
    return GLYPH_COUNT + sdl3_deco_ct;
}

/*
 * Which codepoint sits in slot `i` of the atlas.
 *
 * A function rather than plain arithmetic so the decorative glyphs above ASCII
 * can be appended without touching the builder: the game only ever emits
 * ASCII, so anything past 127 is guaranteed not to collide with a symbol that
 * already means something.
 */
static u32b sdl3_atlas_codepoint(int i)
{
    if (i >= GLYPH_COUNT) return sdl3_deco_cp[i - GLYPH_COUNT];

    return (u32b)(GLYPH_FIRST + i);
}

/* Which slot holds `cp`, or -1 if the atlas has no such glyph. */
int sdl3_atlas_slot_of(u32b cp)
{
    int i;

    if (cp >= GLYPH_FIRST && cp <= GLYPH_LAST) return (int)cp - GLYPH_FIRST;

    for (i = 0; i < sdl3_deco_ct; i++)
        if (sdl3_deco_cp[i] == cp) return GLYPH_COUNT + i;

    return -1;
}


/* A codepoint as UTF-8, so this keeps working past ASCII. */
static int sdl3_utf8(Uint32 cp, char *out)
{
    int n = 0;

    if (cp < 0x80)
    {
        out[n++] = (char)cp;
    }
    else if (cp < 0x800)
    {
        out[n++] = (char)(0xC0 | (cp >> 6));
        out[n++] = (char)(0x80 | (cp & 0x3F));
    }
    else
    {
        out[n++] = (char)(0xE0 | (cp >> 12));
        out[n++] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[n++] = (char)(0x80 | (cp & 0x3F));
    }

    out[n] = 0;
    return n;
}


/*
 * Render the glyph sheet: one fixed-width cell per character.
 *
 * The first version rendered all ninety-five as a single string and divided the
 * resulting width by ninety-five. That works only if the font's advance is an
 * exact whole number of pixels and nothing applies kerning -- and where it is
 * not, the error accumulates along the string, so the first few glyphs look
 * fine and everything after them is sliced through the middle. That is what
 * made some faces come out as garbage.
 *
 * So each glyph is rendered on its own and blitted into a cell of its own. The
 * cell is as wide as the widest glyph in the set and narrower ones are centred
 * in it, which also means a face that is not perfectly monospaced degrades into
 * looking slightly loose rather than into nonsense.
 */
SDL_Texture *sdl3_render_atlas(int *cell_w, int *cell_h)
{
    SDL_Color    white = { 255, 255, 255, SDL_ALPHA_OPAQUE };
    SDL_Surface *glyph[GLYPH_COUNT + SDL3_DECO_MAX];
    SDL_Rect     ink[GLYPH_COUNT + SDL3_DECO_MAX];
    SDL_Surface *sheet;
    SDL_Texture *tex;
    int          i, w = 0, h = 0, slots;

    /*
     * Which decorative codepoints this face carries. Asked before anything is
     * rendered, because a face without the character produces .notdef -- a
     * hollow box -- and a row of boxes through a portrait is worse than no
     * shading at all. A face that has none simply gets the ASCII atlas, which
     * is what shipped before this, so the fallback is known good.
     */
    sdl3_deco_ct = 0;
    for (i = 0; i < (int)(sizeof(sdl3_deco_want) / sizeof(sdl3_deco_want[0]))
             && sdl3_deco_ct < SDL3_DECO_MAX; i++)
    {
        if (TTF_FontHasGlyph(sdl3.font, sdl3_deco_want[i]))
            sdl3_deco_cp[sdl3_deco_ct++] = sdl3_deco_want[i];
    }

    slots = sdl3_atlas_slots();
    memset(last_map_ink, 0, sizeof(last_map_ink));

    for (i = 0; i < slots; i++)
    {
        char utf8[8];
        int  n = sdl3_utf8(sdl3_atlas_codepoint(i), utf8);

        /* One character at a time: a single-glyph string cannot accumulate a
         * rounding error, and comes back baseline-aligned to the font height. */
        glyph[i] = TTF_RenderText_Blended(sdl3.font, utf8, n, white);
    }

    /*
     * The cell is measured over the printable ASCII range **only**.
     *
     * A full block spans the whole em box, taller and often wider than any
     * letter, so letting it into this measurement would enlarge every cell in
     * the game and re-flow the entire screen the moment a face happened to
     * carry one. The grid belongs to the alphabet the game actually writes in;
     * decoration fits itself into that, and is clipped if it does not.
     */
    for (i = 0; i < GLYPH_COUNT; i++)
    {
        if (!glyph[i]) continue;

        if (glyph[i]->w > w) w = glyph[i]->w;
        if (glyph[i]->h > h) h = glyph[i]->h;
    }

    /*
     * How much of the cell the alphabet actually uses.
     *
     * The cell above is the font's own line box, which reserves room for
     * accents and descenders that ninety-five ASCII glyphs barely touch. This
     * measures the band the ink really occupies, and the ink box of `#` --
     * both of which a square-tile map would need, and both of which say how
     * much of the current cell is empty. Reported under -v.
     */
    {
        int top = h, bot = -1, hash_w = 0, hash_h = 0, j;
        int wide = 1, tall = 1;

        memset(ink, 0, sizeof(ink));

        for (i = 0; i < slots; i++)
        {
            int gx, gy, first = -1, last = -1, left = -1, right = -1;

            if (!glyph[i]) continue;

            for (gy = 0; gy < glyph[i]->h; gy++)
            {
                const byte *row = (const byte *)glyph[i]->pixels
                                + gy * glyph[i]->pitch;

                for (gx = 0; gx < glyph[i]->w; gx++)
                {
                    if (row[gx * 4 + 3] < 32) continue;

                    if (first < 0) first = gy;
                    last = gy;
                    if (left < 0 || gx < left) left = gx;
                    if (gx > right) right = gx;
                }
            }

            if (first < 0) continue;

            ink[i].x = left;
            ink[i].y = first;
            ink[i].w = right - left + 1;
            ink[i].h = last - first + 1;

            /* Only the printable range sets the cell. See below. */
            if (i < GLYPH_COUNT)
            {
                if (first < top) top = first;
                if (last > bot) bot = last;
                if (ink[i].w > wide) wide = ink[i].w;
                if (ink[i].h > tall) tall = ink[i].h;
            }

            if (sdl3_atlas_codepoint(i) == '#')
            {
                hash_w = ink[i].w;
                hash_h = ink[i].h;
            }
        }

        j = (bot >= top) ? (bot - top + 1) : 0;

        sdl3_last_ink_w = wide;
        sdl3_last_ink_h = tall;

        if (sdl3_verbose)
        {
            float aspect = (hash_w > 0) ? (float)hash_h / (float)hash_w : 1.0f;

            printf("atlas: cell %dx%d px", w, h);
            printf("   ink band %d px (rows %d..%d), %d px of the cell unused\n",
                   j, top, bot, h - j);
            printf("       '#' ink %dx%d (aspect %.3f); largest ink %dx%d;"
                   " map tile %.1f px\n",
                   hash_w, hash_h, (double)aspect,
                   wide, tall, (double)sdl3_tile_px());
        }
    }

    /* Two atlas banks: text glyphs keep the font baseline; map glyphs have their
     * ink centred horizontally and vertically within square cells. */
    sheet = (w > 0 && h > 0)
          ? SDL_CreateSurface(w * slots, h * 2, SDL_PIXELFORMAT_RGBA32)
          : NULL;

    if (sheet)
    {
        /* Copy rather than blend: the sheet starts empty and each glyph owns
         * its own cell, so the alpha should arrive intact for the renderer to
         * blend with later. */
        SDL_SetSurfaceBlendMode(sheet, SDL_BLENDMODE_NONE);

        for (i = 0; i < slots; i++)
        {
            SDL_Rect dst, cell;

            if (!glyph[i]) continue;

            SDL_SetSurfaceBlendMode(glyph[i], SDL_BLENDMODE_NONE);

            /* Clip to this slot's cell. A decorative glyph may be larger than
             * the cell the alphabet settled on, and without this it would
             * bleed into its neighbour rather than being cropped. */
            cell.x = i * w;
            cell.y = 0;
            cell.w = w;
            cell.h = h;
            SDL_SetSurfaceClipRect(sheet, &cell);

            /* Centred, so an oversized glyph loses the same amount from each
             * side -- which for a block is exactly what you want. */
            dst.x = i * w + (w - glyph[i]->w) / 2;
            dst.y = (h - glyph[i]->h) / 2;
            dst.w = glyph[i]->w;
            dst.h = glyph[i]->h;

            SDL_BlitSurface(glyph[i], NULL, sheet, &dst);

            /* And again into the map bank, this time by its ink. */
            if (ink[i].w > 0 && ink[i].h > 0)
            {
                cell.y = h;
                SDL_SetSurfaceClipRect(sheet, &cell);

                dst.x = i * w + (w - ink[i].w) / 2;
                dst.y = h + (h - ink[i].h) / 2;
                dst.w = ink[i].w;
                dst.h = ink[i].h;

                if (i < GLYPH_COUNT) last_map_ink[i] = dst;
                SDL_BlitSurface(glyph[i], &ink[i], sheet, &dst);
            }
        }

        SDL_SetSurfaceClipRect(sheet, NULL);
    }

    for (i = 0; i < slots; i++)
        if (glyph[i]) SDL_DestroySurface(glyph[i]);

    if (!sheet) return NULL;

    if (cell_w) *cell_w = w;
    if (cell_h) *cell_h = h;

    tex = SDL_CreateTextureFromSurface(sdl3.renderer, sheet);
    SDL_DestroySurface(sheet);

    if (tex) SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);

    return tex;
}


/* Throw the ladder away -- every rung was measured against the old base size. */
void sdl3_atlas_forget(void)
{
    int i;

    for (i = 0; i < SDL3_ATLAS_CACHE; i++)
    {
        if (sdl3_atlas_cache[i].tex) SDL_DestroyTexture(sdl3_atlas_cache[i].tex);
        sdl3_atlas_cache[i].tex = NULL;
        sdl3_atlas_cache[i].ptsize = 0.0f;
        sdl3_atlas_cache[i].used = 0;
    }
}


/*
 * The atlas to use when a cell wants to be `want_h` pixels tall.
 *
 * Returns the nearest cached size, rendering it if need be, or NULL if that
 * fails -- in which case the caller falls back to scaling the base atlas, which
 * is soft but still legible.
 */
sdl3_atlas_entry *sdl3_atlas_for(float want_h)
{
    float ptsize, ratio;
    int i, slot = 0;
    SDL_Texture *tex;
    int cw, ch;

    if (sdl3.cell_h <= 0) return NULL;

    /* Scale the base point size by how much bigger the cell wants to be, then
     * quantise onto the ladder so the wheel does not re-render on every notch. */
    ratio = want_h / (float)sdl3.cell_h;
    ptsize = sdl3_font_size;

    while (ratio > SDL3_ATLAS_RATIO && ptsize * SDL3_ATLAS_RATIO <= SDL3_ATLAS_MAX_PT)
    {
        ptsize *= SDL3_ATLAS_RATIO;
        ratio  /= SDL3_ATLAS_RATIO;
    }
    while (ratio < 1.0f / SDL3_ATLAS_RATIO && ptsize / SDL3_ATLAS_RATIO >= SDL3_ATLAS_MIN_PT)
    {
        ptsize /= SDL3_ATLAS_RATIO;
        ratio  *= SDL3_ATLAS_RATIO;
    }

    /* Already have it? */
    for (i = 0; i < SDL3_ATLAS_CACHE; i++)
    {
        if (sdl3_atlas_cache[i].tex && sdl3_atlas_cache[i].ptsize == ptsize)
        {
            sdl3_atlas_cache[i].used = ++sdl3_atlas_clock;
            return &sdl3_atlas_cache[i];
        }
    }

    /* Take an empty slot, else the one wanted longest ago. */
    for (i = 0; i < SDL3_ATLAS_CACHE; i++)
    {
        if (!sdl3_atlas_cache[i].tex) { slot = i; break; }
        if (sdl3_atlas_cache[i].used < sdl3_atlas_cache[slot].used) slot = i;
    }

    if (!TTF_SetFontSize(sdl3.font, ptsize)) return NULL;
    tex = sdl3_render_atlas(&cw, &ch);
    TTF_SetFontSize(sdl3.font, sdl3_font_size);

    if (!tex) return NULL;

    if (sdl3_atlas_cache[slot].tex) SDL_DestroyTexture(sdl3_atlas_cache[slot].tex);

    /* The base atlas gets this in sdl3_build_atlas; the cached ones were
     * left on SDL's default, which is linear -- so every glyph on the zoomed
     * map was being smeared on its way to the screen. */
    SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);

    sdl3_atlas_cache[slot].ptsize = ptsize;
    sdl3_atlas_cache[slot].tex    = tex;
    sdl3_atlas_cache[slot].cell_w = cw;
    sdl3_atlas_cache[slot].cell_h = ch;
    sdl3_atlas_cache[slot].ink_w = sdl3_last_ink_w;
    sdl3_atlas_cache[slot].ink_h = sdl3_last_ink_h;
    memcpy(sdl3_atlas_cache[slot].map_ink, last_map_ink, sizeof(last_map_ink));
    sdl3_atlas_cache[slot].used   = ++sdl3_atlas_clock;

    return &sdl3_atlas_cache[slot];
}


errr sdl3_build_atlas(void)
{
    sdl3.atlas = sdl3_render_atlas(&sdl3.cell_w, &sdl3.cell_h);

    if (!sdl3.atlas)
    {
        plog_fmt("SDL3: could not build the glyph atlas: %s", SDL_GetError());
        return -1;
    }

    /* The base atlas is sampled one texel to one pixel by everything except
     * the zoomed map, so a hard edge beats a blur if rounding ever puts us
     * half a pixel off. */
    SDL_SetTextureScaleMode(sdl3.atlas, SDL_SCALEMODE_NEAREST);

    sdl3_publish_ink();

    return 0;
}


/*
 * Let every atlas go, at shutdown.
 *
 * Separate from sdl3_atlas_forget(), which drops the ladder so it can be
 * rebuilt at a new size and is called whenever the face or the density
 * changes. This one is the end of the program, and it exists so that the
 * cache stays private to this file: main-sdl3.c used to walk the array
 * itself, which is a reasonable thing to do to an array and a poor thing to
 * do to a module.
 */
void sdl3_atlas_free(void)
{
    int i;

    for (i = 0; i < SDL3_ATLAS_CACHE; i++)
    {
        if (sdl3_atlas_cache[i].tex) SDL_DestroyTexture(sdl3_atlas_cache[i].tex);
        sdl3_atlas_cache[i].tex = NULL;
        sdl3_atlas_cache[i].ptsize = 0.0f;
    }
}

void sdl3_draw_map_glyph(const sdl3_atlas_entry *atlas, SDL_FRect cell, char ch, bool wall)
{
    SDL_Texture *tex = atlas ? atlas->tex : sdl3.atlas;
    SDL_Rect ink;
    SDL_FRect src, dst;
    int maxw = atlas ? atlas->ink_w : sdl3_ink_w;
    int maxh = atlas ? atlas->ink_h : sdl3_ink_h;
    if (!display_square_get())
    {
        sdl3_draw_glyph_scaled(atlas,cell.x,cell.y,cell.w,cell.h,ch,ATLAS_MAP);
        return;
    }
    if (ch < GLYPH_FIRST || ch > GLYPH_LAST) return;
    ink = (atlas ? atlas->map_ink : sdl3_base_map_ink)[ch-GLYPH_FIRST];
    if (!ink.w || !ink.h) return;
    src = (SDL_FRect){(float)ink.x,(float)ink.y,(float)ink.w,(float)ink.h};
    dst = ui_square_rect((float)ink.w,(float)ink.h,(float)maxw,(float)maxh,cell,wall);
    SDL_RenderTexture(sdl3.renderer,tex,&src,&dst);
}
