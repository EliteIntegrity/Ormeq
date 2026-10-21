/*
 * File: ui_terrain.h
 *
 * Deterministic, presentation-only terrain variation.
 *
 * A dungeon drawn in ASCII is a field of identical characters. Every floor is
 * the same full stop, every wall the same hash, and a large room reads as a
 * photocopy rather than a place. The eye has nothing to hold on to.
 *
 * The fix is not to change the characters. In this game every punctuation mark
 * already means something -- a comma is food on the floor, a semicolon is a
 * glyph of warding -- so swapping one in for decoration would be inventing
 * false readings. Instead the character stays exactly as the game chose it,
 * and the *cell around it* varies: a breath of the material's own colour in
 * the ground, a small mark of grit or grain, and a slight lift or drop in the
 * glyph's tone.
 *
 * Two rules keep it honest.
 *
 * **It must never carry meaning.** Only materials that are already visually
 * uniform are touched -- floor, plain rock, permanent wall, water, lava, snow.
 * Anything a player reads for information keeps its exact stock appearance:
 * doors, stairs, traps, glyphs, shop entrances, and any vein that might hold
 * treasure. The test is by feature *flag*, not by a list of features, so a
 * feature this code has never heard of is simply left alone.
 *
 * **It must be stable.** The variation is a hash of the cave coordinate, not
 * of the screen cell, so it belongs to the ground and not to the window onto
 * it. Walk across a room and the pattern stays where it was. It is also
 * clustered: cells are hashed in coarse blocks, so what emerges reads as worn
 * patches and bands of strata rather than television static. A second, finer
 * hash breaks up the block edges so the clusters do not show as rectangles.
 *
 * The game decides *what* varies; a frontend decides how to draw it.
 *
 * There is deliberately no background wash any more. Laying a little of each
 * material's colour into the ground behind it seemed the obvious way to make a
 * surface read as a surface, and it was wrong: a wash varies over an area
 * rather than within a character, so it turns the map into patches of slightly
 * different darkness, which is haze and not texture. What is left is per-grid
 * and crisp -- a shade of tone on the character itself, and a mark of a pixel
 * or two -- which is what wear actually looks like at this size.
 *
 * Copyright (c) 2026 John Horton.
 *
 * Added since the fork and offered on the same terms as the rest of the
 * tree -- the Angband licence in LICENSE.
 */

#ifndef INCLUDED_UI_TERRAIN_H
#define INCLUDED_UI_TERRAIN_H

typedef enum
{
    UI_TERRAIN_NONE = 0,   /* leave alone: this grid is telling you something */
    UI_TERRAIN_FLOOR,      /* dungeon floor                                   */
    UI_TERRAIN_GROUND,     /* grass and dirt: the surface                     */
    UI_TERRAIN_GRANITE,    /* plain rock wall                                 */
    UI_TERRAIN_PERMANENT,  /* the walls that never yield                      */
    UI_TERRAIN_WATER,
    UI_TERRAIN_LAVA,
    UI_TERRAIN_SNOW,

    UI_TERRAIN_MATERIALS
} ui_terrain_material_t;

/* Styles, in the order the setting cycles through them. */
#define UI_TERRAIN_PLAIN   0   /* stock: nothing at all      */
#define UI_TERRAIN_SUBTLE  1
#define UI_TERRAIN_BOLD    2
#define UI_TERRAIN_STYLES  3

typedef struct
{
    byte material;   /* ui_terrain_material_t                              */
    bool lit;        /* the player can see it now, rather than remember it */
    bool torch;      /* lit by their own light rather than by the room     */
    bool detail;     /* this grid was picked out for a mark of its own     */
    u16b seed;       /* stable randomness, for placing that mark           */
    int  tone;       /* percent to lighten (+) or darken (-) the glyph     */
} ui_terrain_t;

/*
 * What the terrain at a cave grid looks like, for decoration purposes.
 *
 * Returns FALSE -- and fills nothing in -- when the grid must be left exactly
 * as the game drew it. That covers an unclassified feature, a grid the player
 * cannot currently see, a style of PLAIN, and any point before a level exists.
 */
extern bool ui_terrain_at(int cave_x, int cave_y, int style, ui_terrain_t *out);

extern cptr ui_terrain_style_name(int style);


/*
 * The colour of the daylight, above ground.
 *
 * Brightness is deliberately not part of this. The game already darkens the
 * surface at night -- town grids lose their glow, and the map is drawn in the
 * duller remembered colours -- so dimming it again here would double up and
 * make a legible screen illegible. What the game does not model is the
 * *colour* of the light: gold along the horizon at either end of the day, blue
 * once the sun is down. That is all this adds.
 *
 * Returns FALSE underground, where there is no sky, and when the setting is
 * off.
 */
typedef struct
{
    byte r, g, b;    /* the colour the light is                            */
    int  strength;   /* percent of it to mix in; zero means leave it alone */
    cptr name;       /* "dawn", "noon" ... for the settings line           */
} ui_daylight_t;

extern bool ui_daylight(ui_daylight_t *out);

/*
 * What the sky here would be doing, if the player has asked to see it.
 *
 * NONE underground, where there is no sky. Above ground it is snow where the
 * land around the player is under snow and rain everywhere else -- which is
 * a guess, but a guess made from something the world actually says rather
 * than from a coin. Walk out of the snow and it turns to rain.
 */
#define UI_WEATHER_NONE  0
#define UI_WEATHER_RAIN  1
#define UI_WEATHER_SNOW  2

extern int ui_weather_kind(void);

#endif
