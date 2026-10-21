/*
 * File: ui_event.c
 *
 * The semantic channel. See ui_event.h for what it is and why.
 *
 * Copyright (c) 2026 John Horton.
 *
 * Added since the fork and offered on the same terms as the rest of the
 * tree -- the Angband licence in LICENSE.
 */

#include "scottish.h"

static ui_event_handler_f _handler = NULL;

void ui_event_subscribe(ui_event_handler_f handler)
{
    _handler = handler;
}

/*
 * Hand an event to the frontend.
 *
 * Returns TRUE if the frontend claimed it. Everything routes through here so
 * there is exactly one place that knows what "claimed" means.
 */
static bool _emit(const ui_event_t *event)
{
    if (!_handler) return FALSE;
    return _handler(event);
}

bool ui_event_bolt(int from_x, int from_y, int to_x, int to_y,
                   byte attr, char ch, int msec)
{
    ui_event_t event = {0};

    if (!_handler) return FALSE;

    /* Both ends must be on screen. A handler cannot draw what is not
     * displayed, and the caller's fallback path checks the same thing. */
    if (!cave_xy_is_visible(from_x, from_y)) return FALSE;
    if (!cave_xy_is_visible(to_x, to_y)) return FALSE;

    event.kind = UI_EVENT_BOLT;
    event.from = cave_xy_to_ui_pt(from_x, from_y);
    event.to   = cave_xy_to_ui_pt(to_x, to_y);
    event.attr = attr;
    event.ch   = ch;
    event.msec = msec;

    return _emit(&event);
}


bool ui_event_damage(int x, int y, int amount, byte attr)
{
    ui_event_t event = {0};

    if (!_handler) return FALSE;
    if (amount <= 0) return FALSE;
    if (!cave_xy_is_visible(x, y)) return FALSE;

    event.kind   = UI_EVENT_DAMAGE;
    event.from   = cave_xy_to_ui_pt(x, y);
    event.to     = event.from;
    event.attr   = attr;
    event.ch     = 0;
    event.amount = amount;
    event.msec   = 0;

    return _emit(&event);
}

bool ui_event_bump(int from_x, int from_y, int to_x, int to_y,
                   byte attr, char ch)
{
    ui_event_t event = {0};

    if (!_handler) return FALSE;
    if (!cave_xy_is_visible(from_x, from_y)) return FALSE;
    if (!cave_xy_is_visible(to_x, to_y)) return FALSE;

    event.kind   = UI_EVENT_BUMP;
    event.from   = cave_xy_to_ui_pt(from_x, from_y);
    event.to     = cave_xy_to_ui_pt(to_x, to_y);
    event.attr   = attr;
    event.ch     = ch;
    event.amount = 0;
    event.msec   = 0;

    return _emit(&event);
}


bool ui_event_blast_cell(int x, int y, byte attr, char ch)
{
    ui_event_t event = {0};

    if (!_handler) return FALSE;
    if (!cave_xy_is_visible(x, y)) return FALSE;

    event.kind   = UI_EVENT_BLAST_CELL;
    event.from   = cave_xy_to_ui_pt(x, y);
    event.to     = event.from;
    event.attr   = attr;
    event.ch     = ch;
    event.amount = 0;
    event.msec   = 0;

    return _emit(&event);
}

bool ui_event_blast_end(void)
{
    ui_event_t event = {0};

    if (!_handler) return FALSE;

    event.kind   = UI_EVENT_BLAST_END;
    event.from   = point(0, 0);
    event.to     = event.from;
    event.attr   = 0;
    event.ch     = 0;
    event.amount = 0;
    event.msec   = 0;

    return _emit(&event);
}


bool ui_event_knockback(int from_x, int from_y, int to_x, int to_y,
                        byte attr, char ch, int msec)
{
    ui_event_t event = {0};

    if (!_handler) return FALSE;
    if (!cave_xy_is_visible(from_x, from_y)) return FALSE;
    if (!cave_xy_is_visible(to_x, to_y)) return FALSE;

    event.kind   = UI_EVENT_KNOCKBACK;
    event.from   = cave_xy_to_ui_pt(from_x, from_y);
    event.to     = cave_xy_to_ui_pt(to_x, to_y);
    event.attr   = attr;
    event.ch     = ch;
    event.amount = 0;
    event.msec   = msec;

    return _emit(&event);
}

bool ui_event_death(int x, int y, byte attr, char ch)
{
    ui_event_t event = {0};

    if (!_handler) return FALSE;
    if (!cave_xy_is_visible(x, y)) return FALSE;

    event.kind   = UI_EVENT_DEATH;
    event.from   = cave_xy_to_ui_pt(x, y);
    event.to     = event.from;
    event.attr   = attr;
    event.ch     = ch;
    event.amount = 0;
    event.msec   = 0;

    return _emit(&event);
}


bool ui_event_menu_clear(void)
{
    ui_event_t event = {0};

    if (!_handler) return FALSE;

    event.kind   = UI_EVENT_MENU_CLEAR;
    event.from   = point(0, 0);
    event.to     = event.from;
    event.attr   = 0;
    event.ch     = 0;
    event.amount = 0;
    event.msec   = 0;

    return _emit(&event);
}

static bool _menu_row(ui_event_kind kind, int x, int y, int width)
{
    ui_event_t event = {0};

    if (!_handler) return FALSE;

    /* Already a ui point -- do not convert. */
    event.kind   = kind;
    event.from   = point(x, y);
    event.to     = event.from;
    event.attr   = 0;
    event.ch     = 0;
    event.amount = width;
    event.msec   = 0;

    return _emit(&event);
}

bool ui_event_menu_title(int x, int y, int width)
{
    return _menu_row(UI_EVENT_MENU_TITLE, x, y, width);
}

bool ui_event_menu_select(int x, int y, int width)
{
    return _menu_row(UI_EVENT_MENU_SELECT, x, y, width);
}

bool ui_event_gauge(int x, int y, int width, int cur, int max, byte attr)
{
    ui_event_t event = {0};

    if (!_handler) return FALSE;
    if (max <= 0) return FALSE;

    if (cur < 0) cur = 0;
    if (cur > max) cur = max;

    /* Already a ui point -- do not convert. */
    event.kind   = UI_EVENT_GAUGE;
    event.from   = point(x, y);
    event.to     = point(cur, max);
    event.attr   = attr;
    event.ch     = 0;
    event.amount = width;
    event.msec   = 0;

    return _emit(&event);
}

bool ui_event_menu_item(int x, int y, int width, char key)
{
    ui_event_t event = {0};

    if (!_handler) return FALSE;

    /* Already a ui point -- do not convert. */
    event.kind   = UI_EVENT_MENU_ITEM;
    event.from   = point(x, y);
    event.to     = event.from;
    event.attr   = 0;
    event.ch     = key;
    event.amount = width;
    event.msec   = 0;

    return _emit(&event);
}


static bool _map_visibility(ui_event_kind kind)
{
    ui_event_t event = {0};

    if (!_handler) return FALSE;

    event.kind   = kind;
    event.from   = point(0, 0);
    event.to     = event.from;
    event.attr   = 0;
    event.ch     = 0;
    event.amount = 0;
    event.msec   = 0;

    return _emit(&event);
}

bool ui_event_map_shown(void)  { return _map_visibility(UI_EVENT_MAP_SHOWN); }
bool ui_event_map_hidden(void) { return _map_visibility(UI_EVENT_MAP_HIDDEN); }


bool ui_event_panel(int x, int y, int width, int height)
{
    ui_event_t event = {0};

    if (!_handler) return FALSE;
    if (width <= 0 || height <= 0) return FALSE;

    /* Already a ui rectangle -- do not convert. */
    event.kind   = UI_EVENT_PANEL;
    event.from   = point(x, y);
    event.to     = event.from;
    event.attr   = 0;
    event.ch     = 0;
    event.amount = width;
    event.msec   = height;

    return _emit(&event);
}

/*
 * A panel over the whole screen.
 *
 * For the pages that take the screen outright -- help, the option browsers,
 * the macro and knowledge screens, and every page of character creation.
 * Those all used to say ui_event_panel_none() and then draw, which put their
 * text on bare canvas: a black band above the first line, black behind the
 * header, and nothing tying the page together.
 *
 * They are not overlays and never were. A page that owns the screen should
 * say so and get the theme's ground under all of it.
 */
bool ui_event_panel_screen(void)
{
    rect_t r = ui_screen_rect();

    return ui_event_panel(r.x, r.y, r.cx, r.cy);
}


bool ui_event_panel_none(void)
{
    ui_event_t event = {0};

    if (!_handler) return FALSE;

    /* A frame of no size, said the same way a portrait of no size is said.
     * Something that takes the whole screen needs to be able to put down the
     * frame belonging to whatever was on it a moment ago -- otherwise the
     * frontend goes on painting a menu's border under a page of text. */
    event.kind   = UI_EVENT_PANEL;
    event.from   = point(0, 0);
    event.to     = event.from;
    event.amount = 0;
    event.msec   = 0;

    return _emit(&event);
}


bool ui_event_portrait(int x, int y, int width, int height,
                       int collection, cptr name, cptr fallback,
                       byte attr, char ch)
{
    ui_event_t event = {0};

    if (!_handler) return FALSE;
    if (width <= 0 || height <= 0) return FALSE;
    if (!name || !name[0]) return FALSE;

    /* Already a ui rectangle -- do not convert. */
    event.kind   = UI_EVENT_PORTRAIT;
    event.from   = point(x, y);
    event.to     = point(collection, 0);
    event.attr   = attr;
    event.ch     = ch;
    event.amount = width;
    event.msec   = height;
    event.text   = name;
    event.text2  = fallback;

    return _emit(&event);
}


bool ui_event_rule(int x, int y, int width)
{
    ui_event_t event = {0};

    if (!_handler) return FALSE;
    if (width <= 0) return FALSE;

    /* Already a ui row -- do not convert. */
    event.kind   = UI_EVENT_RULE;
    event.from   = point(x, y);
    event.to     = event.from;
    event.amount = width;

    return _emit(&event);
}


bool ui_event_scroll(bool above, bool below)
{
    ui_event_t event = {0};

    if (!_handler) return FALSE;

    event.kind = UI_EVENT_SCROLL;
    event.from = point(0, 0);
    event.to   = point(above ? 1 : 0, below ? 1 : 0);

    return _emit(&event);
}


bool ui_event_portrait_none(void)
{
    ui_event_t event = {0};

    if (!_handler) return FALSE;

    /* A box of no size is how "none" is said: there is nowhere for a picture
     * to be, so there is no picture. */
    event.kind   = UI_EVENT_PORTRAIT;
    event.from   = point(0, 0);
    event.to     = point(0, 0);
    event.amount = 0;
    event.msec   = 0;
    event.text   = NULL;

    return _emit(&event);
}


bool ui_event_display_changed(void)
{
    ui_event_t event = {0};

    if (!_handler) return FALSE;

    event.kind   = UI_EVENT_DISPLAY_CHANGED;
    event.from   = point(0, 0);
    event.to     = event.from;
    event.attr   = 0;
    event.ch     = 0;
    event.amount = 0;
    event.msec   = 0;

    return _emit(&event);
}

bool ui_event_screen_changed(void)
{
    ui_event_t event = {0};

    if (!_handler) return FALSE;

    event.kind   = UI_EVENT_SCREEN_CHANGED;
    event.from   = point(0, 0);
    event.to     = event.from;
    event.attr   = 0;
    event.ch     = 0;
    event.amount = 0;
    event.msec   = 0;

    return _emit(&event);
}


bool ui_event_calculation_card(const melee_calc_blow_t *blow, rect_t area, bool replay)
{
    ui_event_t event = {0};
    event.kind = UI_EVENT_CALCULATION_CARD;
    event.calculation = blow;
    event.from = point(area.x, area.y);
    event.to = point(area.cx, area.cy);
    event.amount = replay;
    return _emit(&event);
}

bool ui_event_calculation(void)
{
    ui_event_t event = {0};
    event.kind = UI_EVENT_CALCULATION;
    return _emit(&event);
}

bool ui_event_look_card(ui_look_card_t *card)
{
    ui_event_t event = {0};
    event.kind = UI_EVENT_LOOK_CARD;
    event.look_card = card;
    return _emit(&event);
}

bool ui_event_map_focus(int x, int y)
{
    ui_event_t event={0};
    event.kind=UI_EVENT_MAP_FOCUS;
    event.to=point(x,y);
    return _emit(&event);
}
