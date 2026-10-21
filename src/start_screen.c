/*
 * File: start_screen.c
 *
 * The screen the game opens on.
 *
 * Stock FrogComposBand relies on the Win32 port's File menu for New and Open,
 * which means every other frontend has no way to choose at all -- ours simply
 * loaded whatever savefile the default player name implied, so "press any key"
 * dropped you into your last character whether you wanted it or not.
 *
 * This is built as a doc, deliberately. Everything the menus have gained comes
 * along for free that way: the panel and border, clickable rows, the hover
 * highlight, and the theme's colours. A screen that had to reimplement those
 * would drift away from the rest of the game the first time one of them
 * changed.
 *
 * Copyright (c) 2026 John Horton.
 *
 * Added since the fork and offered on the same terms as the rest of the
 * tree -- the Angband licence in LICENSE.
 */

#include "scottish.h"

/* For the date on a savefile the game has no other way to describe. */
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>

#ifdef WINDOWS
# include <io.h>          /* _findfirst, for the savefile list */
#else
# include <dirent.h>
#endif

/* Where the player's savefile would be, if they have one. */
static void _save_fate(cptr name, char *fate, int len);
static bool _fate_is_over(cptr fate);

/*
 * Whether there is a character to Continue -- a *living* one.
 *
 * A savefile belonging to somebody who has died is still on disk and still
 * opens, because the game writes it on the way out so the score and the dump
 * can be made from it. Testing only that the file exists is what offered a
 * dead character as Continue, and what made choosing it show their death
 * screen again.
 */
static bool _save_exists(char *name, int len)
{
    char path[1024], base[64], fate[128];
    cptr slash;
    int  fd;

    if (name && len) name[0] = '\0';

    if (!savefile[0]) return FALSE;

    fd = fd_open(savefile, O_RDONLY);
    if (fd < 0) return FALSE;
    (void)fd_close(fd);

    /* The savefile's own name is the closest thing to a character name we can
     * show without loading the thing. */
    my_strcpy(path, savefile, sizeof(path));
    slash = strrchr(path, PATH_SEPC);
    my_strcpy(base, slash ? slash + 1 : path, sizeof(base));

    _save_fate(base, fate, sizeof(fate));
    if (_fate_is_over(fate)) return FALSE;

    if (name && len) my_strcpy(name, base, len);
    return TRUE;
}


static void _row(doc_ptr doc, char key, cptr label, cptr value, bool enabled);

/*
 * The savefiles sitting in lib/save.
 *
 * Enumerating a directory is the one thing this game has no portable helper
 * for -- the only one in the tree is in the old SDL 1.2 port, which we do not
 * build -- so it is done here, once, with the two cases it actually needs.
 *
 * Files the game writes for its own purposes are skipped by name. There is no
 * marker inside a savefile that says "I am a savefile" without opening it, and
 * opening every file in the directory to find out is a lot of work to save a
 * player from seeing one wrong line.
 */
#define SAVE_MAX  26

typedef struct {
    char name[64];
    char fate[128];   /* "dead: a Kobold", or empty while they live */
} save_entry;

static bool _is_savefile(cptr name)
{
    if (!name || !name[0] || name[0] == '.') return FALSE;

    if (streq(name, "Makefile")) return FALSE;
    if (streq(name, "delete.me")) return FALSE;

    /* Scores, dumps and the like all carry an extension; savefiles do not. */
    if (strchr(name, '.')) return FALSE;

    return TRUE;
}

static int _list_saves(save_entry *out, int max)
{
    int ct = 0;

#ifdef WINDOWS
    {
        char            pattern[1024];
        struct _finddata_t fd;
        intptr_t        h;

        path_build(pattern, sizeof(pattern), SCOTTISH_DIR_SAVE, "*");

        h = _findfirst(pattern, &fd);
        if (h == -1) return 0;

        do
        {
            if (fd.attrib & _A_SUBDIR) continue;
            if (!_is_savefile(fd.name)) continue;
            if (ct >= max) break;

            my_strcpy(out[ct].name, fd.name, sizeof(out[ct].name));
            ct++;
        }
        while (_findnext(h, &fd) == 0);

        _findclose(h);
    }
#else
    {
        DIR           *dir = opendir(SCOTTISH_DIR_SAVE);
        struct dirent *ent;

        if (!dir) return 0;

        while ((ent = readdir(dir)) != NULL)
        {
            if (!_is_savefile(ent->d_name)) continue;
            if (ct >= max) break;

            my_strcpy(out[ct].name, ent->d_name, sizeof(out[ct].name));
            ct++;
        }

        closedir(dir);
    }
#endif

    /*
     * And how each of them ended, if it has.
     *
     * Here rather than in the two branches above, which enumerate a
     * directory two different ways and should not also have to agree
     * about what is being read out of each file they find.
     */
    {
        int i;

        for (i = 0; i < ct; i++)
            _save_fate(out[i].name, out[i].fate, sizeof(out[i].fate));
    }

    return ct;
}


/*
 * Pick a savefile. Returns TRUE if one was chosen, and points savefile at it.
 *
 * Stock FrogComposBand has no way to do this at all outside the Win32 port's
 * File menu: the savefile is derived from the player name and that is that. So
 * a second character means editing a preference by hand, which is not a thing
 * anybody should have to know.
 */
/*
 * The line save_summary() left beside a savefile, or the file's own date.
 *
 * Two lines are read: who they are, and where they got to. Anything older
 * than the summaries -- or a character who has never saved since they were
 * added -- has no file, and the date is the honest fallback.
 */
/*
 * How a character's run ended, or an empty string if it has not.
 *
 * The third line of the .who file, written by save_summary() only when the
 * character is dead -- "dead: a Kobold", "retired: Ripe Old Age". A savefile
 * written before that line existed has two lines and reports alive, which is
 * what it was.
 *
 * This is the whole of how the front screen knows. A savefile cannot be asked
 * anything without loading it, and loading a dead character is precisely the
 * thing that was going wrong.
 */
static void _save_fate(cptr name, char *fate, int len)
{
    char  path[1024], info[1024], line[256];
    FILE *fp;
    int   n = 0;

    fate[0] = '\0';

    path_build(path, sizeof(path), SCOTTISH_DIR_SAVE, name);
    strnfmt(info, sizeof(info), "%s.who", path);

    fp = my_fopen(info, "r");
    if (!fp) return;

    while (my_fgets(fp, line, sizeof(line)) == 0)
    {
        if (n == 2 && line[0]) my_strcpy(fate, line, len);
        n++;
    }

    my_fclose(fp);
}


/* Whether that fate means the character is finished. */
static bool _fate_is_over(cptr fate)
{
    return BOOL(fate && fate[0]);
}


static void _save_blurb(cptr name, char *who, int who_len,
                        char *where, int where_len)
{
    char        path[1024], line[256];
    FILE       *fp;
    struct stat st;

    who[0] = where[0] = '\0';

    path_build(path, sizeof(path), SCOTTISH_DIR_SAVE, name);

    {
        char info[1024];

        strnfmt(info, sizeof(info), "%s.who", path);
        fp = my_fopen(info, "r");
        if (fp)
        {
            if (my_fgets(fp, line, sizeof(line)) == 0) my_strcpy(who, line, who_len);
            if (my_fgets(fp, line, sizeof(line)) == 0) my_strcpy(where, line, where_len);
            my_fclose(fp);
            if (who[0]) return;
        }
    }

    if (stat(path, &st) == 0)
    {
        struct tm *t = localtime(&st.st_mtime);

        if (t)
            strftime(who, who_len, "last played %d %b %Y", t);
    }
}


/*
 * Throw a character away.
 *
 * The savefile and the line beside it. Confirmed first, because this is the
 * only thing on any of these screens that cannot be undone -- there is no
 * copy of a savefile anywhere and the game has never had one.
 */
static bool _delete_save(cptr name)
{
    /*
     * Everything that belongs to this character, not just the file you can
     * see. Saving writes <name>.new, renames <name> to <name>.old, renames
     * the new one into place and removes the old -- so an interrupted save
     * leaves one of those behind. _is_savefile() skips anything with a dot,
     * so such a leftover is invisible, and deleting the character used to
     * leave it in the directory for ever.
     */
    static const char *tails[] = { "", ".who", ".new", ".old", NULL };
    char path[1024], one[1024];
    int  i, fd;

    path_build(path, sizeof(path), SCOTTISH_DIR_SAVE, name);

    for (i = 0; tails[i]; i++)
    {
        strnfmt(one, sizeof(one), "%s%s", path, tails[i]);
        fd_kill(one);
    }

    /*
     * And check. fd_kill() throws away what remove() said, so a file that
     * will not go -- read-only, or held open by something else -- produced a
     * menu that redrew with the character still in it and no word about why.
     * That is indistinguishable from a Delete that does nothing at all.
     */
    fd = fd_open(path, O_RDONLY);
    if (fd >= 0)
    {
        fd_close(fd);
        return FALSE;
    }

    /* If that was the one Continue would have opened, it is not any more. */
    if (streq(savefile, path))
    {
        savefile[0] = '\0';
        savefile_base[0] = '\0';
    }
    return TRUE;
}


static bool _load_menu(void)
{
    save_entry saves[SAVE_MAX];
    int        ct = _list_saves(saves, SAVE_MAX);
/* Wide enough for the longest thing that goes there, and never anything
 * else -- see the note where it is used. */
#define _BLURB_W 44

    doc_ptr    doc;
    bool       chosen = FALSE;
    int        sel = 0;
    int        confirm = -1;
    char       problem[128];
    bool       problem_is_delete = FALSE;

    problem[0] = '\0';

    doc = doc_alloc(MIN(64, ui_doc_menu_rect().cx));

    /* The line under the list changes with the bar, and a frame that resizes
     * with it drags the whole page sideways. Two for the leading spaces. */
    doc_hold_width(doc, _BLURB_W + 2);

    screen_save();

    for (;;)
    {
        int cmd, i;

        doc_clear(doc);
        doc_insert(doc, "<style:table>");
        doc_insert(doc, "<color:U>Load</color>\n");
        doc_add_rule(doc);
        doc_newline(doc);

        if (!ct)
        {
            doc_insert(doc, "  <color:D>No saved characters in lib/save.</color>\n\n");
        }
        else
        {
            for (i = 0; i < ct; i++)
            {
                char key  = (char)('a' + i);
                bool over = _fate_is_over(saves[i].fate);

                /*
                 * A finished character is shown, dimmed, and says so.
                 *
                 * Not hidden: their savefile is still in lib/save, and a file
                 * you cannot see is a file you can never tidy away. This row
                 * is how Delete reaches them. It just will not load.
                 */
                doc_printf(doc, "  <color:%c>%c</color><color:D>)</color> "
                                "<color:%c>%s</color>",
                           over ? 'D' : 'y', key,
                           over ? 'D' : 'w', saves[i].name);

                if (over)
                {
                    /* The word, not the whole sentence: "dead: a Kobold" is
                     * for the line under the list, where there is room. */
                    doc_printf(doc, "  <color:D>%s</color>",
                               prefix(saves[i].fate, "retired") ? "retired"
                                                                : "dead");
                }

                doc_add_hotspot(doc, key);
                doc_newline(doc);
            }
            doc_newline(doc);

            /*
             * What the selected one was -- or, if Delete has been pressed,
             * the question about it.
             *
             * Under the list rather than beside each row: a dozen names each
             * trailing a sentence is a wall, and only one of them is the one
             * being considered. The bar says which.
             *
             * Both lines are padded to a fixed width, and that is not
             * cosmetic. A menu is laid out to its widest line, so a blurb
             * that changes length as the bar moves makes the whole page slide
             * left and right under the cursor. Constant width, still page.
             */
            {
                char who[128], where[128];

                if (confirm >= 0 && confirm < ct)
                {
                    doc_printf(doc, "  <color:R>%-*.*s</color>\n",
                               _BLURB_W, _BLURB_W,
                               format("Delete %s for good?", saves[confirm].name));
                    doc_printf(doc, "  <color:y>%-*.*s</color>\n",
                               _BLURB_W, _BLURB_W,
                               "y) yes, and it cannot be undone    n) no");
                }
                else if (sel >= 0 && sel < ct)
                {
                    _save_blurb(saves[sel].name, who, sizeof(who),
                                where, sizeof(where));

                    doc_printf(doc, "  <color:W>%-*.*s</color>\n",
                               _BLURB_W, _BLURB_W, who);
                    doc_printf(doc, "  <color:D>%-*.*s</color>\n",
                               _BLURB_W, _BLURB_W, where);
                }
                else
                {
                    doc_printf(doc, "  %-*.*s\n", _BLURB_W, _BLURB_W, "");
                    doc_printf(doc, "  %-*.*s\n", _BLURB_W, _BLURB_W, "");
                }
                doc_newline(doc);
            }
        }

        if (ct) _row(doc, '!', "Delete", NULL, BOOL(confirm < 0));
        _row(doc, 'x', "Back", NULL, TRUE);

        /* Said here rather than nowhere: a Delete that quietly does nothing
         * is the same screen as a Delete that is not wired up. */
        if (problem[0])
        {
            doc_printf(doc, "\n<color:r>%s</color>\n", problem);

            /* The hint belongs to a delete that would not go, and to nothing
             * else. Printing "Read-only, or open elsewhere?" under "PLAYER is
             * dead." sent people looking for a file permission problem that
             * was not there. */
            /* Both of these are short on purpose. This menu is 44 columns
             * and does not wrap, and the first draft of the second one --
             * "Their run is over. Delete removes the file." -- arrived on
             * screen as "Delete removes t". */
            doc_insert(doc, problem_is_delete
                            ? "<color:D>Read-only, or open elsewhere?</color>\n"
                            : "<color:D>Delete removes the file.</color>\n");
        }

        doc_insert(doc, "</style>");

        /*
         * Put the bar where we already believe it is.
         *
         * The line about a character is written while the page is being
         * built, and doc_selected() cannot answer until the page exists --
         * so the selection is kept here instead and handed to the doc. The
         * first pass would otherwise show a bar on the first row and say
         * nothing about it.
         */
        if (ct) doc_select(doc, sel);

        doc_sync_menu(doc);

        cmd = ui_menu_inkey(doc);

        if (ct)
        {
            int now = doc_selected(doc);

            if (now >= 0 && now < ct) sel = now;
        }

        /* While the question is up it is the only question. */
        if (confirm >= 0)
        {
            if (cmd == 'y' || cmd == 'Y')
            {
                if (_delete_save(saves[confirm].name))
                {
                    problem[0] = '\0';
                    ct = _list_saves(saves, SAVE_MAX);
                    if (sel >= ct) sel = ct ? ct - 1 : 0;
                }
                else
                {
                    /* Short, and in two pieces: this menu is 44 columns
                     * wide and does not wrap, so a sentence long enough to
                     * explain itself is cut off mid-explanation. */
                    strnfmt(problem, sizeof(problem),
                            "%s could not be deleted.",
                            saves[confirm].name);
                    problem_is_delete = TRUE;
                }
            }
            confirm = -1;
            continue;
        }

        if (cmd == ESCAPE || cmd == 'x' || cmd == 'X') break;

        if (cmd == '!' && ct)
        {
            confirm = sel;
            continue;
        }

        if (cmd >= 'a' && cmd < 'a' + ct)
        {
            /*
             * A finished character does not open.
             *
             * The old refusal happened *after* loading, deep inside
             * play_game(), which meant choosing one wiped the front screen,
             * read the file, and dropped you onto their death again before
             * saying anything. Refuse here, where the answer is already
             * known, and the screen never moves.
             */
            if (_fate_is_over(saves[cmd - 'a'].fate))
            {
                problem_is_delete = FALSE;
                strnfmt(problem, sizeof(problem), "%s is %s.",
                        saves[cmd - 'a'].name,
                        prefix(saves[cmd - 'a'].fate, "retired") ? "retired"
                                                                 : "dead");
                sel = cmd - 'a';
                continue;
            }

            path_build(savefile, sizeof(savefile), SCOTTISH_DIR_SAVE,
                       saves[cmd - 'a'].name);

            /* savefile_base is derived from savefile on demand, but only if it
             * is empty -- and it may be holding the last character's. */
            savefile_base[0] = '\0';

            chosen = TRUE;
            break;
        }
    }

    doc_free(doc);
    screen_load();

    return chosen;
}


static void _row(doc_ptr doc, char key, cptr label, cptr value, bool enabled)
{
    if (!enabled)
    {
        doc_printf(doc, "  <color:D>%c) %-20.20s</color>\n", key, label);
        return;
    }

    doc_printf(doc, "  <color:y>%c</color><color:D>)</color> %-20.20s", key, label);
    if (value && value[0]) doc_printf(doc, "<color:B>%s</color>", value);
    /* Before the newline: doc_add_hotspot() records the row the cursor is on,
     * and doc_newline() has already moved it down. Registering afterwards put
     * every row's key on the row below it, so clicking Quit chose Settings. */
    doc_add_hotspot(doc, key);
    doc_newline(doc);
}


/*
 * Returns TRUE to start a new character, FALSE to continue an existing one.
 * Quitting leaves through quit() and does not return.
 */
/* Whether the start screen has been shown this run. See start_screen_shown(). */
static bool _shown = FALSE;

/*
 * The news splash exists to fill the wait before the game starts and to give
 * you something to press a key at. A frontend with a start screen of its own
 * needs neither -- which is exactly why the Win32 port has always skipped it
 * -- and having both means a screen of somebody else's character appears
 * between choosing what to do and doing it.
 */
bool start_screen_shown(void)
{
    return _shown;
}


/*
 * Something to say next time the front screen is drawn.
 *
 * play_game() can discover that there is nothing to continue -- no savefile,
 * or one belonging to somebody who died -- and its only options were to go on
 * silently or to guess at a way of telling the player. This is the way: it
 * says it once, in the place the Load menu says a file would not delete, and
 * forgets it.
 */
static char _notice[128] = "";

void start_screen_say(cptr msg)
{
    my_strcpy(_notice, msg ? msg : "", sizeof(_notice));
}

bool start_screen(void)
{
    doc_ptr doc = doc_alloc(MIN(72, ui_doc_menu_rect().cx));
    char    save_name[64];
    bool    have_save;
    bool    result = TRUE;

    have_save = _save_exists(save_name, sizeof(save_name));

    /* This screen is laid out rather than centred: see ui_doc_menu_rect().
     * Anything opened from it is a page in its own right and gets the screen
     * back, so the flag comes off around those. */
    ui_home_layout = TRUE;

    _shown = TRUE;

    ui_screen_phase_set(UI_PHASE_HOME);

    /*
     * Nothing is underneath this screen, so there is nothing to put away and
     * restore -- but this loop has to clear the screen itself.
     *
     * It did not use to. doc_sync_menu() clears the rect it has been given
     * before it draws, and that rect used to be the whole screen. Since the
     * home screen was laid out as a column down the left it is not, so coming
     * back from the lore page or the credits cleared the column and left the
     * rest of the page it had been reading still on screen.
     */
    for (;;)
    {
        int cmd;

        Term_clear();
        doc_clear(doc);
        doc_insert(doc, "<style:table>");

        /* No name here any more: the frontend draws it, six inches tall, with
         * the last letter refusing to arrive. Saying it twice on one screen
         * would spend the joke and the space. */
        doc_printf(doc, "<color:D>%s%s</color>\n", ORMEQ_VER,
            VERSION_IS_DEVELOPMENT ? " (Development)" : "");
        doc_add_rule(doc);
        doc_newline(doc);

        if (have_save)
            _row(doc, 'c', "Continue", save_name, TRUE);
        else
            _row(doc, 'c', "Continue", NULL, FALSE);

        _row(doc, 'l', "Load", NULL, TRUE);
        _row(doc, 'n', "New character", NULL, TRUE);
        doc_newline(doc);

        /* Where the dungeons came from, on the way in rather than buried in
         * the help. A player who is about to spend a hundred hours descending
         * through somebody else's countries is entitled to one page on whose
         * they were, and it costs a keystroke to skip. */
        _row(doc, 'w', "The world", NULL, TRUE);

        /* And who built it, which is very nearly everybody except us. It sits
         * next to the world deliberately: the two pages are the setting and
         * the debt, and a player who reads one may as well be offered both. */
        _row(doc, 't', "Credits", NULL, TRUE);

        /* And everybody who has already tried. The score file has been
         * keeping this list since the fork began and only ever offered it as
         * a table sorted by a number, which is the least interesting question
         * you can ask of two hundred dead people. */
        _row(doc, 'd', "The dead", NULL, TRUE);

        /* The display settings used to sit here, which put three things you
         * set once in front of you every time you opened the game. They are
         * behind this door now. */
        _row(doc, 's', "Settings", NULL, TRUE);
        _row(doc, 'q', "Quit", NULL, TRUE);

        if (_notice[0])
            doc_printf(doc, "\n<color:r>%s</color>\n", _notice);

        doc_insert(doc, "</style>");

        /*
         * The one menu that wants a bar before you have touched anything:
         * Continue is what Return does here, and saying so is the whole
         * point of the first screen.
         *
         * Zero either way, and that is not a coincidence: a greyed row
         * registers no hotspot, so when there is nothing to continue the
         * first *selectable* row is Load. A bar sitting on a greyed row
         * looks like a menu that has stopped working -- you press Return and
         * the game ignores you.
         */
        if (doc_selected(doc) < 0) doc_select(doc, 0);

        doc_sync_menu(doc);

        /* Cleared once it has been on the screen, not when it is set: a
         * message cleared at the moment of setting is one nobody reads. */
        _notice[0] = '\0';

        cmd = ui_menu_inkey(doc);

        switch (cmd)
        {
        case 'c': case 'C':
            if (have_save) { result = FALSE; goto done; }
            break;

        case 'l': case 'L':
            /* Picking one points savefile at it; play_game() loads
             * whatever savefile names, so there is nothing else to do. */
            if (_load_menu()) { result = FALSE; goto done; }
            break;

        case 'n': case 'N':
            /* Clear the savefile name so birth picks a fresh one -- and so
             * load_player() does not treat a file that is not there as a file
             * that is broken. See main-sdl3.c. */
            savefile[0] = '\0';
            result = TRUE;
            goto done;

        case 'w': case 'W':
            /* doc_display_help() puts the screen away and back itself, and
             * the loop above redraws from scratch on every pass, so there is
             * nothing to save here. */
            ui_home_layout = FALSE;
            doc_display_help("lore.txt", NULL);
            ui_home_layout = TRUE;
            break;

        case 't': case 'T':
            ui_home_layout = FALSE;
            doc_display_help("people.txt", NULL);
            ui_home_layout = TRUE;
            break;

        case 'd': case 'D':
            ui_home_layout = FALSE;
            ui_graveyard();
            ui_home_layout = TRUE;
            break;

        case 's': case 'S':
            ui_home_layout = FALSE;
            ui_settings_menu();
            ui_home_layout = TRUE;
            break;

        case 'q': case 'Q':
        case ESCAPE:
            ui_home_layout = FALSE;
            doc_free(doc);
            quit(NULL);
            return TRUE;   /* not reached */
        }
    }

done:
    ui_home_layout = FALSE;
    doc_free(doc);
    Term_clear();

    /* Show the cleared screen before returning. Loading a savefile takes long
     * enough to notice, and without this the home menu sat there apparently
     * ignoring the keypress until the game got round to drawing over it. */
    Term_fresh();

    ui_screen_phase_set(UI_PHASE_STARTUP);
    return result;
}
