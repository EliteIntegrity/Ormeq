/*
 * File: ui_art.h
 *
 * Portraits, as coloured characters.
 *
 * A monster in this game is one letter. That letter is doing a great deal of
 * work -- it tells you the family, and the colour tells you the kind -- but it
 * is still one letter, and the first time you meet something the recall page
 * describes it in prose you have to read to picture. This turns a painting of
 * the thing into characters, so the page can show you what it is as well as
 * tell you.
 *
 * The important part is that it is not a tile set. There are no pixels on the
 * screen that are not characters, and the characters are the ones your chosen
 * typeface draws. It works like this:
 *
 *   - Every glyph that might be used is rendered once in the current font and
 *     its ink is measured. Sorting by ink gives a ramp from light to dark that
 *     is *correct for that font* -- VT323's comma is not Cozette's comma, and
 *     a ramp built for one is wrong for the other.
 *   - The source painting is cropped to its subject, divided into a grid, and
 *     each cell's pixels are pooled: their average alpha decides how dark the
 *     cell is, and so which glyph off the ramp it gets, while their
 *     alpha-weighted average colour decides what colour to draw it in.
 *
 * So the same portrait re-renders in a different alphabet when you change the
 * typeface, and stays a picture, because the ramp was measured rather than
 * guessed.
 *
 * This is frontend-private. The game says only "a portrait of X belongs in
 * this box" (UI_EVENT_PORTRAIT) and never learns whether one existed.
 *
 * Copyright (c) 2026 John Horton.
 *
 * Added since the fork and offered on the same terms as the rest of the
 * tree -- the Angband licence in LICENSE.
 */

#ifndef INCLUDED_UI_ART_H
#define INCLUDED_UI_ART_H

/*
 * The most detail worth asking for. Past this the cells are smaller than the
 * eye separates at normal reading distance, and the conversion stops being a
 * picture made of letters and becomes a photograph of noise.
 *
 * Square, because a plate is a picture rather than a page: it is stored on a
 * grid of square cells and pooled down to whatever shape the cell happens to
 * be at the moment it is drawn. Storing it on a one-by-two grid -- which is
 * what a page of text is -- threw away half the vertical detail before
 * anything had asked for it, and a portrait drawn on the map's square tiles
 * came back with the resolution of a thumbnail.
 */
/*
 * 96 rather than 48 since 29 August 2026.
 *
 * This is the resolution of the stored asset, and 48 was chosen when the
 * largest thing that drew one was a 30x20 page. A shopkeeper's portrait is now
 * given a 440x680 box, and 48 cells across that is a fourteen-pixel glyph
 * where the look card's are four -- the same picture, drawn coarser, because
 * the detail was thrown away at the storing end and no care at the drawing end
 * can put it back.
 *
 * The cost is memory and disk, both of which are cheap: sixteen cached
 * pictures at 74k each, and the plates on disk go from 5.3MB to about 21MB.
 */
#define UI_ART_MAX_COLS 96
#define UI_ART_MAX_ROWS 96

typedef struct
{
    byte r, g, b;
    u32b cp;       /* a codepoint, not a char: the heavy end of the ramp lives
                    * above ASCII, where nothing the game emits can reach */
    bool ink;      /* FALSE: nothing here, leave the ground showing */
} ui_art_cell;

typedef struct
{
    int         cols, rows;
    ui_art_cell cells[UI_ART_MAX_COLS * UI_ART_MAX_ROWS];
} ui_art_t;

/*
 * Whose densities to measure, and the shape of a cell.
 *
 * Borrowed, not owned -- the frontend already has this font open for the
 * atlas, and measuring a second copy of it would only be a way to disagree
 * with ourselves. Call it whenever the atlas is rebuilt; the ramp is remade
 * and everything converted under the old font is dropped.
 *
 * `extra` is the decorative codepoints above ASCII that the atlas actually
 * carries, which is why they arrive from the frontend rather than being
 * decided here: the atlas is the only thing that knows what it can draw, and
 * two places deciding that separately is two places to disagree. Pass none and
 * the ramp is built from printable ASCII, exactly as it was.
 */
extern void ui_art_set_font(TTF_Font *font, int cell_w, int cell_h,
                            const u32b *extra, int extra_ct);

/*
 * The portrait of `name` from `collection` (UI_ART_MONSTER or
 * UI_ART_SHOPKEEPER), converted to fit inside a box of the given size without
 * distorting it.
 *
 * NULL when there is no painting for that name, which is the common case and
 * not a failure: the caller draws the classic glyph instead. The result
 * belongs to the cache and is good until the next call.
 */
extern const ui_art_t *ui_art_get(int collection, cptr name,
                                  int box_cols, int box_rows);

/*
 * The same, but fitted to a rectangle measured in pixels.
 *
 * For one grid of a zoomed-in map, where the space a portrait has to live in
 * has nothing to do with the size of a character. NULL if the rectangle is too
 * small to hold a picture worth calling one.
 */
extern const ui_art_t *ui_art_get_px(int collection, cptr name,
                                     int px_w, int px_h);

/*
 * Turn every painting in `src_dir` into a plate in `out_dir`.
 *
 * Run by hand with -A, rarely. The paintings are a source asset and live
 * outside the tree; the plates are what ships, at four bytes a cell instead of
 * a megabyte and a half of PNG.
 */
extern void ui_art_generate(cptr src_dir, cptr out_dir);

extern void ui_art_shutdown(void);

#endif
