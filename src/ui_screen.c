/*
 * File: ui_screen.c
 *
 * What is on the screen right now. See ui_screen.h for why this is modelled
 * rather than inferred.
 *
 * Copyright (c) 2026 John Horton.
 *
 * Added since the fork and offered on the same terms as the rest of the
 * tree -- the Angband licence in LICENSE.
 */

#include "scottish.h"

static ui_phase_t _phase = UI_PHASE_STARTUP;
static int        _layers;
static int        _pixel_width;
void ui_screen_pixel_width_set(int width) { _pixel_width = MAX(0, width); }
int ui_screen_pixel_width(void) { return _pixel_width; }


/*
 * Tell the frontend the screen's shape changed.
 *
 * A frontend may have drawn things Term has no idea about -- a panel's ground
 * being the obvious one, since Term cannot express a cell's background colour.
 * Those have to come down when whatever justified them goes away, and the
 * region underneath has to be repainted, or they stay on screen with nothing
 * to remove them. That was the menu frame left stranded on the dungeon.
 */
static void _changed(void)
{
    ui_event_screen_changed();
}


void ui_screen_phase_set(ui_phase_t phase)
{
    if (_phase == phase) return;

    _phase = phase;

    /* A new phase starts clean. Layers belong to the phase that raised them,
     * and a phase change means whatever raised them is long gone. */
    _layers = 0;
    _changed();
}

ui_phase_t ui_screen_phase(void)
{
    return _phase;
}


void ui_screen_layer_push(int layer)
{
    if ((_layers & layer) == layer) return;

    _layers |= layer;
    _changed();
}

void ui_screen_layer_pop(int layer)
{
    if (!(_layers & layer)) return;

    _layers &= ~layer;
    _changed();
}

bool ui_screen_has_layer(int layer)
{
    return BOOL((_layers & layer) != 0);
}


bool ui_screen_map_owns(void)
{
    if (_phase != UI_PHASE_WORLD) return FALSE;
    if (_layers) return FALSE;

    /* Belt and braces while the layers are still being retrofitted: eight
     * files take the screen over with a bare Term_save() rather than
     * screen_save(), so screen_depth is the one signal that already covers
     * every well-behaved caller. */
    if (screen_is_saved()) return FALSE;

    /* And the shops, the home, the quest pages and birth: they take the whole
     * screen without a screen_save() and without painting a menu, so neither
     * of the rules above notices them. The game has kept this flag for exactly
     * this question since 1990 -- better to ask it than to keep inventing ways
     * to guess the answer.
     *
     * It used to be ANDed on by sdl3_map_showing() instead, which meant the
     * frontend knew something ui_screen did not, and anything else that asked
     * this got a wrong answer. lite_spot() was asking nothing at all. */
    if (character_icky) return FALSE;

    return TRUE;
}

void ui_screen_selection(void)
{
    ui_screen_layer_push(UI_LAYER_MENU);
    ui_event_map_hidden();
    ui_event_menu_clear();
    Term_clear();
    ui_event_panel_screen();
}
