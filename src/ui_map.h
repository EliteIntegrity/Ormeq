/*
 * File: ui_map.h
 *
 * The map layer.
 *
 * Term carries the 80x27 *window* onto the level and nothing else. That is
 * enough to draw the game as a terminal always has, but it is not enough to
 * zoom out: to show the whole dungeon a frontend needs the whole dungeon, and
 * Term simply does not transmit it.
 *
 * So this is the one place where the traffic runs the other way. The semantic
 * channel (ui_event.h) is the game telling the frontend what happened; this is
 * the frontend asking the game what the level looks like. A pull rather than a
 * push, because the frontend only wants it when it is actually drawing zoomed
 * out, and streaming a whole level on every change to answer a question nobody
 * asked would be silly.
 *
 * The same data is what tiles and lighting will need, when we get to them.
 *
 * Call these while the game is between operations -- from a frontend's redraw,
 * not from the middle of an animation.
 *
 * Copyright (c) 2026 John Horton.
 *
 * Added since the fork and offered on the same terms as the rest of the
 * tree -- the Angband licence in LICENSE.
 */

#ifndef INCLUDED_UI_MAP_H
#define INCLUDED_UI_MAP_H

/* Shift a camera origin just enough to keep a focus and its neighbours clear.
 * Pure presentation geometry, also exercised by the headless fixture. */
extern float ui_map_safe_axis(float origin, int focus, float scale, float low, float high);

/* Size of the current level, in cave grids. */
extern void ui_map_size(int *width, int *height);
extern bool ui_map_is_world(void);

/* Where the player is, in cave grids. */
extern void ui_map_player(int *x, int *y);

/* Health colours preserve the normal glyph colour above half health. */
extern byte ui_player_health_attr(byte normal);

typedef enum {
    UI_WARNING_NONE,
    UI_WARNING_HEALTH,
    UI_WARNING_POISON,
    UI_WARNING_BLEEDING,
    UI_WARNING_IMPAIRED,
    UI_WARNING_HUNGER
} ui_player_warning_t;

/* One warning at a time, with low health taking priority over statuses. */
extern ui_player_warning_t ui_player_warning(void);

/* What is at a grid, as the player currently knows it.
 *
 * Returns FALSE for anything not yet discovered, which is what makes a
 * zoomed-out view show explored ground rather than handing over the level. */
extern bool ui_map_cell(int x, int y, byte *attr, char *ch);

/*
 * Whether the player can see this grid **right now**, as opposed to merely
 * remembering it.
 *
 * Ask the game; do not work it out from the colours. The tile renderer used
 * to derive its dimming from the ratio between the colour the game chose for
 * a grid and the colour it would choose lit -- which sounds equivalent and is
 * not. `lighting_colours` in cave.c maps a colour to its darkened form, and
 * for red, green and blue **the darkened form is the same colour**. Those
 * grids came out at a ratio of exactly one and were drawn at full brightness
 * whether you could see them or not, so a level you had walked once stayed
 * lit for the rest of the game.
 *
 * This is player_can_see_bold(), which is the game's own answer and already
 * knows about blindness, torch light, line of sight, noctovision, perma-lit
 * rooms and the glowing-wall case that stops you seeing into a room from the
 * corridor outside it.
 */
extern bool ui_map_seen(int x, int y);

/*
 * The name of the creature standing on this grid, or NULL.
 *
 * For a frontend that can draw a portrait: zoomed far enough in there is room
 * on a single grid for a picture of the thing standing on it, and a picture
 * needs a name where a letter did not.
 *
 * It is the *apparent* race, so a mimic pretending to be a chest stays a
 * chest. Nothing is returned while hallucinating: the game is already lying
 * about what is there, and answering honestly would give the trick away.
 */
extern cptr ui_map_monster(int x, int y);

/*
 * Everything a frontend needs to answer "what is that" about a creature.
 *
 * The look command's answer, in one call: the name, how hurt it is, the
 * letter and colour it draws as, and its level.
 *
 * Every field is something the player can already reach without this. The
 * health wording is mon_health_desc()'s, the same one the monster list uses,
 * and everything from `level` down is the recall page's, gated by the recall
 * page's own predicates -- exported from mon_info.c rather than copied here,
 * so the two cannot come to different views about what has been earned.
 *
 * **A card is allowed to save keystrokes. It is not allowed to hand over
 * anything the player has not earned.**
 *
 * FALSE, and every field cleared, for an empty grid, a creature the player
 * cannot see, and while hallucinating.
 */
typedef struct
{
    cptr name;      /* the apparent race, so a mimic keeps its disguise */
    cptr health;    /* "unhurt", "badly wounded", ... */
    byte attr;      /* the colour it draws in */
    char ch;        /* and the letter, for a frontend with no picture of it */
    int  kills;     /* how many they have killed, this life */

    /*
     * The recall page's figures, already formatted, and already carrying the
     * "?" where the player has not earned the answer yet. Strings rather than
     * numbers precisely so that the unknown case has somewhere to live: a
     * level of -1 is a number the caller has to remember to translate, and
     * one day will not.
     *
     * Much of this reads "?" or "0" until the player has killed one, and that
     * is the point -- a row of question marks is itself information, and it
     * is what the recall page shows.
     */
    char level[16];     /* "?", "Town", "7", "7 to 12" */
    char speed[16];     /* always known: "+0", "1.0x" */
    char ac[16];        /* "?" until the kills add up */
    char hp[16];        /* "?", "42", "8d6" */
    char attacks[16];   /* "?", "None", "3" */
    char exp[16];       /* "?", or the experience at your level */
} ui_look_t;

extern bool ui_map_look(int x, int y, ui_look_t *out);

/*
 * The name of the ground at a grid, and how that ground draws on its own.
 *
 * A frontend with tiles needs two things a terminal never did: the floor
 * under whatever is standing on it, and a name to look the floor up by.
 * ui_map_cell() gives only the topmost thing -- the monster's letter when a
 * monster is there -- so a tiled floor drawn from it would vanish the moment
 * anything walked onto it.
 *
 * The name is the readable one from f_info ("granite wall", "open floor"),
 * and it is the *mimicked* feature, so a secret door still says wall.
 *
 * attr and ch, either of which may be NULL, come back as the terrain's own
 * appearance for the current lighting -- what the grid would look like with
 * nothing on it. `lit` comes back as the attr the same terrain would use if
 * the grid were fully lit, so a caller drawing something other than a letter
 * can still tell how dark this square is: compare the two.
 *
 * NULL return for ground the player does not know about.
 */
extern cptr ui_map_terrain(int x, int y, byte *attr, char *ch, byte *lit);

/* Optional scenery key. Pass only the knowledge-gated name returned above.
 * Village finishes use static plans and stable wilderness coordinates. */
extern cptr ui_map_terrain_tile(int x, int y, cptr known_name);

/* Only apparent terrain names belong here. Shop emblems reuse object art;
 * marker labels retain the existing glyph/colour and never inspect subtypes. */
extern cptr ui_map_terrain_emblem(cptr known_name);
extern bool ui_map_terrain_label(cptr known_name);

/* Cosmetic surface edges in N,E,S,W order, using only apparent, known
 * terrain. NULL edges mean no blend. TRUE permits subtle texture variation.
 * The seed remains stable when the wilderness viewport scrolls. */
extern bool ui_map_terrain_edges(int x, int y, cptr known_name, cptr edges[4]);
extern u32b ui_map_terrain_seed(int x, int y);

/* N,E,S,W bits for discovered matching curtains/chasm squares. Unknown
 * neighbours never connect; regional world-map cells remain independent. */
extern unsigned ui_map_terrain_links(int x, int y, cptr known_name);

/* Cosmetic ground beneath a known transparent prop or flower glyph. */
extern cptr ui_map_terrain_ground(cptr known_name);
extern bool ui_map_plain_floor(cptr known_name);

/*
 * What kind of thing is lying on this grid, or NULL.
 *
 * The *kind*, not the item: "Potion", "Sword", "Scroll". That is deliberate
 * and it is not laziness. A red potion and a blue potion are the same glyph
 * in the same place in the same list until one of them is drunk, and a tile
 * that named them apart would be telling the player something the game has
 * not told them yet. The category is exactly the information a letter
 * carries, which is the right amount.
 *
 * Nothing while hallucinating, for the same reason as ui_map_monster().
 */
extern cptr ui_map_object(int x, int y);

/*
 * The art keys for whatever is lying on (x, y), most specific first.
 *
 * `specific` is the object's own base name and is left empty when the player
 * is not yet aware of the kind -- an unknown potion must not be drawn as the
 * potion it will turn out to be. `generic` is what the player can see: the
 * kind, prefixed "unidentified " while it is unknown, so the two states are
 * two pictures rather than one picture and a filter.
 *
 * `attr` and `ch` come back as the object's own colour and letter -- the
 * flavour's while the kind is unknown -- so a caller with no picture for
 * either key has something to draw that is not a blank square.
 *
 * FALSE when there is nothing there, or nothing the player has found.
 */
extern bool ui_map_object_art(int x, int y, char *specific, char *generic,
                              int len, byte *attr, char *ch);

extern bool ui_map_monster_present(int x, int y);
extern bool ui_map_occupied(int x, int y);
extern bool ui_map_wall(int x, int y);

#endif
