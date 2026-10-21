/*
 * File: main-sdl3.c
 *
 * An SDL3 frontend for Ormeq.
 *
 * This is a fresh frontend, not a port of main-sdl.c (which is SDL 1.2 and
 * built around software surface blits). Everything here goes through an
 * SDL_Renderer, so the drawing is GPU-accelerated and the window can be
 * resized freely.
 *
 * Structure:
 *   - The game draws through the Term abstraction (see z-term.h): a grid of
 *     cells, each holding one char and one colour index. A frontend is a set
 *     of hooks Term calls when it wants real pixels.
 *   - All hooks render into a single "grid" target texture. TERM_XTRA_FRESH
 *     copies that texture to the window one texel to one pixel, and presents.
 *   - Glyphs come from a one-row atlas texture built once at startup by
 *     rendering the printable ASCII range as a single string. Because the
 *     font is monospaced, glyph N sits at exactly x = N * cell_w, which
 *     saves us doing any glyph metric arithmetic.
 *   - Resizing the window changes how many cells fit, not how big they are:
 *     a bigger window shows more map rather than a magnified one.
 *
 * This file owns Term hooks, input, the event pump, window and compositing.
 * Map rendering, animation, look cards, marks and glyph atlases live in
 * sdl3_map.c, sdl3_anim.c, sdl3_look.c, sdl3_marks.c and sdl3_atlas.c.
 *
 * Copyright (c) 2026 John Horton.
 *
 * Added since the fork and offered on the same terms as the rest of the
 * tree -- the Angband licence in LICENSE.
 */

/*
 * SDL's headers MUST be included before angband.h.
 *
 * angband.h (via h-type.h) does:
 *     #define bool bool_hack
 *     #define byte byte_hack
 * to dodge a C++ compatibility problem from the 1990s. Those are plain
 * preprocessor macros, so any header included afterwards that mentions
 * "bool" gets silently rewritten -- and SDL3 uses bool throughout its API.
 * Include SDL first and its declarations are parsed against the real bool.
 */
#include <SDL3/SDL.h>

#include <math.h>   /* sin, for the last letter of the title */
#include <SDL3/SDL_main.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <SDL3_image/SDL_image.h>

#include "sdl3_local.h"   /* SDL first, then the game -- see the note */
#include "ui_sound.h"
#include "ui_ambient.h"
#include "ui_music.h"


/*
 * Defaults. All of these want to become options later.
 */
#define SDL3_COLS        80
#define SDL3_ROWS        27
#define SDL3_KEY_QUEUE   1024

/* The game's screen layout assumes a classic terminal, so never go below the
 * starting size. A term_win stores its cursor as a byte, which is where the
 * upper bound comes from -- see z-term.h. */
#define SDL3_MIN_COLS    SDL3_COLS
#define SDL3_MIN_ROWS    SDL3_ROWS
#define SDL3_MAX_DIM     255

/* GLYPH_FIRST, GLYPH_LAST and GLYPH_COUNT are in sdl3_local.h: the atlas
 * that covers them is built in sdl3_atlas.c and drawn from here. */


/*
 * Everything the frontend owns.
 */
sdl3_state sdl3;


/*
 * Set from the command line; see sdl3_parse_args(). The typeface's own
 * settings went to sdl3_atlas.c with it.
 */
bool  sdl3_verbose = FALSE;        /* -v: report geometry at startup  */

/*
 * -w: start windowed whatever the preference says.
 *
 * For driving the game from a script while somebody is using the machine. A
 * fullscreen window takes the whole screen and the keyboard with it, so an
 * automated run stops being a background task and becomes an interruption --
 * and a stray turn of the mouse wheel over it lands on the map as a zoom.
 */
static bool  sdl3_windowed = FALSE;


/*
 * -x<keys>: a string of keystrokes to play into the game at startup.
 *
 * For driving it from a script. Windows will not hand the foreground to a
 * window it did not see the user open, and SDL ignores keys posted to a window
 * that is not focused, so from outside the process there is no way to type at
 * it that does not also risk typing at whatever the person at the machine is
 * doing. From inside there is.
 *
 * '~' stands for Return and '^' for Escape; '{' '}' '[' ']' are the four
 * arrows; and a '\' sends the next character exactly as written, which is the
 * way back to the six spent here -- '~' in particular is the knowledge
 * command, and everything behind it would otherwise be unreachable.
 */
static cptr  sdl3_script = NULL;
static cptr sdl3_capture = NULL;
static bool sdl3_capture_pending;
static int sdl3_requested_w, sdl3_requested_h;
static bool sdl3_bolt_actor;
static char sdl3_bolt_tile[160];
static float sdl3_bolt_w, sdl3_bolt_h;
static int   sdl3_start_depth = 0;   /* -D<n>: open on this dungeon floor */
static int   sdl3_start_dungeon = 1; /* -Y<n>: and in this dungeon */
static bool  sdl3_dived = FALSE;
static int   sdl3_start_level = 0;   /* -L<n>: play up to character level n */
static int   sdl3_script_at = 0;




/*
 * Special keys, named exactly as SDL 1.2 named them.
 *
 * The game maps keys to commands through lib/pref/pref-sdl.prf, which spells
 * its triggers with SDL 1.2's key names ("up", "[0]" for keypad zero, and so
 * on). SDL3 renamed several of these -- SDL_GetKeyName(SDLK_KP_0) is now
 * "Keypad 0" -- so we translate here instead, and keep SCOTTISH_SYS as "sdl".
 * That buys us a working keymap for free.
 *
 * If we ever design our own input layer, this table and its pref file go
 * together.
 */
static cptr sdl3_special_key_name(SDL_Keycode key)
{
    switch (key)
    {
        case SDLK_UP:       return "up";
        case SDLK_DOWN:     return "down";
        case SDLK_LEFT:     return "left";
        case SDLK_RIGHT:    return "right";
        case SDLK_HOME:     return "home";
        case SDLK_END:      return "end";
        case SDLK_PAGEUP:   return "page_up";
        case SDLK_PAGEDOWN: return "page_down";

        case SDLK_KP_0:     return "[0]";
        case SDLK_KP_1:     return "[1]";
        case SDLK_KP_2:     return "[2]";
        case SDLK_KP_3:     return "[3]";
        case SDLK_KP_4:     return "[4]";
        case SDLK_KP_5:     return "[5]";
        case SDLK_KP_6:     return "[6]";
        case SDLK_KP_7:     return "[7]";
        case SDLK_KP_8:     return "[8]";
        case SDLK_KP_9:     return "[9]";
    }

    return NULL;
}


static bool sdl3_is_keypad(SDL_Keycode key)
{
    return BOOL(key >= SDLK_KP_DIVIDE && key <= SDLK_KP_PERIOD);
}


/*
 * Pick the draw colour from the game's palette.
 *
 * ormeq_color_table[i] is {unused, r, g, b}.
 */
static void sdl3_draw_colour(byte a)
{
    SDL_SetRenderDrawColor(sdl3.renderer,
                           ormeq_color_table[a][1],
                           ormeq_color_table[a][2],
                           ormeq_color_table[a][3],
                           SDL_ALPHA_OPAQUE);
}


/*
 * A chrome colour from the current theme.
 *
 * Deliberately separate from sdl3_draw_colour(), which takes a *game* colour
 * index. The map speaks in game colours and the interface speaks in roles;
 * mixing them means a retint in one place quietly wrecks the other.
 */
/*
 * Where the grid sits inside the window: half of whatever it does not fill.
 * One place computes it, because the renderer and the mouse have to agree.
 */
void sdl3_grid_origin(int *ox, int *oy)
{
    int out_w = 0, out_h = 0;

    SDL_GetRenderOutputSize(sdl3.renderer, &out_w, &out_h);

    *ox = (out_w - sdl3.grid_w) / 2;
    *oy = (out_h - sdl3.grid_h) / 2;

    if (*ox < 0) *ox = 0;
    if (*oy < 0) *oy = 0;
}


static void sdl3_ui_colour(int role)
{
    theme_color_t c = theme_ui(role);
    SDL_SetRenderDrawColor(sdl3.renderer, c.r, c.g, c.b, c.a);
}


/*
 * The bar under the selected row.
 *
 * A menu inside a frame can afford a solid one: it has a panel to lie in and
 * the two read as a single piece of furniture. The home screen has no frame,
 * and there the same bar came out as a green plank laid across the picture --
 * the second loudest thing on the screen after the title, which is not what it
 * is for. Dropped to a wash it does the identical job, because what marks the
 * row is the change in ground, not the strength of it.
 */
static void sdl3_selection_colour(void)
{
    theme_color_t c = theme_ui(THEME_UI_SELECTION);

    SDL_SetRenderDrawColor(sdl3.renderer, c.r, c.g, c.b,
                           sdl3_home_active() ? 104 : c.a);
}


/* Defined below, with the panel state. Reports the columns the frame covers on
 * row y, so a fill can be split where it crosses the edge. */
static bool sdl3_panel_span(int y, int *x0, int *x1);

/*
 * The menu's heading row and its selected row.
 *
 * Held the same way as the panel, and for the same reason: both are chrome in
 * the theme's interface colours, which a document cannot ask for -- it has
 * only the thirty-two colours the game draws the world in. The document says
 * where they are; this decides what they look like.
 */
/*
 * Gauges: a row whose background says how full something is.
 *
 * A fixed slot per row rather than a list, because the two that exist
 * redeclare themselves every time they are drawn and a list would only grow.
 */
#define SDL3_MAX_GAUGES 4

/*
 * How solid a pane of chrome is while the level is behind it.
 *
 * Nearer a window than a wash. It started at 218 of 255, which read as solid:
 * the panes were surfaces the dungeon happened to be behind rather than glass
 * it was seen through, and the point of putting the level behind the terminal
 * is lost if you cannot tell that it is there. Under 150 the text starts
 * fighting whatever it is standing over.
 *
 * All of which was measured against a map made of characters: a near-black
 * canvas with sparse glyphs on it, where a hundred and fifty-two is plenty.
 * Tile mode puts opaque grass behind the same pane and the message line
 * becomes something you have to squint through. So the glass gets thicker
 * when there are tiles behind it -- the level is still visible through it,
 * which is the whole point, but the words win.
 */
#define SDL3_CHROME_ALPHA 196
#define SDL3_CHROME_ALPHA_TILES 226

static struct { short x, y, w, cur, max; byte attr; bool live; }
    sdl3_gauge[SDL3_MAX_GAUGES];

static int sdl3_title_row = -1, sdl3_title_x, sdl3_title_w;
static int sdl3_select_row = -1, sdl3_select_x, sdl3_select_w;

/*
 * How wide a highlighted row is drawn.
 *
 * The frame, where there is one, rather than the width of the text. A bar
 * that stops short of the value on the right of the row looks like it has
 * been cut off, and the eye reads the frame as the edge of the thing being
 * pointed at. Falls back to the reported text width when no frame is up.
 */
/* Written further down, needed here. */
static bool sdl3_panel_fullscreen(void);

static void sdl3_row_span(int y, int fallback_x, int fallback_w,
                          int *x0, int *x1)
{
    /*
     * A bar reaches the frame it is inside -- but a *page* has no frame, and
     * its ground is the whole window, so stretching to it drew a highlight
     * from one edge of the screen to the other across a column of text. On a
     * page the game's own width is the answer, and the game has been sending
     * it all along.
     */
    if (!sdl3_panel_fullscreen() && sdl3_panel_span(y, x0, x1))
    {
        /* Inside the frame, not over it. */
        (*x0)++;
        (*x1)--;
        return;
    }

    *x0 = fallback_x;
    *x1 = fallback_x + fallback_w;
}


/* Colour helpers, defined with the terrain drawing further down. */
static void sdl3_draw_chrome(void);
static bool sdl3_cell_hidden(int x, int y);

/*
 * The zoomed map transform. One place computes it, because two things have to
 * agree exactly: what gets drawn, and what a click means.
 *
 *   cave grid -> pixel:  origin + grid * scale
 *   pixel -> cave grid:  (pixel - origin) / scale
 */


/* Hoisted from the zoom section below: the size of a map tile depends on it,
 * and the size of a map tile is decided up here. */
float sdl3_zoom = 1.0f;

/*
 * Two that are used above where they are written.
 *
 * Without these the compiler assumes each returns int and takes anything --
 * which is what C does with an undeclared function, and which happened to be
 * true of both. Happening to be true is not the same as being true, and the
 * day one of them starts returning a float is the day it breaks in a way
 * nothing points at.
 */
static int  sdl3_gauge_full(int gi, byte out[3]);

/* Whether tile mode is both wanted and possible. Defined with the zoom it
 * depends on; declared here because the tile size is decided above it. */

/* Where a glyph sits inside a square tile. See sdl3_map_quad(). */

/*
 * The side of a map tile at zoom 1, in pixels.
 *
 * The cell's height, which is the taller side -- so at the default zoom the
 * largest glyph in the font is drawn at very close to the size it was
 * rendered, and the map is crisp because nothing is being resampled. The first
 * try used the geometric mean, which is a defensible average and put every
 * glyph through a three-quarter minification: soft edges everywhere, at the
 * one zoom people spend all their time at.
 *
 * It costs columns -- a square tile is wider than a cell was -- but that is
 * only where zoom one sits, and the wheel goes both ways.
 */
float sdl3_tile_px(void)
{
    float px = (float)(sdl3.cell_h > 0 ? sdl3.cell_h : 1);

    /* Magnification uses a shared 32-pixel step. Source detail (32/64) and
     * missing artwork never affect camera geometry. Minification stays free. */
    if (sdl3_tiles_on() && sdl3_zoom > 0.0f)
    {
        float want = px * sdl3_zoom;

        if (want >= (float)UI_TILE_PX)
        {
            int mult = (int)((want + UI_TILE_PX / 2) / UI_TILE_PX);

            if (mult < 1) mult = 1;
            px = (float)(mult * UI_TILE_PX) / sdl3_zoom;
        }
    }

    return px;
}


/* One run, one colour, no questions asked. */
static void sdl3_fill_run(int x, int y, int n)
{
    SDL_FRect r;

    if (n <= 0) return;

    r.x = (float)(x * sdl3.cell_w);
    r.y = (float)(y * sdl3.cell_h);
    r.w = (float)(n * sdl3.cell_w);
    r.h = (float)sdl3.cell_h;

    SDL_RenderFillRect(sdl3.renderer, &r);
}

/*
 * The ground under a cell, while the level is behind the terminal.
 *
 * Nothing at all: a hole, so the map drawn underneath is the map you see. That
 * goes for the chrome as well -- the three strips get their surface painted as
 * whole rectangles between the map and the terminal, in sdl3_draw_chrome(),
 * and not cell by cell here.
 *
 * The first attempt did it here, and it is worth saying why it cannot be done
 * here: the terminal only ever redraws the cells that *changed*. A surface
 * painted as each cell is written appears only under the cells that were
 * written, so the sidebar came out as a ragged scatter of tiles around the
 * numbers with the dungeon showing through the gaps between them.
 */
/*
 * How opaque a health bar is.
 *
 * The same as the pane it stands on. Left solid while the pane around it went
 * to glass, the bars read as two painted blocks with a panel faintly sketched
 * behind them -- the one thing on the screen that had not been let in on what
 * the rest of it was doing.
 */
static Uint8 sdl3_bar_alpha(void)
{
    return sdl3_map_overlay() ? SDL3_CHROME_ALPHA : SDL_ALPHA_OPAQUE;
}

void sdl3_ground_colour(byte a)
{
    if (a == TERM_DARK && (sdl3_map_overlay() || sdl3_home_active()))
    {
        SDL_SetRenderDrawColor(sdl3.renderer, 0, 0, 0, 0);
        return;
    }

    sdl3_draw_colour(a);
}

static void sdl3_fill_cells(int x, int y, int n, byte a)
{
    /*
     * A gauge fills its row from the left in the colour the game chose for
     * the number, mixed well down towards the ground so it stays a
     * background and the text on it stays the thing you read. What is left
     * of the row is plain, so the boundary between them *is* the reading.
     */
    /*
     * A gauge belongs to the sidebar, and the sidebar belongs to the map.
     * Anything that takes the screen over -- the character sheet, a shop,
     * the tomb -- puts the screen away first, and the bars were carrying on
     * regardless: two green stripes across the death screen.
     */
    if (a == TERM_DARK && sdl3_map_showing() && panel_sidebar)
    {
        int gi;

        for (gi = 0; gi < SDL3_MAX_GAUGES; gi++)
        {
            byte mix[3];
            int  full, lo, hi;

            if (!sdl3_gauge[gi].live) continue;
            if (y != sdl3_gauge[gi].y) continue;

            full = sdl3_gauge_full(gi, mix);

            lo = x > sdl3_gauge[gi].x ? x : sdl3_gauge[gi].x;
            hi = (x + n < sdl3_gauge[gi].x + full)
               ? x + n : sdl3_gauge[gi].x + full;

            if (lo >= hi) break;

            sdl3_ground_colour(a);
            sdl3_fill_run(x, y, lo - x);
            sdl3_fill_run(hi, y, x + n - hi);

            SDL_SetRenderDrawColor(sdl3.renderer, mix[0], mix[1], mix[2],
                                   sdl3_bar_alpha());
            sdl3_fill_run(lo, y, hi - lo);
            return;
        }
    }

    /*
     * The selected row is a bar of the theme's selection colour, laid *under*
     * the text rather than washed over it. Over the top at full strength it
     * would drown what it is meant to be pointing at; the lighter hover
     * highlight can be an overlay because it is half the strength and only
     * ever follows the pointer.
     */
    if (a == TERM_DARK && y == sdl3_select_row)
    {
        int bar_x, bar_end, lo, hi;

        sdl3_row_span(y, sdl3_select_x, sdl3_select_w, &bar_x, &bar_end);

        lo = x > bar_x ? x : bar_x;
        hi = (x + n < bar_end) ? x + n : bar_end;

        if (lo >= hi) goto plain_ground;

        sdl3_ground_colour(a);
        sdl3_fill_run(x, y, lo - x);
        sdl3_fill_run(hi, y, x + n - hi);

        sdl3_selection_colour();
        sdl3_fill_run(lo, y, hi - lo);
        return;
    }

plain_ground:

    /*
     * Ground inside a menu's frame is the panel colour, not the canvas. This
     * is the only place it can be done: Term erases on its own schedule, so
     * anything painted earlier would be wiped by exactly this call.
     *
     * The run has to be split where it crosses the frame's edge. Deciding the
     * colour once, from the run's first cell, meant an erase that began inside
     * the panel and carried on past it painted the whole length in panel
     * colour -- which showed as blocks of menu ground stranded beside the
     * frame, on exactly the rows whose text ran wide.
     */
    int px0, px1;

    if (a == TERM_DARK && !sdl3_home_active() && sdl3_panel_span(y, &px0, &px1))
    {
        int lo = x;
        int hi = x + n;

        if (lo < px0) lo = px0;
        if (hi > px1) hi = px1;

        if (lo < hi)
        {
            sdl3_ground_colour(a);
            sdl3_fill_run(x, y, lo - x);
            sdl3_fill_run(hi, y, x + n - hi);

            sdl3_ui_colour(THEME_UI_PANEL);
            sdl3_fill_run(lo, y, hi - lo);
            return;
        }
    }

    sdl3_ground_colour(a);
    sdl3_fill_run(x, y, n);
}




static void sdl3_draw_glyph(int x, int y, char c)
{
    SDL_FRect src, dst;

    if (c < GLYPH_FIRST || c > GLYPH_LAST) return;

    src.x = (float)((c - GLYPH_FIRST) * sdl3.cell_w);
    src.y = 0.0f;
    src.w = (float)sdl3.cell_w;
    src.h = (float)sdl3.cell_h;

    dst.x = (float)(x * sdl3.cell_w);
    dst.y = (float)(y * sdl3.cell_h);
    dst.w = (float)sdl3.cell_w;
    dst.h = (float)sdl3.cell_h;

    SDL_RenderTexture(sdl3.renderer, sdl3.atlas, &src, &dst);
}


/*
 * Draw the cursor as an outlined box, the way the other ports do.
 */
static void sdl3_draw_cursor(void)
{
    SDL_FRect r;

    if (sdl3_look_recall_active()) return;

    /*
     * Ask Term, do not remember.
     *
     * This used to read a flag set by TERM_XTRA_SHAPE, which is about the
     * *shape* of a hardware cursor and is barely used by a soft-cursor term
     * like ours -- so the flag went stale and a yellow box turned up on the
     * map with nothing pointing at it. Term already knows whether a cursor
     * belongs on screen: with a soft cursor it calls the hook only when one
     * does, and Term_get_cursor() is that same answer asked directly.
     */
    {
        int visible = 0;

        Term_get_cursor(&visible);
        if (!visible) return;
    }

    /* A menu is chosen from, not typed into, so a caret parked wherever the
     * last character landed is just a stray box on the screen. */
    if (ui_screen_has_layer(UI_LAYER_MENU)) return;

    /*
     * On the map, the cursor is a *grid* -- it is what the look and target
     * commands point at -- so it has to be drawn through the same transform
     * as the grid it is pointing at. Drawing it at the unzoomed cell size
     * left a small box sitting somewhere near the thing it meant, and the
     * error grew with the zoom.
     */
    r = sdl3_cell_rect(sdl3.cursor_x, sdl3.cursor_y);

    sdl3_draw_colour(TERM_YELLOW);
    SDL_RenderRect(sdl3.renderer, &r);
}


/**** Input ****/


/*
 * Turn a special key into the macro trigger the pref files expect:
 *
 *     0x1F  <modifier letters>  '[' <key name> ']'
 *
 * e.g. Shift+Up becomes  ^_S[up]  , keypad 4 becomes  ^_[[4]] .
 */
static void sdl3_emit_trigger(cptr name, SDL_Keymod mod)
{
    int i;

    Term_keypress(31);

    if (mod & SDL_KMOD_CTRL)  Term_keypress('C');
    if (mod & SDL_KMOD_SHIFT) Term_keypress('S');
    if (mod & SDL_KMOD_ALT)   Term_keypress('A');
    if (mod & SDL_KMOD_GUI)   Term_keypress('M');

    Term_keypress('[');
    for (i = 0; name[i]; i++) Term_keypress(name[i]);
    Term_keypress(']');
}


/*
 * One notch of zoom, in or out.
 *
 * The wheel had this to itself and the arithmetic lived inside its event case.
 * Ctrl +/- wants exactly the same behaviour -- the same ratio, the same clamps,
 * the same snap to 1.0 -- so it lives here rather than being written twice and
 * drifting. Each notch is a constant *ratio*, so a step feels the same whether
 * you are peering at three grids or looking at the whole level.
 *
 * The wheel was originally chosen *instead* of a key because every letter in
 * this game already means something. Ctrl and a punctuation key does not
 * collide with anything, so the objection does not apply to it.
 */
static void sdl3_zoom_step(int direction)
{
    float z = sdl3_zoom;
    float floor;

    if (direction > 0) z *= SDL3_ZOOM_STEP;
    else if (direction < 0) z /= SDL3_ZOOM_STEP;
    else return;

    if (z > SDL3_ZOOM_MAX) z = SDL3_ZOOM_MAX;
    if (z < SDL3_ZOOM_MIN) z = SDL3_ZOOM_MIN;

    /* And no further out than there is anything to see. */
    floor = sdl3_zoom_floor();
    if (z < floor) z = floor;

    /* Snap to exactly 1 when passing near it, so there is a home position you
     * can find by feel rather than by luck. */
    if (z > 0.94f && z < 1.06f) z = 1.0f;

    if (z != sdl3_zoom)
    {
        sdl3_zoom = z;
        sdl3_hover_dirty = TRUE;   /* reuse the repaint nudge */
    }
}


/*
 * Keys that produce text (letters, digits, punctuation) are handled by
 * SDL_EVENT_TEXT_INPUT, not here -- that is the only way to get the right
 * character out of a non-US keyboard layout. This function deals with
 * everything text input cannot express: control combinations, the editing
 * keys, and the direction keys.
 */
static void sdl3_handle_key(const SDL_KeyboardEvent *e)
{
    cptr name;

    if (!sdl3.initialised) return;

    /* Any fresh key press cancels a swallow armed by an earlier one.
     *
     * Without this, arming the flag on a keypad press and only clearing it
     * when a text event turns up means that if none ever turns up -- num lock
     * off, or a keypad key that produces no text -- the flag stays armed and
     * eats the *next* keystroke instead. That is the "I had to press it twice"
     * bug: move with the numpad, get a -more- prompt, and the key you press to
     * dismiss it disappears. A swallow may only ever apply to the text event
     * belonging to the key press immediately before it. */
    sdl3.swallow_text = FALSE;

    /*
     * Ctrl +/- zooms, the same notch the wheel gives.
     *
     * Before the special-key lookup below, because the keypad's own plus and
     * minus are special keys and would be sent to the game as direction
     * triggers before ever reaching here.
     *
     * Both the unshifted '=' and the shifted '+' are accepted: on a US layout
     * they are one physical key, and asking the player to hold three keys to
     * zoom in and two to zoom out would be a nonsense.
     */
    if (e->mod & SDL_KMOD_CTRL)
    {
        switch (e->key)
        {
        case SDLK_EQUALS:
        case SDLK_PLUS:
        case SDLK_KP_PLUS:
            sdl3_zoom_step(1);
            return;

        case SDLK_MINUS:
        case SDLK_KP_MINUS:
            sdl3_zoom_step(-1);
            return;

        default:
            break;
        }
    }

    name = sdl3_special_key_name(e->key);
    if (name)
    {
        sdl3_emit_trigger(name, e->mod);

        /* With num lock on a keypad key also raises a text event; we have
         * already dealt with the key, so drop the digit that follows. */
        if (sdl3_is_keypad(e->key)) sdl3.swallow_text = TRUE;
        return;
    }

    /* Control codes: Ctrl-A .. Ctrl-Z map to 1 .. 26. */
    if ((e->mod & SDL_KMOD_CTRL) && e->key >= SDLK_A && e->key <= SDLK_Z)
    {
        Term_keypress((int)(e->key - SDLK_A) + 1);
        return;
    }

    /*
     * Shifted 1, 2 and 3 on the top row: the three chrome toggles.
     *
     * Here rather than in the text event, because what a shifted digit
     * *produces* depends on the keyboard -- 2 gives " here and @ in America --
     * while which key was pressed does not. And here rather than as plain
     * digits, because the digits are walk keys: this is the one place that can
     * tell the top row from the keypad, since the keypad has its own keycodes
     * and was dealt with above.
     */
    /*
     * Only while the map is what is on screen.
     *
     * These three adjust the map screen's furniture -- the message line, the
     * status bar, the sidebar -- and on a menu there is no furniture to
     * adjust. Ungated, the swallow below threw away the "!" that the same
     * press produces, so `!` could not be typed anywhere in the game, and the
     * Load menu's own `!) Delete` row was unreachable.
     */
    if ((e->mod & SDL_KMOD_SHIFT)
     && !(e->mod & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI))
     && sdl3_map_showing())
    {
        int panel = 0;

        /* By scancode, which is the physical key, rather than by keycode.
         * Whether a keycode carries the shift or not is a decision SDL makes
         * for us and can change; where the key is on the board is not. */
        switch (e->scancode)
        {
            case SDL_SCANCODE_1: panel = SPECIAL_KEY_PANEL_MSG;    break;
            case SDL_SCANCODE_2: panel = SPECIAL_KEY_PANEL_STATUS; break;
            case SDL_SCANCODE_3: panel = SPECIAL_KEY_PANEL_SIDE;   break;
            default: break;
        }

        if (panel)
        {
            Term_keypress(panel);

            /* And drop the '!' or '"' the same press is about to produce. */
            sdl3.swallow_text = TRUE;
            return;
        }
    }

    switch (e->key)
    {
        case SDLK_ESCAPE:    Term_keypress(ESCAPE); break;
        case SDLK_RETURN:    Term_keypress('\r');   break;
        case SDLK_KP_ENTER:  Term_keypress('\r');   break;
        case SDLK_BACKSPACE: Term_keypress('\b');   break;
        case SDLK_TAB:       Term_keypress('\t');   break;
        case SDLK_DELETE:    Term_keypress(0x7F);   break;
    }
}


static void sdl3_handle_text(const SDL_TextInputEvent *e)
{
    int i;

    if (!sdl3.initialised) return;

    if (sdl3.swallow_text)
    {
        sdl3.swallow_text = FALSE;
        return;
    }

    /* Phase 1 is ASCII only. Anything wider than 7 bits has nowhere to go:
     * a Term cell holds a single char. */
    for (i = 0; e->text[i]; i++)
    {
        unsigned char c = (unsigned char)e->text[i];
        if (c >= 32 && c < 127) Term_keypress(c);
    }
}


/*
 * The player closed the window.
 *
 * Mirrors what main-win.c does for WM_CLOSE: hand the game a quit key so it
 * can save properly, rather than tearing the process down underneath it.
 */
static void sdl3_handle_quit_request(void)
{
    if (character_generated)
    {
        if (!can_save)
        {
            plog("You may not do that right now.");
            return;
        }

        forget_lite();
        forget_view();
        clear_mon_lite();

        Term_key_push(SPECIAL_KEY_QUIT);
        return;
    }

    quit(NULL);
}


/*
 * Clickable menu rows.
 *
 * A menu that declares itself on the channel gets a mouse for free: we keep
 * the rows it reported, and a click inside one becomes the keypress that row
 * stands for. The menu neither draws differently nor reads keys differently --
 * which is the whole point, because a half-converted UI is only tolerable if
 * the player cannot tell which menus are converted.
 */
#define SDL3_MAX_MENU_ROWS 64

typedef struct { short x, y, w; char key; } sdl3_menu_row;

static sdl3_menu_row sdl3_menu[SDL3_MAX_MENU_ROWS];
static int           sdl3_menu_ct;

/*
 * Whether anything has declared a row since the last time the screen was
 * wiped.
 *
 * Term_clear() does not clear anything when it is called. It sets a flag,
 * and Term issues the actual TERM_XTRA_CLEAR from inside the next
 * Term_fresh() -- which is to say *after* the game has finished drawing
 * the screen and declaring what is on it. So a clear arriving here does
 * not mean "a new screen is coming"; it means "the frame you are about to
 * paint starts from blank", and the rows for that very frame have already
 * been declared.
 *
 * Forgetting them at that moment threw away every menu on the frame that
 * drew it, which is why no row anywhere could be clicked or even
 * highlighted. The rule the old code wanted is still the right one -- a
 * menu declares its rows on every draw, so rows that were not declared
 * again are stale -- it just has to be asked one frame later.
 */
static bool          sdl3_menu_fresh;

/* Which declared row the pointer is over, or -1. Redrawing while the game is
 * blocked in inkey() needs a nudge, hence the dirty flag. */
static int  sdl3_hover_row = -1;
bool sdl3_hover_dirty;


/* Presentation selection also controls geometry when the artwork is absent. */
bool sdl3_tiles_on(void)
{
    return display_tiles_get();
}

/*
 * Whether the map region is showing the map.
 *
 * The game already knows most of this. screen_save() / screen_load() maintain
 * screen_depth, and screen_is_saved() is true exactly while a sub-screen is up
 * -- thirty-six files use that properly, and asking it beats anything I can
 * infer from the outside.
 *
 * Eight files bypass it and call Term_save() directly, obj_prompt.c among them,
 * which is why every heuristic I tried came apart on the object prompt. Those
 * are caught by the one extra rule below: a document painting covers the map,
 * and restoring the screen or redrawing the map uncovers it.
 *
 * Two rules, one of them the game's own. Earlier attempts had four, including
 * "the screen was cleared" -- which is what made bumping into a wall flicker,
 * since a message can clear -- and "the screen was saved", which stuck when a
 * prompt bailed out without restoring.
 */
bool sdl3_doc_covered = FALSE;

/*
 * The panel a menu is currently framed by, in cells.
 *
 * Chrome cannot simply be painted when the panel event arrives: Term batches,
 * so its clear and its text land afterwards and wipe anything drawn early.
 * Instead the rectangle is remembered, and the cell-erasing that Term does on
 * its own schedule fills with the panel colour inside it -- which puts the
 * ground genuinely behind the text rather than tinted over the top of it. The
 * border is a hairline and can go on at present time.
 */
bool sdl3_panel_active = FALSE;
int  sdl3_panel_x, sdl3_panel_y, sdl3_panel_w, sdl3_panel_h;

/*
 * (The portrait, the rule and the scroll arrows moved to sdl3_marks.c on
 * 7 September 2026, and the note that stood here went with them.)
 *
 * The portrait was described here, in cells.
 *
 * Unlike the panel ground this is *not* laid in as Term erases cells, because
 * it is foreground: a picture in true colour, which is exactly what a Term
 * cell cannot carry. It is drawn at present time instead, over the finished
 * grid, so nothing Term does to those cells can scrub it -- the game leaves
 * them blank and the picture goes on top of the blank.
 *
 * Held rather than drawn on arrival for the usual reason: the event comes
 * while the document is still being built, and Term's clear lands afterwards.
 */


/* Full-page panels fill the window margins as well as the terminal grid.
 * Cell placement remains on the grid; only the background and border extend. */
static bool sdl3_panel_fullscreen(void)
{
    if (!sdl3_panel_active) return FALSE;
    if (sdl3_home_active()) return FALSE;

    return (sdl3_panel_x == 0 && sdl3_panel_y == 0
         && sdl3_panel_w >= sdl3.cols && sdl3_panel_h >= sdl3.rows);
}

static bool sdl3_panel_span(int y, int *x0, int *x1)
{
    if (!sdl3_panel_active) return FALSE;
    if (y < sdl3_panel_y || y >= sdl3_panel_y + sdl3_panel_h) return FALSE;

    *x0 = sdl3_panel_x;
    *x1 = sdl3_panel_x + sdl3_panel_w;   /* exclusive */
    return TRUE;
}

/*
 * Lay the panel's ground into the grid.
 *
 * Term only repaints cells whose contents changed, so a blank cell inside the
 * panel produces no erase at all and would keep whatever colour was under it.
 * That showed as bands of canvas striping the menu. So the ground is laid
 * whole here -- on every full clear, and when the panel is first declared --
 * and the per-cell erase paints the same colour, so the two always agree.
 */
static void sdl3_panel_ground(void)
{
    SDL_FRect r;

    if (!sdl3_panel_active) return;

    r.x = (float)(sdl3_panel_x * sdl3.cell_w);
    r.y = (float)(sdl3_panel_y * sdl3.cell_h);
    r.w = (float)(sdl3_panel_w * sdl3.cell_w);
    r.h = (float)(sdl3_panel_h * sdl3.cell_h);

    /*
     * ...except on the home screen, where the menu has no frame and no ground
     * of its own. A panel is the right shape for something that has interrupted
     * you; the first screen has interrupted nothing, and a box in the middle of
     * it reads as a dialog somebody forgot to design. The rows stand on the
     * backdrop instead, and the selection bar below is the only chrome left.
     */
    if (!sdl3_home_active())
    {
        sdl3_ui_colour(THEME_UI_PANEL);
        SDL_RenderFillRect(sdl3.renderer, &r);
    }

    /*
     * The selection bar goes down with it, whole.
     *
     * Painting it only where Term erases is not enough, for the same reason
     * the panel's own ground is laid here: Term repaints the cells that
     * changed and no others, so on a redraw the bar came out only as long as
     * the text that happened to differ -- it stopped a few columns past the
     * last word instead of running to the frame. The per-cell erase paints
     * the same colour, so the two agree wherever they overlap.
     */
    if (sdl3_select_row >= sdl3_panel_y
     && sdl3_select_row < sdl3_panel_y + sdl3_panel_h)
    {
        int x0, x1;

        sdl3_row_span(sdl3_select_row, sdl3_select_x, sdl3_select_w, &x0, &x1);

        if (x1 > x0)
        {
            sdl3_selection_colour();
            sdl3_fill_run(x0, sdl3_select_row, x1 - x0);
        }
    }
}

/*
 * Take the frame down, and have Term repaint what was under it.
 *
 * This is the part that was missing. The panel's ground is painted into the
 * grid texture, and Term knows nothing about it -- so when the menu closed,
 * Term restored the screen, saw that most cells already held the right
 * characters, and repainted almost nothing. The ground stayed exactly where it
 * was, a rectangle of menu colour stranded across the dungeon.
 *
 * Anything a frontend draws that Term cannot account for has to be undone like
 * this, by asking Term to repaint the region it occupied.
 */
static void sdl3_panel_drop(void)
{
    /*
     * The heading and the selected row go first, before the early return.
     *
     * They were cleared *after* it, so a menu whose frame was already down
     * left its selected row behind -- and the next time the map was drawn,
     * that row got a bar of selection colour straight across the dungeon.
     * Chrome has to be taken down whether or not the thing it was hung on
     * is still there.
     */
    sdl3_title_row = sdl3_select_row = -1;

    /* Same reasoning as the heading: these belong to the page, not to the
     * frame, and a page can go while the frame stays. */
    sdl3_marks_drop();

    if (!sdl3_panel_active) return;

    sdl3_panel_active = FALSE;

    /* Term_dirty() rather than Term_redraw_section(): the section call *sets*
     * the pending dirty bounds rather than widening them, so it happily
     * narrows a full-screen repaint that was already queued. That is what left
     * fragments of one menu showing around the edges of the next. Marking the
     * whole screen costs nothing at four thousand cells -- and it must not
     * paint, or the bare map flashes up before the menu redraws over it. */
    Term_dirty();
}


/**** Weather ****/


/*
 * How many columns of a gauge are full, and the colour to fill them with.
 */
static int sdl3_gauge_full(int gi, byte out[3])
{
    theme_color_t canvas = theme_ui(THEME_UI_CANVAS);
    byte  bg[3], fg[3];
    int   full;

    full = sdl3_gauge[gi].max
         ? (sdl3_gauge[gi].w * sdl3_gauge[gi].cur + sdl3_gauge[gi].max / 2)
           / sdl3_gauge[gi].max
         : 0;

    /* Anything left at all keeps a column, or the last few points of a long
     * bar vanish and it reads as empty when it is not. */
    if (!full && sdl3_gauge[gi].cur > 0) full = 1;

    bg[0] = canvas.r; bg[1] = canvas.g; bg[2] = canvas.b;
    fg[0] = ormeq_color_table[sdl3_gauge[gi].attr][1];
    fg[1] = ormeq_color_table[sdl3_gauge[gi].attr][2];
    fg[2] = ormeq_color_table[sdl3_gauge[gi].attr][3];

    sdl3_blend(bg, fg, 22, out);

    return full;
}

/*
 * Lay every gauge's row, whole.
 *
 * Called from the full clear, exactly like the panel's ground and for exactly
 * the same reason -- with one extra twist. After a total erase Term knows the
 * screen is blank, so it draws only the cells that are *not* blank: the space
 * between "HP" and the digits is never drawn at all, and the bar came out in
 * two pieces with a gap where that space sits. Laying the row here catches
 * every column; the per-cell erase paints the same colours, so the two agree
 * wherever they overlap.
 */
static void sdl3_gauge_ground(void)
{
    int gi;

    if (!sdl3_map_showing()) return;

    /* A gauge is part of the character panel, and a panel that is not on
     * screen has no bars. Without this the last health bar stayed painted as a
     * green block floating on the dungeon after the panel was switched off,
     * because nothing else ever paints over it again. */
    if (!panel_sidebar) return;

    for (gi = 0; gi < SDL3_MAX_GAUGES; gi++)
    {
        byte mix[3];
        int  full;

        if (!sdl3_gauge[gi].live) continue;

        full = sdl3_gauge_full(gi, mix);
        if (full <= 0) continue;

        SDL_SetRenderDrawColor(sdl3.renderer, mix[0], mix[1], mix[2],
                               sdl3_bar_alpha());
        sdl3_fill_run(sdl3_gauge[gi].x, sdl3_gauge[gi].y, full);
    }
}


/* Which density the current grid reflects. Compared against the game's setting
 * in the event pump, which is a safe place to rebuild everything -- unlike the
 * middle of a redraw, where Term is part-way through its own bookkeeping. */
static int sdl3_density_applied = -1;

/* Likewise for the typeface: swapping it means a new atlas at every rung of
 * the ladder, and a new fit of the grid to the window. */
static int sdl3_font_applied = -1;

/* And the window mode, which we set rather than the game. */
static int sdl3_fullscreen_applied = -1;


/*
 * Whether a cell belongs to one of the three strips of chrome.
 *
 * The message line along the top, the character panel down the right, the
 * status bar along the bottom. Each is only chrome while it is switched on and
 * -- for the message line, which is an overlay and not a reserved row -- while
 * it has something on it. Everywhere else the level shows through.
 *
 * The two panels come from the game's own options, so the game's idea of where
 * the map goes and ours stay one thing: ui_map_rect() shrinks by exactly the
 * strips that are switched on, and what it leaves is what we punch through.
 */
static bool sdl3_msg_visible(void)
{
    /* Never hidden while the game is waiting on it: a "-more-" that has been
     * switched off does not read as a hidden message, it reads as a game that
     * has stopped. See msg_line_waiting(). */
    return BOOL(panel_message || msg_line_waiting());
}


/*
 * Whether a row has anything on it that we are going to draw.
 *
 * "That we are going to draw" is the important half: the terminal still holds
 * the message line's text when the player has switched the message line off,
 * and a surface painted for text nobody can see is a grey bar across the top
 * of the dungeon for no reason.
 */
static bool sdl3_row_has_text(int y)
{
    int x;

    for (x = 0; x < sdl3.cols; x++)
    {
        byte a;
        char c;

        if (sdl3_cell_hidden(x, y)) continue;
        if (Term_what(x, y, &a, &c)) continue;
        if (c && c != ' ') return TRUE;
    }

    return FALSE;
}

/*
 * A pane, given the cells it has to cover and how much air to leave round
 * them.
 *
 * The padding is in pixels and fractional on purpose: a whole cell of margin
 * on either side of a twelve-column panel is a sixth of it given over to
 * nothing, which is too much. Half a cell across and a third of one down is
 * enough for the text to stop looking as though it has been pushed up against
 * the glass.
 */
void sdl3_chrome_slab(rect_t r, float pad_x, float pad_y)
{
    SDL_FRect q;
    if (r.cx <= 0 || r.cy <= 0) return;
    q.x = r.x * sdl3.cell_w - pad_x;
    q.y = r.y * sdl3.cell_h - pad_y;
    q.w = r.cx * sdl3.cell_w + pad_x * 2;
    q.h = r.cy * sdl3.cell_h + pad_y * 2;
    sdl3_hud_surface(q);
    /* Terminal erases must replace transparent pixels, never blend them. */
    SDL_SetRenderDrawBlendMode(sdl3.renderer,SDL_BLENDMODE_NONE);
}

/* All map furniture shares one surface, including Reckoner. */
void sdl3_hud_surface(SDL_FRect r)
{
    SDL_BlendMode old;
    theme_color_t fill=theme_ui(THEME_UI_PANEL), edge=theme_ui(THEME_UI_BORDER);
    SDL_GetRenderDrawBlendMode(sdl3.renderer,&old);
    SDL_SetRenderDrawBlendMode(sdl3.renderer,SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(sdl3.renderer,fill.r,fill.g,fill.b,
        sdl3_tiles_on() ? SDL3_CHROME_ALPHA_TILES : SDL3_CHROME_ALPHA);
    SDL_RenderFillRect(sdl3.renderer,&r);
    SDL_SetRenderDrawColor(sdl3.renderer,edge.r,edge.g,edge.b,100);
    SDL_RenderRect(sdl3.renderer,&r);
    SDL_SetRenderDrawBlendMode(sdl3.renderer,old);
}

static void sdl3_draw_panel_cards(float pad_x, float pad_y)
{
    rect_t p=ui_char_info_rect();
    int x,y,bottom=p.y;
    if (p.cx<=0) return;
    for(y=p.y;y<sdl3.rows;++y) for(x=p.x;x<p.x+p.cx && x<sdl3.cols;++x)
    {
        byte a; char c;
        if(!Term_what(x,y,&a,&c) && c && c!=' ') bottom=y+1;
    }
    sdl3_chrome_slab(rect(p.x,p.y,p.cx,bottom-p.y),pad_x,pad_y);
}

/* Draw one status pane per run of text, leaving the dungeon visible in gaps.
 * Short spaces within a label do not split a run. */
#define _STATUS_GAP 3   /* blank cells that do not end a run */

static void sdl3_draw_status_cards(float pad_x, float pad_y)
{
    rect_t m     = ui_map_rect();
    int    right = MIN(m.x + m.cx, sdl3.cols);
    int    below = m.y + m.cy;
    int    y;

    for (y = below; y < sdl3.rows; y++)
    {
        int x, start = -1, last = -1;

        for (x = 0; x < right; x++)
        {
            byte a;
            char c;

            if (Term_what(x, y, &a, &c)) continue;
            if (!c || c == ' ') continue;

            if (start >= 0 && x - last > _STATUS_GAP)
            {
                sdl3_chrome_slab(rect(start, y, last - start + 1, 1),
                                 pad_x, pad_y);
                start = -1;
            }

            if (start < 0) start = x;
            last = x;
        }

        if (start >= 0)
            sdl3_chrome_slab(rect(start, y, last - start + 1, 1),
                             pad_x, pad_y);
    }
}

/* Shared translucent surfaces sit between the level and terminal text.
 * Content bounds keep unused map space clear; strips stop at the sidebar. */
static void sdl3_draw_chrome(void)
{
    rect_t m     = ui_map_rect();
    int    right = m.x + m.cx;
    int    below = m.y + m.cy;

    float pad_x = (float)sdl3.cell_w * 0.5f;
    float pad_y = (float)sdl3.cell_h * 0.34f;

    if (!sdl3_map_overlay()) return;

    if (panel_sidebar) sdl3_draw_panel_cards(pad_x, pad_y);

    if (panel_status) sdl3_draw_status_cards(pad_x, pad_y);

    /*
     * The top strip is the awkward one, because two different things write
     * there. Most of it is the message line, which keeps its own document and
     * can grow to several rows. But the look and target prompts -- and a
     * handful of others -- are put straight onto row zero with prt(), and know
     * nothing about the message line at all. Those get no rect to ask for, so
     * ask the terminal instead: if there is anything up there that we are
     * actually drawing, it wants something to stand on.
     */
    {
        rect_t msg  = msg_line_rect();
        int    rows = 0;

        if (sdl3_msg_visible() && msg.cy > 0) rows = msg.y + msg.cy;
        else if (sdl3_row_has_text(0))        rows = 1;

        if (rows > sdl3.rows) rows = sdl3.rows;
        if (rows > 0)
        {
            int x,y,width=0;
            int limit=(sdl3_msg_visible() && msg.cy>0) ? MIN(right,msg.x+msg.cx) : right;
            for(y=0;y<rows;++y) for(x=0;x<limit;++x)
            {
                byte a; char c;
                if(!Term_what(x,y,&a,&c) && c && c!=' ') width=MAX(width,x+1);
            }
            if(width) sdl3_chrome_slab(rect(0,0,MIN(right,width+1),rows),0,pad_y);
        }
    }
}

/*
 * Whether a character the terminal wants drawn is one we are not going to.
 *
 * Two kinds. The level's own, because we draw the level ourselves and at a
 * different geometry -- the game still writes it into the terminal, and left
 * alone it would sit on our map as a second, unsquare copy of itself. This
 * used to be handled by painting over the map region afterwards, which came to
 * the same thing and hid the reason.
 *
 * And the message line, when the player has switched it off. Suppressing it
 * here rather than in the game means messages still accumulate, still reach
 * the history, and still appear the instant the line comes back on or the game
 * starts waiting on one.
 */
static bool sdl3_cell_hidden(int x, int y)
{
    if (!sdl3_map_overlay()) return FALSE;

    if (!sdl3_msg_visible() && msg_line_contains(y, x)) return TRUE;

    return sdl3_in_map(x, y);
}


/*
 * Which declared menu row, if any, is under this cell.
 */
static int sdl3_menu_row_at(int x, int y)
{
    int i;

    /*
     * None, while the map owns the screen.
     *
     * The rows a menu declares are cleared when it says it is going, but not
     * every screen that takes over from a menu says anything at all -- so the
     * start screen's rows could still be on file with the dungeon drawn over
     * them. The pointer resting on one then painted a bar of selection colour
     * straight across the map, and a *click* on one would have been turned
     * into whatever keypress that row used to stand for.
     */
    if (sdl3_map_showing()) return -1;

    for (i = 0; i < sdl3_menu_ct; i++)
    {
        sdl3_menu_row *m = &sdl3_menu[i];
        if (y == m->y && x >= m->x && x < m->x + m->w) return i;
    }

    return -1;
}


static void sdl3_handle_motion(const SDL_MouseMotionEvent *e)
{
    int row;

    if (!sdl3.initialised) return;
    if (sdl3.cell_w <= 0 || sdl3.cell_h <= 0) return;

    {
        int ox, oy;
        sdl3_grid_origin(&ox, &oy);
        row = sdl3_menu_row_at(((int)e->x - ox) / sdl3.cell_w,
                               ((int)e->y - oy) / sdl3.cell_h);
    }

    if (row != sdl3_hover_row)
    {
        if (row >= 0) sound(SOUND_UI_NAVIGATE);
        sdl3_hover_row = row;
        sdl3_hover_dirty = TRUE;
    }

    sdl3_look_at(e->x, e->y);
}


/*
 * Report a click to the game.
 *
 * We hand over the cell, not the pixel -- the game thinks in terminal cells
 * and has helpers to turn one into a dungeon square. What it does with the
 * click is the game's business; see _do_mouse_click() in dungeon.c.
 */
static void sdl3_handle_mouse(const SDL_MouseButtonEvent *e)
{
    int   x, y;
    float px, py;

    if (!sdl3.initialised) return;
    if (sdl3.cell_w <= 0 || sdl3.cell_h <= 0) return;

    /* The grid is centred in the window, so take that off first. */
    {
        int ox, oy;
        sdl3_grid_origin(&ox, &oy);
        px = (float)e->x - (float)ox;
        py = (float)e->y - (float)oy;
    }

    /* The Look card must not turn a click into a click on the hidden map. */
    if (sdl3_look_recall_click(px, py, e->button) || sdl3_calc_click(px, py, e->button)) return;
    x = (int)px / sdl3.cell_w;
    y = (int)py / sdl3.cell_h;

    if (px < 0.0f || py < 0.0f) return;
    if (x < 0 || y < 0 || x >= sdl3.cols || y >= sdl3.rows) return;

    if (sdl3_verbose)
    {
        printf("SDL3: click button %d at pixel %.0f,%.0f -> cell %d,%d\n",
               (int)e->button, e->x, e->y, x, y);
        fflush(stdout);
    }

    /* A declared menu row wins over the map: if a menu is up, that is what the
     * player is looking at. Pushing the key is exactly what the keyboard does,
     * so the menu itself needs to know nothing about any of this. */
    if (e->button == SDL_BUTTON_LEFT)
    {
        int row = sdl3_menu_row_at(x, y);

        if (row >= 0)
        {
            Term_keypress(sdl3_menu[row].key);
            return;
        }
    }

    if (!sdl3_map_showing()) return;

    /* While zoomed, the terminal cell under the pointer means nothing -- we
     * drew the map ourselves, so we invert our own transform and hand the game
     * the dungeon grid directly. Same transform the renderer used, from the
     * same function, because the two disagreeing is how you end up walking
     * somewhere you did not point at. */
    {
        sdl3_map_view v;
        int gx, gy, lvl_w, lvl_h;

        if (!sdl3_map_input_clear(px,py)) return;
        sdl3_map_view_get(&v);
        ui_map_size(&lvl_w, &lvl_h);

        gx = (int)floorf((px - v.origin_x) / v.scale_x);
        gy = (int)floorf((py - v.origin_y) / v.scale_y);

        if (gx < 0 || gy < 0 || gx >= lvl_w || gy >= lvl_h) return;

        switch (e->button)
        {
            case SDL_BUTTON_LEFT:  ui_mouse_click_cave(gx, gy, 1); break;
            case SDL_BUTTON_RIGHT: ui_mouse_click_cave(gx, gy, 2); break;
        }
        return;
    }

}


/* Defined below, with the rest of the grid handling. */
static void sdl3_handle_resize(int px_w, int px_h);

/* Defined below, with the semantic channel. */
static void sdl3_draw_hover(void);

static void sdl3_repaint_after_reset(void)
{
    /* Only the pixels were lost. Keep the current menu's click targets and
     * selection through TERM_XTRA_CLEAR, then repaint without waiting for
     * another key or animation frame. */
    sdl3_menu_fresh = TRUE;
    Term_dirty();
    Term_fresh();
}

static void sdl3_dispatch(const SDL_Event *e)
{
    switch (e->type)
    {
        case SDL_EVENT_QUIT:
            sdl3_handle_quit_request();
            break;

        case SDL_EVENT_KEY_DOWN:
            if (e->key.scancode < SDL_SCANCODE_LCTRL || e->key.scancode > SDL_SCANCODE_RGUI)
                ui_sound_input(e->key.repeat);
            sdl3_handle_key(&e->key);
            break;

        case SDL_EVENT_TEXT_INPUT:
            sdl3_handle_text(&e->text);
            break;

        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            ui_sound_input(FALSE);
            sdl3_handle_mouse(&e->button);
            break;

        case SDL_EVENT_MOUSE_MOTION:
            sdl3_handle_motion(&e->motion);
            break;

        case SDL_EVENT_MOUSE_WHEEL:
            if (sdl3_look_recall_wheel(e->wheel.y)) break;
            if (reckoner_history_active() || doc_viewer_active())
            {
                if (e->wheel.y) sdl3_emit_trigger(e->wheel.y>0 ? "up" : "down",SDL_KMOD_NONE);
                break;
            }
            if (e->wheel.y > 0.0f) sdl3_zoom_step(1);
            else if (e->wheel.y < 0.0f) sdl3_zoom_step(-1);
            break;

        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            sdl3_handle_resize(e->window.data1, e->window.data2);
            break;

        case SDL_EVENT_RENDER_DEVICE_RESET:
        {
            int width, height;
            ui_tiles_reset();
            sdl3_atlas_forget();
            SDL_DestroyTexture(sdl3.atlas);
            sdl3.atlas = NULL;
            if (sdl3_build_atlas()) quit("Could not restore the font after a renderer reset.");
            SDL_GetRenderOutputSize(sdl3.renderer,&width,&height);
            sdl3_handle_resize(width,height);
            sdl3_repaint_after_reset();
            break;
        }
        case SDL_EVENT_RENDER_TARGETS_RESET:
            sdl3_repaint_after_reset();
            break;

        case SDL_EVENT_WINDOW_FOCUS_LOST:
            ui_music_focus(FALSE);
            ui_ambient_focus(FALSE);
            break;
        case SDL_EVENT_WINDOW_FOCUS_GAINED:
            ui_music_focus(TRUE);
            ui_ambient_focus(TRUE);
            break;
    }
}


/*
 * Pump events. With wait set, block until something arrives -- that is what
 * keeps the game from spinning the CPU while it waits for a command.
 */
static errr sdl3_pump_events(bool wait)
{
    SDL_Event e;

    /*
     * Weather runs on the clock, and it runs here.
     *
     * It always measured itself in milliseconds, but it was only ever *asked*
     * from the branch below that waits for a key -- so it advanced while the
     * game sat idle and stopped dead the moment you did anything. Hold a
     * direction down and the rain hangs in the air; let go and it falls again.
     * Which is precisely backwards: weather is the one thing on this screen
     * that has nothing to do with whose turn it is.
     *
     * Above the scripted-key branch as well as the wait, because the same
     * argument applies to where the keys are coming from, and because a
     * feature that stops whenever the game is busy is a feature that cannot
     * be looked at while the game is busy.
     */
    if (sdl3_weather_advance()) sdl3_present();

    /*
     * Open on a dungeon floor, if the run asked for one.
     *
     * The same five lines the wizard jump runs, and the same five the
     * headless build uses -- see main-none.c, which explains why Ctrl-A
     * is not an option. This is for showing somebody a floor, which
     * should not mean walking a character to a staircase blind.
     */
    if (sdl3_start_depth > 0 && !sdl3_dived && character_generated)
    {
        sdl3_dived = TRUE;
        set_dungeon_type(sdl3_start_dungeon);
        dun_level = sdl3_start_depth;
        p_ptr->inside_arena = FALSE;
        p_ptr->wild_mode = FALSE;
        p_ptr->energy_need = 0;
        prepare_change_floor_mode(CFM_RAND_PLACE | CFM_FIRST_FLOOR);
        p_ptr->leaving = TRUE;
    }

    /*
     * Climb to a character level, if the run asked for one.
     *
     * Not by setting p_ptr->lev, which would skip everything interesting.
     * gain_exp() runs the real path -- check_experience(), the message, the
     * stat prompt every fifth level -- because the thing worth testing is
     * what levelling *does* to the screen, not what level the character is.
     */
    if (sdl3_start_level > 0 && character_generated
     && p_ptr->lev < sdl3_start_level && !p_ptr->is_dead)
    {
        gain_exp(exp_requirement(p_ptr->lev + 1) - p_ptr->exp + 1);
    }

    /* One key per pump: the game gets to draw, animate and settle between
     * them, exactly as it would if somebody were typing. */
    if (sdl3_capture && sdl3_script &&
        sdl3_script[sdl3_script_at] == '%' && sdl3_script[sdl3_script_at + 1] == '!')
    {
        /* Capture-only idle step: run the real nonblocking event path without
         * supplying a key. inkey() can then refresh before the next scripted
         * click, just as it does while waiting for a person. */
        sdl3_script_at += 2;
        wait = FALSE;
    }
    else if (sdl3_script && sdl3_script[sdl3_script_at])
    {
        char c = sdl3_script[sdl3_script_at++];

        /* '{' and '}' are the arrows, which are the one thing a string of
         * characters cannot otherwise say. They go in as the same trigger
         * sequence a real key press would produce, so they travel the whole
         * path rather than a shortcut through it. */
        if (c == '{' || c == '}' || c == '[' || c == ']')
        {
            cptr dir = (c == '{') ? "up"
                     : (c == '}') ? "down"
                     : (c == '[') ? "left" : "right";

            sdl3_emit_trigger(dir, SDL_KMOD_NONE);
            return 0;
        }

        /* A backslash sends the next character exactly as it is, which is the
         * only way to reach the four the escapes spent -- and '~' is the
         * knowledge command, so without this there is no scripted route to
         * anything behind it. */
        if (c == '\\' && sdl3_script[sdl3_script_at])
        {
            Term_keypress(sdl3_script[sdl3_script_at++]);
            return 0;
        }

        /*
         * '%' and a digit: the three chrome toggles.
         *
         * They are the one part of the game a keystroke script cannot
         * otherwise reach, because they are read from the *keyboard* -- which
         * physical key, with shift held -- rather than from a character. A
         * script has no keyboard, so it needs to be able to say the command
         * rather than the key that produces it.
         */
        if (c == '%' && sdl3_script[sdl3_script_at])
        {
            char which = sdl3_script[sdl3_script_at++];

            if ((which == '0' || which == '9') && sdl3_capture)
            {
                SDL_Event reset = {0};
                reset.type = which == '0' ? SDL_EVENT_RENDER_DEVICE_RESET : SDL_EVENT_RENDER_TARGETS_RESET;
                if (which == '9')
                {
                    /* Model lost target contents: a synthetic event alone
                     * would leave the existing texture intact. */
                    SDL_SetRenderTarget(sdl3.renderer,sdl3.grid);
                    SDL_SetRenderDrawColor(sdl3.renderer,0,0,0,SDL_ALPHA_OPAQUE);
                    SDL_RenderClear(sdl3.renderer);
                }
                sdl3_dispatch(&reset);
                /* Capture immediately, before another input or Term_fresh()
                 * can conceal a missing repaint in the reset handler. */
                sdl3_capture_pending = TRUE;
                sdl3_present();
                return 0;
            }
            /* Capture-only pointer replay, in grid pixels: %@x,y; clicks,
             * %&x,y; hovers. Use the real handlers without a desktop window. */
            if ((which == '@' || which == '&') && sdl3_capture)
            {
                float x, y;
                int used=0,ox,oy;
                if (sscanf(sdl3_script+sdl3_script_at,"%f,%f;%n",&x,&y,&used)==2 && used>0)
                {
                    sdl3_script_at+=used;
                    sdl3_grid_origin(&ox,&oy);
                    if (which == '@')
                    {
                        SDL_MouseButtonEvent click={0};
                        click.x=x+ox; click.y=y+oy; click.button=SDL_BUTTON_LEFT;
                        sdl3_handle_mouse(&click);
                    }
                    else
                    {
                        SDL_MouseMotionEvent motion={0};
                        motion.x=x+ox; motion.y=y+oy;
                        sdl3_handle_motion(&motion);
                    }
                }
                return 0;
            }
            if (which == 'p' && sdl3_capture)
            {
                Term_fresh();
                sdl3_capture_pending = TRUE;
                sdl3_present();
                return 0;
            }
            if (which == '1') Term_keypress(SPECIAL_KEY_PANEL_MSG);
            else if (which == '2') Term_keypress(SPECIAL_KEY_PANEL_STATUS);
            else if (which == '3') Term_keypress(SPECIAL_KEY_PANEL_SIDE);

            /*
             * A digit is a panel; a letter is a control key.
             *
             * There was no way to say Ctrl-anything, which meant no way to
             * say Ctrl-X, which meant a scripted run could not end -- it sat
             * there until something killed it, and anything the game prints
             * on its way out was never printed. That includes the list of
             * tiles it wanted and did not have.
             */
            else if (isalpha((unsigned char)which))
                Term_keypress(toupper((unsigned char)which) & 0x1F);

            return 0;
        }

        Term_keypress(c == '~' ? 13 : (c == '^' ? ESCAPE : c));
        return 0;
    }

    /* The player changed the interface size. Rebuild here rather than where the
     * option was toggled: this is between operations, and resizing Term from
     * inside its own redraw is asking for trouble. */

    /*
     * The level moved behind the terminal, or came back out from behind it.
     *
     * Every cell's ground means something different on the two sides of that:
     * over the level it is a hole, everywhere else it is a colour. The
     * terminal only ever redraws the cells that *changed*, so cells whose
     * content happens to be the same on both sides keep whichever ground they
     * were given first -- which showed as opaque black patches scattered
     * through the sidebar, in the shape of whatever had last been written
     * there before the map took over.
     *
     * Here rather than in the drawing, for the same reason as the resize above:
     * this is between operations.
     */
    {
        static int applied = -1;
        int        now     = sdl3_map_overlay() ? 1 : 0;

        if (applied != now)
        {
            applied = now;
            /* Hiding the map repaints the same page, whose own Term_fresh()
             * may already have consumed its first clear. Keep its declared
             * rows and selection through this second clear, as on renderer
             * reset. Returning to the map must still discard stale rows. */
            if (!now) sdl3_menu_fresh = TRUE;
            Term_dirty();
        }
    }

    if (wait)
    {
        for (;;)
        {
            if (SDL_PollEvent(&e)) { sdl3_dispatch(&e); break; }

            /* Something is still animating. Keep the picture moving rather
             * than freezing a half-faded damage number on screen until the
             * player happens to press a key. */
            if (sdl3_sprites_reap() | sdl3_weather_advance()
             | sdl3_home_advance() | sdl3_calc_advance())
            {
                sdl3_present();
                SDL_Delay(16);
                continue;
            }

            /* The pointer moved onto or off a row while the game sat in
             * inkey(). Nothing else is going to repaint, so do it here. */
            if (sdl3_hover_dirty)
            {
                sdl3_hover_dirty = FALSE;
                sdl3_present();
                continue;
            }

            /*
             * Nothing to draw this instant -- but if the weather is running,
             * something is due shortly, so sleep only until then. See
             * sdl3_weather_due_ms() for what blocking outright did to it.
             */
            {
                int due  = sdl3_weather_due_ms();
                int home = sdl3_home_due_ms();
                int look = sdl3_look_due_ms();

                if (home >= 0 && (due < 0 || home < due)) due = home;
                if (look >= 0 && (due < 0 || look < due)) due = look;

                if (due >= 0)
                {
                    if (SDL_WaitEventTimeout(&e, due > 0 ? due : 1))
                        sdl3_dispatch(&e);
                    break;
                }
            }

            /* Nothing to draw and nothing to do: block properly, so an idle
             * game costs no CPU. */
            if (SDL_WaitEvent(&e)) sdl3_dispatch(&e);
            break;
        }
    }

    while (SDL_PollEvent(&e)) sdl3_dispatch(&e);

    return 0;
}


/**** Term hooks ****/


static errr Term_xtra_sdl3(int n, int v)
{
    switch (n)
    {
        case TERM_XTRA_EVENT:
            return sdl3_pump_events(BOOL(v));

        case TERM_XTRA_FLUSH:
        {
            SDL_Event e;
            while (SDL_PollEvent(&e)) /* discard */;
            return 0;
        }

        case TERM_XTRA_CLEAR:
        {
            SDL_FRect all;

            /*
             * A full clear means no menu is showing, whatever the last menu
             * event said. Without this the selected row survived its own
             * menu: the start screen's bar was still live when the screen was
             * cleared, so the erase painted it back in selection colour and
             * left a green slab lying under the character sheet. It only
             * became visible when the home menu stopped drawing a panel over
             * the top of it. The frame itself is NOT dropped here: the home
             * screen declares its panel once and then only redraws rows, so
             * clearing it took the title and the backdrop with it.
             *
             * The frame goes with it, for the same reason. The start screen's
             * panel was still declared when the screen was cleared, and the
             * moment the home layout switched off, the per-cell erase started
             * painting panel colour again inside a frame that was no longer
             * on screen -- one green slab across the character sheet, on
             * whichever row happened to be erased next.
             */
            if (!sdl3_menu_fresh) sdl3_title_row = sdl3_select_row = -1;

            /*
             * And the clickable rows -- but only the stale ones. See
             * sdl3_menu_fresh: this clear arrives from inside Term_fresh(),
             * after the frame's own rows have been declared, so dropping them
             * here unconditionally threw away the menu that was about to be
             * drawn. Rows survive one clear; a screen that declares none gets
             * the old ones taken away at the next.
             */
            if (!sdl3_menu_fresh)
            {
                sdl3_menu_ct = 0;
                if (sdl3_hover_row >= 0) sdl3_hover_dirty = TRUE;
                sdl3_hover_row = -1;
            }
            sdl3_menu_fresh = FALSE;

            all.x = 0.0f;
            all.y = 0.0f;
            all.w = (float)(sdl3.cols * sdl3.cell_w);
            all.h = (float)(sdl3.rows * sdl3.cell_h);

            /*
             * This is what actually lays down the blank ground, and it is
             * easy to miss why.
             *
             * A clear wipes the terminal's record of what is on screen to
             * blanks, and then only *changed* cells are redrawn -- so a cell
             * that is blank before and after is never written again. Whatever
             * this fill leaves is what a blank cell looks like until something
             * is put in it. Painting it opaque here while the map was behind
             * the terminal left the sidebar and the status bar as black
             * patches with the panel showing only in the shape of the text
             * that had been drawn over them.
             */
            sdl3_ground_colour(TERM_DARK);
            SDL_RenderFillRect(sdl3.renderer, &all);

            /* A clear says every cell is about to be redrawn -- not that the
             * menu went away. Only the semantic events mean that, and they
             * call sdl3_panel_drop(). So put the ground straight back, or the
             * canvas fill above would erase a panel that is still up. */
            sdl3_panel_ground();
            sdl3_gauge_ground();
            return 0;
        }

        case TERM_XTRA_SHAPE:
            sdl3.cursor_visible = BOOL(v);
            return 0;

        case TERM_XTRA_FRESH:
            sdl3_sprites_reap();
            sdl3_present();
            return 0;

        case TERM_XTRA_DELAY:
            if (v > 0) SDL_Delay((Uint32)v);
            return 0;

        case TERM_XTRA_SOUND:
            ui_sound_options(display_sound_get(), display_sound_pack_get(), display_sound_volume_get());
            ui_sound_details(display_footsteps_get(), display_menu_sounds_get());
            ui_sound_play(v);
            return 0;

        /* Not wired up yet, but the game is allowed to ask. */
        case TERM_XTRA_NOISE:
        case TERM_XTRA_BORED:
        case TERM_XTRA_REACT:
        case TERM_XTRA_ALIVE:
        case TERM_XTRA_LEVEL:
        case TERM_XTRA_FROSH:
            return 0;
    }

    return 1;
}


/**** The semantic channel ****/









/*
 * Draw one glyph at an arbitrary size, from whichever atlas is closest to it.
 * Passing NULL falls back to the base atlas.
 */
void sdl3_draw_glyph_scaled(const sdl3_atlas_entry *atlas,
                                   float px, float py, float w, float h,
                                   char ch, int bank)
{
    SDL_Texture *tex = atlas ? atlas->tex : sdl3.atlas;
    int cw = atlas ? atlas->cell_w : sdl3.cell_w;
    int chh = atlas ? atlas->cell_h : sdl3.cell_h;
    SDL_FRect src, dst;

    if (ch < GLYPH_FIRST || ch > GLYPH_LAST) return;

    src.x = (float)((ch - GLYPH_FIRST) * cw);
    src.y = (float)(bank * chh);
    src.w = (float)cw;
    src.h = (float)chh;

    dst.x = px;
    dst.y = py;
    dst.w = w;
    dst.h = h;

    SDL_RenderTexture(sdl3.renderer, tex, &src, &dst);
}


/* The perceived brightness of one of the game's colours, 0..255. */
int sdl3_luma(byte attr)
{
    return (299 * ormeq_color_table[attr][1]
          + 587 * ormeq_color_table[attr][2]
          + 114 * ormeq_color_table[attr][3]) / 1000;
}








/*
 * One glyph at an arbitrary pixel position, addressed by codepoint so the
 * decorative slots above ASCII can be reached.
 *
 * A codepoint the atlas has not got draws nothing. That is deliberate: a gap
 * is a shade lighter than it should be, where a .notdef box is a hollow
 * rectangle in the middle of a picture.
 */


/* One atlas slot at an arbitrary size, for pictures rather than for text. */
void sdl3_draw_slot_scaled(const sdl3_atlas_entry *atlas, int slot,
                                  float px, float py, float w, float h,
                                  int bank)
{
    SDL_Texture *tex = atlas ? atlas->tex   : sdl3.atlas;
    int          cw  = atlas ? atlas->cell_w : sdl3.cell_w;
    int          chh = atlas ? atlas->cell_h : sdl3.cell_h;
    SDL_FRect    src, dst;

    if (slot < 0) return;

    src.x = (float)(slot * cw);
    src.y = (float)(bank * chh);
    src.w = (float)cw;
    src.h = (float)chh;

    dst.x = px;
    dst.y = py;
    dst.w = w;
    dst.h = h;

    SDL_RenderTexture(sdl3.renderer, tex, &src, &dst);
}


void sdl3_draw_glyph_cp(float px, float py, u32b cp)
{
    SDL_FRect src, dst;
    int       slot = sdl3_atlas_slot_of(cp);

    if (slot < 0) return;

    src.x = (float)(slot * sdl3.cell_w);
    src.y = 0.0f;
    src.w = (float)sdl3.cell_w;
    src.h = (float)sdl3.cell_h;

    dst.x = px;
    dst.y = py;
    dst.w = (float)sdl3.cell_w;
    dst.h = (float)sdl3.cell_h;

    SDL_RenderTexture(sdl3.renderer, sdl3.atlas, &src, &dst);
}


void sdl3_draw_glyph_at(float px, float py, char ch)
{
    sdl3_draw_glyph_cp(px, py, (u32b)(byte)ch);   /* ATLAS_TEXT */
}




/*
 * Tint the row under the pointer.
 *
 * A wash on top rather than a background behind: the grid texture already has
 * the text baked into it, so there is nothing to draw behind. Alpha is kept
 * low enough that the row reads normally through it.
 */
static void sdl3_draw_hover(void)
{
    SDL_FRect r;
    sdl3_menu_row *m;

    if (sdl3_hover_row < 0 || sdl3_hover_row >= sdl3_menu_ct) return;

    m = &sdl3_menu[sdl3_hover_row];

    {
        int x0, x1;

        sdl3_row_span(m->y, m->x, m->w, &x0, &x1);

        r.x = (float)(x0 * sdl3.cell_w);
        r.y = (float)(m->y * sdl3.cell_h);
        r.w = (float)((x1 - x0) * sdl3.cell_w);
        r.h = (float)sdl3.cell_h;
    }

    SDL_SetRenderDrawBlendMode(sdl3.renderer, SDL_BLENDMODE_BLEND);
    {
        /* Selection carries its own alpha; hover is a lighter touch than a
         * committed selection, so take about half of it. */
        theme_color_t c = theme_ui(THEME_UI_SELECTION);
        SDL_SetRenderDrawColor(sdl3.renderer, c.r, c.g, c.b, (Uint8)(c.a / 2));
    }
    SDL_RenderFillRect(sdl3.renderer, &r);
    SDL_SetRenderDrawBlendMode(sdl3.renderer, SDL_BLENDMODE_NONE);
}


/*
 * What the pointer is resting on, named.
 *
 * The game's look command is `l`, then the target cycle, then a line to read
 * and an escape to get back out -- four keystrokes to answer "what is that".
 * This answers it by pointing, and answers nothing else: the creature's name
 * and how hurt it is, which the monster list already shows for everything in
 * sight. Faster to reach, not more to know.
 *
 * Placed on the side of the window the pointer is not on. A card that covers
 * the square you are asking about is a card that answers by hiding.
 */




/*
 * Put everything on the screen: the grid Term owns, then the cursor, then our
 * sprites, then whatever the bolt animation is holding.
 *
 * Note the cursor is drawn *here* rather than into the grid texture. Painting
 * it into the grid baked it in until something else redrew that cell, which
 * left stale cursor boxes lying around.
 */


void sdl3_present(void)
{
    SDL_FRect dst;
    ui_ambient_options(display_ambient_get(), display_sound_pack_get(), display_ambient_volume_get());
    ui_ambient_update(ui_ambient_choose(ui_screen_phase() == UI_PHASE_WORLD &&
        character_dungeon && !p_ptr->is_dead, dun_level > 0 && !p_ptr->wild_mode,
        dun_level, is_daytime()), character_icky || store_hack || screen_is_saved() ||
        ui_screen_has_layer(UI_LAYER_MENU | UI_LAYER_MODAL));

    dst.x = 0.0f;
    dst.y = 0.0f;
    dst.w = (float)sdl3.grid_w;
    dst.h = (float)sdl3.grid_h;

    SDL_SetRenderTarget(sdl3.renderer, NULL);

    /*
     * A page that owns the screen owns the margins too.
     *
     * Everything up to here is drawn into the grid texture, which is exactly
     * cols x rows cells and so never quite the size of the window. The margin
     * is cleared here, and clearing it to canvas put a black band above and
     * below any full-screen page -- help, options, character creation. The
     * band was ours, not the game's; nothing was missing from the page.
     */
    sdl3_ui_colour(sdl3_panel_fullscreen() ? THEME_UI_PANEL : THEME_UI_CANVAS);
    SDL_RenderClear(sdl3.renderer);

    /*
     * Centre whatever the grid does not fill. The grid has fixed dimensions
     * now, so a window of a different shape always leaves something over, and
     * splitting it reads as deliberate where pinning it to a corner does not.
     *
     * Done with a viewport rather than by offsetting each draw, so everything
     * after this -- the grid, the zoomed map, the hover bar, the sprites --
     * carries on working in grid coordinates and knows nothing about it. The
     * mouse subtracts the same origin; see sdl3_grid_origin().
     */
    {
        SDL_Rect view;
        int ox, oy;

        sdl3_grid_origin(&ox, &oy);
        view.x = ox;
        view.y = oy;
        view.w = sdl3.grid_w;
        view.h = sdl3.grid_h;
        SDL_SetRenderViewport(sdl3.renderer, &view);
    }

    /*
     * The level first, the terminal over it.
     *
     * It used to be the other way about, with the map painted into the hole
     * the chrome left. Behind is better and it is not only a look: the chrome
     * stops being a wall the dungeon ends at, the player sits in the middle of
     * the window rather than in the middle of what was left of it, and a panel
     * switched off gives back the ground it was standing on instead of moving
     * the whole level sideways.
     *
     * What makes it work is that the grid carries alpha: a hole over the
     * level, and three panes of the theme's panel colour painted on it before
     * the terminal goes over the top. See sdl3_draw_chrome().
     */
    if (sdl3_map_overlay())
    {
        sdl3_draw_zoomed_map();
        sdl3_draw_chrome();
    }

    sdl3_draw_home();

    SDL_RenderTexture(sdl3.renderer, sdl3.grid, NULL, &dst);
    sdl3_draw_edge_warning();

    /* A page has no edge: the window is its edge. Only an overlay is framed. */
    if (sdl3_panel_active && !sdl3_home_active() && !sdl3_panel_fullscreen())
    {
        SDL_FRect r;

        r.x = (float)(sdl3_panel_x * sdl3.cell_w);
        r.y = (float)(sdl3_panel_y * sdl3.cell_h);
        r.w = (float)(sdl3_panel_w * sdl3.cell_w);
        r.h = (float)(sdl3_panel_h * sdl3.cell_h);

        sdl3_ui_colour(THEME_UI_BORDER);
        SDL_RenderRect(sdl3.renderer, &r);
    }

    sdl3_draw_rule();
    sdl3_draw_scroll();
    sdl3_draw_portrait();
    sdl3_draw_weather();
    sdl3_draw_hover();
    sdl3_draw_cursor();
    sdl3_draw_blast();
    sdl3_draw_sprites();
    sdl3_draw_calculation();
    sdl3_draw_look_card();

    if (sdl3.bolt_live)
    {
        SDL_SetTextureColorMod(sdl3.atlas,
                               ormeq_color_table[sdl3.bolt_attr][1],
                               ormeq_color_table[sdl3.bolt_attr][2],
                               ormeq_color_table[sdl3.bolt_attr][3]);
        if (sdl3_bolt_actor)
        {
            SDL_Texture *texture;
            SDL_FRect source, cell={sdl3.bolt_x,sdl3.bolt_y,sdl3_bolt_w,sdl3_bolt_h};
            if (sdl3_tiles_on() && ui_tiles_find("monster",sdl3_bolt_tile,&texture,&source))
            {
                SDL_SetTextureColorMod(texture,255,255,255);
                SDL_RenderTexture(sdl3.renderer,texture,&source,&cell);
            }
            else
            {
                sdl3_atlas_entry *atlas=sdl3_atlas_for(sdl3_map_quad(0,0,cell.w,cell.h).h);
                texture=atlas ? atlas->tex : sdl3.atlas;
                SDL_SetTextureColorMod(texture,ormeq_color_table[sdl3.bolt_attr][1],
                    ormeq_color_table[sdl3.bolt_attr][2],ormeq_color_table[sdl3.bolt_attr][3]);
                sdl3_draw_map_glyph(atlas,cell,sdl3.bolt_ch,FALSE);
            }
        }
        else sdl3_draw_glyph_at(sdl3.bolt_x, sdl3.bolt_y, sdl3.bolt_ch);
    }

    SDL_SetRenderViewport(sdl3.renderer, NULL);
    if (sdl3_capture_pending && sdl3_capture)
    {
        SDL_Surface *shot = SDL_RenderReadPixels(sdl3.renderer, NULL);
        sdl3_capture_pending = FALSE;
        if (shot)
        {
            IMG_SavePNG(shot, sdl3_capture);
            if (sdl3_verbose) sdl3_calc_capture(shot);
            SDL_DestroySurface(shot);
        }
        printf("capture-menu %d %d %d %d\n", sdl3_map_overlay() ? 1 : 0,
            sdl3_menu_ct, sdl3_select_row, sdl3_hover_row);
        if (sdl3_verbose)
        {
            int i, x, y, tail = 0;
            u32b cells = 2166136261U;
            /* The title's backdrop animates on the clock. Compare its actual
             * text/colours separately from pixels in headless input tests. */
            for (y = 0; y < sdl3.rows; ++y)
                for (x = 0; x < sdl3.cols; ++x)
                {
                    byte a = 0;
                    char c = 0;
                    Term_what(x, y, &a, &c);
                    if (y >= sdl3.rows - 4 && c && c != ' ') ++tail;
                    cells = (cells ^ a) * 16777619U;
                    cells = (cells ^ (unsigned char)c) * 16777619U;
                }
            printf("capture-cells %d %lu\n", sdl3_home_active() ? 1 : 0,
                (unsigned long)cells);
            printf("capture-panel %d %d %d %d %d %d\n", sdl3_panel_active ? 1 : 0,
                sdl3_panel_x, sdl3_panel_y, sdl3_panel_w, sdl3_panel_h, tail);
            for (i = 0; i < sdl3_menu_ct; ++i)
            {
                sdl3_menu_row *row = &sdl3_menu[i];
                printf("capture-row %u %d %d %d\n", (unsigned char)row->key,
                    row->x, row->y, row->w);
            }
        }
        if (sdl3_map_overlay())
        {
            sdl3_map_view v;
            int x,y;
            sdl3_map_view_get(&v); ui_map_player(&x,&y);
            printf("capture-map %.3f %.3f %.3f %.3f %d %d %d\n",
                v.origin_x,v.origin_y,v.scale_x,v.scale_y,x,y,ui_tiles_count());
        }
    }
    SDL_RenderPresent(sdl3.renderer);
    SDL_SetRenderTarget(sdl3.renderer, sdl3.grid);
}


/*
 * Animate one step of a projectile.
 *
 * The game would draw the bolt into a cell, sleep, and rub it out -- one hard
 * jump per dungeon square. Knowing it *is* a projectile, we slide it between
 * the two cells instead.
 *
 * This one is deliberately synchronous: the game resolves the hit as soon as
 * it returns, so an animation that trailed behind would show a bolt still in
 * flight after its victim had already died.
 *
 * The loop is bounded by wall clock, not by frame count. Four SDL_Delay(2)
 * calls do not take eight milliseconds -- each rounds up to the scheduler's
 * quantum, so a naive subdivision overran the game's budget several times
 * over. That is slow enough that a key pressed during the flight gets eaten by
 * the input flush afterwards, which is exactly the "I had to press F twice"
 * symptom.
 */
static bool sdl3_animate_bolt(const ui_event_t *e)
{
    const int frames = 4;

    float x0, y0, x1, y1;

    Uint64 start = SDL_GetTicks();
    int budget = e->msec > 0 ? e->msec : 0;
    int i;

    sdl3_cell_to_px(e->from.x, e->from.y, &x0, &y0);
    sdl3_cell_to_px(e->to.x,   e->to.y,   &x1, &y1);

    sdl3.bolt_live = TRUE;
    sdl3.bolt_attr = e->attr;
    sdl3.bolt_ch   = e->ch;
    sdl3_bolt_actor = e->kind == UI_EVENT_KNOCKBACK;
    sdl3_bolt_tile[0]=0;
    if (sdl3_bolt_actor)
    {
        SDL_FRect from=sdl3_cell_rect(e->from.x,e->from.y);
        SDL_FRect to=sdl3_cell_rect(e->to.x,e->to.y);
        point_t cave=ui_xy_to_cave_pt(e->to.x,e->to.y);
        cptr name=ui_map_monster(cave.x,cave.y);
        x0=from.x; y0=from.y; x1=to.x; y1=to.y;
        sdl3_bolt_w=to.w; sdl3_bolt_h=to.h;
        if (player_bold(cave.y,cave.x)) sdl3.bolt_attr=ui_player_health_attr(e->attr);
        else if (name) my_strcpy(sdl3_bolt_tile,name,sizeof(sdl3_bolt_tile));
    }

    for (i = 1; i <= frames; i++)
    {
        float t = (float)i / (float)frames;
        int elapsed, left;

        sdl3.bolt_x = x0 + (x1 - x0) * t;
        sdl3.bolt_y = y0 + (y1 - y0) * t;
        sdl3_present();

        elapsed = (int)(SDL_GetTicks() - start);
        left    = budget - elapsed;

        /* Out of time: jump straight to the end and stop. */
        if (left <= 0) break;

        /* Share what is left between the frames we have not drawn yet. */
        SDL_Delay((Uint32)(left / (frames - i + 1)));
    }

    sdl3.bolt_live = FALSE;
    return TRUE;
}


/*
 * Events the game offers us. Returning TRUE means "we drew it"; returning
 * FALSE leaves the game to draw it the old way, which is what makes it safe
 * to ignore anything we have not learned yet.
 */
/*
 * A display preference changed: typeface, density, theme, fullscreen.
 *
 * The longest of these by a good margin, and every paragraph in it is a bug
 * that was fixed once. Read it before changing the order of anything.
 */
static bool _on_display_changed(void)
{
        int  out_w = 0, out_h = 0;
        bool geometry = FALSE;
        bool typeface = FALSE;

        ui_sound_options(display_sound_get(), display_sound_pack_get(), display_sound_volume_get());
        ui_music_options(display_music_get(), display_music_volume_get());
        ui_sound_details(display_footsteps_get(), display_menu_sounds_get());
        ui_ambient_options(display_ambient_get(), display_sound_pack_get(), display_ambient_volume_get());
        if (!sdl3.initialised) return TRUE;
        ui_tiles_select(display_map_mode_get(), display_tile_detail_get());

        if (sdl3_fullscreen_applied != display_fullscreen_get() && !sdl3_windowed)
        {
            sdl3_fullscreen_applied = display_fullscreen_get();

            SDL_SetWindowFullscreen(sdl3.window,
                                    display_fullscreen_get() ? true : false);

            /*
             * SDL3 applies window state asynchronously: the call above
             * asks, and the window changes some time later. Everything
             * below measures the window, so without waiting the grid
             * gets fitted to the size it used to be, and the real
             * change then lands on a grid that was rebuilt for the old
             * one. That is what left it apparently dropping out of
             * fullscreen when the typeface changed, and what left the
             * window without keyboard or mouse focus afterwards.
             */
            SDL_SyncWindow(sdl3.window);

            /* And text input does not always survive the transition. */
            SDL_StopTextInput(sdl3.window);
            SDL_StartTextInput(sdl3.window);

            geometry = TRUE;
        }

        if (sdl3_font_applied != display_font_get())
        {
            TTF_Font *previous = sdl3.font;
            float     old_size = sdl3_font_size;

            /*
             * Open the new face before letting go of the old one. If it
             * cannot be opened -- or cannot be made into an atlas below --
             * the game carries on in the typeface it already had, which is
             * a great deal better than carrying on with none.
             */
            sdl3.font = NULL;
            sdl3_font_size = 0.0f;

            if (sdl3_open_font() || !sdl3_font_got_choice)
            {
                if (sdl3.font) TTF_CloseFont(sdl3.font);

                sdl3.font = previous;
                sdl3_font_size = old_size;

                plog_fmt("SDL3: keeping %s; %s could not be opened",
                         display_font_name(sdl3_font_applied),
                         display_font_name(display_font_get()));

                /* Put the setting back so the menu does not claim a face
                 * that is not on screen. Setting it announces again, and
                 * that pass finds nothing to do. */
                display_font_set(sdl3_font_applied);
                return TRUE;
            }

            if (previous) TTF_CloseFont(previous);
            sdl3_font_applied = display_font_get();

            /*
             * Remember that it changed, rather than asking again
             * below whether it did.
             *
             * The line above has just made the answer no. So the
             * refit was skipped, the new face was open but no atlas
             * was ever built from it, and the screen carried on in
             * the old one -- which looked like the setting changing
             * sometimes and not others, the difference being whether
             * something else happened to force a refit in the same
             * breath.
             */
            typeface = TRUE;
        }

        if (geometry || typeface
         || sdl3_density_applied != display_density_get())
        {
            sdl3_density_applied = display_density_get();
            SDL_GetRenderOutputSize(sdl3.renderer, &out_w, &out_h);
            if (out_w > 0 && out_h > 0) sdl3_handle_resize(out_w, out_h);
        }
        else
        {
            /* A theme, then: same geometry, different colours in every
             * cell, so everything has to be laid down again. */
            Term_dirty();
        }

    return TRUE;
}

/* A panel is about to be painted here. Held rather than drawn: see the note
 * above sdl3_panel_active. */
static bool _on_panel(const ui_event_t *e)
{
        int x = e->from.x, y = e->from.y;
        int w = e->amount,  h = e->msec;

        if (x < 0) { w += x; x = 0; }
        if (y < 0) { h += y; y = 0; }
        if (x + w > sdl3.cols) w = sdl3.cols - x;
        if (y + h > sdl3.rows) h = sdl3.rows - y;
        /* A frame of no size means there is no frame: something has taken
         * the screen from whatever menu declared the last one. See
         * ui_event_panel_none(). */
        if (w <= 0 || h <= 0)
        {
            sdl3_panel_drop();
            return TRUE;
        }

        sdl3_panel_x = x;
        sdl3_panel_y = y;
        sdl3_panel_w = w;
        sdl3_panel_h = h;
        sdl3_panel_active = TRUE;

        if (sdl3_verbose)
        {
            printf("SDL3: panel %d,%d %dx%d in a %dx%d grid\n",
                   x, y, w, h, sdl3.cols, sdl3.rows);
            fflush(stdout);
        }

        /*
         * Laying the ground wipes glyphs out of the grid texture that Term
         * still believes are on screen -- so it would not redraw them, and
         * the panel came up empty on every repaint after the first. Mark
         * the screen instead: the next refresh lays the ground itself (see
         * the clear case) and puts all the text back on top of it, in one
         * pass, with nothing half-drawn shown on the way.
         */
        Term_dirty();

    return TRUE;
}

/* A quantity worth seeing as well as reading -- one bar per row of the
 * sidebar. */
static bool _on_gauge(const ui_event_t *e)
{
        int  gi, slot = -1, was, full;
        byte mix[3], was_col;

        /* One slot per row: the sidebar redraws itself constantly and a
         * list would fill up with the same two bars. */
        for (gi = 0; gi < SDL3_MAX_GAUGES; gi++)
        {
            if (sdl3_gauge[gi].live && sdl3_gauge[gi].y == e->from.y)
            {
                slot = gi;
                break;
            }
            if (slot < 0 && !sdl3_gauge[gi].live) slot = gi;
        }

        if (slot < 0) return TRUE;


        was     = sdl3_gauge[slot].live ? sdl3_gauge_full(slot, mix) : -1;
        was_col = sdl3_gauge[slot].live ? sdl3_gauge[slot].attr : 255;

        sdl3_gauge[slot].x    = (short)e->from.x;
        sdl3_gauge[slot].y    = (short)e->from.y;
        sdl3_gauge[slot].w    = (short)e->amount;
        sdl3_gauge[slot].cur  = (short)e->to.x;
        sdl3_gauge[slot].max  = (short)e->to.y;
        sdl3_gauge[slot].attr = e->attr;
        sdl3_gauge[slot].live = TRUE;

        full = sdl3_gauge_full(slot, mix);

        /*
         * Ask for a repaint when the bar's *length* changes.
         *
         * The row cannot be repainted on its own: Term skips cells whose
         * contents are unchanged, and the blank column between "HP" and
         * the digits never changes -- so the bar came out in two pieces
         * with a gap where that space sits. Painting the row directly
         * instead went underneath everything, because Term then had no
         * reason to put the text back. A whole repaint is the mechanism
         * that already knows how to do both, and hit points change a few
         * times a fight rather than a few times a frame.
         *
         * The *colour* counts as a change too. Going from hurt to whole
         * leaves the length alone but turns the bar from amber to green,
         * and repainting only the cells whose digits altered left the row
         * in two colours at once.
         */
        if (full != was || sdl3_gauge[slot].attr != was_col) Term_dirty();

    return TRUE;
}

/* The rows a document declared are no longer true. Sent whenever anything
 * paints, which is what makes it safe to forget everything hung off them. */
static bool _on_menu_clear(void)
{
        sdl3_menu_ct = 0;
        sdl3_menu_fresh = FALSE;
        sdl3_title_row = sdl3_select_row = -1;
        /* The rows are gone, so any highlight on one is a lie. */
        if (sdl3_hover_row >= 0) sdl3_hover_dirty = TRUE;
        sdl3_hover_row = -1;
        /* And so is any portrait, and any rule: see sdl3_marks_forget(). */
        sdl3_marks_forget();

    return TRUE;
}

/* One clickable row. */
static bool _on_menu_item(const ui_event_t *e)
{
        sdl3_menu_fresh = TRUE;
        if (sdl3_menu_ct < SDL3_MAX_MENU_ROWS)
        {
            sdl3_menu_row *m = &sdl3_menu[sdl3_menu_ct++];

            m->x   = (short)e->to.x;
            m->y   = (short)e->to.y;
            m->w   = (short)e->amount;
            m->key = e->ch;

        }

    return TRUE;
}

static bool sdl3_on_ui_event(const ui_event_t *e)
{
    if (!sdl3.initialised) return FALSE;

    switch (e->kind)
    {
        case UI_EVENT_BOLT:
            return sdl3_animate_bolt(e);

        case UI_EVENT_DAMAGE:      return sdl3_marks_damage(e);
        case UI_EVENT_BUMP:        return sdl3_marks_bump(e);
        case UI_EVENT_DEATH:       return sdl3_marks_death(e);
        case UI_EVENT_RULE:        return sdl3_marks_rule(e);
        case UI_EVENT_SCROLL:      return sdl3_marks_scroll(e);
        case UI_EVENT_PORTRAIT:    return sdl3_marks_portrait(e);
        case UI_EVENT_BLAST_CELL:  return sdl3_marks_blast_cell(e);
        case UI_EVENT_BLAST_END:   return sdl3_marks_blast_end();

        /*
         * The eight above are sdl3_marks.c: things the game declares and we
         * draw over the finished grid. The switch stays here because it is
         * the table of what this port answers, and a table is worth reading.
         */
        case UI_EVENT_KNOCKBACK:
            /* Same slide as a bolt: the glyph is already at the destination in
             * the grid, so this reads as the shove that put it there. */
            return sdl3_animate_bolt(e);

        case UI_EVENT_MAP_SHOWN:
            sdl3_doc_covered = FALSE;
            sdl3_panel_drop();
            return TRUE;

        case UI_EVENT_DISPLAY_CHANGED:   return _on_display_changed();

        case UI_EVENT_SCREEN_CHANGED:
            /* A phase or layer came or went -- but "came" includes the menu
             * layer the panel we are holding belongs to, and dropping the panel
             * on that fired a teardown a few microseconds after the menu asked
             * for the frame. So ask what the new shape actually is: the frame
             * is justified for exactly as long as a menu is up. */
            if (!ui_screen_has_layer(UI_LAYER_MENU)) sdl3_panel_drop();
            return TRUE;

        case UI_EVENT_MAP_HIDDEN:
            sdl3_doc_covered = TRUE;
            return TRUE;

        case UI_EVENT_CALCULATION_CARD:  return sdl3_calc_card(e);
        case UI_EVENT_CALCULATION:       return sdl3_calc_event();
        case UI_EVENT_MAP_FOCUS: sdl3_map_focus(e->to.x,e->to.y); return TRUE;
        case UI_EVENT_LOOK_CARD:         return sdl3_look_recall(e->look_card);
        case UI_EVENT_PANEL:             return _on_panel(e);

        case UI_EVENT_GAUGE:             return _on_gauge(e);

        case UI_EVENT_MENU_TITLE:
            sdl3_title_row = e->from.y;
            sdl3_title_x   = e->from.x;
            sdl3_title_w   = e->amount;
            return TRUE;

        case UI_EVENT_MENU_SELECT:
            sdl3_select_row = e->from.y;
            sdl3_select_x   = e->from.x;
            sdl3_select_w   = e->amount;
            return TRUE;

        case UI_EVENT_MENU_CLEAR:        return _on_menu_clear();

        case UI_EVENT_MENU_ITEM:         return _on_menu_item(e);

        default:
            return FALSE;
    }
}


static errr Term_curs_sdl3(int x, int y)
{
    sdl3.cursor_x = x;
    sdl3.cursor_y = y;
    return 0;
}


static errr Term_bigcurs_sdl3(int x, int y)
{
    return Term_curs_sdl3(x, y);
}


static errr Term_wipe_sdl3(int x, int y, int n)
{
    sdl3_fill_cells(x, y, n, TERM_DARK);
    return 0;
}


/**** Terrain variation ****/

/*
 * See ui_terrain.h. The game decides which grids vary and by how much; this is
 * the part that draws it.
 *
 * The tint is mixed from the glyph's *own* colour rather than a table of
 * material colours. Rock is grey, lava is red, water is blue -- the game has
 * already said what each material looks like, so a breath of the foreground
 * into the ground gives every material the right hue for nothing, and follows
 * the theme when the theme changes.
 */

static int sdl3_terrain_style(void)
{
    /* Never over a menu. Chrome and decoration painting the same cells is how
     * every one of the panel bugs started. */
    if (ui_screen_has_layer(UI_LAYER_MENU)) return UI_TERRAIN_PLAIN;
    if (sdl3_panel_active) return UI_TERRAIN_PLAIN;

    return display_terrain_get();
}


/* What is under this Term cell, if it is on the map and worth decorating? */
static bool sdl3_terrain_cell(int x, int y, int style, ui_terrain_t *t)
{
    point_t cp;

    /* Only tint map cells when the map is actually visible. A covering menu,
     * shop or character sheet shares the map rectangle but must keep its own colours. */
    if (!sdl3_map_showing()) return FALSE;
    if (!sdl3_in_map(x, y)) return FALSE;

    cp = ui_xy_to_cave_pt(x, y);

    return ui_terrain_at(cp.x, cp.y, style, t);
}

/* weight/64 of b, the rest a. */
void sdl3_blend(const byte a[3], const byte b[3], int weight, byte out[3])
{
    int i;

    for (i = 0; i < 3; i++)
        out[i] = (byte)((a[i] * (64 - weight) + b[i] * weight) / 64);
}

/* Move a colour `tone` percent towards white (+) or black (-). */
void sdl3_tone(const byte in[3], int tone, byte out[3])
{
    int target = tone > 0 ? 255 : 0;
    int t = tone < 0 ? -tone : tone;
    int i;

    if (t > 100) t = 100;

    for (i = 0; i < 3; i++)
        out[i] = (byte)((in[i] * (100 - t) + target * t) / 100);
}

/*
 * Is the light outside worth colouring, and is this row part of the map?
 *
 * The sidebar and the message line are not outdoors, so they keep the colours
 * the game chose for them whatever the hour.
 */
static bool sdl3_daylight(ui_daylight_t *sky)
{
    if (ui_screen_has_layer(UI_LAYER_MENU)) return FALSE;
    if (sdl3_panel_active) return FALSE;

    return ui_daylight(sky);
}


/*
 * `base` is the colour the cell would have been drawn in with no decoration at
 * all -- which is not always the game's colour for `a`. A menu heading has
 * already been given the theme's title colour by the time it gets here, and
 * taking the colour from `a` again would quietly throw that away.
 */
static void sdl3_draw_terrain_glyph(int x, int y, const byte base[3], char ch,
                                    int style, const ui_daylight_t *sky)
{
    ui_terrain_t  t;
    theme_color_t canvas;
    byte          ground[3], fore[3], lit[3];
    bool          on_map = sdl3_map_showing() && sdl3_in_map(x, y);

    fore[0] = base[0];
    fore[1] = base[1];
    fore[2] = base[2];

    if (on_map && ch == '@')
    {
        point_t player = cave_xy_to_ui_pt(px, py);
        if (x == player.x && y == player.y)
        {
            SDL_SetTextureColorMod(sdl3.atlas, base[0], base[1], base[2]);
            sdl3_draw_glyph(x, y, ch);
            return;
        }
    }

    /*
     * The hour first, so everything downstream -- the ground tint, the mark,
     * the tone -- is mixed from a colour that has already been through the
     * light outside. Doing it last would leave the decoration lit at noon
     * while the thing it decorates sat in moonlight.
     */
    if (sky && on_map)
    {
        byte sky_rgb[3];

        sky_rgb[0] = sky->r;
        sky_rgb[1] = sky->g;
        sky_rgb[2] = sky->b;

        sdl3_blend(fore, sky_rgb, sky->strength * 64 / 100, fore);
    }

    if (!sdl3_terrain_cell(x, y, style, &t))
    {
        SDL_SetTextureColorMod(sdl3.atlas, fore[0], fore[1], fore[2]);
        sdl3_draw_glyph(x, y, ch);
        return;
    }

    canvas    = theme_ui(THEME_UI_CANVAS);
    ground[0] = canvas.r;
    ground[1] = canvas.g;
    ground[2] = canvas.b;

    /*
     * Walls take more tint than floors: a wall is a solid thing and can carry
     * colour, while a floor is mostly the dark you walk through and turns
     * muddy if it is pushed. Grids lit by your own torch take a touch more
     * again, which puts a faint pool of warmth around you as you move.
     */
    /*
     * Nothing is laid in the ground. See ui_terrain.h: a wash varies over an
     * area rather than within a character, so it turned the map into patches
     * of slightly different darkness -- haze, not texture. The mark and the
     * tone are per-grid and crisp, which is what wear looks like at this size.
     */

    sdl3_tone(fore, t.tone, lit);

    SDL_SetTextureColorMod(sdl3.atlas, lit[0], lit[1], lit[2]);
    sdl3_draw_glyph(x, y, ch);
}


static errr Term_text_sdl3(int x, int y, int n, byte a, cptr s)
{
    int          i;
    int          style = sdl3_terrain_style();
    ui_daylight_t sky;
    bool         lit = sdl3_daylight(&sky);
    byte         base[3];

    sdl3_fill_cells(x, y, n, TERM_DARK);

    /*
     * The colour first, for every path.
     *
     * This used to sit *below* the terrain branch, which returned early -- so
     * with terrain variation on, or daylight on, or both, the heading never
     * reached it and every menu lost its title colour. It read as the terrain
     * setting leaking into the chrome, and in a sense it was: a decoration
     * path that skipped the chrome entirely.
     */
    if (y == sdl3_title_row)
    {
        /*
         * The heading takes the theme's title colour rather than whichever of
         * the game's thirty-two the document happened to name. Those thirty-two
         * are the colours the *world* is drawn in; a menu's heading is
         * furniture, and a theme that has chosen a title colour should get to
         * use it.
         */
        theme_color_t c = theme_ui(THEME_UI_TITLE);

        base[0] = c.r;
        base[1] = c.g;
        base[2] = c.b;
    }
    else
    {
        base[0] = ormeq_color_table[a][1];
        base[1] = ormeq_color_table[a][2];
        base[2] = ormeq_color_table[a][3];
    }

    if (style > UI_TERRAIN_PLAIN || lit)
    {
        /* Per cell rather than per run: the tint and the tone come from the
         * ground under each one, so a run cannot share them. Cells that are
         * not on the map take neither, and come out as `base`. */
        for (i = 0; i < n; i++)
        {
            if (sdl3_cell_hidden(x + i, y)) continue;
            sdl3_draw_terrain_glyph(x + i, y, base, s[i], style,
                                    lit ? &sky : NULL);
        }
        return 0;
    }

    SDL_SetTextureColorMod(sdl3.atlas, base[0], base[1], base[2]);

    for (i = 0; i < n; i++)
    {
        if (sdl3_cell_hidden(x + i, y)) continue;
        sdl3_draw_glyph(x + i, y, s[i]);
    }

    return 0;
}


static void Term_init_sdl3(term *t)
{
    /* Everything is set up before the term exists; nothing to do. */
    (void)t;
}


static void Term_nuke_sdl3(term *t)
{
    (void)t;
}


/**** Setup ****/






/*
 * (Re)create the target texture that the whole grid is drawn into.
 */
static errr sdl3_make_grid(int cols, int rows)
{
    SDL_Texture *grid;

    grid = SDL_CreateTexture(sdl3.renderer, SDL_PIXELFORMAT_RGBA8888,
                             SDL_TEXTUREACCESS_TARGET,
                             cols * sdl3.cell_w, rows * sdl3.cell_h);
    if (!grid)
    {
        plog_fmt("SDL3: could not create the grid texture: %s", SDL_GetError());
        return -1;
    }

    if (sdl3.grid) SDL_DestroyTexture(sdl3.grid);

    sdl3.grid   = grid;
    sdl3.cols   = cols;
    sdl3.rows   = rows;
    sdl3.grid_w = cols * sdl3.cell_w;
    sdl3.grid_h = rows * sdl3.cell_h;

    /* The grid is composited over the level, so its own transparency is the
     * thing that decides what shows through. Without this it is drawn opaque
     * and the level behind it never appears at all. */
    SDL_SetTextureBlendMode(grid, SDL_BLENDMODE_BLEND);

    /* A newly created target texture holds garbage. */
    SDL_SetRenderTarget(sdl3.renderer, sdl3.grid);
    SDL_SetRenderDrawColor(sdl3.renderer, 0, 0, 0, SDL_ALPHA_OPAQUE);
    SDL_RenderClear(sdl3.renderer);

    return 0;
}


/*
 * The window changed size: fit as many whole cells as it now holds, and tell
 * the game.
 *
 * Glyphs keep their pixel size, so a bigger window means more of the map
 * rather than a magnified version of the same map. Term_resize() runs the
 * resize hook itself, which is what makes the game re-lay-out its screen.
 */
/*
 * Fit the font to the window so that the density's grid fills it.
 *
 * This is the inversion that makes everything else work. The obvious way round
 * -- fix the font, let the grid grow -- scales nothing: a bigger window just
 * shows more cells at the same size, and every screen in the game stays exactly
 * as small as it was. Fixing the *grid* and fitting the font means asking for a
 * larger interface enlarges all of it, including the dozens of screens we have
 * never touched and are not going to.
 *
 * Point size is found by walking up while it still fits. A binary search would
 * be neater and is not worth it: this runs when a window is resized, tens of
 * times a session, and TTF_SetFontSize is cheap next to the atlas rebuild.
 */
static errr sdl3_fit_font(int px_w, int px_h, int *cols, int *row_count)
{
    float best = SDL3_ATLAS_MIN_PT;
    float pt;
    int   tries, rows = *row_count;
    bool  width_limited = FALSE;

    if (rows <= 0) return -1;

    /*
     * Fit the requested height, and let the columns follow. Keep the engine
     * minimum width visible too: Large density must not clip menus at 800x600.
     *
     * Fitting both ways means the window's shape almost never matches the
     * grid's, so one axis binds and the other is left as dead margin -- which
     * on a wide screen was a broad black band down each side. Density decides
     * how tall a row is; how many columns there is room for is then just
     * arithmetic, and the width always comes out full.
     */
    for (pt = SDL3_ATLAS_MIN_PT; pt <= SDL3_ATLAS_MAX_PT; pt += 1.0f)
    {
        int h, w, measured_h;

        if (!TTF_SetFontSize(sdl3.font, pt)) break;

        h = TTF_GetFontHeight(sdl3.font);
        if (h <= 0) break;
        if (h * rows > px_h) break;
        if (TTF_GetStringSize(sdl3.font, "M", 1, &w, &measured_h) && w * SDL3_MIN_COLS > px_w)
        {
            width_limited = TRUE;
            break;
        }

        best = pt;
    }

    /*
     * Second pass: check what we will actually draw.
     *
     * The metrics and the rendered atlas can disagree by a pixel -- the atlas
     * cell is the height of the surface SDL_ttf hands back, not what
     * TTF_GetFontHeight() reports -- and one pixel times thirty-four rows is a
     * clipped bottom line. So build it and measure the real thing, stepping
     * down until it genuinely fits.
     */
    for (tries = 0; tries < 8; tries++)
    {
        SDL_Texture *fresh;
        int          cw = 0, ch = 0;

        sdl3_font_size = best;
        TTF_SetFontSize(sdl3.font, best);

        /*
         * Build the replacement before letting go of what we have.
         *
         * This used to throw the old atlas away first and return on failure,
         * which left the game with no atlas at all -- so every glyph drew
         * nothing, for the rest of the session, and the only way out was to
         * quit. Picking certain typefaces did exactly that. A failure here now
         * costs nothing: the old sheet is still on the card and still correct.
         */
        fresh = sdl3_render_atlas(&cw, &ch);

        if (!fresh || cw <= 0 || ch <= 0)
        {
            if (fresh) SDL_DestroyTexture(fresh);
            plog_fmt("SDL3: could not build a glyph atlas at %.0fpt: %s",
                     best, SDL_GetError());
            return -1;
        }

        /* The cached ladder was measured against the old base size. */
        sdl3_atlas_forget();
        if (sdl3.atlas) SDL_DestroyTexture(sdl3.atlas);

        sdl3.atlas  = fresh;
        sdl3.cell_w = cw;
        sdl3.cell_h = ch;

        /* Sampled one texel to one pixel by everything except the zoomed map,
         * so a hard edge beats a blur if rounding puts us half a pixel off. */
        SDL_SetTextureScaleMode(sdl3.atlas, SDL_SCALEMODE_NEAREST);

        sdl3_publish_ink();

        if (sdl3.cell_w * SDL3_MIN_COLS > px_w) width_limited = TRUE;
        if (sdl3.cell_h * rows <= px_h && sdl3.cell_w * SDL3_MIN_COLS <= px_w) break;
        if (best <= SDL3_ATLAS_MIN_PT) break;

        best -= 1.0f;
    }

    /* If width constrained the font, use the newly available vertical space. */
    if (width_limited && sdl3.cell_h > 0)
        *row_count = MIN(SDL3_MAX_DIM, MAX(rows, px_h / sdl3.cell_h));

    /* Now the columns: as many whole cells as the width holds. */
    *cols = sdl3.cell_w > 0 ? px_w / sdl3.cell_w : SDL3_MIN_COLS;

    if (*cols < SDL3_MIN_COLS) *cols = SDL3_MIN_COLS;
    if (*cols > SDL3_MAX_DIM)  *cols = SDL3_MAX_DIM;

    return 0;
}


/*
 * The window changed size. The grid keeps the dimensions the density asked
 * for; the font grows or shrinks to suit.
 */
static void sdl3_handle_resize(int px_w, int px_h)
{
    int cols, rows;

    if (!sdl3.initialised) return;

    ui_screen_pixel_width_set(px_w);
    display_density_size(display_density_get(), &cols, &rows);

    if (rows < SDL3_MIN_ROWS) rows = SDL3_MIN_ROWS;
    if (rows > SDL3_MAX_DIM)  rows = SDL3_MAX_DIM;

    /* cols comes back from the fit: density sets the row height, the window
     * decides how many columns fit beside each other. */
    if (sdl3_fit_font(px_w, px_h, &cols, &rows))
    {
        /* The old atlas is still good -- see the comment in sdl3_fit_font --
         * so the screen keeps working. Nothing to do but leave it alone. */
        plog("SDL3: could not fit the font; keeping the previous size");
        return;
    }

    if (sdl3_verbose)
    {
        printf("SDL3: %dx%d px, %s -> %dx%d cells at %.0fpt (cell %dx%d)\n",
               px_w, px_h, display_density_name(display_density_get()),
               cols, rows, sdl3_font_size, sdl3.cell_w, sdl3.cell_h);
        fflush(stdout);
    }

    /* The texture must already be at its new size, because Term_resize() will
     * ask us to draw into it before it returns. */
    if (sdl3_make_grid(cols, rows)) return;

    if (cols != (int)sdl3.t.wid || rows != (int)sdl3.t.hgt)
    {
        Term_resize(cols, rows);
    }

    /* Mark, do not paint: a refit can happen while a menu is open, and
     * painting here put the dungeon on screen for a moment between one menu
     * frame and the next. Whatever is genuinely on screen redraws itself. */
    Term_dirty();
    if(reckoner_history_active() || doc_viewer_active()) Term_keypress(SPECIAL_KEY_UI_REDRAW);
}


static void sdl3_link_term(void)
{
    term *t = &sdl3.t;

    term_init(t, sdl3.cols, sdl3.rows, SDL3_KEY_QUEUE);

    /* We draw the cursor ourselves. */
    t->soft_cursor = TRUE;

    /* Erase to black. */
    t->attr_blank = TERM_DARK;
    t->char_blank = ' ';

    t->init_hook    = Term_init_sdl3;
    t->nuke_hook    = Term_nuke_sdl3;
    t->xtra_hook    = Term_xtra_sdl3;
    t->curs_hook    = Term_curs_sdl3;
    t->bigcurs_hook = Term_bigcurs_sdl3;
    t->wipe_hook    = Term_wipe_sdl3;
    t->text_hook    = Term_text_sdl3;

    t->data = (vptr)&sdl3;

    ormeq_term[0] = t;
    Term_activate(t);
}


static errr init_sdl3(void)
{
    display_density_size(display_density_get(), &sdl3.cols, &sdl3.rows);
    sdl3.cursor_visible = TRUE;

    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        plog_fmt("SDL3: SDL_Init failed: %s", SDL_GetError());
        return -1;
    }

    /* Sound is optional in the strongest sense: a machine with no audio device
     * plays the game exactly as before, and says so once rather than failing. */
    ui_sound_init();
    ui_music_init();
    ui_ambient_init();

    if (!TTF_Init())
    {
        plog_fmt("SDL3: TTF_Init failed: %s", SDL_GetError());
        return -1;
    }

    if (sdl3_open_font()) return -1;

    /* The window size is not known until the font tells us how big a cell is,
     * so open a placeholder and resize once the atlas is built. */
    if (!SDL_CreateWindowAndRenderer("Ormeq", 640, 400,
                                     SDL_WINDOW_RESIZABLE,
                                     &sdl3.window, &sdl3.renderer))
    {
        plog_fmt("SDL3: could not create a window: %s", SDL_GetError());
        return -1;
    }

    /* Load only the selected resolution of the optional sprite pack. */
    {
        /* Beside the executable, like every other lib directory -- not
         * relative to wherever the game happened to be started from. A
         * shortcut on a desktop has a working directory of its own, and
         * "lib/tiles" quietly found nothing there. */
        char        tiles[1024];
        const char *base = SDL_GetBasePath();

        strnfmt(tiles, sizeof(tiles), "%slib%stiles/pack",
                base ? base : "", PATH_SEP);

        ui_tiles_init(sdl3.renderer, tiles);
        ui_tiles_select(display_map_mode_get(), display_tile_detail_get());
        if (sdl3_verbose)
            printf("tiles: %d loaded from %s\n",
                   ui_tiles_count(), tiles);
    }

    if (sdl3_build_atlas()) return -1;

    /* From here on the renderer draws into the grid texture; only
     * TERM_XTRA_FRESH switches back to the window. */
    if (sdl3_make_grid(sdl3.cols, sdl3.rows)) return -1;

    SDL_SetWindowSize(sdl3.window, sdl3.grid_w, sdl3.grid_h);

    /* A floor rather than a limit: the grid no longer grows with the window, so
     * this only stops the font being fitted down to something unreadable. */
    SDL_SetWindowMinimumSize(sdl3.window, 640, 400);

    /* Fullscreen by default -- borderless desktop fullscreen, since no display
     * mode is set, so it neither changes resolution nor takes over the machine.
     * The resize that follows refits the font and the columns to whatever the
     * screen turns out to be. */
    sdl3_fullscreen_applied = display_fullscreen_get();
    if (display_fullscreen_get() && !sdl3_windowed)
        SDL_SetWindowFullscreen(sdl3.window, true);

    SDL_StartTextInput(sdl3.window);

    sdl3_density_applied = display_density_get();
    sdl3_font_applied    = display_font_get();

    sdl3_link_term();
    ui_event_subscribe(sdl3_on_ui_event);
    sdl3.initialised = TRUE;
    if (sdl3_requested_w && sdl3_windowed)
    {
        SDL_SetWindowSize(sdl3.window,sdl3_requested_w,sdl3_requested_h);
        SDL_SyncWindow(sdl3.window);
    }

    /* Fit to the window we actually ended up with. Going fullscreen raises a
     * resize before this point, and the handler drops those because nothing is
     * ready yet -- so ask once, now that everything is. */
    {
        int out_w = 0, out_h = 0;

        SDL_GetRenderOutputSize(sdl3.renderer, &out_w, &out_h);
        if (out_w > 0 && out_h > 0) sdl3_handle_resize(out_w, out_h);
    }

    if (sdl3_verbose)
    {
        int out_w = 0, out_h = 0, win_w = 0, win_h = 0;

        SDL_GetRenderOutputSize(sdl3.renderer, &out_w, &out_h);
        SDL_GetWindowSize(sdl3.window, &win_w, &win_h);

        printf("SDL3: font %.1fpt, cell %dx%d, grid %dx%d cells = %dx%d px\n",
               sdl3_font_size, sdl3.cell_w, sdl3.cell_h,
               sdl3.cols, sdl3.rows, sdl3.grid_w, sdl3.grid_h);
        printf("SDL3: window %dx%d, render output %dx%d, display scale %.2f\n",
               win_w, win_h, out_w, out_h,
               SDL_GetWindowDisplayScale(sdl3.window));
        fflush(stdout);
    }

    return 0;
}


static void sdl3_shutdown(void)
{
    sdl3_atlas_free();

    if (sdl3.grid)     SDL_DestroyTexture(sdl3.grid);
    /* Before the font closes: the art cache holds converted portraits that
     * only mean anything in the alphabet that is about to go. */
    ui_art_shutdown();
    if (sdl3_verbose) ui_tiles_report_missing();
    ui_tiles_shutdown();
    ui_ambient_shutdown();
    ui_music_shutdown();
    ui_sound_shutdown();

    if (sdl3.atlas)    SDL_DestroyTexture(sdl3.atlas);
    if (sdl3.font)     TTF_CloseFont(sdl3.font);
    if (sdl3.renderer) SDL_DestroyRenderer(sdl3.renderer);
    if (sdl3.window)   SDL_DestroyWindow(sdl3.window);

    sdl3.grid = NULL;
    sdl3.atlas = NULL;
    sdl3.font = NULL;
    sdl3.renderer = NULL;
    sdl3.window = NULL;

    TTF_Quit();
    SDL_Quit();
}


/*
 * Installed as quit_aux, so it runs however the game decides to leave.
 */
static void sdl3_quit_hook(cptr s)
{
    sdl3_shutdown();

    if (s && s[0])
    {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Ormeq", s, NULL);
    }
}


/*
 * Work out where the game data lives.
 *
 * Same rule as the Windows port: lib/ sits next to the executable.
 */
static void sdl3_init_paths(void)
{
    char path[1024];
    const char *base = SDL_GetBasePath();

    if (!base) base = ".";

    strnfmt(path, sizeof(path), "%slib%s", base, PATH_SEP);

    init_file_paths(path, path, path);
}


/*
 * Command line.
 *
 * Deliberately minimal -- the things you want to change before the window
 * exists, and nothing else. Anything the game can be told at runtime belongs
 * in the game, not here.
 */
/*
 * Rebuild the portrait plates.
 *
 * The paintings live in `_art-src/`, which git excludes: three and a half
 * megabytes of source asset that nothing at play time reads, and keeping them
 * out of the tree is the whole difference between shipping them and not.
 */
static void sdl3_generate_plates(void)
{
    static const char *kinds[] = { "monsters", "shopkeepers", "objects" };
    int  i;

    /* Neither SDL's video nor the game's directories are needed, but the
     * image loader wants SDL initialised. */
    if (!SDL_Init(0))
    {
        printf("SDL_Init failed: %s\n", SDL_GetError());
        return;
    }

    for (i = 0; i < (int)(sizeof(kinds) / sizeof(kinds[0])); i++)
    {
        char src[1024], out[1024];

        path_build(src, sizeof(src), "_art-src", kinds[i]);
        path_build(out, sizeof(out), "lib/art", kinds[i]);

        printf("%s:\n", kinds[i]);
        ui_art_generate(src, out);
    }

    SDL_Quit();
}


static void sdl3_usage(cptr name)
{
    printf("Usage: %s [options]\n", name);
    printf("  -f<size>   font size in points (default: scaled to the display)\n");
    printf("  -F<path>   path to a monospaced TTF to use\n");
    printf("  -n         start a new character, ignoring any existing save\n");
    printf("  -u<name>   play this character, so Continue can offer it\n");
    printf("  -x<keys>   play these keystrokes in at startup (~ Return, ^ Escape, {} arrows,"
           " %%1..3 panels, %%x is Ctrl-X)\n");
    printf("  -w         start windowed, ignoring the fullscreen preference\n");
    printf("  -S         audition sound effects (-S0 for synth only), then stop\n");
    printf("  -A         rebuild lib/art/*.art from the paintings in _art-src/\n");
    printf("  -D<n>      open on dungeon floor n, rather than in town\n");
    printf("  -L<n>      play up to character level n, the real way\n");
    printf("  -Y<n>      which dungeon to open in (1 is The Sink)\n");
    printf("  -z<factor> start at this zoom (the wheel is not scriptable)\n");
    printf("  -h         this message\n");
}


int main(int argc, char *argv[])
{
    bool new_game = FALSE;
    int i;

    argv0 = argv[0];

    for (i = 1; i < argc; i++)
    {
        if (argv[i][0] != '-') continue;

        switch (argv[i][1])
        {
            case 'f': sdl3_font_size = (float)atof(&argv[i][2]); break;
            case 'F': sdl3_font_override = &argv[i][2];          break;
            case 'n': new_game = TRUE;                           break;

            /*
             * Which character to play.
             *
             * Every other port has had this from the beginning and we simply
             * had not carried it over. Without it the start screen can never
             * offer Continue, because the name it looks a savefile up by is
             * never set -- and a game that cannot be told which character to
             * resume cannot be tested twice on the same dungeon.
             */
            case 'u':
            case 'U':
                if (argv[i][2]) my_strcpy(player_name, &argv[i][2], 32);
                break;
            case 'P': sdl3_capture = &argv[i][2]; break;
            case 'W':
                if (sscanf(&argv[i][2],"%dx%d",&sdl3_requested_w,&sdl3_requested_h) != 2 ||
                    sdl3_requested_w < 320 || sdl3_requested_w > 4096 ||
                    sdl3_requested_h < 240 || sdl3_requested_h > 2160)
                    sdl3_requested_w = sdl3_requested_h = 0;
                break;
            case 'v': sdl3_verbose = TRUE;                       break;
            case 'w': sdl3_windowed = TRUE;                      break;
            case 'x': sdl3_script = &argv[i][2];                 break;
            case 'D': sdl3_start_depth = atoi(&argv[i][2]);      break;
            case 'L': sdl3_start_level = atoi(&argv[i][2]);      break;
            case 'Y': sdl3_start_dungeon = atoi(&argv[i][2]);    break;

            /* Start at this zoom. The wheel is the way a player changes it,
             * and a wheel is the one thing a keystroke script cannot send. */
            case 'z':
            {
                float z = (float)atof(&argv[i][2]);

                if (z >= SDL3_ZOOM_MIN && z <= SDL3_ZOOM_MAX) sdl3_zoom = z;
                break;
            }

            /* Play every sound in turn and stop. The one thing about this
             * fork that cannot be checked by looking at the screen. */
            case 'S':
                ui_sound_options(TRUE, argv[i][2] != '0', 70);
                ui_sound_audition();
                return 0;

            /* Rebuild the portrait plates from the paintings. Run by hand,
             * rarely; see ui_art.h. Needs the game's directories, which
             * init_stuff() has not set up this early, so it is done here. */
            case 'A':
                sdl3_generate_plates();
                return 0;

            case 'h':
                sdl3_usage(argv[0]);
                return 0;

            default:
                printf("Unknown option '%s'\n", argv[i]);
                sdl3_usage(argv[0]);
                return 1;
        }
    }

    sdl3_init_paths();

    process_player_name(TRUE);
    create_needed_dirs();

    quit_aux = sdl3_quit_hook;

    if (init_sdl3()) quit("Unable to prepare the SDL3 display module!");

    /* Claim the SDL 1.2 port's identity so lib/pref/pref-sdl.prf, which holds
     * the key bindings, is the one the game loads. See the note on
     * sdl3_special_key_name(). */
    SCOTTISH_SYS = "sdl";

    signals_init();

    init_ormeq();

    /*
     * Ask what the player wants, unless -n already said.
     *
     * The start screen clears the savefile name itself when a new character is
     * chosen, which matters for more than tidiness: load_player() has an early
     * out for a savefile that is not there, but it sits inside
     * "#if !defined(WINDOWS)", so on Windows a missing file falls through to
     * quit("broken savefile"). An empty name returns early and is safe.
     */
    /*
     * Play, then come back here.
     *
     * A character being put away is not a reason to close the window. You
     * save, you are on the front screen, and the character you just saved is
     * sitting under Continue -- which is what anybody expects of a program
     * with a menu at the front of it.
     *
     * The loop is the whole of it: start_screen() only returns when somebody
     * has chosen to play, and leaves through quit() when they choose not to.
     */
    for (;;)
    {
        if (!new_game)
        {
            ui_music_options(display_music_get(), display_music_volume_get());
            ui_music_title(TRUE);
            new_game = start_screen();
            ui_music_title(FALSE);
        }
        else
            savefile[0] = '\0';

        play_game(new_game);

        /* What that character asked to be drawn and we had no tile for.
         * Said here rather than at shutdown: this is the moment a session
         * of play ends, and it is the list of what to draw next. */
        if (sdl3_verbose) ui_tiles_report_missing();

        /* Whatever happened -- saved, or dead and buried -- the next time
         * round is a decision, so the front screen has to ask again. */
        new_game = FALSE;
    }

    quit(NULL);

    return 0;
}
