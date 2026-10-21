/*
 * File: theme.h
 *
 * Colour themes, loaded from lib/theme/*.thm.
 *
 * A theme carries two separate palettes, and keeping them separate is the
 * whole idea:
 *
 *   [1] The **map palette** -- the thirty-two colours the game itself uses.
 *       Colour carries meaning here: a red 'D' is a different dragon from a
 *       blue one. A theme *retints* these; it must never change what one
 *       means, so every theme lists them in the same order under the same
 *       names. Applying a theme simply overwrites ormeq_color_table, which
 *       is why every frontend gets themes without knowing they exist.
 *
 *   [2] The **interface palette** -- eight semantic roles: canvas, panel,
 *       border, title, text, muted, accent, selection. These are for chrome:
 *       the frame around a menu, the bar under the selected row, the colour
 *       of a heading. Chrome must not reach into the map palette, and the map
 *       must not borrow a role, or a retint in one place will quietly wreck
 *       the other.
 *
 * A theme that lists no map colours (Classic) leaves the game's own palette
 * alone; the original is kept at startup and restored.
 *
 * Copyright (c) 2026 John Horton.
 *
 * Added since the fork and offered on the same terms as the rest of the
 * tree -- the Angband licence in LICENSE.
 */

#ifndef INCLUDED_THEME_H
#define INCLUDED_THEME_H

#define THEME_MAP_COLORS 32
#define THEME_ID_LEN     32
#define THEME_NAME_LEN   32
#define THEME_BLURB_LEN  96
#define THEME_MAX        16

/* The interface roles. Order matters only for the option display. */
enum theme_ui_role
{
    THEME_UI_CANVAS = 0,   /* behind everything                        */
    THEME_UI_PANEL,        /* a menu or dialog's own background        */
    THEME_UI_BORDER,       /* the line around a panel                  */
    THEME_UI_TITLE,        /* headings                                 */
    THEME_UI_TEXT,         /* ordinary prose                           */
    THEME_UI_MUTED,        /* secondary prose: weights, hints, counts  */
    THEME_UI_ACCENT,       /* the thing you are meant to notice        */
    THEME_UI_SELECTION,    /* the bar under the current row (has alpha)*/

    THEME_UI_MAX
};

typedef struct theme_color_s theme_color_t;
struct theme_color_s
{
    byte r, g, b, a;
};

typedef struct theme_s theme_t;
struct theme_s
{
    char          id[THEME_ID_LEN];       /* the filename, minus .thm  */
    char          name[THEME_NAME_LEN];
    char          blurb[THEME_BLURB_LEN];
    bool          has_map;
    byte          map[THEME_MAP_COLORS][3];
    theme_color_t ui[THEME_UI_MAX];
};

/*
 * Interface density -- how big everything is.
 *
 * Not the same question as zoom, and worth keeping straight: density decides
 * how many cells the screen is divided into and therefore how large the text
 * is, while zoom decides how much of the dungeon the map region shows. Ask for
 * a bigger interface and every screen grows together, including the ones we
 * have never touched.
 */
enum display_density
{
    DENSITY_LARGE = 0,
    DENSITY_COMFORTABLE,
    DENSITY_STANDARD,
    DENSITY_COMPACT,

    DENSITY_MAX
};

#define DENSITY_DEFAULT DENSITY_STANDARD

/* Whether the window should open filling the screen. A display preference like
   the others, so a frontend can honour it before the game loads anything. */
extern bool display_fullscreen_get(void);
extern void display_fullscreen_set(bool on);

extern int  display_density_get(void);
extern void display_density_set(int density);
extern void display_density_cycle(int delta);
extern cptr display_density_name(int density);

/* Terrain variation. The values are UI_TERRAIN_* from ui_terrain.h; kept as a
 * plain int here so the display settings do not have to know about them. */
extern int  display_terrain_get(void);
extern void display_terrain_set(int style);
extern void display_terrain_cycle(int delta);

/*
 * Which typeface the game is drawn in.
 *
 * The choice lives here with the other display preferences; resolving it to a
 * file on disk is the frontend's business, since only a frontend that renders
 * its own glyphs can act on it at all.
 */
#define DISPLAY_FONTS 6

extern int  display_font_get(void);
extern void display_font_set(int font);
extern void display_font_cycle(int delta);
extern cptr display_font_name(int font);
extern cptr display_font_file(int font);   /* NULL for the built-in default */

/* Weather, above ground. Off / Auto / Rain / Snow -- Auto asks the world.
 * The values are indices into a table in theme.c; the frontend gets the name
 * for the settings line and the kind for the drawing. */
#define DISPLAY_WEATHER_OFF   0
#define DISPLAY_WEATHER_AUTO  1
#define DISPLAY_WEATHER_RAIN  2
#define DISPLAY_WEATHER_SNOW  3
#define DISPLAY_WEATHERS      4

extern int  display_weather_get(void);
extern void display_weather_set(int mode);
extern void display_weather_cycle(int delta);
extern cptr display_weather_name(int mode);

/* Whether the light outside changes colour with the hour. */
extern bool display_daylight_get(void);
extern void display_daylight_set(bool on);
/* Live calculation strip (SDL3). History works in every frontend. */
#define DISPLAY_CALC_OFF 0
#define DISPLAY_CALC_STATIC 1
#define DISPLAY_CALC_ANIMATED 2
extern int display_calculations_get(void);
extern void display_calculations_set(int mode);
extern cptr display_calculations_name(int mode);
extern bool display_edge_warnings_get(void);
extern void display_edge_warnings_set(bool on);
extern bool display_sound_get(void);
extern void display_sound_set(bool on);
extern bool display_sound_pack_get(void);
extern void display_sound_pack_set(bool on);
extern int display_sound_volume_get(void);
extern void display_sound_volume_set(int volume);
extern bool display_music_get(void);
extern void display_music_set(bool on);
extern int display_music_volume_get(void);
extern int display_ambient_volume_get(void);
extern void display_ambient_volume_set(int value);
extern bool display_ambient_get(void);
extern void display_ambient_set(bool value);
extern bool display_menu_sounds_get(void);
extern void display_menu_sounds_set(bool value);
extern bool display_footsteps_get(void);
extern void display_footsteps_set(bool value);
extern void display_music_volume_set(int volume);
extern bool display_square_get(void);
extern void display_square_set(bool on);

#define DISPLAY_MAP_ASCII 0
#define DISPLAY_MAP_HYBRID 1
#define DISPLAY_MAP_FULL 2
extern int display_map_mode_get(void);
extern void display_map_mode_set(int mode);
extern cptr display_map_mode_name(int mode);
extern int display_tile_detail_get(void);
extern void display_tile_detail_set(int detail);
extern bool display_enhanced_square_get(void);
extern void display_enhanced_square_set(bool on);
extern bool display_ascii_floors_get(void);
extern void display_ascii_floors_set(bool on);
extern bool display_tiles_get(void);
extern void display_tiles_set(bool on);
extern void display_density_size(int density, int *cols, int *rows);

/* Read lib/theme/*.thm and apply whatever lib/user/display.txt asked for.
   Safe to call before any drawing; falls back to a built-in default if the
   directory is missing entirely. */
extern void themes_init(void);

extern int      theme_count(void);
extern cptr     theme_name(int index);
extern cptr     theme_blurb(int index);
extern int      theme_current(void);

/* Select by index, wrapping. Writes the palette and remembers the choice. */
extern void     theme_select(int index);
extern void     theme_cycle(int delta);

/*
 * Editing the theme you are wearing.
 *
 * The old colour editor wrote straight into ormeq_color_table, which the
 * theme system overwrites the moment you change theme -- so a player's work
 * vanished with no warning. Colour editing belongs to the theme instead: you
 * are always editing the palette you are wearing, and it is kept as a copy of
 * that theme under lib/user, which the loader prefers over the one we ship.
 * Deleting that file, or asking here, puts the original back.
 *
 * Only the colours the game actually draws with are offered. The map palette
 * has thirty-two slots but this build defines sixteen (ALLOW_XTRA_COLOURS is
 * not set), and the rest alias down onto those -- so editing them would look
 * like it worked and do nothing.
 */
extern int  theme_map_editable(void);          /* how many are worth showing */
extern cptr theme_map_name(int index);
extern void theme_map_get(int index, byte *r, byte *g, byte *b);
extern void theme_map_set(int index, byte r, byte g, byte b);

extern bool theme_is_edited(void);   /* changed since it was loaded or saved */
extern bool theme_has_own_copy(void);           /* a copy exists under user/ */
extern bool theme_save(void);                    /* keep the edits, as a file */
extern bool theme_revert(void);       /* throw them away and reload the stock */

/* A chrome colour from the current theme. */
extern theme_color_t theme_ui(int role);

#endif
