/*
 * File: ui_menus.c
 *
 * The two menus that belong to the game rather than to the dungeon: Settings,
 * and the Escape menu that reaches it while you are playing.
 *
 * Both are documents, like the start screen, so they inherit the panel and its
 * border, clickable rows, the hover highlight and the theme's colours without
 * asking for any of it.
 *
 * Copyright (c) 2026 John Horton.
 *
 * Added since the fork and offered on the same terms as the rest of the
 * tree -- the Angband licence in LICENSE.
 */

#include "scottish.h"
#ifdef USE_SDL3
#include "ui_music.h"
#endif

/* A host's own settings, appended using the shared row/section helpers.
 * Desktop leaves both hooks NULL. The key hook returns TRUE if it handled it. */
void (*ui_settings_host_rows)(doc_ptr doc) = NULL;
bool (*ui_settings_host_key)(int key) = NULL;

/*
 * A letter menu you can also walk with the arrows.
 *
 * These menus have always been driven by their letters, which is right: it is
 * the fastest way in and the whole game works that way. But a menu you cannot
 * arrow down is a menu you have to read before you can use, and there is no
 * reason not to have both.
 *
 * So this sits where inkey() used to. Up and down move the selection and
 * return zero, which the caller ignores and redraws on. Return presses the
 * letter the selection is sitting on. Everything else is passed straight out,
 * so the existing switch in each menu carries on unchanged and every letter
 * still works.
 */
/*
 * `claim_return` is the whole difference between the two entry points below.
 *
 * A Settings menu is a list of things to do and Return means "do the one I am
 * pointing at". A birth page is not: it is a list of things you *may* change,
 * on a screen whose own footer says "RET) Next Screen", and Return there means
 * go on. Letting the bar take it made character creation unplayable -- arrow
 * once and the key that advances the game started editing a field instead.
 *
 * So the bar activates on Return only where nothing else wants it. On a birth
 * page the bar is for reading and the letter is for doing, which is what the
 * page has always said.
 */
static int _menu_inkey(doc_ptr doc, bool claim_return)
{
    int ct  = doc_hotspot_count(doc);
    int sel = doc_selected(doc);
    int cmd = inkey_special(FALSE);

    int i;
    if (cmd == ESCAPE) sound(SOUND_UI_BACK);
    for (i = 0; i < ct; ++i)
        if (cmd == doc_hotspot_key(doc, i) ||
            (cmd >= 'A' && cmd <= 'Z' && cmd + 32 == doc_hotspot_key(doc, i)))
        { sound(SOUND_UI_ACCEPT); break; }
    if (!ct) return cmd;

    if (!claim_return)
    {
        /*
         * Return is the page's, so the bar needs a key of its own.
         *
         * Right presses the row it is sitting on and left backs out, which is
         * the arrangement every list in every file manager has had for thirty
         * years and which nothing on a birth page was using. Without it the
         * bar moved and could not be pressed -- a highlight that does nothing,
         * which is worse than no highlight at all.
         *
         * Only on this variant. The Settings and Escape menus reach here
         * through ui_menu_inkey(), where Return already does the job, and
         * some of the option pages behind them use left and right for their
         * own paging.
         */
        switch (cmd)
        {
        case SKEY_RIGHT:
            if (sel >= 0) { sound(SOUND_UI_ACCEPT); return doc_hotspot_key(doc, sel); }
            return 0;

        case SKEY_LEFT:
            sound(SOUND_UI_BACK);
            return ESCAPE;

        case '\r':
        case '\n':
            /* The page plays accept only if it acts on Return. */
            return cmd;
        }
    }

    switch (cmd)
    {
    case SKEY_UP:
        doc_select(doc, sel <= 0 ? ct - 1 : sel - 1);
        if (doc_selected(doc) != sel) sound(SOUND_UI_NAVIGATE);
        return 0;

    case SKEY_DOWN:
        doc_select(doc, (sel < 0 || sel >= ct - 1) ? 0 : sel + 1);
        if (doc_selected(doc) != sel) sound(SOUND_UI_NAVIGATE);
        return 0;

    case SKEY_TOP:
        doc_select(doc, 0);
        if (doc_selected(doc) != sel) sound(SOUND_UI_NAVIGATE);
        return 0;

    case SKEY_BOTTOM:
        doc_select(doc, ct - 1);
        if (doc_selected(doc) != sel) sound(SOUND_UI_NAVIGATE);
        return 0;

    case '\r':
    case '\n':
        if (sel >= 0) { sound(SOUND_UI_ACCEPT); return doc_hotspot_key(doc, sel); }

        /*
         * Nothing is selected, so Return is not ours.
         *
         * It used to be swallowed here, which was invisible while only the
         * Settings menus used this -- they have nothing else for Return to
         * mean. The birth pages do: Return is "next screen" there, and
         * routing them through this function stopped character creation dead
         * on its first page, with the key that advances it doing nothing.
         *
         * The bar belongs to the keyboard and appears only once an arrow has
         * been pressed. Until then every key, Return included, means what the
         * page says it means.
         */
        return cmd;
    }

    /*
     * A letter is not a selection.
     *
     * This used to move the bar to whatever letter was pressed, on the
     * reasoning that a following arrow key should carry on from where you
     * had just been. But a click arrives here as a letter, so every click
     * left the bar sitting on the row you had just visited -- a second
     * highlight, the same colour as the one under the pointer, on a row you
     * had already finished with.
     *
     * The bar belongs to the keyboard. Pressing a letter does the thing; it
     * does not move a cursor you were not using.
     */
    return cmd;
}


/* Settings and the Escape menu: the bar activates on Return. */
int ui_menu_inkey(doc_ptr doc)
{
    return _menu_inkey(doc, TRUE);
}


/*
 * Birth pages: the bar walks, and Return belongs to the page.
 *
 * Up and down move the bar, right presses it, and left backs out. Return
 * passes through silently: the caller decides whether it advances, accepts a
 * choice or does nothing, and plays the accept sound only when appropriate.
 */
int ui_menu_inkey_nav(doc_ptr doc)
{
    return _menu_inkey(doc, FALSE);
}


void ui_settings_row(doc_ptr doc, char key, cptr label, cptr value)
{
    doc_printf(doc, "    <color:y>%c</color><color:D>)</color> %-*s",
        key, MAX(22, (int)strlen(label) + 1), label);
    if (value && value[0]) doc_printf(doc, "<color:B>%s</color>", value);
    /* Before the newline: doc_add_hotspot() records the row the cursor is on,
     * and doc_newline() has already moved it down. Registering afterwards put
     * every row's key on the row below it, so clicking Quit chose Settings. */
    doc_add_hotspot(doc, key);
    doc_newline(doc);
}

/*
 * A heading, for grouping.
 *
 * Six settings in one list is a list; nine is a wall. Two things are being
 * chosen here and they are not the same thing -- how the game is put on your
 * screen, and how the world in it is dressed -- so they are separated. The
 * heading is quiet on purpose: it is there to be skimmed past, not read.
 */
void ui_settings_section(doc_ptr doc, cptr name)
{
    doc_printf(doc, "  <color:s>%s</color>\n", name);
}


#ifdef USE_SDL3
static void _music_settings(void)
{
    doc_ptr doc = doc_alloc(MIN(64, ui_doc_menu_rect().cx));
    screen_save();
    sound(SOUND_UI_OPEN);
    for (;;)
    {
        int cmd;
        char volume[20];
        doc_clear(doc);
        doc_insert(doc, "<style:table><color:U>Title music</color>\n\n");
        ui_settings_row(doc, 'm', "Music", display_music_get() ? "on" : "off");
        sprintf(volume, "%d%%", display_music_volume_get());
        ui_settings_row(doc, '-', "Quieter", volume);
        ui_settings_row(doc, '+', "Louder", NULL);
        doc_printf(doc, "\n%s\nPlays on the title screen only.\n\n", ui_music_status());
        ui_settings_row(doc, 'x', "Back", NULL);
        doc_insert(doc, "</style>");
        doc_sync_menu(doc);
        cmd = ui_menu_inkey(doc);
        switch (cmd)
        {
        case 'm': case 'M': display_music_set(!display_music_get()); break;
        case '-': display_music_volume_set(display_music_volume_get() - 10); break;
        case '+': case '=': display_music_volume_set(display_music_volume_get() + 10); break;
        case 'x': case 'X': case ESCAPE: doc_free(doc); screen_load(); return;
        }
    }
}

static void _ambient_settings(void)
{
    doc_ptr doc = doc_alloc(MIN(64, ui_doc_menu_rect().cx));
    screen_save();
    sound(SOUND_UI_OPEN);
    for (;;)
    {
        int cmd;
        char volume[20];
        doc_clear(doc);
        doc_insert(doc, "<style:table><color:U>Ambience</color>\n\n");
        ui_settings_row(doc, 'a', "Background ambience", display_ambient_get() ? "on" : "off");
        sprintf(volume, "%d%%", display_ambient_volume_get());
        ui_settings_row(doc, '-', "Quieter", volume);
        ui_settings_row(doc, '+', "Louder", NULL);
        doc_insert(doc, "\nSurface day/night and five dungeon depths.\nQuieter in menus; pauses when unfocused.\nUses the selected sound source.\n\n");
        ui_settings_row(doc, 'x', "Back", NULL);
        doc_insert(doc, "</style>");
        doc_sync_menu(doc);
        cmd = ui_menu_inkey(doc);
        switch (cmd)
        {
        case 'a': case 'A': display_ambient_set(!display_ambient_get()); break;
        case '-': display_ambient_volume_set(display_ambient_volume_get() - 5); break;
        case '+': case '=': display_ambient_volume_set(display_ambient_volume_get() + 5); break;
        case 'x': case 'X': case ESCAPE: doc_free(doc); screen_load(); return;
        }
    }
}

static void _sound_settings(void)
{
    doc_ptr doc = doc_alloc(MIN(64, ui_doc_menu_rect().cx));
    screen_save();
    sound(SOUND_UI_OPEN);
    for (;;)
    {
        int cmd;
        char volume[20];
        doc_clear(doc);
        doc_insert(doc, "<style:table><color:U>Sound</color>\n\n");
        ui_settings_row(doc, 's', "Effects", display_sound_get() ? "on" : "off");
        ui_settings_row(doc, 'p', "Sound source", display_sound_pack_get() ? "sound pack + synth fallback" : "original synth");
        sprintf(volume, "%d%%", display_sound_volume_get());
        ui_settings_row(doc, '-', "Quieter", volume);
        ui_settings_row(doc, '+', "Louder", NULL);
        ui_settings_row(doc, 't', "Preview effect", NULL);
        ui_settings_row(doc, 'f', "Footsteps (quiet)", display_footsteps_get() ? "on" : "off");
        ui_settings_row(doc, 'n', "Menu sounds", display_menu_sounds_get() ? "on" : "off");
        ui_settings_row(doc, 'a', "Ambience", "Open settings >");
        ui_settings_row(doc, 'm', "Title music", "Open settings >");
        doc_insert(doc, "\nMissing pack effects use the original synth.\n\n");
        ui_settings_row(doc, 'x', "Back", NULL);
        doc_insert(doc, "</style>");
        doc_sync_menu(doc);
        cmd = ui_menu_inkey(doc);
        switch (cmd)
        {
        case 's': case 'S': display_sound_set(!display_sound_get()); break;
        case 'p': case 'P': display_sound_pack_set(!display_sound_pack_get()); break;
        case '-': display_sound_volume_set(display_sound_volume_get() - 10); break;
        case '+': case '=': display_sound_volume_set(display_sound_volume_get() + 10); break;
        case 't': case 'T': sound(SOUND_HIT); break;
        case 'f': case 'F': display_footsteps_set(!display_footsteps_get()); break;
        case 'n': case 'N': display_menu_sounds_set(!display_menu_sounds_get()); break;
        case 'a': case 'A': _ambient_settings(); break;
        case 'm': case 'M': _music_settings(); break;
        case 'x': case 'X': case ESCAPE: doc_free(doc); screen_load(); return;
        }
        if (cmd == '-' || cmd == '+' || cmd == '=' || cmd == 'p' || cmd == 'P') sound(SOUND_HIT);
    }
}
#endif

/*
 * Settings.
 *
 * These used to sit on the start screen, which put three things you set once
 * in front of you every time you opened the game. They belong behind a door
 * you can find rather than in the way.
 */
void ui_settings_menu(void)
{
    doc_ptr doc = doc_alloc(MIN(64, ui_doc_menu_rect().cx));

    /*
     * screen_save() rather than Term_save(): Term keeps exactly one saved
     * screen, so a menu opened from inside another menu overwrote what the
     * outer one had put away and the map never came back. screen_save()
     * counts depth and only stores at the outermost level, which is the one
     * holding the picture that actually matters.
     *
     * There is no restore inside the loop either -- doc_sync_menu() clears the
     * whole screen before it draws, so every pass already starts clean.
     */
    screen_save();
    sound(SOUND_UI_OPEN);

    for (;;)
    {
        int cmd;

        doc_clear(doc);
        doc_insert(doc, "<style:table>");

        doc_insert(doc, "<color:U>Settings</color>\n");

        /* A rule on the blank row the heading already had, so the page is no
         * taller than it was. */
        doc_add_rule(doc);
        doc_newline(doc);

        ui_settings_section(doc, "Display");
        ui_settings_row(doc, 't', "Colour theme", theme_name(theme_current()));
        doc_printf(doc, "       <color:D>%s</color>\n",
                   theme_blurb(theme_current()));
        ui_settings_row(doc, 'o', "Typeface", display_font_name(display_font_get()));
        ui_settings_row(doc, 'd', "Interface size",
             display_density_name(display_density_get()));
        ui_settings_row(doc, 'f', "Fullscreen",
             display_fullscreen_get() ? "yes" : "no");

        doc_newline(doc);

        ui_settings_section(doc, "Presentation");
        ui_settings_row(doc, 'v', "Terrain variation",
             ui_terrain_style_name(display_terrain_get()));
        ui_settings_row(doc, 'l', "Light of day",
             display_daylight_get() ? "yes" : "no");
        ui_settings_row(doc, 'w', "Weather",
             display_weather_name(display_weather_get()));
#ifdef USE_SDL3
        ui_settings_row(doc, 'u', "Sound", "Open settings >");
#else
        ui_settings_row(doc, 'u', "Sound effects", display_sound_get() ? "on" : "off");
#endif
        ui_settings_row(doc, 'i', "Map presentation", display_map_mode_name(display_map_mode_get()));
        if (display_map_mode_get() == DISPLAY_MAP_FULL)
            ui_settings_row(doc, 'a', "  Keep ASCII floors only", display_ascii_floors_get() ? "[x]" : "[ ]");
        if (display_tiles_get())
            ui_settings_row(doc, 'r', "Tile detail", display_tile_detail_get() == 32 ? "32 x 32" : "64 x 64");
        else
        {
            ui_settings_row(doc, 'q', "ASCII grid", display_square_get() ? "Square" : "Classic");
            if (display_square_get())
                ui_settings_row(doc, 'e', "  Enhanced Square", display_enhanced_square_get() ? "[x]" : "[ ]");
        }
        ui_settings_row(doc, 'b', "Warning border",
             display_edge_warnings_get() ? "subtle (static)" : "off");
        ui_settings_row(doc, 'c', "The Reckoner",
             display_calculations_name(display_calculations_get()));

        if (ui_settings_host_rows) ui_settings_host_rows(doc);

        doc_newline(doc);

        ui_settings_row(doc, 'x', "Back", NULL);

        doc_insert(doc, "</style>");
        doc_sync_menu(doc);

        cmd = ui_menu_inkey(doc);

        /* Host keys: g, h, j, k, m, n, p, s, y, z are unused here.
         * Keep p/P reserved for Android's Touch settings. */
        switch (cmd)
        {
        case 't': case 'T': theme_cycle(1);                                  break;
        case 'd': case 'D': display_density_cycle(1);                        break;
        case 'f': case 'F': display_fullscreen_set(!display_fullscreen_get()); break;
        case 'v': case 'V': display_terrain_cycle(1);                        break;
        case 'o': case 'O': display_font_cycle(1);                           break;
        case 'l': case 'L': display_daylight_set(!display_daylight_get());    break;
        case 'w': case 'W': display_weather_cycle(1);                         break;

        case 'q': case 'Q':
            if (!display_tiles_get()) display_square_set(!display_square_get());
            break;

        case 'c': case 'C':
            display_calculations_set((display_calculations_get()+1)%3);
            break;

        case 'b': case 'B':
            display_edge_warnings_set(!display_edge_warnings_get());
            break;

        case 'e': case 'E':
            if (!display_tiles_get() && display_square_get())
                display_enhanced_square_set(!display_enhanced_square_get());
            break;
        case 'a': case 'A':
            if (display_map_mode_get() == DISPLAY_MAP_FULL)
                display_ascii_floors_set(!display_ascii_floors_get());
            break;
        case 'r': case 'R':
            if (display_tiles_get()) display_tile_detail_set(display_tile_detail_get() == 32 ? 64 : 32);
            break;
        case 'i': case 'I':
            display_map_mode_set((display_map_mode_get() + 1) % 3);
            break;

        case 'u': case 'U':
#ifdef USE_SDL3
            _sound_settings();
#else
            display_sound_set(!display_sound_get());
            if (display_sound_get()) sound(SOUND_LEVEL);
#endif
            break;

        case 'x': case 'X':
        case ESCAPE:
            doc_free(doc);
            screen_load();
            return;

        default:
            if (cmd && ui_settings_host_key) ui_settings_host_key(cmd);
            break;
        }
    }
}


/*
 * The Escape menu.
 *
 * Escape during play has always meant "never mind"; here it also gets you to
 * the things you cannot otherwise reach without remembering a key. Continue is
 * first and Escape picks it, so the reflex of pressing Escape twice to dismiss
 * something still costs nothing.
 */
void ui_escape_menu(void)
{
    doc_ptr doc = doc_alloc(MIN(64, ui_doc_menu_rect().cx));

    screen_save();
    sound(SOUND_UI_OPEN);

    for (;;)
    {
        int cmd;

        /* Options and Settings can change the terminal's size. Recreate the
         * document at the new width, retaining this menu's keyboard cursor. */
        int width = MIN(64, ui_doc_menu_rect().cx);
        if (doc_width(doc) != width)
        {
            int selected = doc_selected(doc);
            doc_free(doc);
            doc = doc_alloc(width);
            doc_select(doc, selected);
        }
        doc_clear(doc);
        doc_insert(doc, "<style:table>");

        doc_insert(doc, "<color:U>Paused</color>\n");
        doc_add_rule(doc);
        doc_newline(doc);

        ui_settings_row(doc, 'c', "Continue", NULL);
        ui_settings_row(doc, 's', "Settings", NULL);
        ui_settings_row(doc, 'o', "Options", NULL);
        ui_settings_row(doc, 'h', "Help", NULL);
        ui_settings_row(doc, 'e', "Current effects", NULL);
        doc_newline(doc);
        ui_settings_row(doc, 'q', "Save and quit", NULL);
        ui_settings_row(doc, 'a', "Abandon character", NULL);

        doc_insert(doc, "</style>");
        doc_sync_menu(doc);

        cmd = ui_menu_inkey(doc);

        switch (cmd)
        {
        case 'c': case 'C':
        case ESCAPE:
            doc_free(doc);
            ui_event_menu_clear();
            screen_load();
            return;

        case 'e': case 'E':
            ui_effects_show();
            break;

        case 's': case 'S':
            ui_settings_menu();
            break;

        /* Each command balances its own screen_save()/screen_load(). The
         * outer saved map stays intact; the next loop repaints Paused. */
        case 'o': case 'O':
            do_cmd_options();
            break;

        case 'h': case 'H': case '?':
            do_cmd_help();
            break;

        case 'q': case 'Q':
            doc_free(doc);
            ui_event_menu_clear();
            screen_load();
            do_cmd_save_and_exit();
            return;

        case 'a': case 'A':
            /* Deliberately the game's own suicide command rather than a
             * shortcut to it: it asks for confirmation, and asks properly. */
            doc_free(doc);
            ui_event_menu_clear();
            screen_load();
            do_cmd_suicide();
            return;
        }
    }
}
