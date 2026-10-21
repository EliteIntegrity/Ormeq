/*
 * File: ui_event.h
 *
 * The semantic channel.
 *
 * Term tells a frontend *which characters to paint*: "put a red asterisk at
 * cell (12,7)". That is all it has ever been able to say, which is why every
 * *band frontend is a terminal emulator wearing a costume -- it cannot animate
 * a fireball, because as far as it knows nothing happened except that some
 * cells changed colour for a moment.
 *
 * This is the other channel. Here the game says *what happened*: a bolt
 * travelled from here to there, something took forty-seven damage, the player
 * was knocked back. A frontend that understands those can draw them properly;
 * one that doesn't is unaffected, because Term is still the authority on what
 * the screen contains.
 *
 * The contract has two halves:
 *
 *   - Emitting is nearly free when nobody is listening. ui_event_*() returns
 *     FALSE immediately if there is no handler.
 *   - A handler that returns TRUE has *claimed* the effect: the game skips its
 *     own rendering of it. A handler that returns FALSE, or no handler at all,
 *     leaves the classic character-cell version to run untouched.
 *
 * That second half is what makes this safe to add incrementally. Every call
 * site keeps its original code as the fallback path, so the curses and Win32
 * builds behave exactly as they did before, and any event we have not taught a
 * frontend about simply falls through.
 *
 * Positions are UI points -- terminal cells -- because that is what a frontend
 * can turn into pixels without knowing anything about the dungeon.
 *
 * Copyright (c) 2026 John Horton.
 *
 * Added since the fork and offered on the same terms as the rest of the
 * tree -- the Angband licence in LICENSE.
 */

#ifndef INCLUDED_UI_EVENT_H
#define INCLUDED_UI_EVENT_H

typedef enum
{
    UI_EVENT_NONE = 0,

    /* A projectile moved one step along its path. The game emits one of these
     * per grid, so a handler animating the step has `msec` to play with. */
    UI_EVENT_BOLT,

    /* Something took damage. `to` is where, `amount` is how much.
     *
     * The game has no visual for this at all -- it only ever mentioned damage
     * in the message line -- so there is nothing to suppress and the return
     * value is ignored. Purely additive. */
    UI_EVENT_DAMAGE,

    /* A melee attack: `from` swung at `to`. Also purely additive; the game
     * resolves melee without drawing anything. */
    UI_EVENT_BUMP,

    /* One cell of an explosion or breath, as the blast expands ring by ring.
     * The game walks the affected grids itself, so these arrive already
     * clipped to walls and line of sight -- a frontend must not try to guess
     * the shape, or fireballs will leak through walls.
     *
     * A handler that claims these owns the whole blast until UI_EVENT_BLAST_END,
     * and the game skips its usual draw-then-erase pass. */
    UI_EVENT_BLAST_CELL,

    /* The blast is finished; stop showing it. */
    UI_EVENT_BLAST_END,

    /* Something was shoved from `from` to `to`. Unlike a bolt this is a real
     * move -- the game relocates the monster either way -- so a handler that
     * claims it is claiming the *pause*, not the drawing. */
    UI_EVENT_KNOCKBACK,

    /* A monster died at `to`, wearing `attr`/`ch`. Purely additive. */
    UI_EVENT_DEATH,

    /* Menu structure. A menu that declares itself this way becomes clickable
     * without any change to how it draws or reads keys.
     *
     * MENU_CLEAR is sent whenever a document is painted to the screen, which
     * is the point at which any previously declared rows stop being true.
     * MENU_ITEM then declares one row: `to` is where it starts, `amount` is
     * how wide it is, and `ch` is the key that row stands for.
     *
     * Unlike every other event here these carry **ui** coordinates already --
     * they are screen furniture, not things in the dungeon -- so nothing is
     * converted on the way through. */
    UI_EVENT_MENU_CLEAR,
    UI_EVENT_MENU_ITEM,

    /* Which row is the menu's heading, and which row the keyboard is on.
     *
     * Both are chrome rather than content: a frontend with a theme draws the
     * heading in the theme's title colour and lays a bar of its selection
     * colour under the selected row, neither of which can be said in the
     * thirty-two colours a document has to choose from. A frontend that
     * ignores them gets a menu that still works, in the colours the document
     * asked for. */
    UI_EVENT_MENU_TITLE,
    UI_EVENT_MENU_SELECT,

    /* A quantity worth seeing as well as reading.
     *
     * Hit points are the thing you check most often and the thing you have
     * least time to read, and "12/37" is two numbers and a division. This
     * says where the row is and how full it is; a frontend that can fill a
     * rectangle lays a bar under the text, so the row itself empties as you
     * take damage.
     *
     * Deliberately not a glyph. A Term cell holds eight bits, so a block
     * character could not travel through one anyway -- and a drawn bar is
     * smoother than eight steps, needs nothing of the typeface, and cannot
     * be mistaken for a character.
     *
     * The colour is the one the game already chose for the number, so a bar
     * that means trouble turns red at the same moment the number does. */
    UI_EVENT_GAUGE,

    /* Whether the map region currently shows the map, or something has taken
     * the screen over. A frontend drawing the map its own way -- zoomed, say --
     * has to stop while a menu is up, or it paints straight over it.
     *
     * Hidden when the screen is saved (which is how every full-screen takeover
     * begins) and when a document paints; shown again when the screen is
     * restored. Those pair up correctly even though save and restore are not
     * balanced: a menu that redraws restores first and repaints after. */
    UI_EVENT_MAP_SHOWN,
    UI_EVENT_MAP_HIDDEN,

    /* A panel is about to be painted here: `from` is its top-left cell,
     * `amount` its width and `msec` its height, all in terminal cells.
     *
     * Emitted after the area is cleared and before any text goes into it, so a
     * frontend can lay chrome underneath -- a background, a border -- in the
     * theme's interface colours rather than in one of the game's thirty-two.
     * A frontend that ignores it gets exactly what it got before. */
    UI_EVENT_PANEL,

    /* A rule belongs on this row: `from` is where it starts and `amount` how
     * wide it runs, in cells.
     *
     * A rule under a heading is typography rather than decoration -- it says
     * the heading has ended -- which is why this is here when box-drawn frames
     * are not. The document marks the row (doc_add_rule) and leaves it blank;
     * a frontend draws the line, and one that cannot leaves the blank row,
     * which costs the page nothing.
     *
     * Deliberately not a row of dashes in the document itself: a rule made of
     * '-' is a row of hyphens with gaps at every cell boundary, and it is one
     * of the things that makes a terminal look like a terminal. */
    UI_EVENT_RULE,

    /* There is more of this page above and/or below what is on screen.
     * `to.x` is "more above", `to.y` is "more below".
     *
     * The game is the only thing that knows, and a page that scrolls without
     * saying so is a page most players will never scroll. */
    UI_EVENT_SCROLL,

    /* A portrait belongs in this box: `from` is its top-left cell, `amount`
     * its width and `msec` its height, exactly like UI_EVENT_PANEL. `text`
     * names the subject and `to.x` says which collection to look in.
     *
     * The game reserves the box and says who is in it; it does not know or
     * care whether art exists. A frontend that has a portrait converts and
     * draws it, one that has none draws the classic glyph big, and one that
     * ignores the event leaves an empty box that the game has already filled
     * with something sensible. That is why the fallback glyph travels with
     * the event: the decision is the frontend's, but the answer has to look
     * deliberate either way.
     *
     * Like the menu and panel events these are screen furniture and carry ui
     * coordinates already, so nothing is converted on the way through. */
    UI_EVENT_PORTRAIT,

    /* The shape of the screen changed: a phase or a layer came or went.
     *
     * A frontend that has drawn anything Term does not know about -- a panel's
     * ground, say -- must take it down here and have the region repainted, or
     * it survives with nothing left to remove it. */
    UI_EVENT_SCREEN_CHANGED,

    /* A display preference changed: typeface, density, theme, fullscreen.
     *
     * Sent the moment the setting is set rather than left for a frontend to
     * notice on its next poll, because *when* it is applied is visible. Polling
     * meant the settings menu drew itself at the old size, the change landed
     * while waiting for the next key, and what you were left looking at was the
     * dungeon rather than the menu you were still in. */
    UI_EVENT_DISPLAY_CHANGED,

    /* A knowledge-gated document over the live map; NULL dismisses it. */
    UI_EVENT_LOOK_CARD,
    UI_EVENT_MAP_FOCUS, /* cave coordinates; negative x restores player focus */
    UI_EVENT_CALCULATION, /* a completed, recorded melee action */
    UI_EVENT_CALCULATION_CARD

    /* Still to come: knockback, deaths, level feelings, menu structure. */

} ui_event_kind;

/* The caller owns card and doc until it dismisses them. The frontend reports the
 * visible row count and clamps top, so scrolling also works after a resize. */
typedef struct {
    doc_ptr doc;
    point_t target; /* cave coordinates */
    int top;
    int rows;
    bool fullscreen;
    bool more; /* expanded optional actions; Tab toggles without leaving Look */
    cptr commands; /* available legacy actions, supplied by the game */
    cptr status;
} ui_look_card_t;

typedef struct
{
    ui_event_kind kind;
    const struct melee_calc_blow_s *calculation; /* borrowed until cleared */
    ui_look_card_t *look_card; /* only for UI_EVENT_LOOK_CARD */

    point_t from;   /* ui cell the effect starts at   */
    point_t to;     /* ui cell the effect ends at     */

    byte    attr;   /* colour the game would have used */
    char    ch;     /* glyph the game would have used  */

    int     amount; /* damage, for UI_EVENT_DAMAGE */
    int     msec;   /* how long the game intended to spend on this */

    cptr    text;   /* who or what this is about, where a name is needed.
                     * Borrowed for the length of the call and no longer. */
    cptr    text2;  /* a second, less specific name to try when the first
                     * finds nothing: the shop when the shopkeeper has no
                     * portrait of their own. Usually NULL. */
} ui_event_t;

/* Which set of portraits to look in. */
#define UI_ART_MONSTER      0
#define UI_ART_SHOPKEEPER   1
#define UI_ART_OBJECT       2

/* Returns TRUE if it drew the effect itself, FALSE to let the game draw it. */
typedef bool (*ui_event_handler_f)(const ui_event_t *event);

extern void ui_event_subscribe(ui_event_handler_f handler);

/* Emitters. Each takes *cave* coordinates and converts, so callers can pass
 * the same coordinates they already have. Each returns TRUE if a handler
 * claimed the effect, in which case the caller must not draw it itself. */
extern bool ui_event_bolt(int from_x, int from_y, int to_x, int to_y,
                          byte attr, char ch, int msec);
extern bool ui_event_damage(int x, int y, int amount, byte attr);
extern bool ui_event_bump(int from_x, int from_y, int to_x, int to_y,
                          byte attr, char ch);
extern bool ui_event_blast_cell(int x, int y, byte attr, char ch);
extern bool ui_event_blast_end(void);
extern bool ui_event_knockback(int from_x, int from_y, int to_x, int to_y,
                               byte attr, char ch, int msec);
extern bool ui_event_death(int x, int y, byte attr, char ch);
extern bool ui_event_menu_clear(void);
extern bool ui_event_menu_item(int x, int y, int width, char key);
extern bool ui_event_menu_title(int x, int y, int width);
extern bool ui_event_menu_select(int x, int y, int width);
extern bool ui_event_gauge(int x, int y, int width,
                           int cur, int max, byte attr);
extern bool ui_event_map_shown(void);
extern bool ui_event_map_hidden(void);
extern bool ui_event_panel(int x, int y, int width, int height);
/*
 * A panel over the whole screen, for a page that takes the screen outright:
 * help, the option browsers, knowledge, and character creation. Says "this is
 * a page, not an overlay", and gives the text a ground instead of bare black.
 */
extern bool ui_event_panel_screen(void);
extern bool ui_event_panel_none(void);
/*
 * `fallback` is tried when `name` has no picture, and may be NULL. It exists
 * for the shops: the key is the owner, and most owners are not painted, so
 * the shop itself is the answer for all but twenty-one of them.
 */
extern bool ui_event_portrait(int x, int y, int width, int height,
                              int collection, cptr name, cptr fallback,
                              byte attr, char ch);
/* There is no portrait any more. Sent when the screen is restored, which is
 * how every full-screen page in this game ends -- including the ones drawn
 * with prt() rather than as documents, which announce nothing at all and so
 * could not otherwise take their own chrome down. */
extern bool ui_event_portrait_none(void);
extern bool ui_event_rule(int x, int y, int width);
extern bool ui_event_scroll(bool above, bool below);
extern bool ui_event_screen_changed(void);
extern bool ui_event_display_changed(void);
extern bool ui_event_map_focus(int x, int y);
extern bool ui_event_look_card(ui_look_card_t *card);
extern bool ui_event_calculation(void);
extern bool ui_event_calculation_card(const struct melee_calc_blow_s *blow, rect_t area, bool replay);

#endif
