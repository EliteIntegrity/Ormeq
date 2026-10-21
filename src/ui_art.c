/*
 * File: ui_art.c
 *
 * Turning paintings into coloured characters. See ui_art.h for what this is
 * for; this file is the arithmetic.
 *
 * The method is the one the Angband SDL3 fork worked out, and it was worth
 * taking rather than reinventing: measure the font, pool the pixels, pick off
 * the ramp. What is different here is that the font is borrowed from the atlas
 * instead of loaded again, and the aspect ratio comes from the real cell size
 * rather than an assumed one, because we have both to hand.
 *
 * Copyright (c) 2026 John Horton.
 *
 * Added since the fork and offered on the same terms as the rest of the
 * tree -- the Angband licence in LICENSE.
 */

/* SDL's headers must come before angband.h -- see the note in main-sdl3.c. */
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <SDL3_image/SDL_image.h>

#include <math.h>   /* pow, for the tone curve */

#include "scottish.h"
#include "ui_art.h"
#ifdef WINDOWS
# include <direct.h>   /* _mkdir, for the generator below */
#endif

#ifdef WINDOWS
#include <io.h>     /* _findfirst, for the plate generator */
#endif

/*
 * A pixel this faint is not part of the subject. Used twice, for two different
 * questions: which pixels count when finding the edges of the subject, and
 * whether a pooled cell has enough in it to be worth a character at all. The
 * crop is the stricter of the two, because a stray almost-transparent pixel in
 * a corner would otherwise stretch the picture to include it.
 */
#define ART_ALPHA_CROP     16
#define ART_ALPHA_VISIBLE  12

/*
 * Portraits are read on a dark panel, where honest colour is too dark to see.
 * So the colour of every cell is pushed one and a half times towards its own
 * hue, and anything still dimmer than the floor is lifted to it -- brightened
 * without being washed towards white, which is what raising all three
 * channels equally would do.
 */
#define ART_COLOUR_GAIN_NUM  3
#define ART_COLOUR_GAIN_DEN  2
#define ART_COLOUR_FLOOR   160
#define ART_DARK_NEUTRAL    88

/* Below this a cell is too small to read a character in. */
#define ART_MIN_CELL_W  4
#define ART_MIN_CELL_H  9

/* And the same for a square sub-tile, where width and height are one number.
 *
 * Smaller than the character minimum on purpose. A sub-tile of a portrait is
 * not read as a character -- at this size no glyph is legible as itself, and
 * what carries the picture is how much of the tile is inked and what colour it
 * is. Five is where the reference sits, and the difference between five and
 * seven is a fifth again as much of the creature. */
#define ART_MIN_TILE    5

#define ART_RAMP_MAX   64

/*
 * How many shades the ramp is thinned to.
 *
 * Not as many as can be measured, which was the first attempt and came out as
 * static. At the size a portrait actually gets, the eye reads the *shape* of a
 * character long before it reads how much ink is in it -- so sixty shades put
 * sixty different silhouettes next to each other and the shading disappeared
 * behind them. A dozen is about where the alphabet stops competing with the
 * picture, and it is the depth hand-drawn ASCII art has always used.
 */
#define ART_RAMP_STEPS 12
#define ART_CACHE_SLOTS 16
#define ART_ID_MAX     64


typedef struct
{
    char     id[ART_ID_MAX];
    int      collection;
    int      box_cols, box_rows;   /* what was asked for -- the key */
    bool     by_px;                /* ...and in which units */
    u32b     used;          /* for picking the coldest slot to evict */
    bool     live;
    bool     empty;         /* looked, found nothing -- do not look again */
    ui_art_t art;
} art_slot;

static struct
{
    u32b     ramp[ART_RAMP_MAX];
    int      ramp_count;
    int      cell_w, cell_h;
    u32b     clock;
    art_slot slot[ART_CACHE_SLOTS];
} art;


/**** The ramp ****/

/*
 * Every printable glyph that is worth considering as a shade, roughly in the
 * order a person would guess -- but only roughly, because the whole point is
 * that we do not trust the guess. They are measured and re-sorted below.
 */
static const char art_candidates[] =
    ".'`^\",:;Il!i><~+-_?][}{1)(|\\/tfjrxnuvczXYUJCLQ0OZmwqpdbkhao*#MW&8%B@$";

typedef struct
{
    u32b cp;
    u32b ink;
} art_density;

static void art_sort(art_density *e, int n)
{
    int i;

    /* Insertion sort: seventy-odd entries, once per font change. */
    for (i = 1; i < n; i++)
    {
        art_density v = e[i];
        int         j = i;

        while (j > 0 && (e[j - 1].ink > v.ink
                     || (e[j - 1].ink == v.ink && e[j - 1].cp > v.cp)))
        {
            e[j] = e[j - 1];
            j--;
        }
        e[j] = v;
    }
}

/* How much ink one codepoint costs in this font. 0 if it will not render. */
static u32b art_measure(TTF_Font *font, u32b cp)
{
    SDL_Color    white = { 255, 255, 255, 255 };
    SDL_Surface *glyph = TTF_RenderGlyph_Blended(font, cp, white);
    SDL_Surface *rgba;
    u32b         ink = 0;
    int          x, y;

    if (!glyph) return 0;

    rgba = SDL_ConvertSurface(glyph, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(glyph);
    if (!rgba) return 0;

    for (y = 0; y < rgba->h; y++)
    {
        const byte *row = (const byte *)rgba->pixels + y * rgba->pitch;

        for (x = 0; x < rgba->w; x++) ink += row[x * 4 + 3];
    }
    SDL_DestroySurface(rgba);

    return ink;
}

/*
 * How much ink each candidate costs in this font, sorted lightest first, then
 * thinned to an evenly spaced ramp with a space at the bottom.
 *
 * The spacing matters: taking every measured glyph would crowd the ramp where
 * the font happens to have many glyphs of similar weight, and a ramp that is
 * dense in the middle turns smooth shading into banding.
 */
static void art_build_ramp(TTF_Font *font, const u32b *extra, int extra_ct)
{
    art_density e[sizeof(art_candidates) + 16];
    int         cap = (int)(sizeof(e) / sizeof(e[0]));
    int         n = 0;
    int         i;

    art.ramp_count = 0;
    if (!font) return;

    for (i = 0; art_candidates[i] && n < cap; i++)
    {
        u32b cp  = (byte)art_candidates[i];
        u32b ink = art_measure(font, cp);

        if (!ink) continue;

        e[n].cp  = cp;
        e[n].ink = ink;
        n++;
    }

    if (n < 2) return;

    art_sort(e, n);

    art.ramp[0]    = ' ';
    art.ramp_count = MIN(ART_RAMP_STEPS, n + 1);

    for (i = 1; i < art.ramp_count; i++)
        art.ramp[i] = e[(i - 1) * (n - 1) / MAX(1, art.ramp_count - 2)].cp;

    /*
     * The shading blocks replace the top step, and nothing else.
     *
     * The obvious thing -- measure them alongside the letters and let the
     * sort place them -- makes the picture worse, and it is worth writing down
     * why. The ramp is spaced by *rank*, and a shading block is enormously
     * heavy: even a half-tone stipple carries more ink than `@`. So the blocks
     * take the top few ranks and every one of the twelve sample points slides
     * down with them. The whole progression comes out lighter and thinner --
     * measured: `^!vxlhCd#@` became `?"/xI]Xp$`, which is a page of punctuation
     * where there used to be a silhouette -- and all of it was spent to buy one
     * solid cell.
     *
     * So the alphabet keeps the progression it had, which has been looked at
     * and works, and the blocks do the one thing letters cannot: give the
     * darkest end something without holes in it.
     */
    if (extra && extra_ct > 0)
    {
        u32b best     = 0;
        u32b best_ink = e[n - 1].ink;   /* must beat the heaviest letter */

        for (i = 0; i < extra_ct; i++)
        {
            u32b ink = art_measure(font, extra[i]);

            if (ink > best_ink) { best_ink = ink; best = extra[i]; }
        }

        if (best) art.ramp[art.ramp_count - 1] = best;
    }
}

void ui_art_set_font(TTF_Font *font, int cell_w, int cell_h,
                     const u32b *extra, int extra_ct)
{
    int i;

    art.cell_w = cell_w > 0 ? cell_w : 1;
    art.cell_h = cell_h > 0 ? cell_h : 1;

    art_build_ramp(font, extra, extra_ct);

    /* Everything in the cache was drawn with the old alphabet. */
    for (i = 0; i < ART_CACHE_SLOTS; i++) art.slot[i].live = FALSE;
}

void ui_art_shutdown(void)
{
    memset(&art, 0, sizeof(art));
}


/**** Colour ****/

static byte art_luma(byte r, byte g, byte b)
{
    return (byte)((54 * r + 183 * g + 19 * b + 128) / 256);
}

static byte art_gain(byte c)
{
    int v = (c * ART_COLOUR_GAIN_NUM + ART_COLOUR_GAIN_DEN / 2)
          / ART_COLOUR_GAIN_DEN;

    return (byte)MIN(255, v);
}

static void art_readable(int r, int g, int b, ui_art_cell *cell)
{
    int peak = MAX(r, MAX(g, b));

    if (!peak)
    {
        cell->r = cell->g = cell->b = ART_DARK_NEUTRAL;
        return;
    }

    r = art_gain((byte)r);
    g = art_gain((byte)g);
    b = art_gain((byte)b);

    peak = MAX(r, MAX(g, b));
    if (peak && peak < ART_COLOUR_FLOOR)
    {
        r = (r * ART_COLOUR_FLOOR + peak / 2) / peak;
        g = (g * ART_COLOUR_FLOOR + peak / 2) / peak;
        b = (b * ART_COLOUR_FLOOR + peak / 2) / peak;
    }

    cell->r = (byte)MIN(255, r);
    cell->g = (byte)MIN(255, g);
    cell->b = (byte)MIN(255, b);
}


/**** Conversion ****/

typedef struct { int left, top, right, bottom; } art_bounds;

/*
 * The smallest rectangle containing the subject.
 *
 * Source paintings are centred but not tightly framed, and the empty margin
 * differs from one to the next. Cropping to the subject is what makes a row of
 * portraits look like a set rather than a set of accidents.
 */
static bool art_find_bounds(const byte *pixels, int w, int h, int pitch,
                            art_bounds *out)
{
    int x, y;

    if (!pixels || w <= 0 || h <= 0 || pitch < w * 4) return FALSE;

    out->left = w; out->top = h; out->right = -1; out->bottom = -1;

    for (y = 0; y < h; y++)
    {
        const byte *row = pixels + y * pitch;

        for (x = 0; x < w; x++)
        {
            if (row[x * 4 + 3] < ART_ALPHA_CROP) continue;

            if (x < out->left)   out->left = x;
            if (y < out->top)    out->top = y;
            if (x > out->right)  out->right = x;
            if (y > out->bottom) out->bottom = y;
        }
    }

    return out->right >= out->left && out->bottom >= out->top;
}

/*
 * A plate: the picture reduced to cells, but not yet to characters.
 *
 * This is the halfway point, and putting a name to it is what lets the source
 * paintings stop shipping. Everything expensive and everything that depends on
 * the *image* -- decoding a PNG, finding the subject's edges, pooling several
 * thousand pixels into each cell -- happens once, offline, and lands here.
 * Everything that depends on the *font* -- which character carries this much
 * ink, how the range is stretched, what colour reads on a dark panel -- is
 * still done at the moment of drawing.
 *
 * So a plate is four bytes a cell and a couple of hundred kilobytes for the
 * whole set, against three and a half megabytes of paintings; and portraits
 * still re-render in whatever typeface is loaded, because the thing stored is
 * what the picture *is*, not which letter was chosen last time.
 *
 * Two separate things per cell, and keeping them separate is the point:
 *
 *   cover -- how much of the cell the subject occupies, from the alpha. This
 *            is the silhouette, and nothing else knows where the creature
 *            ends.
 *   rgb   -- the subject's colour there, averaged in proportion to cover.
 *
 * These used to be one number, a "density" that multiplied coverage by
 * brightness. It cost the one question worth asking at drawing time -- is this
 * cell *inside* the creature, or is it a wisp of its edge? -- and without that
 * there is no way to say "solid cells are drawn boldly, edges are not", which
 * is what makes a small picture read as a body rather than a smudge.
 */
typedef struct
{
    int  cols, rows;
    int  src_w, src_h;                              /* of the painting */
    byte cover[UI_ART_MAX_COLS * UI_ART_MAX_ROWS];  /* 0 = nothing here */
    byte rgb[UI_ART_MAX_COLS * UI_ART_MAX_ROWS][3];
} art_plate;

/*
 * At or above this much coverage, out of 255, a cell is properly inside the
 * subject rather than a wisp of its edge -- so it gets the silhouette floor.
 * Where the *edge* is is a decision about the surface: see art_look.
 */
#define ART_COVER_SOLID 128    /* 0.50 */


/*
 * Painting -> plate. The offline half; nothing at play time calls this.
 */
static bool art_plate_from_pixels(const byte *pixels, int w, int h, int pitch,
                                  int cols, int rows, art_plate *plate)
{
    art_bounds b;
    int        cw, ch, col, row;

    if (!plate || cols <= 0 || rows <= 0) return FALSE;
    if (cols > UI_ART_MAX_COLS || rows > UI_ART_MAX_ROWS) return FALSE;
    if (!art_find_bounds(pixels, w, h, pitch, &b)) return FALSE;

    memset(plate, 0, sizeof(*plate));
    plate->cols  = cols;
    plate->rows  = rows;
    plate->src_w = w;
    plate->src_h = h;

    cw = b.right - b.left + 1;
    ch = b.bottom - b.top + 1;

    for (row = 0; row < rows; row++)
    {
        int top    = b.top + row * ch / rows;
        int bottom = b.top + (row + 1) * ch / rows;

        if (bottom <= top) bottom = top + 1;

        for (col = 0; col < cols; col++)
        {
            int   at    = row * cols + col;
            int   left  = b.left + col * cw / cols;
            int   right = b.left + (col + 1) * cw / cols;
            u32b  a_sum = 0, r_sum = 0, g_sum = 0, b_sum = 0;
            int   n = 0, x, y;
            int   avg_a, r, g, bl;

            if (right <= left) right = left + 1;

            for (y = top; y < bottom; y++)
            {
                const byte *src = pixels + y * pitch + left * 4;

                for (x = left; x < right; x++)
                {
                    byte a = src[3];

                    /* Colour is weighted by alpha, so the half-transparent
                     * pixels around an edge contribute their colour in
                     * proportion to how much of them is really there. Without
                     * that, a bright outline against nothing drags the whole
                     * cell towards the outline's colour. */
                    a_sum += a;
                    r_sum += src[0] * a;
                    g_sum += src[1] * a;
                    b_sum += src[2] * a;
                    n++;
                    src += 4;
                }
            }

            if (!n || !a_sum) continue;

            avg_a = (int)(a_sum / (u32b)n);
            if (avg_a < ART_ALPHA_VISIBLE) continue;

            r  = (int)(r_sum / a_sum);
            g  = (int)(g_sum / a_sum);
            bl = (int)(b_sum / a_sum);

            /*
             * Coverage and colour, stored raw and stored apart. How heavy a
             * character to spend on this cell is a question about the surface
             * it will be drawn on -- a map tile five pixels across wants a
             * bold silhouette, a portrait on a page wants tone -- and that
             * belongs at drawing time, where it can still be changed.
             */
            plate->cover[at]  = (byte)avg_a;
            plate->rgb[at][0] = (byte)r;
            plate->rgb[at][1] = (byte)g;
            plate->rgb[at][2] = (byte)bl;
        }
    }

    return TRUE;
}


/*
 * How a picture is turned into characters, for one kind of surface.
 *
 * The same plate serves a portrait on a page and a creature standing on a map
 * tile a few pixels across, and those two want opposite things.
 *
 * On a page there is room for tone: a face has cheekbones because some of its
 * cells are lighter than their neighbours. On a tile there is not. At five
 * pixels a cell no glyph is legible as itself, tonal subtlety reads as mud,
 * and what the eye needs is a bold coloured silhouette with a little variation
 * on top of it. So the surface says which it wants, and the conversion obeys.
 *
 * floor_pct is the whole difference and it is worth naming: a cell properly
 * inside the creature never falls below that percentage of the ramp, however
 * dark it really is. Without it a brown kobold at map size is a dark smudge --
 * every cell honest, none of them visible.
 */
typedef struct
{
    /*
     * What decides how heavy a cell is.
     *
     * On a page, both how much of the cell the subject covers and how bright
     * it is -- coverage leading, because a white highlight and a black eye are
     * both *there*, and if brightness led alone the creature would dissolve
     * wherever it was dark. On a tile, brightness alone: coverage has already
     * had its say in the silhouette floor below, and asking it twice flattens
     * the little tonal variation a five-pixel cell can still carry.
     */
    bool tone_by_cover;

    /* How much of a cell the subject must cover before the cell is drawn at
     * all. A tile wants this high: the soft rim of a picture five pixels a
     * cell is half its area, and drawn it turns a creature into a blur. A page
     * wants it low, because there the rim is the modelling. */
    int  bg_cover;

    /* The black and white points, as percentiles of the cells' tone -- over
     * the solid cells only, when the floor is going to look after the rest. */
    int  lo_pct, hi_pct;
    bool solid_only;

    int  gamma_pct;   /* the tone curve; under a hundred lifts the middle */

    int  floor_step;  /* solid cells never fall below this step...        */
    int  floor_pct;   /* ...nor below this percentage of the whole ramp   */

    /* Whether a cell that lands at the very bottom draws nothing at all.
     * On a tile that emptiness is the outline. On a page it is a hole. */
    bool drop_empty;
} art_look;

/* ART_LOOK_PAGE retains the whole-character-cell tuning for ui_art_get().
 * Current page callers request pixel-sized art through ART_LOOK_GRID. */
static const art_look ART_LOOK_PAGE = { TRUE,  24,  3, 97, FALSE, 100, 1,  0, FALSE };
static const art_look ART_LOOK_GRID = { FALSE, 76, 30, 97, TRUE,   75, 2, 60, TRUE  };


/*
 * Plate -> characters, at whatever size is wanted.
 *
 * Pooling a plate down works exactly as pooling pixels did -- average over the
 * block, weight the colour by how much is there -- because a plate cell is the
 * same kind of thing as a pixel, only larger.
 */
static bool art_plate_render(const art_plate *plate, int cols, int rows,
                             const art_look *look, ui_art_t *out)
{
    static byte cover[UI_ART_MAX_COLS * UI_ART_MAX_ROWS];
    static byte tone[UI_ART_MAX_COLS * UI_ART_MAX_ROWS];
    int hist[256];
    int col, row, i, n_cells;
    int counted = 0, inked = 0, lo = 0, hi = 255, span, floor_idx;

    if (!plate || !out || cols <= 0 || rows <= 0) return FALSE;
    if (cols > UI_ART_MAX_COLS || rows > UI_ART_MAX_ROWS) return FALSE;
    if (plate->cols <= 0 || plate->rows <= 0) return FALSE;
    if (art.ramp_count < 2) return FALSE;
    if (!look) look = &ART_LOOK_PAGE;

    memset(out, 0, sizeof(*out));
    memset(hist, 0, sizeof(hist));
    out->cols = cols;
    out->rows = rows;
    n_cells   = cols * rows;

    /* Pass one: pool the plate down onto the grid that was asked for. */
    for (row = 0; row < rows; row++)
    {
        int top    = row * plate->rows / rows;
        int bottom = (row + 1) * plate->rows / rows;

        if (bottom <= top) bottom = top + 1;

        for (col = 0; col < cols; col++)
        {
            int  at    = row * cols + col;
            int  left  = col * plate->cols / cols;
            int  right = (col + 1) * plate->cols / cols;
            u32b c_sum = 0, r_sum = 0, g_sum = 0, b_sum = 0;
            int  n = 0, x, y, avg_c, r, g, b;

            if (right <= left) right = left + 1;

            for (y = top; y < bottom; y++)
            {
                for (x = left; x < right; x++)
                {
                    int  p = y * plate->cols + x;
                    byte c = plate->cover[p];

                    c_sum += c;
                    r_sum += plate->rgb[p][0] * c;
                    g_sum += plate->rgb[p][1] * c;
                    b_sum += plate->rgb[p][2] * c;
                    n++;
                }
            }

            cover[at] = 0;
            tone[at]  = 0;

            if (!n || !c_sum) continue;

            avg_c = (int)(c_sum / (u32b)n);

            /* Too little of the subject here to be anything but background,
             * and background is how a picture with no frame says where the
             * creature ends. */
            if (avg_c < look->bg_cover) continue;

            r = (int)(r_sum / c_sum);
            g = (int)(g_sum / c_sum);
            b = (int)(b_sum / c_sum);

            {
                int l = art_luma((byte)r, (byte)g, (byte)b);
                int t = look->tone_by_cover
                      ? (avg_c * ((3 * l + 255) / 4) + 127) / 255
                      : l;

                cover[at] = (byte)avg_c;
                tone[at]  = (byte)(t > 255 ? 255 : t);
            }

            out->cells[at].ink = TRUE;
            inked++;
            art_readable(r, g, b, &out->cells[at]);

            if (!look->solid_only || avg_c >= ART_COVER_SOLID)
            {
                hist[tone[at]]++;
                counted++;
            }
        }
    }

    if (!inked) return FALSE;

    /* The black and white points. Too few cells to be a picture at all -- a
     * chain of specks -- and the honest range is the whole range. */
    if (counted >= 8)
    {
        int cut, acc, v;

        cut = counted * look->lo_pct / 100;
        for (acc = 0, v = 0; v < 255; v++)
        {
            acc += hist[v];
            if (acc > cut) break;
        }
        lo = v;

        cut = counted * (100 - look->hi_pct) / 100;
        for (acc = 0, v = 255; v > 0; v--)
        {
            acc += hist[v];
            if (acc > cut) break;
        }
        hi = v;
    }

    if (hi <= lo) { lo = 0; hi = 255; }
    span = hi - lo;

    floor_idx = 1 + (look->floor_pct * (art.ramp_count - 2)) / 100;
    if (floor_idx < look->floor_step)  floor_idx = look->floor_step;
    if (floor_idx > art.ramp_count - 1) floor_idx = art.ramp_count - 1;

    /* Pass two: brightness to a step of the ramp. */
    for (i = 0; i < n_cells; i++)
    {
        ui_art_cell *cell = &out->cells[i];
        int          t, idx;

        if (!cell->ink) continue;

        t = ((int)tone[i] - lo) * 1000 / span;
        if (t < 0)    t = 0;
        if (t > 1000) t = 1000;

        if (look->gamma_pct != 100 && t > 0)
        {
            double g = (double)look->gamma_pct / 100.0;

            t = (int)(1000.0 * pow((double)t / 1000.0, g) + 0.5);
        }

        idx = (t * (art.ramp_count - 1) + 500) / 1000;

        /* The silhouette floor. Only for cells that are properly inside the
         * creature: fattening the soft rim as well would trade the outline
         * for a cut-out. */
        if (cover[i] >= ART_COVER_SOLID && idx < floor_idx) idx = floor_idx;

        if (idx < 1)
        {
            if (look->drop_empty) { cell->ink = FALSE; continue; }
            idx = 1;
        }
        if (idx > art.ramp_count - 1) idx = art.ramp_count - 1;

        cell->cp = art.ramp[idx];
    }

    return TRUE;
}


/**** Finding the painting ****/

/*
 * A name, reduced to something that can be a filename: lower case, letters and
 * digits kept, every other run turned into a single hyphen. "Grip, Farmer
 * Bulley's Dog" becomes "grip-farmer-maggot-s-dog".
 */
static void art_make_id(char *dst, size_t cap, cptr name)
{
    size_t used = 0;
    bool   gap  = FALSE;

    if (!dst || !cap) return;
    dst[0] = '\0';
    if (!name) return;

    while (*name && used + 1 < cap)
    {
        char c = 0;

        if (*name >= 'A' && *name <= 'Z')       c = *name - 'A' + 'a';
        else if ((*name >= 'a' && *name <= 'z')
              || (*name >= '0' && *name <= '9')) c = *name;

        if (c)
        {
            if (gap && used && used + 1 < cap) dst[used++] = '-';
            dst[used++] = c;
            gap = FALSE;
        }
        else if (used) gap = TRUE;

        name++;
    }

    dst[used] = '\0';
}

/*
 * Where the painting for `id` would live. Whether it is actually there is not
 * asked separately -- the load is about to answer that, and most names have no
 * painting, so an existence check would only be a slower way to be told no.
 */
static bool art_path(char *path, size_t cap, int collection, cptr id,
                     cptr suffix)
{
    char dir[1024];
    char file[ART_ID_MAX + 8];

    if (!SCOTTISH_DIR_ART || !SCOTTISH_DIR_ART[0]) return FALSE;

    strnfmt(file, sizeof(file), "%s%s", id, suffix);
    path_build(dir, sizeof(dir), SCOTTISH_DIR_ART,
               collection == UI_ART_SHOPKEEPER ? "shopkeepers" :
               collection == UI_ART_OBJECT     ? "objects" : "monsters");
    path_build(path, cap, dir, file);

    return TRUE;
}


/**** Plates on disk ****/

/*
 * "FROGART2", then two shorts for the grid and two for the painting it came
 * from, then four bytes a cell: coverage, then red, green and blue. Little-endian and written a byte at a time, so
 * the file does not depend on this machine's word order or on how the compiler
 * felt about padding the struct.
 */
#define ART_MAGIC "FROGART2"

static void _put16(FILE *fp, int v)
{
    fputc(v & 0xFF, fp);
    fputc((v >> 8) & 0xFF, fp);
}

static int _get16(FILE *fp)
{
    int lo = fgetc(fp);
    int hi = fgetc(fp);

    if (lo == EOF || hi == EOF) return -1;

    return lo | (hi << 8);
}

static bool art_plate_write(cptr path, const art_plate *plate)
{
    FILE *fp = my_fopen(path, "wb");
    int   i, n;

    if (!fp) return FALSE;

    fwrite(ART_MAGIC, 1, 8, fp);
    _put16(fp, plate->cols);
    _put16(fp, plate->rows);
    _put16(fp, plate->src_w);
    _put16(fp, plate->src_h);

    n = plate->cols * plate->rows;
    for (i = 0; i < n; i++)
    {
        fputc(plate->cover[i], fp);
        fputc(plate->rgb[i][0], fp);
        fputc(plate->rgb[i][1], fp);
        fputc(plate->rgb[i][2], fp);
    }

    my_fclose(fp);

    return TRUE;
}

static bool art_plate_read(cptr path, art_plate *plate)
{
    FILE *fp = my_fopen(path, "rb");
    char  magic[8];
    int   i, n;

    if (!fp) return FALSE;

    memset(plate, 0, sizeof(*plate));

    if (fread(magic, 1, 8, fp) != 8 || memcmp(magic, ART_MAGIC, 8))
    {
        my_fclose(fp);
        return FALSE;
    }

    plate->cols  = _get16(fp);
    plate->rows  = _get16(fp);
    plate->src_w = _get16(fp);
    plate->src_h = _get16(fp);

    if (plate->cols < 1 || plate->cols > UI_ART_MAX_COLS
     || plate->rows < 1 || plate->rows > UI_ART_MAX_ROWS)
    {
        my_fclose(fp);
        return FALSE;
    }

    n = plate->cols * plate->rows;
    for (i = 0; i < n; i++)
    {
        int d = fgetc(fp), r = fgetc(fp), g = fgetc(fp), b = fgetc(fp);

        if (b == EOF) { my_fclose(fp); return FALSE; }

        plate->cover[i] = (byte)d;
        plate->rgb[i][0]  = (byte)r;
        plate->rgb[i][1]  = (byte)g;
        plate->rgb[i][2]  = (byte)b;
    }

    my_fclose(fp);

    return TRUE;
}


/**** The cache ****/

/*
 * How many columns and rows to divide the painting into, so that the result
 * fits the box it was asked for and is still the shape it started as.
 *
 * Cells are taller than they are wide, so a portrait that is square in pixels
 * needs roughly twice as many columns as rows. Getting this from the real cell
 * size rather than a constant means the portrait stays honest at every
 * interface size and in every typeface.
 *
 * min_w and min_h are the smallest cell worth asking for, and they belong to
 * the caller because they belong to the cell it is fitting against. Holding
 * the character minimums here while a caller fitted against square sub-tiles
 * meant the height guard alone fired, and the picture came back squatter than
 * it went in -- a distortion introduced by a guard against grit.
 */
static void art_fit(int src_w, int src_h, int box_cols, int box_rows,
                    int min_w, int min_h, int *cols, int *rows)
{
    int c = box_cols;
    int r = box_rows;

    if (c > UI_ART_MAX_COLS) c = UI_ART_MAX_COLS;
    if (r > UI_ART_MAX_ROWS) r = UI_ART_MAX_ROWS;
    if (c < 1) c = 1;
    if (r < 1) r = 1;

    if (src_w > 0 && src_h > 0 && art.cell_w > 0 && art.cell_h > 0)
    {
        /* Widest the picture may be, at this height, without stretching. */
        int want_c = (r * art.cell_h * src_w) / (src_h * art.cell_w);

        if (want_c < c && want_c >= 1) c = want_c;
        else if (want_c > c)
        {
            int want_r = (c * art.cell_w * src_h) / (src_w * art.cell_h);

            if (want_r >= 1 && want_r < r) r = want_r;
        }
    }

    /* A cell too small to hold a character is not detail, it is grit. */
    while (c > 1 && box_cols * art.cell_w / c < min_w) c--;
    while (r > 1 && box_rows * art.cell_h / r < min_h) r--;

    *cols = c;
    *rows = r;
}

static art_slot *art_claim(void)
{
    art_slot *pick = &art.slot[0];
    int       i;

    for (i = 0; i < ART_CACHE_SLOTS; i++)
    {
        if (!art.slot[i].live) return &art.slot[i];
        if (art.slot[i].used < pick->used) pick = &art.slot[i];
    }

    return pick;
}

static const ui_art_t *art_fetch(int collection, cptr name,
                                 int box_cols, int box_rows, bool by_px)
{
    char       id[ART_ID_MAX];
    char       path[1024];
    art_plate  plate;
    art_slot  *slot;
    int        cols, rows, i;
    bool       ok;

    if (art.ramp_count < 2) return NULL;
    if (!name || !name[0]) return NULL;

    art_make_id(id, sizeof(id), name);
    if (!id[0]) return NULL;

    /*
     * The box is part of the key, not the fitted size. The same monster on the
     * recall page and in a corner of the shop screen are two different
     * conversions -- each cell was pooled from a different patch of the
     * painting -- and the box is what the caller knows before the painting has
     * been opened, so it is the only thing a lookup can be made of.
     */
    for (i = 0; i < ART_CACHE_SLOTS; i++)
    {
        art_slot *s = &art.slot[i];

        if (!s->live) continue;
        if (s->collection != collection) continue;
        if (s->by_px != by_px) continue;
        if (s->box_cols != box_cols || s->box_rows != box_rows) continue;
        if (strcmp(s->id, id)) continue;

        s->used = ++art.clock;
        return s->empty ? NULL : &s->art;
    }

    if (!art_path(path, sizeof(path), collection, id, ".art")
     || !art_plate_read(path, &plate))
    {
        /* Remember the absence. Most of the fourteen hundred races have no
         * portrait, and the recall page is rebuilt on every keypress -- so
         * without this, reading one page means opening a file that is not
         * there, over and over. */
        slot = art_claim();
        my_strcpy(slot->id, id, sizeof(slot->id));
        slot->collection = collection;
        slot->box_cols   = box_cols;
        slot->box_rows   = box_rows;
        slot->by_px      = by_px;
        slot->empty      = TRUE;
        slot->live       = TRUE;
        slot->used       = ++art.clock;
        return NULL;
    }

    /* The painting's own proportions travel with the plate, so the target grid
     * is still worked out against the cell shape in use right now rather than
     * whatever it was when the plate was made. */
    if (by_px)
    {
        /*
         * A pixel rectangle rather than a box of terminal cells: this is a
         * portrait squeezed into one grid of a map, where the space has
         * nothing to do with the size of a character.
         *
         * Fitted against a *square* sub-tile, because that is what it will be
         * drawn on -- the map's tiles are square and the picture's cells are
         * square within them. Asking for the painting's proportions against a
         * one-to-two cell and then drawing on square ones is how a picture
         * comes out the wrong shape.
         */
        int keep_w = art.cell_w, keep_h = art.cell_h;

        art.cell_w = ART_MIN_TILE;
        art.cell_h = ART_MIN_TILE;
        art_fit(plate.src_w, plate.src_h,
                box_cols / ART_MIN_TILE, box_rows / ART_MIN_TILE,
                ART_MIN_TILE, ART_MIN_TILE, &cols, &rows);
        art.cell_w = keep_w;
        art.cell_h = keep_h;
    }
    else art_fit(plate.src_w, plate.src_h, box_cols, box_rows,
                 ART_MIN_CELL_W, ART_MIN_CELL_H, &cols, &rows);

    slot = art_claim();
    ok = art_plate_render(&plate, cols, rows,
                          by_px ? &ART_LOOK_GRID : &ART_LOOK_PAGE, &slot->art);

    /* art_claim() may have picked a slot that was holding something; it is
     * gone either way now, so do not leave it claiming to hold it. */
    if (!ok)
    {
        slot->live = FALSE;
        return NULL;
    }

    my_strcpy(slot->id, id, sizeof(slot->id));
    slot->collection = collection;
    slot->box_cols   = box_cols;
    slot->box_rows   = box_rows;
    slot->by_px      = by_px;
    slot->empty      = FALSE;
    slot->live       = TRUE;
    slot->used       = ++art.clock;

    return &slot->art;
}

/* By boxes of character cells. No live caller: see ART_LOOK_PAGE above. */
const ui_art_t *ui_art_get(int collection, cptr name,
                           int box_cols, int box_rows)
{
    return art_fetch(collection, name, box_cols, box_rows, FALSE);
}

const ui_art_t *ui_art_get_px(int collection, cptr name, int px_w, int px_h)
{
    if (px_w < ART_MIN_CELL_W * 3 || px_h < ART_MIN_CELL_H * 3) return NULL;

    return art_fetch(collection, name, px_w, px_h, TRUE);
}


/**** Making the plates ****/

/*
 * Painting to plate, for every file in a directory. Reached with -A.
 *
 * Run rarely and by hand, which is the point: the paintings are a source
 * asset, not a shipped one, and this is the step that lets them stay outside
 * the tree entirely.
 *
 * The grid is fitted against a cell that is assumed to be twice as tall as it
 * is wide, rather than against whatever typeface happens to be loaded when
 * this runs. A plate should be the same file whoever generates it; the real
 * cell shape is applied later, when the plate is pooled down to the size a
 * page actually asked for.
 */
static bool art_generate_one(cptr in_path, cptr out_path)
{
    SDL_Surface *loaded, *rgba;
    art_plate    plate;
    int          cols, rows;
    bool         ok;

    loaded = IMG_Load(in_path);
    if (!loaded)
    {
        printf("  ! could not read %s\n", in_path);
        return FALSE;
    }

    rgba = SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(loaded);
    if (!rgba) return FALSE;

    /*
     * A canonical cell, so the plate does not depend on the machine that made
     * it -- and a *square* one, because a plate is a picture and not a page.
     *
     * This was ten by twenty, the shape of a character cell, and that was the
     * mistake: it baked the proportions of text into the stored asset, so a
     * square painting became forty-eight cells across by twenty-four down.
     * Drawn back onto the map's square tiles the creature came out half its
     * proper height, and no amount of care at the drawing end could put back
     * detail thrown away at the storing end. Square here, and every consumer
     * pools it down to its own cell shape -- which art_fit already does.
     *
     * Ten by ten rather than one by one for the same reason as before: the
     * ratio is what matters, but art_fit refuses to ask for detail finer than
     * a few pixels a cell, and a one-by-one cell trips that guard. Restored
     * afterwards.
     */
    {
        int keep_w = art.cell_w, keep_h = art.cell_h;

        art.cell_w = 10;
        art.cell_h = 10;
        art_fit(rgba->w, rgba->h, UI_ART_MAX_COLS, UI_ART_MAX_ROWS,
                ART_MIN_CELL_W, ART_MIN_CELL_W, &cols, &rows);
        art.cell_w = keep_w;
        art.cell_h = keep_h;
    }

    ok = art_plate_from_pixels((const byte *)rgba->pixels, rgba->w, rgba->h,
                               rgba->pitch, cols, rows, &plate);
    SDL_DestroySurface(rgba);

    if (!ok)
    {
        printf("  ! nothing visible in %s\n", in_path);
        return FALSE;
    }

    if (!art_plate_write(out_path, &plate))
    {
        printf("  ! could not write %s\n", out_path);
        return FALSE;
    }

    printf("  %-34s %2d x %-2d  %5d bytes\n",
           out_path, plate.cols, plate.rows, 8 + 8 + plate.cols * plate.rows * 4);

    return TRUE;
}

void ui_art_generate(cptr src_dir, cptr out_dir)
{
    char pattern[1024];
    int  made = 0;

#ifdef WINDOWS
    /* A new collection has no output folder yet, and art_generate_one fails
     * quietly when it cannot open the file it was asked to write. The first
     * run of the objects collection wrote nothing and reported "0 plates" --
     * the true answer, and useless. Make the folder rather than expect it.
     * Harmless when it is already there. */
    (void)_mkdir(out_dir);
#endif

#ifdef WINDOWS
    struct _finddata_t entry;
    intptr_t           handle;

    path_build(pattern, sizeof(pattern), src_dir, "*.png");

    handle = _findfirst(pattern, &entry);
    if (handle < 0)
    {
        printf("No paintings in %s\n", src_dir);
        return;
    }

    do
    {
        char in_path[1024], out_path[1024], stem[ART_ID_MAX + 8];
        char *dot;

        my_strcpy(stem, entry.name, sizeof(stem));
        dot = strrchr(stem, '.');
        if (dot) *dot = '\0';

        path_build(in_path, sizeof(in_path), src_dir, entry.name);
        strnfmt(pattern, sizeof(pattern), "%s.art", stem);
        path_build(out_path, sizeof(out_path), out_dir, pattern);

        if (art_generate_one(in_path, out_path)) made++;
    }
    while (_findnext(handle, &entry) == 0);

    _findclose(handle);
#else
    (void)pattern;
    printf("Plate generation is only wired up for Windows so far.\n");
#endif

    printf("%d plate%s written to %s\n", made, (made == 1) ? "" : "s", out_dir);
}
