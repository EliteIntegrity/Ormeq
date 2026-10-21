/*
 * File: sdl3_local.h
 *
 * The shared innards of the SDL3 frontend. Not a public header: nothing
 * outside the port includes it, and nothing in it belongs to the game.
 *
 * main-sdl3.c was one translation unit of 6,452 lines, which is why every
 * function in it was static -- there was nowhere else for one to be seen
 * from. This header is that somewhere else. It carries three things:
 *
 *   - the frontend's own state, which is one struct and a few settings;
 *   - the small drawing helpers the whole port shares;
 *   - the entry points of each layer that has been split out.
 *
 * And it refuses a fourth. Anything a layer only calls from inside itself
 * stays static in that layer's file, because the point of a split is a
 * smaller surface, not the same surface spread thinner. If this header grows
 * to the point where it is a copy of the old file's contents, the split has
 * failed and is worth undoing.
 *
 * SDL's headers must be parsed before the game's. angband.h (via h-type.h)
 * does `#define bool bool_hack` to dodge a C++ problem from the 1990s, and
 * SDL3 uses bool throughout its API, so any SDL header read afterwards is
 * silently rewritten. This header does that ordering itself, so an includer
 * cannot get it wrong.
 *
 * Copyright (c) 2026 John Horton.
 *
 * Added since the fork and offered on the same terms as the rest of the
 * tree -- the Angband licence in LICENSE.
 */

#ifndef INCLUDED_SDL3_LOCAL_H
#define INCLUDED_SDL3_LOCAL_H

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include "scottish.h"
#include "ui_art.h"
#include "ui_tiles.h"


/*
 * Which baking of a glyph to draw. See sdl3_render_atlas().
 *
 * TEXT keeps the font's baseline, so prose sits on one line. MAP centres
 * each glyph's ink in its cell, because a square grid has no baseline -- it
 * is a lattice of symbols, not a line of text.
 */
#define ATLAS_TEXT       0
#define ATLAS_MAP        1


/*
 * Zoom, as a multiple of the normal cell size.
 *
 * Exactly 1.0 means "off": the map region is whatever Term is carrying, drawn
 * the way it always was. Anything else and the frontend draws the map itself
 * from the map layer, at that scale, centred on the player. Below about a
 * third of a cell a glyph is unreadable, so it switches to coloured blocks.
 */
#define SDL3_ZOOM_MIN   0.06f
#define SDL3_ZOOM_MAX   6.00f
#define SDL3_ZOOM_STEP  1.12f

/*
 * How bright a grid you remember but cannot currently see is drawn, as a
 * percentage of its lit self.
 *
 * Named rather than buried, because it is a presentation number and the only
 * way to choose one is to render several and look at them. See the note in
 * sdl3_draw_grid_tiles() for why a derived ratio could not do this job.
 */
#define SDL3_DIM_REMEMBERED_PCT  25

/*
 * The smallest a grid may be drawn, in pixels, before the wheel stops going
 * out. This is the whole of the zoom-out limit now: the map used to stop as
 * soon as the level fitted the window, which is a decision about how somebody
 * ought to want to look at their own dungeon. Two pixels is where it genuinely
 * stops carrying information.
 */
#define SDL3_MIN_GRID_PX  2
#define SDL3_ZOOM_GLYPH 0.34f   /* below this, blocks rather than letters */

/*
 * Above this, a monster's own grid is drawn as a portrait rather than as its
 * letter.
 *
 * Four times is where a grid becomes big enough to hold a picture of anything
 * -- at three it is a smear, and the letter says more. It is also far enough
 * in that the view holds only a handful of grids, which is what keeps the cost
 * of drawing a few hundred extra characters per monster irrelevant.
 */
#define SDL3_ZOOM_PORTRAIT 4.00f


/*
 * Everything the frontend owns. One instance, called sdl3.
 */
typedef struct
{
    term          t;

    SDL_Window   *window;
    SDL_Renderer *renderer;
    SDL_Texture  *grid;        /* render target: the character grid       */
    SDL_Texture  *atlas;       /* white glyphs in one row                 */
    TTF_Font     *font;

    int  cell_w, cell_h;
    int  cols, rows;
    int  grid_w, grid_h;       /* cols * cell_w, rows * cell_h            */

    int  cursor_x, cursor_y;
    bool cursor_visible;

    /* The projectile currently in flight, if any. Drawn over the grid rather
     * than into it, so Term never sees it. */
    bool  bolt_live;
    float bolt_x, bolt_y;
    byte  bolt_attr;
    char  bolt_ch;

    /* Numpad keys raise both a key event (which we turn into a macro
     * trigger) and a text event (the digit). Swallow the second one. */
    bool swallow_text;

    bool initialised;
} sdl3_state;

extern sdl3_state sdl3;


typedef struct
{
    float        ptsize;    /* 0 = empty slot        */
    SDL_Texture *tex;
    int          cell_w, cell_h;
    SDL_Rect     map_ink[95];
    int          ink_w, ink_h;
    Uint64       used;      /* for evicting the least recently wanted */
} sdl3_atlas_entry;

typedef struct
{
    float  origin_x, origin_y;
    float  scale_x, scale_y;
    rect_t map;
} sdl3_map_view;

/* The glyph atlas, and where a given codepoint sits in it. */
/*
 * sdl3_atlas.c -- the typeface, and the texture baked from it.
 *
 * The face is chosen and opened there, the atlas is baked there, and a short
 * ladder of sizes is kept there so a zoomed map is drawn from a texture near
 * its own size rather than from one stretched to it.
 */
/*
  * The atlas covers printable 7-bit ASCII, plus the handful of decorative
  * code points sdl3_atlas.c appends after it. Anything outside draws blank:
  * widening it means deciding what a non-ASCII cell even means, which is a
  * bigger question than this port wants to answer.
  */
#define GLYPH_FIRST       32
#define GLYPH_LAST        126
#define GLYPH_COUNT       (GLYPH_LAST - GLYPH_FIRST + 1)

#define SDL3_FONT_SIZE    16.0f  /* points, before the display scale */
#define SDL3_ATLAS_MIN_PT 5.0f   /* the ladder's ends */
#define SDL3_ATLAS_MAX_PT 160.0f

extern SDL_Rect sdl3_base_map_ink[95];
extern void sdl3_draw_map_glyph(const sdl3_atlas_entry *atlas, SDL_FRect cell, char ch, bool wall);
extern sdl3_atlas_entry *sdl3_atlas_for(float want_h);
extern SDL_Texture *sdl3_render_atlas(int *cell_w, int *cell_h);
extern errr  sdl3_build_atlas(void);
/* Drop the ladder so it can be rebuilt: the face or the density changed. */
extern void  sdl3_atlas_forget(void);
/* And let it all go, at shutdown. */
extern void  sdl3_atlas_free(void);
/* The base atlas has been rebuilt: what it measured is now the truth. */
extern void  sdl3_publish_ink(void);
extern errr  sdl3_open_font(void);
extern float sdl3_font_size;        /* points; 0 means "work it out" */
extern cptr  sdl3_font_override;    /* -F<path> */
extern bool  sdl3_font_got_choice;  /* is the open face the one asked for? */
extern int   sdl3_atlas_slot_of(u32b cp);

/* Drawing, at a size that is not the cell size. */
extern void  sdl3_draw_glyph_scaled(const sdl3_atlas_entry *atlas,
                                    float px, float py, float w, float h,
                                    char ch, int bank);
extern void  sdl3_draw_slot_scaled(const sdl3_atlas_entry *atlas, int slot,
                                   float px, float py, float w, float h,
                                   int bank);

/* Colour. */
extern void  sdl3_blend(const byte a[3], const byte b[3], int weight,
                        byte out[3]);
extern void  sdl3_tone(const byte in[3], int tone, byte out[3]);
extern int   sdl3_luma(byte attr);

/* Tiles: whether they are wanted and possible, and how big one is. */
extern bool  sdl3_tiles_on(void);
extern float sdl3_tile_px(void);

/* Settings and state the whole port reads. */
extern float sdl3_zoom;
extern int   sdl3_ink_w, sdl3_ink_h;
extern bool  sdl3_verbose;
extern bool  sdl3_doc_covered;


/*
 * sdl3_map.c -- the map layer.
 *
 * Everything about drawing the dungeon at a scale of the player's choosing,
 * and about agreeing where a pixel is. The two have to agree exactly, which
 * is why they live together: sdl3_map_view_get() is the single transform,
 * and both the drawing and the mouse go through it.
 */
extern bool  sdl3_map_showing(void);
extern bool  sdl3_map_is_ours(void);
extern bool  sdl3_map_overlay(void);
extern bool  sdl3_in_map(int x, int y);
extern float sdl3_zoom_floor(void);

extern SDL_FRect sdl3_cell_rect(int cx, int cy);
extern void      sdl3_cell_to_px(int cx, int cy, float *px, float *py);
extern SDL_FRect sdl3_map_quad(float px, float py, float tw, float th);

extern void sdl3_map_focus(int x, int y);
extern SDL_FRect sdl3_map_safe_rect(void);
extern bool sdl3_map_input_clear(float x, float y);
extern void  sdl3_map_view_get(sdl3_map_view *v);
extern void  sdl3_draw_zoomed_map(void);
extern void  sdl3_draw_edge_warning(void);
extern bool  sdl3_draw_grid_portrait(int cave_x, int cave_y,
                                     float px, float py, float w, float h);

/* The message panel: whether one is up, and where it starts. */
extern bool sdl3_panel_active;
extern int  sdl3_panel_x, sdl3_panel_y, sdl3_panel_w, sdl3_panel_h;

/* The colour a cell's ground is painted before anything is drawn on it. */
extern void sdl3_ground_colour(byte a);


/*
 * sdl3_anim.c -- the two things that move on their own clock.
 *
 * Both answer "how long may the frontend sleep before I want the screen
 * again", in milliseconds, or -1 for "not while things are as they are".
 */
extern bool sdl3_weather_advance(void);
extern int  sdl3_weather_due_ms(void);
extern void sdl3_draw_weather(void);

extern bool sdl3_home_active(void);
extern bool sdl3_home_advance(void);
extern int  sdl3_home_due_ms(void);
extern void sdl3_draw_home(void);

extern void sdl3_draw_art_box(const ui_art_t *art, float px, float py,
                              float w, float h, Uint8 alpha);
extern void sdl3_draw_art_rect(const ui_art_t *art, float px, float py,
                               float side, Uint8 alpha);

/* Where the character grid starts inside the window: it is centred, so raw
 * window pixels mean nothing until this has been taken off them. */
extern void sdl3_grid_origin(int *ox, int *oy);

/* The dark slab a card or panel is laid on, and one glyph at a pixel. */
extern void sdl3_chrome_slab(rect_t r, float pad_x, float pad_y);
extern void sdl3_draw_glyph_at(float px, float py, char ch);
extern void sdl3_draw_glyph_cp(float px, float py, u32b cp);
extern void sdl3_present(void);

/*
 * sdl3_marks.c -- what the game declares and we draw over the finished grid.
 *
 * The handlers are called from the one switch in main-sdl3.c, which stays
 * there as the table of what this port answers. The draw calls are made from
 * sdl3_present(), in the order they appear there, which is the order they end
 * up in on the screen.
 */
extern bool sdl3_marks_damage(const ui_event_t *e);
extern bool sdl3_marks_bump(const ui_event_t *e);
extern bool sdl3_marks_death(const ui_event_t *e);
extern bool sdl3_marks_blast_cell(const ui_event_t *e);
extern bool sdl3_marks_blast_end(void);
extern bool sdl3_marks_rule(const ui_event_t *e);
extern bool sdl3_marks_scroll(const ui_event_t *e);
extern bool sdl3_marks_portrait(const ui_event_t *e);
/* The portrait and the rule belong to whatever menu declared them. */
extern void sdl3_marks_forget(void);
/* And the scroll arrows too, when the frame comes down. */
extern void sdl3_marks_drop(void);

extern void sdl3_draw_rule(void);
extern void sdl3_draw_scroll(void);
extern void sdl3_draw_portrait(void);
extern void sdl3_draw_sprites(void);
extern void sdl3_draw_blast(void);
/* Drop the sprites whose time is up; how many went, so the frame loop knows
 * the screen changed. */
extern int  sdl3_sprites_reap(void);
extern void sdl3_sprite_add(cptr text, float x, float y,
                            float vx, float vy, byte attr, int life);

/* Set when something drawn over the grid has changed and the frontend needs
 * waking -- inkey() is blocked and will not notice on its own. */
extern bool sdl3_hover_dirty;


/*
 * sdl3_look.c -- the card that says what the pointer is over.
 */
extern void sdl3_look_at(float wx, float wy);
extern int  sdl3_look_due_ms(void);
extern void sdl3_draw_look_card(void);
extern bool sdl3_look_recall(ui_look_card_t *card);
extern bool sdl3_look_recall_active(void);
extern bool sdl3_look_recall_contains(float x, float y);
extern bool sdl3_look_recall_click(float x, float y, int button);
extern bool sdl3_look_recall_wheel(float amount);
extern float sdl3_look_reserved_width(void);

extern void sdl3_hud_surface(SDL_FRect r);
extern bool sdl3_calc_event(void);
extern bool sdl3_calc_card(const ui_event_t *e);
extern bool sdl3_calc_advance(void);
extern float sdl3_calc_unit(void);
extern SDL_FRect sdl3_calc_bounds(bool reserve);
extern bool sdl3_calc_contains(float x, float y);
extern bool sdl3_calc_click(float x, float y, int button);
extern void sdl3_draw_calculation(void);
extern void sdl3_calc_capture(SDL_Surface *shot);
extern void sdl3_calc_diagram(const melee_calc_blow_t *b, SDL_FRect r, bool animate);

#endif /* INCLUDED_SDL3_LOCAL_H */
