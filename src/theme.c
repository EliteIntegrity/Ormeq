/*
 * File: theme.c
 *
 * Colour themes. See theme.h for the two-palette idea this rests on.
 *
 * Copyright (c) 2026 John Horton.
 *
 * Added since the fork and offered on the same terms as the rest of the
 * tree -- the Angband licence in LICENSE.
 */

#include "scottish.h"

#include <ctype.h>

static theme_t _themes[THEME_MAX];
static int     _theme_ct;
static int     _theme_cur;

/* The game's palette exactly as it shipped, kept so a theme with no map
 * colours of its own (Classic) can put it back. */
static byte _stock_map[THEME_MAP_COLORS][3];
static bool _stock_saved;

/* Whether the theme in use has been recoloured since it was read. */
static bool _edited;

/*
 * The thirty-two colours, in the order the game numbers them. Themes name
 * their colours rather than counting them, so a file reads as a palette and a
 * missing line is obvious rather than silently shifting everything after it.
 */
static cptr _color_names[THEME_MAP_COLORS] =
{
    "DARK",      "WHITE",     "SLATE",     "ORANGE",
    "RED",       "GREEN",     "BLUE",      "UMBER",
    "L_DARK",    "L_WHITE",   "VIOLET",    "YELLOW",
    "L_RED",     "L_GREEN",   "L_BLUE",    "L_UMBER",
    "I_GREEN",   "PINK",      "I_BLUE",    "PURPLE",
    "TEAL",      "SKY_BLUE",  "MUD",       "D_YELLOW",
    "TURQUOISE", "L_ORANGE",  "LILAC",     "D_PURPLE",
    "SKY_DARK",  "PALE_BLUE", "D_PINK",    "CHESTNUT"
};

static cptr _role_names[THEME_UI_MAX] =
{
    "canvas", "panel", "border", "title", "text", "muted", "accent", "selection"
};

/* Used when a theme leaves a role out, so a half-written file still runs. */
static theme_color_t _role_fallback[THEME_UI_MAX] =
{
    { 0x0E, 0x11, 0x16, 0xFF },   /* canvas    */
    { 0x16, 0x1B, 0x22, 0xFF },   /* panel     */
    { 0x2D, 0x37, 0x42, 0xFF },   /* border    */
    { 0xE3, 0xB3, 0x41, 0xFF },   /* title     */
    { 0xDD, 0xE3, 0xEA, 0xFF },   /* text      */
    { 0x8B, 0x98, 0xA6, 0xFF },   /* muted     */
    { 0x58, 0xA6, 0xFF, 0xFF },   /* accent    */
    { 0x38, 0x52, 0x6E, 0xD8 }    /* selection */
};


static int  _density = DENSITY_DEFAULT;
static bool _fullscreen = TRUE;
static int  _terrain = UI_TERRAIN_SUBTLE;
static int  _font = 0;
static bool _sky_colour = TRUE;
static bool _sound = TRUE;
static bool _sound_pack = TRUE;
static int _sound_volume = 70;
static bool _music = TRUE;
static int _music_volume = 30;
static int _ambient_volume = 25;
static bool _ambient = TRUE;
static bool _menu_sounds = TRUE;
static bool _footsteps = TRUE;
static bool _square = TRUE;
static int _map_mode = DISPLAY_MAP_ASCII;
static int _tile_detail = 64;
static bool _enhanced_square = FALSE;
static bool _ascii_floors = FALSE;
static bool _edge_warnings = TRUE;
static int _calculations = DISPLAY_CALC_OFF;
static cptr _calculation_names[] = {"off", "static", "animated"};
int display_calculations_get(void) { return _calculations; }
cptr display_calculations_name(int mode)
{ return _calculation_names[MAX(0, MIN(2, mode))]; }
static int  _weather = DISPLAY_WEATHER_AUTO;

static const struct { cptr id; cptr name; } _weather_table[DISPLAY_WEATHERS] =
{
    { "off",  "Off"  },
    { "auto", "Auto" },
    { "rain", "Rain" },
    { "snow", "Snow" },
};

int display_weather_get(void)
{
    return _weather;
}

cptr display_weather_name(int mode)
{
    if (mode < 0 || mode >= DISPLAY_WEATHERS) return "Off";
    return _weather_table[mode].name;
}


bool display_daylight_get(void)
{
    return _sky_colour;
}

bool display_sound_get(void)
{
    return _sound;
}

bool display_sound_pack_get(void) { return _sound_pack; }
int display_sound_volume_get(void) { return _sound_volume; }
bool display_music_get(void) { return _music; }
int display_music_volume_get(void) { return _music_volume; }
int display_ambient_volume_get(void) { return _ambient_volume; }
bool display_ambient_get(void) { return _ambient; }
bool display_menu_sounds_get(void) { return _menu_sounds; }
bool display_footsteps_get(void) { return _footsteps; }

bool display_square_get(void)
{
    return _square || _map_mode != DISPLAY_MAP_ASCII;
}

bool display_edge_warnings_get(void)
{
    return _edge_warnings;
}

int display_map_mode_get(void) { return _map_mode; }
int display_tile_detail_get(void) { return _tile_detail; }
bool display_enhanced_square_get(void) { return _enhanced_square; }
bool display_ascii_floors_get(void) { return _ascii_floors; }
cptr display_map_mode_name(int mode)
{
    static cptr names[] = { "ASCII", "Hybrid", "Full Tile" };
    return names[mode >= 0 && mode < 3 ? mode : 0];
}

bool display_tiles_get(void)
{
    return _map_mode != DISPLAY_MAP_ASCII;
}




/*
 * The typefaces we ship with, in the order the setting cycles through them.
 *
 * A fixed list rather than whatever is in the folder, for the same reason the
 * themes are a fixed list: this is a short sequence a player steps through, so
 * it should read as a considered order and each entry should have a name
 * chosen by a person rather than a filename. A missing file falls back to the
 * built-in default rather than to a blank screen.
 */
static const struct { cptr id; cptr name; cptr file; } _font_table[DISPLAY_FONTS] =
{
    { "default",   "Default",    NULL                        },
    { "cozette",   "Cozette",    "CozetteVector.ttf"         },
    { "plex",      "IBM Plex",   "IBMPlexMono-Regular.ttf"   },
    { "roboto",    "Roboto",     "RobotoMono-SemiBold.ttf"   },
    { "sharetech", "Share Tech", "ShareTechMono-Regular.ttf" },
    { "vt323",     "VT323",      "VT323-Regular.ttf"         },
};


int display_font_get(void)
{
    return _font;
}

cptr display_font_name(int font)
{
    if (font < 0 || font >= DISPLAY_FONTS) return "Default";
    return _font_table[font].name;
}

cptr display_font_file(int font)
{
    if (font < 0 || font >= DISPLAY_FONTS) return NULL;
    return _font_table[font].file;
}


int display_terrain_get(void)
{
    return _terrain;
}


bool display_fullscreen_get(void)
{
    return _fullscreen;
}

/*
 * How many cells each density asks for.
 *
 * These are grid *dimensions*, not pixel sizes: the frontend fits the font to
 * whatever window it has so that this many cells fill it. Choosing a point
 * size instead only ever scales one thing; choosing the grid scales all of it.
 *
 * Nothing may go below 80x27 -- the game's screen layout assumes a classic
 * terminal, and menus start losing rows below it.
 */
static const struct { cptr name; int cols, rows; } _density_table[DENSITY_MAX] =
{
    { "Large",        80,  27 },
    { "Comfortable",  92,  30 },
    { "Standard",    104,  34 },
    { "Compact",     124,  40 }
};


int display_density_get(void)
{
    return _density;
}

cptr display_density_name(int density)
{
    if (density < 0 || density >= DENSITY_MAX) density = DENSITY_DEFAULT;
    return _density_table[density].name;
}

void display_density_size(int density, int *cols, int *rows)
{
    if (density < 0 || density >= DENSITY_MAX) density = DENSITY_DEFAULT;
    if (cols) *cols = _density_table[density].cols;
    if (rows) *rows = _density_table[density].rows;
}


static int _hex_digit(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* "0A0F0C" -> 10, 15, 12. Returns FALSE on anything that is not six digits. */
static bool _parse_rgb(cptr text, byte *r, byte *g, byte *b)
{
    int v[6], i;

    if (!text) return FALSE;
    for (i = 0; i < 6; i++)
    {
        v[i] = _hex_digit(text[i]);
        if (v[i] < 0) return FALSE;
    }
    if (text[6] && !isspace((unsigned char)text[6])) return FALSE;

    *r = (byte)(v[0] * 16 + v[1]);
    *g = (byte)(v[2] * 16 + v[3]);
    *b = (byte)(v[4] * 16 + v[5]);
    return TRUE;
}

static bool _parse_byte(cptr text, byte *out)
{
    int hi, lo;

    if (!text) return FALSE;
    hi = _hex_digit(text[0]);
    lo = _hex_digit(text[1]);
    if (hi < 0 || lo < 0) return FALSE;

    *out = (byte)(hi * 16 + lo);
    return TRUE;
}

static int _lookup(cptr name, cptr *table, int count)
{
    int i;

    for (i = 0; i < count; i++)
    {
        if (streq(name, table[i])) return i;
    }
    return -1;
}

/*
 * Everything after the first word, trimmed, with any comment removed.
 * Used for name and blurb, which are prose and may contain spaces.
 */
static cptr _rest_of(cptr line)
{
    static char rest[256];
    cptr p = line;
    int i = 0, last = -1;

    while (*p && isspace((unsigned char)*p)) p++;
    while (*p && !isspace((unsigned char)*p)) p++;
    while (*p && isspace((unsigned char)*p)) p++;

    while (*p && *p != '#' && i < (int)sizeof(rest) - 1)
    {
        rest[i] = *p++;
        if (!isspace((unsigned char)rest[i])) last = i;
        i++;
    }

    rest[last + 1] = '\0';
    return rest;
}


/* Chop a line into whitespace-separated tokens, stopping at a comment. */
static int _tokenize(char *line, char **tok, int max)
{
    int n = 0;
    char *p = line;

    while (*p && n < max)
    {
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p || *p == '#') break;

        tok[n++] = p;
        while (*p && !isspace((unsigned char)*p)) p++;
        if (*p) *p++ = '\0';
    }
    return n;
}


static void _theme_defaults(theme_t *theme, cptr id)
{
    int i;

    WIPE(theme, theme_t);
    my_strcpy(theme->id, id, sizeof(theme->id));
    my_strcpy(theme->name, id, sizeof(theme->name));
    theme->blurb[0] = '\0';
    theme->has_map = FALSE;

    for (i = 0; i < THEME_UI_MAX; i++) theme->ui[i] = _role_fallback[i];
}


static bool _theme_read(theme_t *theme, cptr path, cptr id)
{
    FILE *fp;
    char  line[256];

    fp = my_fopen(path, "r");
    if (!fp) return FALSE;

    _theme_defaults(theme, id);

    while (my_fgets(fp, line, sizeof(line)) == 0)
    {
        char  raw[256];
        char *tok[8];
        int   n;

        my_strcpy(raw, line, sizeof(raw));
        n = _tokenize(line, tok, 8);

        if (n < 2) continue;

        if (streq(tok[0], "name"))
        {
            my_strcpy(theme->name, _rest_of(raw), sizeof(theme->name));
            continue;
        }
        if (streq(tok[0], "blurb"))
        {
            my_strcpy(theme->blurb, _rest_of(raw), sizeof(theme->blurb));
            continue;
        }
        if (streq(tok[0], "ui") && n >= 3)
        {
            int role = _lookup(tok[1], _role_names, THEME_UI_MAX);
            byte r, g, b;

            if (role < 0 || !_parse_rgb(tok[2], &r, &g, &b)) continue;

            theme->ui[role].r = r;
            theme->ui[role].g = g;
            theme->ui[role].b = b;
            theme->ui[role].a = 255;

            /* Optional trailing alpha, which only selection really wants. */
            if (n >= 4) _parse_byte(tok[3], &theme->ui[role].a);
            continue;
        }
        if (streq(tok[0], "map") && n >= 3)
        {
            int idx = _lookup(tok[1], _color_names, THEME_MAP_COLORS);
            byte r, g, b;

            if (idx < 0 || !_parse_rgb(tok[2], &r, &g, &b)) continue;

            theme->map[idx][0] = r;
            theme->map[idx][1] = g;
            theme->map[idx][2] = b;
            theme->has_map = TRUE;
            continue;
        }
    }

    my_fclose(fp);
    return TRUE;
}


/*
 * Display preferences: lib/user/display.txt.
 *
 * Not the savefile. Which colours you like and how large you want the text are
 * properties of your eyes and your monitor, not of a character -- rolling a new
 * Ranger should not mean setting them again. It also means a frontend can read
 * them before the game has loaded anything at all.
 */
static void _remember(void)
{
    char path[1024];
    FILE *fp;

    path_build(path, sizeof(path), SCOTTISH_DIR_USER, "display.txt");

    fp = my_fopen(path, "w");
    if (!fp) return;

    fprintf(fp, "# Display preferences. Written by the game; safe to edit by hand.\n");
    fprintf(fp, "theme       %s\n", _theme_ct ? _themes[_theme_cur].id : "bog");
    fprintf(fp, "density     %s\n", display_density_name(_density));
    fprintf(fp, "fullscreen  %s\n", _fullscreen ? "yes" : "no");
    fprintf(fp, "terrain     %s\n", ui_terrain_style_name(_terrain));
    /* The id, not the name: names have spaces in them and the reader
     * splits a line on whitespace. */
    fprintf(fp, "font        %s\n", _font_table[_font].id);
    fprintf(fp, "daylight    %s\n", _sky_colour ? "yes" : "no");
    fprintf(fp, "weather     %s\n", _weather_table[_weather].id);
    fprintf(fp, "sound       %s\n", _sound ? "yes" : "no");
    fprintf(fp, "sound_pack %s\n", _sound_pack ? "yes" : "no");
    fprintf(fp, "sound_volume %d\n", _sound_volume);
    fprintf(fp, "music       %s\n", _music ? "yes" : "no");
    fprintf(fp, "music_volume %d\n", _music_volume);
    fprintf(fp, "ambient_volume %d\n", _ambient_volume);
    fprintf(fp, "ambient %s\n", _ambient ? "yes" : "no");
    fprintf(fp, "menu_sounds %s\n", _menu_sounds ? "yes" : "no");
    fprintf(fp, "footsteps %s\n", _footsteps ? "yes" : "no");
    /* The reader has understood "square" all along and nothing ever wrote it,
     * so turning it off lasted until the next restart and no longer. */
    fprintf(fp, "square      %s\n", _square ? "yes" : "no");
    fprintf(fp, "map_mode    %d\n", _map_mode);
    fprintf(fp, "tile_detail %d\n", _tile_detail);
    fprintf(fp, "enhanced_square %s\n", _enhanced_square ? "yes" : "no");
    fprintf(fp, "ascii_floors %s\n", _ascii_floors ? "yes" : "no");
    fprintf(fp, "edge_warnings %s\n", _edge_warnings ? "yes" : "no");
    fprintf(fp, "calculations %s\n", display_calculations_name(_calculations));
    my_fclose(fp);
}

static void _recall(char *theme_id, int len)
{
    char path[1024];
    char line[256];
    FILE *fp;

    theme_id[0] = '\0';

    path_build(path, sizeof(path), SCOTTISH_DIR_USER, "display.txt");

    fp = my_fopen(path, "r");
    if (!fp) return;

    while (my_fgets(fp, line, sizeof(line)) == 0)
    {
        char *tok[4];
        int   n = _tokenize(line, tok, 4);

        if (n < 2) continue;

        if (streq(tok[0], "theme"))
        {
            my_strcpy(theme_id, tok[1], len);
        }
        else if (streq(tok[0], "density"))
        {
            int i;
            for (i = 0; i < DENSITY_MAX; i++)
            {
                if (streq(tok[1], _density_table[i].name)) { _density = i; break; }
            }
        }
        else if (streq(tok[0], "fullscreen"))
        {
            _fullscreen = BOOL(streq(tok[1], "yes") || streq(tok[1], "1"));
        }
        else if (streq(tok[0], "terrain"))
        {
            int i;
            for (i = 0; i < UI_TERRAIN_STYLES; i++)
            {
                if (streq(tok[1], ui_terrain_style_name(i))) { _terrain = i; break; }
            }
        }
        else if (streq(tok[0], "daylight"))
        {
            _sky_colour = BOOL(streq(tok[1], "yes") || streq(tok[1], "1"));
        }
        else if (streq(tok[0], "sound"))
        {
            _sound = BOOL(streq(tok[1], "yes") || streq(tok[1], "1"));
        }
        else if (streq(tok[0], "sound_pack"))
            _sound_pack = BOOL(streq(tok[1], "yes") || streq(tok[1], "1"));
        else if (streq(tok[0], "sound_volume"))
            _sound_volume = MAX(0, MIN(100, atoi(tok[1])));
        else if (streq(tok[0], "music"))
            _music = BOOL(streq(tok[1], "yes") || streq(tok[1], "1"));
        else if (streq(tok[0], "footsteps"))
            _footsteps = BOOL(streq(tok[1], "yes") || streq(tok[1], "1"));
        else if (streq(tok[0], "menu_sounds"))
            _menu_sounds = BOOL(streq(tok[1], "yes") || streq(tok[1], "1"));
        else if (streq(tok[0], "ambient"))
            _ambient = BOOL(streq(tok[1], "yes") || streq(tok[1], "1"));
        else if (streq(tok[0], "ambient_volume"))
            _ambient_volume = MAX(0, MIN(100, atoi(tok[1])));
        else if (streq(tok[0], "music_volume"))
            _music_volume = MAX(0, MIN(100, atoi(tok[1])));
        else if (streq(tok[0], "square"))
        {
            _square = BOOL(streq(tok[1], "yes") || streq(tok[1], "1"));
        }
        else if (streq(tok[0], "tiles"))
        {
            /* Old tiles included scenery, so retain that choice as Full Tile. */
            _map_mode = (streq(tok[1], "yes") || streq(tok[1], "1")) ? DISPLAY_MAP_FULL : DISPLAY_MAP_ASCII;
        }
        else if (streq(tok[0], "map_mode"))
            _map_mode = MAX(0, MIN(2, atoi(tok[1])));
        else if (streq(tok[0], "tile_detail"))
            _tile_detail = atoi(tok[1]) == 32 ? 32 : 64;
        else if (streq(tok[0], "enhanced_square"))
            _enhanced_square = BOOL(streq(tok[1], "yes") || streq(tok[1], "1"));
        else if (streq(tok[0], "ascii_floors"))
            _ascii_floors = BOOL(streq(tok[1], "yes") || streq(tok[1], "1"));
        else if (streq(tok[0], "calculations"))
        {
            int i;
            for (i=0; i<3; ++i)
                if (streq(tok[1], _calculation_names[i])) _calculations = i;
        }
        else if (streq(tok[0], "edge_warnings"))
        {
            if (streq(tok[1], "yes") || streq(tok[1], "1")) _edge_warnings = TRUE;
            else if (streq(tok[1], "no") || streq(tok[1], "0")) _edge_warnings = FALSE;
        }
        else if (streq(tok[0], "weather"))
        {
            int i;
            for (i = 0; i < DISPLAY_WEATHERS; i++)
            {
                if (streq(tok[1], _weather_table[i].id)) { _weather = i; break; }
            }
        }
        else if (streq(tok[0], "font"))
        {
            int i;
            for (i = 0; i < DISPLAY_FONTS; i++)
            {
                if (streq(tok[1], _font_table[i].id)) { _font = i; break; }
            }
        }
    }
    my_fclose(fp);
}


void display_density_set(int density)
{
    /* Wrap, do not clamp. Clamping meant that once you reached the end of
     * the list the key did nothing at all, for ever, which reads exactly
     * like a setting that does not work. Every other cycling setting here
     * wraps; this one was the odd one out. */
    if (density < 0) density = DENSITY_MAX - 1;
    if (density >= DENSITY_MAX) density = 0;

    _density = density;
    _remember();
    ui_event_display_changed();
}

void display_density_cycle(int delta)
{
    display_density_set(_density + delta);
}

void display_fullscreen_set(bool on)
{
    _fullscreen = on;
    _remember();
    ui_event_display_changed();
}


void display_terrain_set(int style)
{
    if (style < 0) style = UI_TERRAIN_STYLES - 1;
    if (style >= UI_TERRAIN_STYLES) style = 0;

    _terrain = style;
    _remember();
    ui_event_display_changed();
}

void display_terrain_cycle(int delta)
{
    display_terrain_set(_terrain + (delta < 0 ? -1 : 1));
}


void display_font_set(int font)
{
    if (font < 0) font = DISPLAY_FONTS - 1;
    if (font >= DISPLAY_FONTS) font = 0;

    _font = font;
    _remember();
    ui_event_display_changed();
}

void display_font_cycle(int delta)
{
    display_font_set(_font + (delta < 0 ? -1 : 1));
}


void display_daylight_set(bool on)
{
    _sky_colour = on;
    _remember();
    ui_event_display_changed();
}

/*
 * Sound on or off.
 *
 * Keep the game's event gate in sync and notify the frontend so muting also
 * stops effects that are already playing.
 */
void display_sound_set(bool on)
{
    _sound = on;
    use_sound = on;
    _remember();
    ui_event_display_changed();
}

void display_sound_pack_set(bool on)
{
    _sound_pack = on;
    _remember();
    ui_event_display_changed();
}
void display_sound_volume_set(int volume)
{
    _sound_volume = MAX(0, MIN(100, volume));
    _remember();
    ui_event_display_changed();
}

void display_footsteps_set(bool value)
{
    _footsteps = value;
    _remember();
    ui_event_display_changed();
}

void display_menu_sounds_set(bool value)
{
    _menu_sounds = value;
    _remember();
    ui_event_display_changed();
}

void display_ambient_set(bool value)
{
    _ambient = value;
    _remember();
    ui_event_display_changed();
}

void display_ambient_volume_set(int value)
{
    _ambient_volume = MAX(0, MIN(100, value));
    _remember();
    ui_event_display_changed();
}

void display_music_set(bool on)
{
    _music = on;
    _remember();
    ui_event_display_changed();
}
void display_music_volume_set(int volume)
{
    _music_volume = MAX(0, MIN(100, volume));
    _remember();
    ui_event_display_changed();
}

/*
 * Square map tiles, or the font's own cell.
 *
 * Announced like the other display changes, because turning it off hands the
 * map back to Term and the whole region has to be repainted by somebody.
 */
void display_square_set(bool on)
{
    _square = on;
    _remember();
    ui_event_display_changed();
}

void display_map_mode_set(int mode)
{
    _map_mode = mode >= 0 && mode < 3 ? mode : DISPLAY_MAP_ASCII;
    _remember();
    ui_event_display_changed();
}
void display_tile_detail_set(int detail)
{
    _tile_detail = detail == 32 ? 32 : 64;
    _remember();
    ui_event_display_changed();
}
void display_enhanced_square_set(bool on)
{
    _enhanced_square = on;
    _remember();
    ui_event_display_changed();
}
void display_tiles_set(bool on)
{
    display_map_mode_set(on ? DISPLAY_MAP_FULL : DISPLAY_MAP_ASCII);
}

void display_ascii_floors_set(bool on)
{
    _ascii_floors = on;
    _remember();
    ui_event_display_changed();
}

void display_calculations_set(int mode)
{
    _calculations = (mode < 0 || mode > 2) ? DISPLAY_CALC_OFF : mode;
    _remember();
    ui_event_display_changed();
}

void display_edge_warnings_set(bool on)
{
    _edge_warnings = on;
    _remember();
    ui_event_display_changed();
}

void display_weather_set(int mode)
{
    if (mode < 0) mode = DISPLAY_WEATHERS - 1;
    if (mode >= DISPLAY_WEATHERS) mode = 0;

    _weather = mode;
    _remember();
    ui_event_display_changed();
}

void display_weather_cycle(int delta)
{
    display_weather_set(_weather + (delta < 0 ? -1 : 1));
}


/*
 * Push a theme's colours into the game.
 *
 * ormeq_color_table is what every frontend already reads, so this is the
 * entire mechanism -- nothing has to be told a theme changed, it just draws
 * the next frame in different colours.
 */
static void _apply(int index)
{
    const theme_t *theme;
    int i;

    if (index < 0 || index >= _theme_ct) return;
    theme = &_themes[index];

    for (i = 0; i < THEME_MAP_COLORS; i++)
    {
        const byte *rgb = theme->has_map ? theme->map[i] : _stock_map[i];

        ormeq_color_table[i][1] = rgb[0];
        ormeq_color_table[i][2] = rgb[1];
        ormeq_color_table[i][3] = rgb[2];
    }

    _theme_cur = index;
    _edited    = FALSE;
}


void themes_init(void)
{
    char pattern[1024];
    int  i;
    /* A considered sequence, not however the filesystem felt like enumerating:
     * ours first, then the two conservative ones, then the loud half. */
    static cptr known[] = {
        "bog", "slate", "classic", "high-contrast",
        "ember", "frost", "blueprint", "neon", "parchment", "phosphor",
        NULL
    };

    if (!_stock_saved)
    {
        for (i = 0; i < THEME_MAP_COLORS; i++)
        {
            _stock_map[i][0] = ormeq_color_table[i][1];
            _stock_map[i][1] = ormeq_color_table[i][2];
            _stock_map[i][2] = ormeq_color_table[i][3];
        }
        _stock_saved = TRUE;
    }

    _theme_ct = 0;

    /* Load the ones we ship, in a deliberate order -- this is a short list a
     * player cycles through, so it should read as a considered sequence
     * rather than however the filesystem felt like enumerating. */
    for (i = 0; known[i] && _theme_ct < THEME_MAX; i++)
    {
        char file[64];

        strnfmt(file, sizeof(file), "%s.thm", known[i]);

        /* A copy under lib/user wins: that is where a theme the player has
         * recoloured is kept, and it should survive us shipping a new one. */
        path_build(pattern, sizeof(pattern), SCOTTISH_DIR_USER, file);
        if (_theme_read(&_themes[_theme_ct], pattern, known[i]))
        {
            _theme_ct++;
            continue;
        }

        path_build(pattern, sizeof(pattern), SCOTTISH_DIR_THEME, file);
        if (_theme_read(&_themes[_theme_ct], pattern, known[i])) _theme_ct++;
    }

    /* Nothing on disk: keep going with the stock palette rather than dying
     * for want of a colour scheme. */
    if (!_theme_ct)
    {
        _theme_defaults(&_themes[0], "classic");
        my_strcpy(_themes[0].name, "Classic", sizeof(_themes[0].name));
        _theme_ct = 1;
    }

    {
        char wanted[THEME_ID_LEN];
        int  choice = 0;

        _recall(wanted, sizeof(wanted));

        for (i = 0; i < _theme_ct; i++)
        {
            if (streq(_themes[i].id, wanted)) { choice = i; break; }
        }
        _apply(choice);
    }

    /*
     * Say so, now that the preferences are actually loaded.
     *
     * A frontend builds its window, its font and its grid before the game
     * starts reading files, so everything it did was done against the
     * defaults. Without this the interface size, typeface and fullscreen
     * saved last time were read, shown in the menu, and never applied --
     * so the menu said Compact while the screen was Standard, and the
     * setting looked broken because it was already claiming to be what you
     * wanted.
     */
    ui_event_display_changed();

    /* Sound needs no announcement, only the game's own flag set to what was
     * read. Every sound() call checks it. */
    use_sound = _sound;
}


int theme_count(void)
{
    return _theme_ct;
}

cptr theme_name(int index)
{
    if (index < 0 || index >= _theme_ct) return "";
    return _themes[index].name;
}

cptr theme_blurb(int index)
{
    if (index < 0 || index >= _theme_ct) return "";
    return _themes[index].blurb;
}

int theme_current(void)
{
    return _theme_cur;
}

void theme_select(int index)
{
    if (!_theme_ct) return;

    while (index < 0) index += _theme_ct;
    index %= _theme_ct;

    _apply(index);
    _remember();
    ui_event_display_changed();
}

void theme_cycle(int delta)
{
    theme_select(_theme_cur + delta);
}

theme_color_t theme_ui(int role)
{
    if (role < 0 || role >= THEME_UI_MAX) role = THEME_UI_TEXT;
    if (!_theme_ct) return _role_fallback[role];

    return _themes[_theme_cur].ui[role];
}


/*
 * How many colours it is honest to show.
 *
 * A theme file carries thirty-two, because that is what the palette has room
 * for, but this build compiles with sixteen -- every constant above fifteen
 * aliases down onto one of the first sixteen. Offering the other half would
 * be offering a control that does nothing.
 */
int theme_map_editable(void)
{
    return MIN(MAX_COLOR, THEME_MAP_COLORS);
}

cptr theme_map_name(int index)
{
    if (index < 0 || index >= THEME_MAP_COLORS) return "";
    return _color_names[index];
}

void theme_map_get(int index, byte *r, byte *g, byte *b)
{
    if (index < 0 || index >= THEME_MAP_COLORS) return;

    /* From the palette in use rather than from the theme, because those are
     * the same thing and the palette is what the player can actually see. */
    if (r) *r = ormeq_color_table[index][1];
    if (g) *g = ormeq_color_table[index][2];
    if (b) *b = ormeq_color_table[index][3];
}

void theme_map_set(int index, byte r, byte g, byte b)
{
    theme_t *theme;

    if (index < 0 || index >= THEME_MAP_COLORS) return;
    if (!_theme_ct) return;

    theme = &_themes[_theme_cur];

    /*
     * Classic carries no palette of its own -- it means "leave the game's own
     * colours alone". Recolouring it has to give it one, or the edit would be
     * thrown away the next time the theme was applied.
     */
    if (!theme->has_map)
    {
        int i;

        for (i = 0; i < THEME_MAP_COLORS; i++)
        {
            theme->map[i][0] = _stock_map[i][0];
            theme->map[i][1] = _stock_map[i][1];
            theme->map[i][2] = _stock_map[i][2];
        }
        theme->has_map = TRUE;
    }

    theme->map[index][0] = r;
    theme->map[index][1] = g;
    theme->map[index][2] = b;

    ormeq_color_table[index][1] = r;
    ormeq_color_table[index][2] = g;
    ormeq_color_table[index][3] = b;

    _edited = TRUE;
}

bool theme_is_edited(void)
{
    return _edited;
}

static void _user_path(char *buf, int cap)
{
    char file[64];

    strnfmt(file, sizeof(file), "%s.thm",
            _theme_ct ? _themes[_theme_cur].id : "bog");
    path_build(buf, cap, SCOTTISH_DIR_USER, file);
}

bool theme_has_own_copy(void)
{
    char path[1024];
    FILE *fp;

    _user_path(path, sizeof(path));
    fp = my_fopen(path, "r");
    if (!fp) return FALSE;

    my_fclose(fp);
    return TRUE;
}

/*
 * Write the theme out under lib/user, in the same format we read.
 *
 * The whole theme, not only the colours that were touched: a theme file is
 * meant to be readable and editable by hand, and half a palette with the
 * rest implied would not be.
 */
bool theme_save(void)
{
    char     path[1024];
    FILE    *fp;
    theme_t *theme;
    int      i;

    if (!_theme_ct) return FALSE;
    theme = &_themes[_theme_cur];

    _user_path(path, sizeof(path));
    FILE_TYPE(FILE_TYPE_TEXT);
    fp = my_fopen(path, "w");
    if (!fp) return FALSE;

    fprintf(fp, "# %s, as you have recoloured it.\n", theme->name);
    fprintf(fp, "#\n");
    fprintf(fp, "# Written by the game and read in preference to the one in\n");
    fprintf(fp, "# lib/theme. Delete this file to have the original back.\n\n");

    fprintf(fp, "name    %s\n", theme->name);
    fprintf(fp, "blurb   %s\n\n", theme->blurb);

    for (i = 0; i < THEME_UI_MAX; i++)
    {
        fprintf(fp, "ui      %-11s %02X%02X%02X",
                _role_names[i], theme->ui[i].r, theme->ui[i].g,
                theme->ui[i].b);
        if (theme->ui[i].a != 255) fprintf(fp, "  %02X", theme->ui[i].a);
        fprintf(fp, "\n");
    }
    fprintf(fp, "\n");

    if (theme->has_map)
    {
        for (i = 0; i < THEME_MAP_COLORS; i++)
        {
            fprintf(fp, "map     %-11s %02X%02X%02X\n", _color_names[i],
                    theme->map[i][0], theme->map[i][1], theme->map[i][2]);
        }
    }

    my_fclose(fp);
    _edited = FALSE;
    return TRUE;
}

/*
 * Throw the edits away and read the shipped file again.
 */
bool theme_revert(void)
{
    char path[1024];
    char file[64];
    int  cur = _theme_cur;

    if (!_theme_ct) return FALSE;

    _user_path(path, sizeof(path));
    fd_kill(path);

    strnfmt(file, sizeof(file), "%s.thm", _themes[cur].id);
    path_build(path, sizeof(path), SCOTTISH_DIR_THEME, file);

    if (!_theme_read(&_themes[cur], path, _themes[cur].id)) return FALSE;

    _apply(cur);
    return TRUE;
}
