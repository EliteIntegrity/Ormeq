/* File: cmd4.c */

/*
 * Copyright (c) 1997 Ben Harrison, James E. Wilson, Robert A. Koeneke
 *
 * This software may be copied and distributed for educational, research,
 * and not for profit purposes provided that this copyright and statement
 * are included in all such copies. Other copyrights may also apply.
 */

/* Purpose: Interface commands */

#include "scottish.h"
#include "equip.h"
#include "int-map.h"
#include "z-doc.h"

#include <assert.h>

static void browser_cursor(char ch, int *column, int *grp_cur, int grp_cnt, int *list_cur, int list_cnt);

/*
 * A set of functions to maintain automatic dumps of various kinds.
 * -Mogami-
 *
 * remove_auto_dump(orig_file, mark)
 *     Remove the old automatic dump of type "mark".
 * auto_dump_printf(fmt, ...)
 *     Dump a formatted string using fprintf().
 * open_auto_dump(buf, mark)
 *     Open a file, remove old dump, and add new header.
 * close_auto_dump(void)
 *     Add a footer, and close the file.
 *
 *    The dump commands of original Angband simply add new lines to
 * existing files; these files will become bigger and bigger unless
 * an user deletes some or all of these files by hand at some
 * point.
 *
 *     These three functions automatically delete old dumped lines
 * before adding new ones. Since there are various kinds of automatic
 * dumps in a single file, we add a header and a footer with a type
 * name for every automatic dump, and kill old lines only when the
 * lines have the correct type of header and footer.
 *
 *     We need to be quite paranoid about correctness; the user might
 * (mistakenly) edit the file by hand, and see all their work come
 * to nothing on the next auto dump otherwise. The current code only
 * detects changes by noting inconsistencies between the actual number
 * of lines and the number written in the footer. Note that this will
 * not catch single-line edits.
 */

/*
 *  Mark strings for auto dump
 */
static char auto_dump_header[] = "# vvvvvvv== %s ==vvvvvvv";
static char auto_dump_footer[] = "# ^^^^^^^== %s ==^^^^^^^";

/*
 * Variables for auto dump
 */
static FILE *auto_dump_stream;
static cptr auto_dump_mark;
static int auto_dump_line_num;

/*
 * Remove old lines automatically generated before.
 */
static void remove_auto_dump(cptr orig_file)
{
    FILE *tmp_fff, *orig_fff;

    char tmp_file[1024];
    char buf[1024];
    bool between_mark = FALSE;
    bool changed = FALSE;
    int line_num = 0;
    long header_location = 0;
    char header_mark_str[80];
    char footer_mark_str[80];
    size_t mark_len;

    /* Prepare a header/footer mark string */
    sprintf(header_mark_str, auto_dump_header, auto_dump_mark);
    sprintf(footer_mark_str, auto_dump_footer, auto_dump_mark);

    mark_len = strlen(footer_mark_str);

    /* Open an old dump file in read-only mode */
    orig_fff = my_fopen(orig_file, "r");

    /* If original file does not exist, nothing to do */
    if (!orig_fff) return;

    /* Open a new (temporary) file */
    tmp_fff = my_fopen_temp(tmp_file, 1024);

    if (!tmp_fff)
    {
        msg_format("Failed to create temporary file %s.", tmp_file);
        msg_print(NULL);
        return;
    }

    /* Loop for every line */
    while (TRUE)
    {
        /* Read a line */
        if (my_fgets(orig_fff, buf, sizeof(buf)))
        {
            /* Read error: Assume End of File */

            /*
             * Was looking for the footer, but not found.
             *
             * Since automatic dump might be edited by hand,
             * it's dangerous to kill these lines.
             * Seek back to the next line of the (pseudo) header,
             * and read again.
             */
            if (between_mark)
            {
                fseek(orig_fff, header_location, SEEK_SET);
                between_mark = FALSE;
                continue;
            }

            /* Success -- End the loop */
            else
            {
                break;
            }
        }

        /* We are looking for the header mark of automatic dump */
        if (!between_mark)
        {
            /* Is this line a header? */
            if (!strcmp(buf, header_mark_str))
            {
                /* Memorise seek point of this line */
                header_location = ftell(orig_fff);

                /* Initialize counter for number of lines */
                line_num = 0;

                /* Look for the footer from now */
                between_mark = TRUE;

                /* There are some changes */
                changed = TRUE;
            }

            /* Not a header */
            else
            {
                /* Copy orginally lines */
                fprintf(tmp_fff, "%s\n", buf);
            }
        }

        /* We are looking for the footer mark of automatic dump */
        else
        {
            /* Is this line a footer? */
            if (!strncmp(buf, footer_mark_str, mark_len))
            {
                int tmp;

                /*
                 * Compare the number of lines
                 *
                 * If there is an inconsistency between
                 * actual number of lines and the
                 * number here, the automatic dump
                 * might be edited by hand. So it's
                 * dangerous to kill these lines.
                 * Seek back to the next line of the
                 * (pseudo) header, and read again.
                 */
                if (!sscanf(buf + mark_len, " (%d)", &tmp)
                    || tmp != line_num)
                {
                    fseek(orig_fff, header_location, SEEK_SET);
                }

                /* Look for another header */
                between_mark = FALSE;
            }

            /* Not a footer */
            else
            {
                /* Ignore old line, and count number of lines */
                line_num++;
            }
        }
    }

    /* Close files */
    my_fclose(orig_fff);
    my_fclose(tmp_fff);

    /* If there are some changes, overwrite the original file with new one */
    if (changed)
    {
        /* Copy contents of temporary file */

        tmp_fff = my_fopen(tmp_file, "r");
        orig_fff = my_fopen(orig_file, "w");

        while (!my_fgets(tmp_fff, buf, sizeof(buf)))
            fprintf(orig_fff, "%s\n", buf);

        my_fclose(orig_fff);
        my_fclose(tmp_fff);
    }

    /* Kill the temporary file */
    fd_kill(tmp_file);

    return;
}


/*
 * Dump a formatted line, using "vstrnfmt()".
 */
static void auto_dump_printf(cptr fmt, ...)
{
    cptr p;
    va_list vp;

    char buf[1024];

    /* Begin the Varargs Stuff */
    va_start(vp, fmt);

    /* Format the args, save the length */
    (void)vstrnfmt(buf, sizeof(buf), fmt, vp);

    /* End the Varargs Stuff */
    va_end(vp);

    /* Count number of lines */
    for (p = buf; *p; p++)
    {
        if (*p == '\n') auto_dump_line_num++;
    }

    /* Dump it */
    fprintf(auto_dump_stream, "%s", buf);
}


/*
 *  Open file to append auto dump.
 */
static bool open_auto_dump(cptr buf, cptr mark)
{

    char header_mark_str[80];

    /* Save the mark string */
    auto_dump_mark = mark;

    /* Prepare a header mark string */
    sprintf(header_mark_str, auto_dump_header, auto_dump_mark);

    /* Remove old macro dumps */
    remove_auto_dump(buf);

    /* Append to the file */
    auto_dump_stream = my_fopen(buf, "a");

    /* Failure */
    if (!auto_dump_stream) {
        msg_format("Failed to open %s.", buf);
        msg_print(NULL);

        /* Failed */
        return FALSE;
    }

    /* Start dumping */
    fprintf(auto_dump_stream, "%s\n", header_mark_str);

    /* Initialize counter */
    auto_dump_line_num = 0;

    auto_dump_printf("# *Warning!*  The lines below are an automatic dump.\n");
    auto_dump_printf("# Don't edit them; changes will be deleted and replaced automatically.\n");

    /* Success */
    return TRUE;
}

/*
 *  Append foot part and close auto dump.
 */
static void close_auto_dump(void)
{
    char footer_mark_str[80];

    /* Prepare a footer mark string */
    sprintf(footer_mark_str, auto_dump_footer, auto_dump_mark);

    auto_dump_printf("# *Warning!*  The lines above are an automatic dump.\n");
    auto_dump_printf("# Don't edit them; changes will be deleted and replaced automatically.\n");

    /* End of dump */
    fprintf(auto_dump_stream, "%s (%d)\n", footer_mark_str, auto_dump_line_num);

    /* Close */
    my_fclose(auto_dump_stream);

    return;
}


/*
 * Return suffix of ordinal number
 */
cptr get_ordinal_number_suffix(int num)
{
    num = ABS(num) % 100;
    switch (num % 10)
    {
    case 1:
        return (num == 11) ? "th" : "st";
    case 2:
        return (num == 12) ? "th" : "nd";
    case 3:
        return (num == 13) ? "th" : "rd";
    default:
        return "th";
    }
}

/*
 * Toggle easy_mimics
 */
void toggle_easy_mimics(bool kayta)
{
    int i;
    for (i = 1; i < max_r_idx; i++)
    {
        monster_race *r_ptr = &r_info[i];
        if (!r_ptr->name) continue;
        if (r_ptr->flags7 & RF7_NASTY_GLYPH)
        {
            if ((kayta) && (r_ptr->x_char == r_ptr->d_char)) r_ptr->x_char = 'x';
            else if ((!kayta) && (r_ptr->x_char == 'x')) r_ptr->x_char = r_ptr->d_char;
            if (r_ptr->d_attr == color_char_to_attr('d'))
            {
                if (kayta) r_ptr->x_attr = color_char_to_attr('D');
                else r_ptr->x_attr = color_char_to_attr('d');
            } 
        }
    }
}

/*
 * Calculate delay length
 */
int delay_time(void)
{
    if (square_delays) return (delay_factor * delay_factor * 2);
    else return (delay_factor * delay_factor * delay_factor);
}


/*
 * Hack -- redraw the screen
 *
 * This command performs various low level updates, clears all the "extra"
 * windows, does a total redraw of the main window, and requests all of the
 * interesting updates and redraws that I can think of.
 *
 * This command is also used to "instantiate" the results of the user
 * selecting various things, such as graphics mode, so it must call
 * the "TERM_XTRA_REACT" hook before redrawing the windows.
 */
bool redraw_hack;
void do_cmd_redraw(void)
{
    int j;

    term *old = Term;


    /* Hack -- react to changes */
    Term_xtra(TERM_XTRA_REACT, 0);


    /* Combine and Reorder the pack (later) */
    p_ptr->notice |= (PN_OPTIMIZE_PACK | PN_OPTIMIZE_QUIVER);


    /* Update torch */
    p_ptr->update |= (PU_TORCH);

    /* Update stuff */
    p_ptr->update |= (PU_BONUS | PU_HP | PU_MANA | PU_SPELLS);

    /* Forget lite/view */
    p_ptr->update |= (PU_UN_VIEW | PU_UN_LITE);

    /* Update lite/view */
    p_ptr->update |= (PU_VIEW | PU_LITE | PU_MON_LITE);

    /* Update monsters */
    p_ptr->update |= (PU_MONSTERS);

    /* Redraw everything */
    p_ptr->redraw |= (PR_WIPE | PR_BASIC | PR_EXTRA | PR_MAP | PR_EQUIPPY | PR_MSG_LINE);

    /* Window stuff */
    p_ptr->window |= (PW_INVEN | PW_EQUIP | PW_SPELL);

    /* Window stuff */
    p_ptr->window |= (PW_MESSAGE | PW_OVERHEAD | PW_DUNGEON |
        PW_MONSTER | PW_MONSTER_LIST | PW_OBJECT_LIST | PW_OBJECT);

    update_playtime();

    /* Prevent spamming ^R to circumvent fuzzy detection */
    redraw_hack = TRUE;
    handle_stuff();
    redraw_hack = FALSE;

    if (p_ptr->prace == RACE_ANDROID) android_calc_exp();


    /* Redraw every window */
    for (j = 0; j < 8; j++)
    {
        /* Dead window */
        if (!ormeq_term[j]) continue;

        /* Activate */
        Term_activate(ormeq_term[j]);

        /* Redraw */
        Term_redraw();

        /* Refresh */
        Term_fresh();

        /* Restore */
        Term_activate(old);
    }
}

/*
 * Show previous messages to the user    -BEN-
 *
 */
void ui_history_tabs(int active, rect_t display)
{
    display=ui_page_rect();
    Term_erase(display.x, display.y, display.cx);
    c_put_str(active == 0 ? TERM_YELLOW : TERM_SLATE, active==0 ? "[>Messages<]" : "[ Messages ]", display.y, display.x);
    c_put_str(active == 1 ? TERM_YELLOW : TERM_SLATE, active==1 ? "[>Past Reckonings<]" : "[ Past Reckonings ]", display.y, display.x + 15);
    c_put_str(TERM_SLATE, "Tab: switch", display.y, display.x + 37);
    ui_event_menu_item(display.x + (active ? 0 : 15), display.y, active ? 12 : 19, '\t');
    ui_event_rule(display.x, display.y + 1, display.cx);
}

static void _message_tab(rect_t display) { ui_history_tabs(0, display); }

void do_cmd_messages(int old_now_turn) { do_cmd_history(FALSE); }

void do_cmd_history(bool reckoning)
{
    int     i;
    doc_ptr doc;
    int     current_turn = 0;
    int     current_row = 0;
    int top, age = 0, blow = 0, tab = reckoning ? 1 : 0;
    bool focus=reckoning;

    if (reckoning) { int pos,total,omitted; reckoner_live_current(&age,&blow,&pos,&total,&omitted); }
    doc = doc_alloc(MIN(80,ui_page_text_rect().cx));
    for (i = msg_count() - 1; i >= 0; i--)
    {
        msg_ptr m = msg_get(i);

        if (m->turn != current_turn)
        {
            if (doc_cursor(doc).y > current_row + 1)
                doc_newline(doc);
            current_turn = m->turn;
            current_row = doc_cursor(doc).y;
        }

        doc_insert_text(doc, m->color, string_buffer(m->msg));
        if (m->count > 1)
        {
            char buf[10];
            sprintf(buf, " (x%d)", m->count);
            doc_insert_text(doc, m->color, buf);
        }
        doc_newline(doc);
    }
    screen_save();
    top = doc_cursor(doc).y;
    string_append_s(doc->name, "messages.txt");
    for (;;)
    {
        int rc = tab ? melee_calc_show(&age, &blow, &focus)
                     : doc_display_tab(doc, "Previous Messages", &top, _message_tab);
        if (rc != '\t') break;
        tab = !tab;
    }
    screen_load();
    doc_free(doc);
}

#ifdef ALLOW_WIZARD

/*
 * Number of cheating options
 */
#define CHEAT_MAX 6

/*
 * Cheating options
 */
static option_type cheat_info[CHEAT_MAX] =
{
    { &cheat_peek,        FALSE,    255,    0x01, 0x00,
    "cheat_peek",        "Peek into object creation"
    },

    { &cheat_hear,        FALSE,    255,    0x02, 0x00,
    "cheat_hear",        "Peek into monster creation"
    },

    { &cheat_room,        FALSE,    255,    0x04, 0x00,
    "cheat_room",        "Peek into dungeon creation"
    },

    { &cheat_xtra,        FALSE,    255,    0x08, 0x00,
    "cheat_xtra",        "Peek into something else"
    },

    { &cheat_live,        FALSE,    255,    0x20, 0x00,
    "cheat_live",        "Allow player to avoid death"
    },

    { &cheat_save,        FALSE,    255,    0x40, 0x00,
    "cheat_save",        "Ask for saving death"
    }
};

/*
 * Interact with some options for cheating
 */
static void do_cmd_options_cheat(cptr info)
{
    int    ch;

    int        i, k = 0, n = CHEAT_MAX;

    char    buf[80];


    /* Clear screen */
    Term_clear();

    /* Interact with the player */
    while (TRUE)
    {
        int dir;

        /* Prompt XXX XXX XXX */
        sprintf(buf, "%s (RET to advance, y/n to set, ESC to accept) ", info);

        prt(buf, 0, 0);

        /* Display the options */
        for (i = 0; i < n; i++)
        {
            byte a = TERM_WHITE;

            /* Color current option */
            if (i == k) a = TERM_L_BLUE;

            /* Display the option text */
            sprintf(buf, "%-48s: %s (%s)",
                cheat_info[i].o_desc,
                (*cheat_info[i].o_var ? "yes" : "no "),

                cheat_info[i].o_text);
            c_prt(a, buf, i + 2, 0);
        }

        /* Hilite current option */
        move_cursor(k + 2, 50);

        autopick_inkey_hack = 1;

        /* Get a key */
        ch = inkey_special(TRUE);

        /*
         * HACK - Try to translate the key into a direction
         * to allow using the roguelike keys for navigation.
         */
        if (ch < 256)
        {
            dir = get_keymap_dir(ch, FALSE);
            if ((dir == 2) || (dir == 4) || (dir == 6) || (dir == 8) || (dir == 9) || (dir == 1))
                ch = I2D(dir);
        }

        /* Analyze */
        switch (ch)
        {
            case ESCAPE:
            {
                return;
            }

            case '-':
            case '8':
            case SKEY_UP:
            {
                k = (n + k - 1) % n;
                break;
            }

            case ' ':
            case '\n':
            case '\r':
            case '2':
            case SKEY_DOWN:
            {
                k = (k + 1) % n;
                break;
            }

            case SKEY_TOP:
            {
                k = MAX(0, k - 10);
                break;
            }

            case SKEY_BOTTOM:
            {
                k = MIN(n - 1, k + 10);
                break;
            }

            case 'y':
            case 'Y':
            case '6':
            case SKEY_RIGHT:
            {
                p_ptr->noscore |= (cheat_info[k].o_set * 256 + cheat_info[k].o_bit);
                (*cheat_info[k].o_var) = TRUE;
                k = (k + 1) % n;
                break;
            }

            case 'n':
            case 'N':
            case '4':
            case SKEY_LEFT:
            {
                (*cheat_info[k].o_var) = FALSE;
                k = (k + 1) % n;
                break;
            }

            case '?':
            {
                doc_display_help("option.txt", cheat_info[k].o_text);
                Term_clear();
                break;
            }

            default:
            {
                bell();
                break;
            }
        }
    }
}
#endif

static option_type autosave_info[2] =
{
    { &autosave_l,      TRUE, 255, 0x01, 0x00,
        "autosave_l",    "Autosave when entering new levels" },


    { &autosave_t,      FALSE, 255, 0x02, 0x00,
        "autosave_t",   "Timed autosave" },

};


static s16b toggle_frequency(s16b current)
{
    switch (current)
    {
    case 0: return 50;
    case 50: return 100;
    case 100: return 250;
    case 250: return 500;
    case 500: return 1000;
    case 1000: return 2500;
    case 2500: return 5000;
    case 5000: return 10000;
    case 10000: return 25000;
    default: return 0;
    }
}


/*
 * Interact with some options for autosaving
 */
static void do_cmd_options_autosave(cptr info)
{
    char    ch;

    int     i, k = 0, n = 2;

    char    buf[80];


    /* Clear screen */
    Term_clear();

    /* Interact with the player */
    while (TRUE)
    {
        /* Prompt XXX XXX XXX */
        sprintf(buf, "%s (RET to advance, y/n to set, 'F' for frequency, ESC to accept) ", info);

        prt(buf, 0, 0);

        /* Display the options */
        for (i = 0; i < n; i++)
        {
            byte a = TERM_WHITE;

            /* Color current option */
            if (i == k) a = TERM_L_BLUE;

            /* Display the option text */
            sprintf(buf, "%-48s: %s (%s)",
                autosave_info[i].o_desc,
                (*autosave_info[i].o_var ? "yes" : "no "),

                autosave_info[i].o_text);
            c_prt(a, buf, i + 2, 0);
        }

        prt(format("Timed autosave frequency: every %d turns",  autosave_freq), 5, 0);



        /* Hilite current option */
        move_cursor(k + 2, 50);

        /* Get a key */
        ch = inkey();

        /* Analyze */
        switch (ch)
        {
            case ESCAPE:
            {
                return;
            }

            case '-':
            case '8':
            {
                k = (n + k - 1) % n;
                break;
            }

            case ' ':
            case '\n':
            case '\r':
            case '2':
            {
                k = (k + 1) % n;
                break;
            }

            case 'y':
            case 'Y':
            case '6':
            {

                (*autosave_info[k].o_var) = TRUE;
                k = (k + 1) % n;
                break;
            }

            case 'n':
            case 'N':
            case '4':
            {
                (*autosave_info[k].o_var) = FALSE;
                k = (k + 1) % n;
                break;
            }

            case 'f':
            case 'F':
            {
                autosave_freq = toggle_frequency(autosave_freq);
                prt(format("Timed autosave frequency: every %d turns",
                       autosave_freq), 5, 0);
                break;
            }

            case '?':
            {
                doc_display_help("option.txt", "Autosave");

                Term_clear();
                break;
            }

            default:
            {
                bell();
                break;
            }
        }
    }
}


/*
 * Interact with some options
 */
/*
 * The options screens.
 *
 * There are a hundred and sixty-four options and the old screen told you the
 * name of each one and nothing else. "Easy Lore" -- and then what? There was
 * an answer: lib/help/option.txt has a written paragraph for almost every
 * option, and pressing '?' would go and fetch it. But fetching it took the
 * list off the screen, so reading about an option and setting it were two
 * different activities and you had to remember what you had just read.
 *
 * So the paragraph is simply the right-hand column, and it follows the cursor.
 * Same idea as the browsers, same reason: the thing you came to read should be
 * on the screen while you act on it.
 *
 * The value-changing code below is untouched. It is a long chain of special
 * cases -- percentages, cycles, widths, pantheons -- and every one of them is
 * somebody's deliberate decision about how that option behaves. Only the
 * drawing has been replaced.
 */
#define _OPT_LIST_W 63

/* Just the value, in words. The label is drawn by the caller. */
static void _opt_value(int idx, char *buf, size_t cap)
{
    option_type *o = &option_info[idx];

    if (o->o_var == &random_artifacts)
    {
        if (random_artifacts) sprintf(buf, "%d%%", random_artifact_pct);
        else my_strcpy(buf, "no", cap);
    }
    else if (o->o_var == &reduce_uniques)
    {
        if (reduce_uniques) sprintf(buf, "%d%%", reduce_uniques_pct);
        else my_strcpy(buf, "no", cap);
    }
    else if (o->o_var == &ironman_empty_levels)
        my_strcpy(buf, empty_lv_description[generate_empty], cap);
    else if (o->o_var == &theme_option)
        my_strcpy(buf, theme_name(theme_current()), cap);
    else if (o->o_var == &density_option)
        my_strcpy(buf, display_density_name(display_density_get()), cap);
    else if (o->o_var == &obj_list_width)
        sprintf(buf, "%d", object_list_width);
    else if (o->o_var == &mon_list_width)
        sprintf(buf, "%d", monster_list_width);
    else if (o->o_var == &single_pantheon)
        sprintf(buf, "%d of %d", pantheon_count, PANTHEON_MAX - 1);
    else if (o->o_var == &guaranteed_pantheon)
    {
        if (pantheon_count == PANTHEON_MAX - 1) my_strcpy(buf, "All", cap);
        else if (game_pantheon && game_pantheon < PANTHEON_MAX)
            sprintf(buf, "%.3s", pant_list[game_pantheon].short_name);
        else my_strcpy(buf, "None", cap);
    }
    else if (o->o_var == &always_small_levels)
        my_strcpy(buf, lv_size_options[small_level_type], cap);
    else
        my_strcpy(buf, *o->o_var ? "yes" : "no", cap);
}

/* Yes and no earn a colour; anything else is a setting rather than a switch. */
static char _opt_value_attr(int idx, cptr value)
{
    if (streq(value, "yes")) return 'G';
    if (streq(value, "no")) return 'D';
    return 'y';
}

/* option.txt, read once, laid out at the width of the pane it will go in. */
static doc_ptr _opt_help_doc(int width)
{
    doc_ptr doc = doc_alloc(width);
    char    path[1024];
    FILE   *fp;

    path_build(path, sizeof(path), SCOTTISH_DIR_HELP, "option.txt");
    fp = my_fopen(path, "r");
    if (fp)
    {
        doc_read_file(doc, fp);
        my_fclose(fp);
    }
    return doc;
}

/*
 * The lines of option.txt belonging to one option: from its own bookmark to
 * whatever bookmark comes next. Returns FALSE if the option has no entry.
 */
static bool _opt_help_range(doc_ptr help, cptr topic, doc_region_t *range,
                            int max_lines)
{
    doc_pos_t start, stop;

    start = doc_find_bookmark(help, topic);
    if (!doc_pos_is_valid(start)) return FALSE;

    stop = doc_next_bookmark(help, start);
    if (!doc_pos_is_valid(stop)) stop.y = doc_line_count(help);

    /* Some entries run to a dozen paragraphs. What fits, fits; '?' still
     * opens the whole page. */
    if (stop.y - start.y > max_lines) stop.y = start.y + max_lines;

    *range = doc_range_middle_lines(help, start.y, stop.y - 1);
    return TRUE;
}

void do_cmd_options_aux(int page, cptr info)
{
    int     ch;
    int     i, k = 0, n = 0, top = 0;
    int     opt[40];
    bool    browse_only = (page == OPT_PAGE_BIRTH) && character_generated &&
                          (!p_ptr->wizard || !allow_debug_opts);
    doc_ptr list_doc, help_doc;
    rect_t  pg = ui_page_rect();
    int     rows, help_w;
    bool    done = FALSE;

    /* Lookup the options */
    for (i = 0; i < 40; i++) opt[i] = 0;

    /* Scan the options */
    for (i = 0; option_info[i].o_desc; i++)
    {
        /* Notice options on this "page" */
        if (option_info[i].o_page == page) opt[n++] = i;
    }
    if (!n) return;

    rows   = pg.cy - 4;   /* a blank row above the footer */
    help_w = pg.cx - _OPT_LIST_W - 2;
    if (help_w < 30) help_w = 30;

    list_doc = doc_alloc(_OPT_LIST_W);
    help_doc = _opt_help_doc(help_w);

    /* Interact with the player */
    while (!done)
    {
        int          dir;
        doc_region_t range;

        if (k < top) top = k;
        if (k >= top + rows) top = k - rows + 1;

        doc_clear(list_doc);
        doc_printf(list_doc, "<color:U>%s</color>", info);
        if (n > rows)
            doc_printf(list_doc, "  <color:D>%d of %d</color>", k + 1, n);
        doc_insert(list_doc, "\n\n");

        for (i = top; i < n && i < top + rows; i++)
        {
            char buf[80];

            _opt_value(opt[i], buf, sizeof(buf));
            doc_printf(list_doc, "%s<color:%c>%-*.*s</color> <color:%c>%s</color>\n",
                       i == k ? "<color:B>></color>" : " ",
                       i == k ? 'B' : 'w',
                       _OPT_LIST_W - 14, _OPT_LIST_W - 14,
                       option_info[opt[i]].o_desc,
                       _opt_value_attr(opt[i], buf), buf);
        }

        Term_clear();
        ui_event_panel_screen();

        doc_sync_term(list_doc, doc_range_all(list_doc),
                      doc_pos_create(pg.x, pg.y));

        if (_opt_help_range(help_doc, option_info[opt[k]].o_text, &range, rows))
        {
            doc_sync_term(help_doc, range,
                          doc_pos_create(pg.x + _OPT_LIST_W + 2, pg.y));
        }
        else
        {
            /* Nothing written about it. Say so rather than leave a hole. */
            doc_ptr d = doc_alloc(help_w);
            doc_printf(d, "<color:U>%s</color>\n\n",
                       option_info[opt[k]].o_desc);
            doc_insert(d, "<color:D>Nothing is written about this one.</color>\n");
            doc_sync_term(d, doc_range_all(d),
                          doc_pos_create(pg.x + _OPT_LIST_W + 2, pg.y));
            doc_free(d);
        }

        prt(browse_only
            ? "Arrows to move. This game is already under way, so these can only be read."
            : "Arrows to move, left and right to change, ESC when you are done.",
            pg.y + pg.cy - 1, pg.x);
        Term_fresh();

        autopick_inkey_hack = 1;

        /* Get a key */
        ch = inkey_special(TRUE);

        /*
         * HACK - Try to translate the key into a direction
         * to allow using the roguelike keys for navigation.
         */
        if (ch < 256)
        {
            dir = get_keymap_dir(ch, FALSE);
            if ((dir == 2) || (dir == 4) || (dir == 6) || (dir == 8) || (dir == 9) || (dir == 1))
                ch = I2D(dir);
        }

        /* Analyze */
        switch (ch)
        {
            case ESCAPE:
            {
                done = TRUE;
                break;
            }

            case '-':
            case '8':
            case SKEY_UP:
            {
                k = (n + k - 1) % n;
                break;
            }

            case ' ':
            case '\n':
            case '\r':
            case '2':
            case SKEY_DOWN:
            {
                k = (k + 1) % n;
                break;
            }

            case '7':
            case '9':
            case SKEY_PGUP:
            case SKEY_TOP:
            {
                k = MAX(0, k - 10);
                break;
            }

            case '1':
            case '3':
            case SKEY_PGDOWN:
            case SKEY_BOTTOM:
            {
                k = MIN(n - 1, k + 10);
                break;
            }
            case 'y':
            case 'Y':
            case '6':
            case SKEY_RIGHT:
            {
                if (browse_only) break;
                sound(SOUND_UI_CHANGE);
                if (option_info[opt[k]].o_var == &random_artifacts)
                {
                    if (!random_artifacts)
                    {
                        random_artifacts = TRUE;
                        random_artifact_pct = 10;
                    }
                    else
                    {
                        random_artifact_pct += 10;
                        if (random_artifact_pct > 100) random_artifacts = FALSE;
                    }
                }
                else if (option_info[opt[k]].o_var == &theme_option)
                {
                    theme_cycle(1);
                }
                else if (option_info[opt[k]].o_var == &density_option)
                {
                    display_density_cycle(1);
                }
                else if (option_info[opt[k]].o_var == &obj_list_width)
                {
                    int maksi = MAX(50, Term->wid - 15);
                    maksi &= ~(0x01);
                    object_list_width += 2;
                    if (object_list_width > maksi) object_list_width = maksi;
                }
                else if (option_info[opt[k]].o_var == &mon_list_width)
                {
                    int maksi = MAX(50, Term->wid - 15);
                    maksi &= ~(0x01);
                    monster_list_width += 2;
                    if (monster_list_width > maksi) monster_list_width = maksi;
                }
                else if (option_info[opt[k]].o_var == &reduce_uniques)
                {
                    if (!reduce_uniques)
                    {
                        reduce_uniques = TRUE;
                        reduce_uniques_pct = 10;
                    }
                    else
                    {
                        reduce_uniques_pct += 10;
                        if (reduce_uniques_pct >= 100) reduce_uniques = FALSE;
                    }
                }
                else if (option_info[opt[k]].o_var == &ironman_empty_levels)
                {
                    generate_empty++;
                    if (generate_empty == EMPTY_MAX) generate_empty = 0;
                    ironman_empty_levels = (generate_empty == EMPTY_ALWAYS);
                }
                else if (option_info[opt[k]].o_var == &single_pantheon)
                {
                    pantheon_count++;
                    if (pantheon_count >= PANTHEON_MAX) pantheon_count = 1;
                }
                else if (option_info[opt[k]].o_var == &guaranteed_pantheon)
                {
                    game_pantheon++;
                    if (game_pantheon >= PANTHEON_MAX) game_pantheon = 0;
                }
                else if (option_info[opt[k]].o_var == &always_small_levels)
                {
                    if (!always_small_levels)
                    {
                        always_small_levels = TRUE;
                        small_level_type = 1;
                    }
                    else
                    {
                        small_level_type++;
                        if (small_level_type > SMALL_LVL_MAX)
                        {
                            always_small_levels = FALSE;
                            small_level_type = 0;
                        }
                    }
                }
                else
                {
                    (*option_info[opt[k]].o_var) = TRUE;
                    k = (k + 1) % n;
                }
                break;
            }

            case 'n':
            case 'N':
            case '4':
            case SKEY_LEFT:
            {
                if (browse_only) break;
                sound(SOUND_UI_CHANGE);
                if (option_info[opt[k]].o_var == &random_artifacts)
                {
                    if (!random_artifacts)
                    {
                        random_artifacts = TRUE;
                        random_artifact_pct = 100;
                    }
                    else
                    {
                        random_artifact_pct -= 10;
                        if (random_artifact_pct <= 0) random_artifacts = FALSE;
                    }
                }
                else if (option_info[opt[k]].o_var == &reduce_uniques)
                {
                    if (!reduce_uniques)
                    {
                        reduce_uniques = TRUE;
                        reduce_uniques_pct = 90;
                    }
                    else
                    {
                        reduce_uniques_pct -= 10;
                        if (reduce_uniques_pct <= 0)
                        {
                            reduce_uniques = FALSE;
                            reduce_uniques_pct = 100;
                        }
                    }
                }
                else if (option_info[opt[k]].o_var == &theme_option)
                {
                    theme_cycle(-1);
                }
                else if (option_info[opt[k]].o_var == &density_option)
                {
                    display_density_cycle(-1);
                }
                else if (option_info[opt[k]].o_var == &obj_list_width)
                {
                    object_list_width -= 2;
                    if (object_list_width < 24) object_list_width = 24;
                }
                else if (option_info[opt[k]].o_var == &mon_list_width)
                {
                    monster_list_width -= 2;
                    if (monster_list_width < 24) monster_list_width = 24;
                }
                else if (option_info[opt[k]].o_var == &ironman_empty_levels)
                {
                    if (generate_empty == 0) generate_empty = EMPTY_MAX - 1;
                    else generate_empty--;
                    ironman_empty_levels = (generate_empty == EMPTY_ALWAYS);
                }
                else if (option_info[opt[k]].o_var == &single_pantheon)
                {
                    pantheon_count--;
                    if (pantheon_count < 1) pantheon_count = PANTHEON_MAX - 1;
                }
                else if (option_info[opt[k]].o_var == &guaranteed_pantheon)
                {
                    if (game_pantheon) game_pantheon--;
                    else game_pantheon = PANTHEON_MAX - 1;
                }
                else if (option_info[opt[k]].o_var == &always_small_levels)
                {
                    if (!always_small_levels)
                    {
                        always_small_levels = TRUE;
                        small_level_type = SMALL_LVL_MAX;
                    }
                    else
                    {
                        small_level_type--;
                        if (small_level_type == 0) always_small_levels = FALSE;
                    }
                }
                else
                {
                    (*option_info[opt[k]].o_var) = FALSE;
                    k = (k + 1) % n;
                }
                break;
            }

            case 't':
            case 'T':
            {
                if (!browse_only) (*option_info[opt[k]].o_var) = !(*option_info[opt[k]].o_var);
                break;
            }

            case '?':
            {
                doc_display_help("option.txt", option_info[opt[k]].o_text);
                Term_clear();
                break;
            }

            default:
            {
                bell();
                break;
            }
        }
    }

    doc_free(list_doc);
    doc_free(help_doc);

    /* Some options change the shape of the screen -- the panel toggles move
     * the map's edges -- so leave with a clean slate rather than hoping every
     * caller happens to repaint the right region. */
    p_ptr->redraw |= (PR_WIPE | PR_BASIC | PR_EXTRA | PR_MAP | PR_EQUIPPY | PR_MSG_LINE);
}


/*
 * A setting that is a single digit.
 *
 * Three of these hang off the options menu: the delay factor, the hitpoint
 * warning and the mana warning. Each was its own copy of the same loop,
 * printing three lines at rows nineteen, twenty and twenty-two -- rows chosen
 * for a terminal twenty-four lines tall. On a screen of thirty-four they sat
 * in the middle of nothing, and the only way to find out what a digit meant
 * was to type it and read the line underneath.
 *
 * So the ten values are a list, each saying what it means, and the paragraph
 * from option.txt sits beside it, the same as every other option page. You can
 * see what you are choosing between before you choose.
 */
static cptr _delay_says(int digit)
{
    int msec = square_delays ? digit * digit * 2 : digit * digit * digit;

    if (!digit) return "no pause at all";
    if (msec == 1) return "1 millisecond";
    return format("%d milliseconds", msec);
}

static cptr _hp_says(int digit)
{
    if (!digit) return "never warn me";
    return format("when I drop below %d%% of my hit points", digit * 10);
}

static cptr _sp_says(int digit)
{
    if (!digit) return "never colour it";
    return format("when I drop below %d%% of my mana", digit * 10);
}

static void _digit_setting(cptr title, cptr topic, int *value,
                           cptr (*says)(int digit))
{
    doc_ptr list_doc, help_doc;
    rect_t  pg = ui_page_rect();
    int     help_w, rows;
    bool    done = FALSE;

    rows   = pg.cy - 4;   /* a blank row above the footer */
    help_w = pg.cx - _OPT_LIST_W - 2;
    if (help_w < 30) help_w = 30;

    list_doc = doc_alloc(_OPT_LIST_W);
    help_doc = _opt_help_doc(help_w);

    if (*value < 0) *value = 0;
    if (*value > 9) *value = 9;

    while (!done)
    {
        doc_region_t range;
        int          i, cmd;

        doc_clear(list_doc);
        doc_printf(list_doc, "<color:U>%s</color>\n\n", title);

        for (i = 0; i <= 9; i++)
        {
            bool sel = (i == *value);

            doc_printf(list_doc, "%s<color:%c>%d</color> "
                                 "<color:%c>%-*.*s</color>\n",
                       sel ? "<color:B>></color>" : " ",
                       sel ? 'B' : 'y', i,
                       sel ? 'B' : 'D',
                       _OPT_LIST_W - 4, _OPT_LIST_W - 4, says(i));
        }

        Term_clear();
        ui_event_panel_screen();

        doc_sync_term(list_doc, doc_range_all(list_doc),
                      doc_pos_create(pg.x, pg.y));

        if (_opt_help_range(help_doc, topic, &range, rows))
        {
            doc_sync_term(help_doc, range,
                          doc_pos_create(pg.x + _OPT_LIST_W + 2, pg.y));
        }

        prt("A digit chooses, or the arrows. ESC when you are done.",
            pg.y + pg.cy - 1, pg.x);
        Term_fresh();

        cmd = inkey_special(TRUE);

        if (cmd == ESCAPE || cmd == 'q' || cmd == 'Q') done = TRUE;
        else if (cmd >= '0' && cmd <= '9') *value = cmd - '0';
        else if (cmd == SKEY_UP)   { if (*value > 0) (*value)--; }
        else if (cmd == SKEY_DOWN) { if (*value < 9) (*value)++; }
        else if (cmd == SKEY_TOP)    *value = 0;
        else if (cmd == SKEY_BOTTOM) *value = 9;
    }

    doc_free(list_doc);
    doc_free(help_doc);
}

/*
 * The options menu.
 *
 * Fifteen entries drawn with prt() at hand-counted rows, in the order they
 * were added over about thirty years, each one saying only its own name.
 * "Mogaminator Preferences" is a real entry on the old screen and it tells a
 * player nothing whatever.
 *
 * It is a document now, so the rows highlight and can be clicked, and it is
 * grouped, and every row says what it is for. The keys have not moved: '1' is
 * still Input Options, wherever it now sits on the page.
 */
static void _opt_menu_row(doc_ptr doc, char key, cptr label, cptr what)
{
    doc_printf(doc, "  <color:y>%c</color><color:D>)</color> %-24.24s", key, label);
    doc_printf(doc, " <color:D>%s</color>", what);
    /* Before the newline: doc_add_hotspot() records the row the cursor is on,
     * and doc_newline() has already moved it down. */
    doc_add_hotspot(doc, key);
    doc_newline(doc);
}

void do_cmd_options(void)
{
    char    k;
    int     i;
    bool    old_easy_mimics = easy_mimics;
    bool    cheats = (p_ptr->noscore || allow_debug_opts);
    doc_ptr doc = doc_alloc(MIN(76, ui_map_rect().cx));

    /* Save the screen */
    screen_save();

    /* Interact */
    while (1)
    {
        doc_clear(doc);
        doc_insert(doc, "<style:table>");
        doc_insert(doc, "<color:U>Options</color>\n");
        doc_add_rule(doc);
        doc_newline(doc);

        doc_insert(doc, "<style:heading>What you look at</style>\n");
        _opt_menu_row(doc, '2', "Map screen", "the map, the panels and the colours");
        _opt_menu_row(doc, '3', "Text display", "how the game words things");
        _opt_menu_row(doc, '7', "List display", "how the lists down the side are drawn");
        doc_newline(doc);

        doc_insert(doc, "<style:heading>How it plays</style>\n");
        _opt_menu_row(doc, '4', "Game play", "the rules you are allowed to bend");
        _opt_menu_row(doc, '5', "Disturbance", "what is allowed to interrupt you");
        _opt_menu_row(doc, '1', "Input", "keys, the mouse, and repeated commands");
        doc_newline(doc);

        doc_insert(doc, "<style:heading>Things done for you</style>\n");
        _opt_menu_row(doc, 'p', "Loot Judgement", "what to pick up, leave, destroy and inscribe");
        _opt_menu_row(doc, '6', "Auto-destroyer", "and what it is not allowed to touch");
        _opt_menu_row(doc, 'a', "Autosave", "when the game saves without being asked");
        doc_newline(doc);

        doc_insert(doc, "<style:heading>Numbers</style>\n");
        _opt_menu_row(doc, 'd', "Base delay factor", "how long the game pauses to show you things");
        _opt_menu_row(doc, 'h', "Hitpoint warning", "how hurt you must be before it stops you");
        _opt_menu_row(doc, 'm', "Mana warning", "and how low your mana must get");
        doc_newline(doc);

        doc_insert(doc, "<style:heading>This game in particular</style>\n");
        _opt_menu_row(doc, 'b', "Birth options",
                      (!p_ptr->wizard || !allow_debug_opts)
                        ? "how this game was set up. Reading only."
                        : "how this game was set up. (*) costs you score.");
        if (cheats)
            _opt_menu_row(doc, 'c', "Cheat options", "spoil the game on purpose");

        doc_newline(doc);
        doc_insert(doc, "  <color:D>ESC) Back</color>\n");
        doc_insert(doc, "</style>");

        Term_clear();
        ui_event_panel_screen();
        doc_sync_menu(doc);

        i = ui_menu_inkey(doc);

        if (i == ESCAPE) break;
        if (!i) continue;                 /* a navigation key; redraw and wait */

        k = (char)i;

        /*
         * Take the frame down before handing over.
         *
         * Most of what this menu opens is still drawn the old way, with
         * prt() at fixed rows and nothing said to the frontend about what
         * is on screen. Those screens inherited this menu's panel and its
         * rule and printed straight across them -- the labels outside the
         * frame at column zero, the content inside it. A menu that hands
         * over should stop claiming the screen first, whatever it is handing
         * over to.
         */
        ui_event_menu_clear();
        ui_event_panel_screen();

        /* Analyze */
        switch (k)
        {
            case '1':
            {
                /* Process the general options */
                do_cmd_options_aux(OPT_PAGE_INPUT, "Input");
                break;
            }

            case '2':
            {
                /* Process the general options */
                do_cmd_options_aux(OPT_PAGE_MAPSCREEN, "The map screen");
                break;
            }

            case '3':
            {
                /* Spawn */
                do_cmd_options_aux(OPT_PAGE_TEXT, "Text display");
                break;
            }

            case '4':
            {
                /* Spawn */
                do_cmd_options_aux(OPT_PAGE_GAMEPLAY, "Game play");
                break;
            }

            case '5':
            {
                /* Spawn */
                do_cmd_options_aux(OPT_PAGE_DISTURBANCE, "Disturbance");
                break;
            }

            case '6':
            {
                /* Spawn */
                do_cmd_options_aux(OPT_PAGE_AUTODESTROY, "The auto-destroyer");
                break;
            }

            case '7':
            {
                /* Spawn */
                do_cmd_options_aux(OPT_PAGE_LIST, "List display");
                break;
            }

            /* Birth Options */
            case 'B':
            case 'b':
            {
                do_cmd_options_aux(OPT_PAGE_BIRTH, (!p_ptr->wizard || !allow_debug_opts) ? "Birth options, as this game was started" : "Birth options -- (*) costs you score");
                break;
            }

            /* Cheating Options */
            case 'C':
            {
#ifdef ALLOW_WIZARD
                if (!p_ptr->noscore && !allow_debug_opts)
                {
                    /* Cheat options are not permitted */
                    bell();
                    break;
                }

                /* Spawn */
                do_cmd_options_cheat("Cheaters never win");
#else
                bell();
#endif
                break;
            }

            case 'a':
            case 'A':
            {
                do_cmd_options_autosave("Autosave");
                break;
            }

            case 'P':
            case 'p':
            {
                do_cmd_loot_rules();
                break;
            }

            case 'D':
            case 'd':
            {
                int v = delay_factor;

                _digit_setting("How long the game pauses to show you things",
                               "BaseDelay", &v, _delay_says);
                delay_factor = v;
                break;
            }

            case 'H':
            case 'h':
            {
                int v = hitpoint_warn;

                _digit_setting("How hurt you must be before it stops you",
                               "Hitpoint", &v, _hp_says);
                hitpoint_warn = v;
                break;
            }

            case 'M':
            case 'm':
            {
                int v = mana_warn;

                _digit_setting("How low your mana must get before it shows",
                               "Manapoint", &v, _sp_says);
                mana_warn = v;
                break;
            }

            case '?':
                doc_display_help("option.txt", NULL);
                Term_clear();
                break;

            /* Unknown option */
            default:
            {
                /* Oops */
                bell();
                break;
            }
        }

        /* Flush messages */
        msg_print(NULL);
    }

    /* Big fat hack */
    if (easy_mimics || old_easy_mimics) toggle_easy_mimics(easy_mimics);

    /* Restore the screen */
    screen_load();

    /* Hack - Redraw equippy chars */
    p_ptr->redraw |= (PR_EQUIPPY);
}



/*
 * Ask for a "user pref line" and process it
 *
 * XXX XXX XXX Allow absolute file names?
 */
void do_cmd_pref(void)
{
    char buf[80];

    /* Default */
    strcpy(buf, "");

    /* Ask for a "user pref command" */
    if (!get_string("Pref: ", buf, 80)) return;


    /* Process that pref command */
    (void)process_pref_file_command(buf);
}

void do_cmd_reload_autopick(void)
{
    if (!get_check("Reload auto-pick preference file? ")) return;

    /* Load the file with messages */
    autopick_load_pref(ALP_DISP_MES);
}

#ifdef ALLOW_MACROS

/*
 * Hack -- append all current macros to the given file
 */
static errr macro_dump(cptr fname)
{
    static cptr mark = "Macro Dump";

    int i;

    char buf[1024];

    /* Build the filename */
    path_build(buf, sizeof(buf), SCOTTISH_DIR_USER, fname);

    /* File type is "TEXT" */
    FILE_TYPE(FILE_TYPE_TEXT);

    /* Append to the file */
    if (!open_auto_dump(buf, mark)) return (-1);

    /* Start dumping */
    auto_dump_printf("\n# Automatic macro dump\n\n");

    /* Dump them */
    for (i = 0; i < macro__num; i++)
    {
        /* Extract the action */
        ascii_to_text(buf, macro__act[i]);

        /* Dump the macro */
        auto_dump_printf("A:%s\n", buf);

        /* Extract the action */
        ascii_to_text(buf, macro__pat[i]);

        /* Dump normal macros */
        auto_dump_printf("P:%s\n", buf);

        /* End the macro */
        auto_dump_printf("\n");
    }

    /* Close */
    close_auto_dump();

    /* Success */
    return (0);
}


/*
 * Hack -- ask for a "trigger" (see below)
 *
 * Note the complex use of the "inkey()" function from "util.c".
 *
 * Note that both "flush()" calls are extremely important.
 */
static void do_cmd_macro_aux(char *buf)
{
    int i, n = 0;

    char tmp[1024];


    /* Flush */
    flush();

    /* Do not process macros */
    inkey_base = TRUE;

    /* First key */
    i = inkey();

    /* Read the pattern */
    while (i)
    {
        /* Save the key */
        buf[n++] = i;

        /* Do not process macros */
        inkey_base = TRUE;

        /* Do not wait for keys */
        inkey_scan = TRUE;

        /* Attempt to read a key */
        i = inkey();
    }

    /* Terminate */
    buf[n] = '\0';

    /* Flush */
    flush();


    /* Convert the trigger */
    ascii_to_text(tmp, buf);

    /* Hack -- display the trigger */
    Term_addstr(-1, TERM_WHITE, tmp);
}

#endif


/*
 * Hack -- ask for a keymap "trigger" (see below)
 *
 * Note that both "flush()" calls are extremely important. This may
 * no longer be true, since "util.c" is much simpler now. XXX XXX XXX
 */
static void do_cmd_macro_aux_keymap(char *buf)
{
    char tmp[1024];


    /* Flush */
    flush();


    /* Get a key */
    buf[0] = inkey();
    buf[1] = '\0';


    /* Convert to ascii */
    ascii_to_text(tmp, buf);

    /* Hack -- display the trigger */
    Term_addstr(-1, TERM_WHITE, tmp);


    /* Flush */
    flush();
}


/*
 * Hack -- append all keymaps to the given file
 */
static errr keymap_dump(cptr fname)
{
    static cptr mark = "Keymap Dump";
    int i;

    char key[1024];
    char buf[1024];

    int mode;

    /* Roguelike */
    if (rogue_like_commands)
    {
        mode = KEYMAP_MODE_ROGUE;
    }

    /* Original */
    else
    {
        mode = KEYMAP_MODE_ORIG;
    }


    /* Build the filename */
    path_build(buf, sizeof(buf), SCOTTISH_DIR_USER, fname);

    /* File type is "TEXT" */
    FILE_TYPE(FILE_TYPE_TEXT);

    /* Append to the file */
    if (!open_auto_dump(buf, mark)) return -1;

    /* Start dumping */
    auto_dump_printf("\n# Automatic keymap dump\n\n");

    /* Dump them */
    for (i = 0; i < 256; i++)
    {
        cptr act;

        /* Loop up the keymap */
        act = keymap_act[mode][i];

        /* Skip empty keymaps */
        if (!act) continue;

        /* Encode the key */
        buf[0] = i;
        buf[1] = '\0';
        ascii_to_text(key, buf);

        /* Encode the action */
        ascii_to_text(buf, act);

        /* Dump the macro */
        auto_dump_printf("A:%s\n", buf);
        auto_dump_printf("C:%d:%s\n", mode, key);
    }

    /* Close */
    close_auto_dump();

    /* Success */
    return (0);
}



/*
 * Interact with "macros"
 *
 * Note that the macro "action" must be defined before the trigger.
 *
 * Could use some helpful instructions on this page. XXX XXX XXX
 */
/*
 * Keys you have bound.
 *
 * The old screen was nine numbered choices and a mystery. You could Query a
 * macro, but only by pressing the trigger you were asking about -- so the one
 * thing you could not do was find out what you had bound, which is the only
 * question anybody actually arrives with. Underneath it sat a hidden global
 * called the "current action" that every operation passed through, displayed
 * at the bottom of the screen with no explanation, so it read as leftover
 * junk rather than as a clipboard.
 *
 * This is the list instead. Everything you have bound, what it does, and the
 * keys to add, change or remove one. The clipboard is gone: creating and
 * changing a binding now carry the action themselves, which is what the
 * clipboard was for.
 *
 * The words are worth keeping straight, because the old screen never said:
 *
 *   A **keymap** replaces one keypress with a run of commands. It is how you
 *   rearrange the keyboard -- put a command somewhere your hand already is.
 *
 *   A **macro** does the same for a whole sequence, which is how a function
 *   key or a mouse button gets to mean something, since those arrive as
 *   several bytes rather than one.
 */
#define _MAC_LIST_W 44

typedef struct {
    bool keymap;      /* otherwise a macro */
    int  key;         /* keymaps: the byte. macros: index into macro__pat */
} _binding_t;

/*
 * Everything currently bound, keymaps first.
 *
 * A macro bound to itself is upstream's way of saying "removed" -- that is
 * literally what Remove a macro did -- so those are left out rather than
 * listed as a key that stands for itself.
 */
static int _collect_bindings(int mode, _binding_t *out, int max)
{
    int i, n = 0;

    for (i = 0; i < 256 && n < max; i++)
    {
        if (!keymap_act[mode][i]) continue;
        out[n].keymap = TRUE;
        out[n].key    = i;
        n++;
    }

    for (i = 0; i < macro__num && n < max; i++)
    {
        if (streq(macro__pat[i], macro__act[i])) continue;
        out[n].keymap = FALSE;
        out[n].key    = i;
        n++;
    }

    return n;
}

static cptr _binding_trigger(int mode, _binding_t *b)
{
    static char buf[1024];
    char        raw[2];

    if (b->keymap)
    {
        raw[0] = (char)b->key;
        raw[1] = '\0';
        ascii_to_text(buf, raw);
    }
    else
        ascii_to_text(buf, macro__pat[b->key]);

    return buf;
}

static cptr _binding_action(int mode, _binding_t *b)
{
    static char buf[1024];

    if (b->keymap)
        ascii_to_text(buf, keymap_act[mode][b->key]);
    else
        ascii_to_text(buf, macro__act[b->key]);

    return buf;
}

/* What the encoding means, since an action is typed rather than recorded. */
static void _mac_encoding(doc_ptr doc)
{
    doc_insert(doc, "<style:heading>Writing an action</style>\n");
    doc_insert(doc, "  <color:D>An action is the keys you would have pressed,"
                    " written out. Most\n"
                    "  characters stand for themselves; the awkward ones are"
                    " spelt:</color>\n\n");
    doc_insert(doc, "    <color:y>\\e</color> <color:D>Escape</color>"
                    "     <color:y>\\s</color> <color:D>space</color>"
                    "      <color:y>\\n</color> <color:D>Return</color>"
                    "     <color:y>\\t</color> <color:D>Tab</color>\n");
    doc_insert(doc, "    <color:y>\\b</color> <color:D>Backspace</color>"
                    "  <color:y>^A</color> <color:D>Control-A, and so on"
                    "</color>\n\n");
    doc_insert(doc, "  <color:D>So an action of</color> <color:y>maa</color>"
                    " <color:D>casts the first spell in your first book,"
                    " and</color>\n"
                    "  <color:y>\\e</color> <color:D>on its own is a key that"
                    " gets you out of anything.</color>\n");
}

static void _mac_detail(doc_ptr doc, int mode, _binding_t *b)
{
    doc_printf(doc, "<color:U>%s</color>\n\n",
               b->keymap ? "A keymap" : "A macro");

    doc_printf(doc, "  <color:D>%-9s</color> <color:y>%s</color>\n",
               b->keymap ? "Key" : "Sequence", _binding_trigger(mode, b));
    doc_printf(doc, "  <color:D>%-9s</color> <color:W>%s</color>\n\n",
               "Does", _binding_action(mode, b));

    if (b->keymap)
    {
        doc_insert(doc, "  <color:D>One keypress standing for a run of"
                        " commands. This is how you\n"
                        "  put a command somewhere your hand already is."
                        "</color>\n\n");
    }
    else
    {
        doc_insert(doc, "  <color:D>A whole sequence standing for a run of"
                        " commands. Function keys\n"
                        "  and mouse buttons arrive as several bytes, so they"
                        " need one of\n  these rather than a keymap.</color>"
                        "\n\n");
    }

    _mac_encoding(doc);
}

static void _mac_nothing(doc_ptr doc)
{
    doc_insert(doc, "<color:U>Nothing is bound</color>\n\n");
    doc_insert(doc, "  <color:D>Two things can be, and they are not the same:"
                    "</color>\n\n");
    doc_insert(doc, "  <color:y>k</color>  <color:W>a keymap</color>"
                    " <color:D>-- one keypress standing for a run of commands."
                    "\n     This is how you rearrange the keyboard: put a"
                    " command\n     somewhere your hand already is.</color>"
                    "\n\n");
    doc_insert(doc, "  <color:y>m</color>  <color:W>a macro</color>"
                    " <color:D>-- a whole key sequence standing for a run of"
                    "\n     commands. Function keys and mouse buttons arrive"
                    " as several\n     bytes rather than one, so they need"
                    " one of these.</color>\n\n");
    _mac_encoding(doc);
}

/* Ask for an action, starting from whatever is there now. */
static bool _mac_ask_action(cptr prompt, char *ascii_out, cptr current)
{
    char text[1024];

    ascii_to_text(text, current ? current : "");
    if (!get_string(prompt, text, 78)) return FALSE;

    text_to_ascii(ascii_out, text);
    return TRUE;
}

void do_cmd_macros(void)
{
    doc_ptr    list_doc, side_doc;
    _binding_t list[512];
    int        n = 0, cur = 0, top = 0;
    int        mode = rogue_like_commands ? KEYMAP_MODE_ROGUE : KEYMAP_MODE_ORIG;
    int        wid, hgt, rows, side_w;
    bool       done = FALSE, recount = TRUE;
    char       trigger[1024], action[1024], tmp[1024];

    Term_get_size(&wid, &hgt);
    rows   = hgt - 7;
    side_w = wid - _MAC_LIST_W - 4;
    if (side_w < 40) side_w = 40;

    list_doc = doc_alloc(_MAC_LIST_W);
    side_doc = doc_alloc(side_w);

    FILE_TYPE(FILE_TYPE_TEXT);
    screen_save();

    while (!done)
    {
        int i, cmd;

        if (recount)
        {
            n = _collect_bindings(mode, list, 512);
            if (cur >= n) cur = MAX(0, n - 1);
            recount = FALSE;
        }

        if (cur < top) top = cur;
        if (cur >= top + rows) top = cur - rows + 1;

        doc_clear(list_doc);
        doc_clear(side_doc);

        doc_insert(list_doc, "<color:U>Bound</color>");
        if (n > rows)
            doc_printf(list_doc, "  <color:D>%d of %d</color>", cur + 1, n);
        doc_insert(list_doc, "\n\n");
        if (!n)
            doc_insert(list_doc, " <color:D>Nothing yet.</color>\n");

        for (i = top; i < n && i < top + rows; i++)
        {
            bool sel = (i == cur);
            char trig[64];

            my_strcpy(trig, _binding_trigger(mode, &list[i]), sizeof(trig));

            doc_printf(list_doc, "%s<color:%c>%-12.12s</color> "
                                 "<color:%c>%-28.28s</color>\n",
                       sel ? "<color:B>></color>" : " ",
                       sel ? 'B' : 'y', trig,
                       sel ? 'B' : 'w', _binding_action(mode, &list[i]));
        }

        if (n)
            _mac_detail(side_doc, mode, &list[cur]);
        else
            _mac_nothing(side_doc);

        Term_clear();
        ui_event_panel_screen();

        prt(format("Keys you have bound - %s keyset",
                   mode == KEYMAP_MODE_ROGUE ? "Roguelike" : "Original"),
            0, 0);

        doc_sync_term(list_doc, doc_range_all(list_doc), doc_pos_create(0, 2));
        doc_sync_term(side_doc, doc_range_all(side_doc),
                      doc_pos_create(_MAC_LIST_W + 4, 2));

        prt("k a keymap,  m a macro,  e change this one,  d remove it",
            hgt - 2, 0);
        prt("l load bindings from a file,  s add these to one,"
            "  ESC when you are done.", hgt - 1, 0);
        Term_fresh();

        cmd = inkey_special(TRUE);

        switch (cmd)
        {
        case ESCAPE: case 'q': case 'Q':
            done = TRUE;
            break;

        case SKEY_UP: case '8':
            if (cur > 0) cur--;
            break;

        case SKEY_DOWN: case '2':
            if (cur < n - 1) cur++;
            break;

        case SKEY_TOP: case SKEY_PGUP:
            cur = 0;
            break;

        case SKEY_BOTTOM: case SKEY_PGDOWN:
            cur = MAX(0, n - 1);
            break;

        case 'k': case 'K':
            prt("Press the key you want to stand for something: ", hgt - 2, 0);
            prt("", hgt - 1, 0);
            do_cmd_macro_aux_keymap(trigger);

            if (_mac_ask_action("It should do: ", action,
                                keymap_act[mode][(byte)trigger[0]]))
            {
                z_string_free(keymap_act[mode][(byte)trigger[0]]);
                keymap_act[mode][(byte)trigger[0]] =
                    action[0] ? z_string_make(action) : NULL;
                recount = TRUE;
            }
            break;

        case 'm': case 'M':
            prt("Press the key or button you want to stand for something: ",
                hgt - 2, 0);
            prt("", hgt - 1, 0);
            do_cmd_macro_aux(trigger);

            {
                int found = macro_find_exact(trigger);

                if (_mac_ask_action("It should do: ", action,
                                    found >= 0 ? macro__act[found] : NULL))
                {
                    macro_add(trigger, action);
                    recount = TRUE;
                }
            }
            break;

        case 'e': case 'E':
            if (!n) break;
            if (list[cur].keymap)
            {
                int key = list[cur].key;

                if (_mac_ask_action("It should do: ", action,
                                    keymap_act[mode][key]))
                {
                    z_string_free(keymap_act[mode][key]);
                    keymap_act[mode][key] =
                        action[0] ? z_string_make(action) : NULL;
                    recount = TRUE;
                }
            }
            else
            {
                int idx = list[cur].key;

                my_strcpy(tmp, macro__pat[idx], sizeof(tmp));
                if (_mac_ask_action("It should do: ", action,
                                    macro__act[idx]))
                {
                    macro_add(tmp, action);
                    recount = TRUE;
                }
            }
            break;

        case 'd': case 'D':
            if (!n) break;
            if (!get_check(format("Remove %s? ",
                                  _binding_trigger(mode, &list[cur])))) break;

            if (list[cur].keymap)
            {
                z_string_free(keymap_act[mode][list[cur].key]);
                keymap_act[mode][list[cur].key] = NULL;
            }
            else
            {
                /* Upstream's way of saying gone: the trigger stands for
                 * itself, which is what it would have done unbound. */
                my_strcpy(tmp, macro__pat[list[cur].key], sizeof(tmp));
                macro_add(tmp, tmp);
            }
            recount = TRUE;
            break;

        case 'l': case 'L':
            strnfmt(tmp, sizeof(tmp), "%s.prf", pref_save_base);
            if (get_string("Load which file? ", tmp, 70))
            {
                errr err = process_pref_file(tmp);

                if (err == -2)   msg_format("Loaded the default '%s'.", tmp);
                else if (err)    msg_format("Could not load '%s'.", tmp);
                else             msg_format("Loaded '%s'.", tmp);
                recount = TRUE;
            }
            break;

        case 's': case 'S':
            strnfmt(tmp, sizeof(tmp), "%s.prf", pref_save_base);
            if (get_string("Add them to which file? ", tmp, 70))
            {
                (void)macro_dump(tmp);
                (void)keymap_dump(tmp);
                msg_format("Added them to '%s'.", tmp);
            }
            break;
        }
    }

    doc_free(list_doc);
    doc_free(side_doc);

    screen_load();
    p_ptr->redraw |= (PR_WIPE | PR_BASIC | PR_EXTRA | PR_MAP | PR_EQUIPPY);
}


static cptr lighting_level_str[F_LIT_MAX] =
{
    "standard",
    "brightly lit",
    "darkened",
};


static void _browse_monsters(void);
static void _browse_artifacts(void);
static void _browse_egos(void);
static void _browse_objects(void);
static void _browse_terrain(void);

/*
 * Colours.
 *
 * This used to be five choices on a menu: load a pref file, dump colours to
 * one, modify colours, and two canned colour sets from other ports. The
 * modifying worked -- it wrote straight into ormeq_color_table and you
 * could watch a colour change under your hands -- but the theme system
 * overwrites that table whenever a theme is applied, so the moment the player
 * changed theme every edit they had made was gone, silently. The two canned
 * sets fought the themes in the same way and from the other direction.
 *
 * So colour editing belongs to the theme now. You are always recolouring the
 * palette you are wearing, and leaving keeps it as a copy of that theme under
 * lib/user, which the loader reads in preference to the one we ship. There is
 * nothing to lose track of and nothing to overwrite you.
 *
 * Loading a pref file has not gone anywhere; it is on the visuals menu behind
 * '%', which is where the rest of the pref-file machinery lives.
 */
#define _COL_LIST_W 26

static void _col_sample(doc_ptr doc, int index)
{
    char a = attr_to_attr_char((byte)index);
    byte r, g, b;

    theme_map_get(index, &r, &g, &b);

    doc_printf(doc, "<color:U>%s</color>\n", theme_map_name(index));
    doc_printf(doc, "<color:D>%02X%02X%02X</color>\n\n", r, g, b);

    /*
     * The point of a sample is legibility, so it has to be the shapes a
     * player actually reads: a line of prose, the glyphs a map is drawn from,
     * and a row of digits. A block of solid colour tells you what the colour
     * is and nothing about whether you can live with it.
     */
    doc_printf(doc, "<color:%c>%s</color>\n", a,
               "The quick brown fox jumps over the lazy dog.");
    doc_printf(doc, "<color:%c>%s</color>\n", a,
               "@ p k o T D & ! ? - _ = \" ~ $ * % # . ,");
    doc_printf(doc, "<color:%c>%s</color>\n\n", a,
               "0 1 2 3 4 5 6 7 8 9   +12  -3  1,450 gold");

    doc_printf(doc, "<color:%c>%s</color>\n", a,
               "################################");

    if (index == TERM_DARK)
    {
        doc_newline(doc);
        doc_insert(doc, "<color:D>Nothing above will be readable, and that is"
                        " what this colour is for: it is what an unlit square"
                        " is drawn in.</color>\n");
    }
}

static void _col_step(int index, int channel, int delta)
{
    byte rgb[3];

    theme_map_get(index, &rgb[0], &rgb[1], &rgb[2]);

    /* Clamp rather than wrap. A colour channel that rolls from black round to
     * white on one keypress is not an adjustment, it is a surprise. */
    if (delta > 0) rgb[channel] = (rgb[channel] > 255 - delta)
                                ? 255 : (byte)(rgb[channel] + delta);
    else           rgb[channel] = (rgb[channel] < -delta)
                                ? 0 : (byte)(rgb[channel] + delta);

    theme_map_set(index, rgb[0], rgb[1], rgb[2]);
}

static void _col_type_hex(int index)
{
    byte r, g, b;
    char buf[16];

    theme_map_get(index, &r, &g, &b);
    sprintf(buf, "%02X%02X%02X", r, g, b);

    if (!get_string("Colour (six hex digits): ", buf, 6)) return;

    if (strlen(buf) == 6)
    {
        char part[3];
        long v[3];
        int  i;

        part[2] = '\0';
        for (i = 0; i < 3; i++)
        {
            char *end;

            part[0] = buf[i * 2];
            part[1] = buf[i * 2 + 1];
            v[i] = strtol(part, &end, 16);
            if (*end) return;                   /* not hex; leave it alone */
        }
        theme_map_set(index, (byte)v[0], (byte)v[1], (byte)v[2]);
    }
}

void do_cmd_colors(void)
{
    doc_ptr list_doc, side_doc;
    int     cur = 1, top = 0;          /* 0 is DARK, which shows nothing */
    int     n = theme_map_editable();
    int     wid, hgt, rows, side_w;
    bool    done = FALSE;

    Term_get_size(&wid, &hgt);
    rows   = hgt - 7;
    side_w = wid - _COL_LIST_W - 4;
    if (side_w < 40) side_w = 40;

    list_doc = doc_alloc(_COL_LIST_W);
    side_doc = doc_alloc(side_w);

    screen_save();

    while (!done)
    {
        int i, cmd;

        if (cur < top) top = cur;
        if (cur >= top + rows) top = cur - rows + 1;

        doc_clear(list_doc);
        doc_clear(side_doc);

        doc_insert(list_doc, "<color:U>Colours</color>\n\n");
        for (i = top; i < n && i < top + rows; i++)
        {
            bool sel = (i == cur);

            doc_printf(list_doc, "%s<color:%c>%-11.11s</color> "
                                 "<color:%c>#####</color>\n",
                       sel ? "<color:B>></color>" : " ",
                       sel ? 'B' : 'w', theme_map_name(i),
                       attr_to_attr_char((byte)i));
        }

        _col_sample(side_doc, cur);

        Term_clear();
        ui_event_panel_screen();

        prt(format("Colours - %s", theme_name(theme_current())), 0, 0);
        if (theme_is_edited())
            c_put_str(TERM_YELLOW, "changed", 0, wid - 8);
        else if (theme_has_own_copy())
            c_put_str(TERM_L_DARK, "yours", 0, wid - 6);

        doc_sync_term(list_doc, doc_range_all(list_doc), doc_pos_create(0, 2));
        doc_sync_term(side_doc, doc_range_all(side_doc),
                      doc_pos_create(_COL_LIST_W + 4, 2));

        prt("r R  g G  b B change it by eight,  # types a value,"
            "  < > another theme", hgt - 2, 0);
        prt("! puts this theme back as it shipped,  ESC keeps what you have"
            " done.", hgt - 1, 0);
        Term_fresh();

        cmd = inkey_special(TRUE);

        switch (cmd)
        {
        case ESCAPE: case 'q': case 'Q':
            done = TRUE;
            break;

        case SKEY_UP: case '8':
            if (cur > 0) cur--;
            break;

        case SKEY_DOWN: case '2':
            if (cur < n - 1) cur++;
            break;

        case SKEY_TOP: case SKEY_PGUP:
            cur = 0;
            break;

        case SKEY_BOTTOM: case SKEY_PGDOWN:
            cur = n - 1;
            break;

        case 'r': _col_step(cur, 0, -8); break;
        case 'R': _col_step(cur, 0,  8); break;
        case 'g': _col_step(cur, 1, -8); break;
        case 'G': _col_step(cur, 1,  8); break;
        case 'b': _col_step(cur, 2, -8); break;
        case 'B': _col_step(cur, 2,  8); break;

        case '#':
            _col_type_hex(cur);
            break;

        case '<': case SKEY_LEFT: case '4':
            if (theme_is_edited()) theme_save();
            theme_cycle(-1);
            break;

        case '>': case SKEY_RIGHT: case '6':
            if (theme_is_edited()) theme_save();
            theme_cycle(1);
            break;

        case '!':
            /* Destroys work, so it asks. */
            if (theme_has_own_copy() || theme_is_edited())
            {
                if (get_check(format("Put %s back as it shipped? ",
                                     theme_name(theme_current()))))
                {
                    theme_revert();
                }
            }
            else
                msg_print("This one is already exactly as it shipped.");
            break;
        }

        Term_xtra(TERM_XTRA_REACT, 0);
    }

    /* Leaving keeps the work. There is nowhere else for it to go, and losing
     * it silently is the fault this screen was rebuilt to stop. */
    if (theme_is_edited()) theme_save();

    doc_free(list_doc);
    doc_free(side_doc);

    screen_load();
    p_ptr->redraw |= (PR_WIPE | PR_BASIC | PR_EXTRA | PR_MAP | PR_EQUIPPY);
}

void msg_add_tiny_screenshot(int cx, int cy)
{
    if (!statistics_hack)
    {
        string_ptr s = get_tiny_screenshot(cx, cy);
        msg_add(string_buffer(s));
        string_free(s);
    }
}

string_ptr get_tiny_screenshot(int cx, int cy)
{
    string_ptr s = string_alloc_size(cx * cy);
    bool       old_use_graphics = use_graphics;
    int        y1, y2, x1, x2, y, x;

    y1 = py - cy/2;
    y2 = py + cy/2;
    if (y1 < 0) y1 = 0;
    if (y2 > cur_hgt) y2 = cur_hgt;

    x1 = px - cx/2;
    x2 = px + cx/2;
    if (x1 < 0) x1 = 0;
    if (x2 > cur_wid) x2 = cur_wid;

    if (old_use_graphics)
    {
        use_graphics = FALSE;
        reset_visuals();
    }

    for (y = y1; y < y2; y++)
    {
        int  current_a = -1;
        for (x = x1; x < x2; x++)
        {
            byte a, ta;
            char c, tc;

            assert(in_bounds2(y, x));
            map_info(y, x, &a, &c, &ta, &tc);

            if (c == 127) /* Hack for special wall characters on Windows. See font-win.prf and main-win.c */
                c = '#';

            if (a != current_a)
            {
                if (current_a >= 0 && current_a != TERM_WHITE)
                {
                    string_append_s(s, "</color>");
                }
                if (a != TERM_WHITE)
                {
                    string_printf(s, "<color:%c>", attr_to_attr_char(a));
                }
                current_a = a;
            }
            string_append_c(s, c);
        }
        if (current_a >= 0 && current_a != TERM_WHITE)
            string_append_s(s, "</color>");
        string_append_c(s, '\n');
    }
    if (old_use_graphics)
    {
        use_graphics = TRUE;
        reset_visuals();
    }
    return s;
}

/* Note: This will not work if the screen is "icky" */
string_ptr get_screenshot(void)
{
    string_ptr s = string_alloc_size(80 * 27);
    bool       old_use_graphics = use_graphics;
    int        wid, hgt, x, y;

    Term_get_size(&wid, &hgt);

    if (old_use_graphics)
    {
        use_graphics = FALSE;
        reset_visuals();

        p_ptr->redraw |= (PR_WIPE | PR_BASIC | PR_EXTRA | PR_MAP | PR_EQUIPPY | PR_MSG_LINE);
        redraw_stuff();
    }

    for (y = 0; y < hgt; y++)
    {
        int  current_a = -1;
        for (x = 0; x < wid; x++)
        {
            byte a;
            char c;

            Term_what(x, y, &a, &c);

            if (c == 127) /* Hack for special wall characters on Windows. See font-win.prf and main-win.c */
                c = '#';

            if (a != current_a)
            {
                if (current_a >= 0 && current_a != TERM_WHITE)
                {
                    string_append_s(s, "</color>");
                }
                if (a != TERM_WHITE)
                {
                    string_printf(s, "<color:%c>", attr_to_attr_char(a));
                }
                current_a = a;
            }
            string_append_c(s, c);
        }
        if (current_a >= 0 && current_a != TERM_WHITE)
            string_append_s(s, "</color>");
        string_append_c(s, '\n');
    }
    if (old_use_graphics)
    {
        use_graphics = TRUE;
        reset_visuals();

        p_ptr->redraw |= (PR_WIPE | PR_BASIC | PR_EXTRA | PR_MAP | PR_EQUIPPY | PR_MSG_LINE);
        redraw_stuff();
    }
    return s;
}

/*
 * Note something in the message recall
 */
void do_cmd_note(void)
{
    char buf[80];
    string_ptr s = 0;

    /* Default */
    strcpy(buf, "");

    /* Input */
    if (!get_string("Note: ", buf, 60)) return;

    /* Ignore empty notes */
    if (!buf[0] || (buf[0] == ' ')) return;

    /* Add the note to the message recall */
    msg_format("<color:R>Note:</color> %s\n", buf);

    s = get_tiny_screenshot(50, 24);
    msg_add(string_buffer(s));
    string_free(s);
}


/*
 * Mention the current version
 */
void do_cmd_version(void)
{
    cptr xtra = "";
    if (VER_MINOR == 0)
    {
/*        if (VER_PATCH == 0) xtra = " (Alpha)"; */
        if (VER_MAJOR < 1) xtra = " (Beta)";
    }
    msg_format("You are playing <color:B>Ormeq</color> <color:r>%s%s</color> <color:D>(engine %d.%d.%s)</color>.",
        ORMEQ_VER, xtra, VER_MAJOR, VER_MINOR, VER_PATCH);
    if (1)
    {
        rect_t r = ui_map_rect();
        msg_format("Map display is %dx%d.", r.cx, r.cy);
    }
}



/*
 * Array of feeling strings
 */
struct _feeling_info_s
{
    byte color;
    cptr msg;
};
typedef struct _feeling_info_s _feeling_info_t;
static _feeling_info_t _level_feelings[11] =
{
    {TERM_SLATE, "Looks like any other level."},
    {TERM_L_BLUE, "You feel there is something special about this level."},
    {TERM_VIOLET, "You nearly faint as horrible visions of death fill your mind!"},
    {TERM_RED, "This level looks very dangerous."},
    {TERM_L_RED, "You have a very bad feeling..."},
    {TERM_ORANGE, "You have a bad feeling..."},
    {TERM_YELLOW, "You feel nervous."},
    {TERM_L_UMBER, "You feel your luck is turning..."},
    {TERM_L_WHITE, "You don't like the look of this place."},
    {TERM_WHITE, "This level looks reasonably safe."},
    {TERM_WHITE, "What a boring place..."},
};

static _feeling_info_t _level_feelings_lucky[11] =
{
    {TERM_SLATE, "Looks like any other level."},
    {TERM_L_BLUE, "You feel there is something special about this level."},
    {TERM_VIOLET, "You have a superb feeling about this level."},
    {TERM_RED, "You have an excellent feeling..."},
    {TERM_L_RED, "You have a very good feeling..."},
    {TERM_ORANGE, "You have a good feeling..."},
    {TERM_YELLOW, "You feel strangely lucky..."},
    {TERM_L_UMBER, "You feel your luck is turning..."},
    {TERM_L_WHITE, "You like the look of this place..."},
    {TERM_WHITE, "This level can't be all bad..."},
    {TERM_WHITE, "What a boring place..."},
};


/*
 * Note that "feeling" is set to zero unless some time has passed.
 * Note that this is done when the level is GENERATED, not entered.
 */
void do_cmd_feeling(void)
{
    /* No useful feeling in quests */
    if (!quests_allow_feeling())
    {
        msg_print("Looks like a typical quest level.");
    }

    /* No useful feeling in town */
    else if (p_ptr->town_num && !dun_level)
    {
        if (!strcmp(town_name(p_ptr->town_num), "Wilderness"))
        {
            msg_print("Looks like a strange wilderness.");
        }
        else
        {
            msg_print("Looks like a typical town.");
        }
    }

    /* No useful feeling in the wilderness */
    else if (!dun_level)
    {
        msg_print("Looks like a typical wilderness.");
    }

    /* Display the feeling */
    else
    {
        _feeling_info_t feeling;
        assert(/*0 <= p_ptr->feeling &&*/ p_ptr->feeling < 11);
        if (p_ptr->good_luck || p_ptr->pclass == CLASS_ARCHAEOLOGIST)
            feeling = _level_feelings_lucky[p_ptr->feeling];
        else
            feeling = _level_feelings[p_ptr->feeling];
        cmsg_print(feeling.color, feeling.msg);
    }
}



/*
 * Description of each monster group.
 */
static cptr monster_group_text[] =
{
    "Corpses",
    "Uniques",
    "Ridable monsters",
    "Wanted monsters",
    "Dungeon guardians",
    "Crossling",
    "God",
    "Ant",
    "Bat",
    "Centipede",
    "Dragon",
    "Floating Eye",
    "Feline/Fox",
    "Golem",
    "Warrener/Elf/Dwarf",
    "Icky Thing",
    "Jelly",
    "Kobold",
    "Aquatic monster",
    "Mold",
    "Naga",
    "Orc",
    "Person/Human",
    "Quadruped",
    "Rodent",
    "Skeleton",
    "Demon",
    "Vortex",
    "Worm/Worm-Mass",
    /* "unused", */
    "Yeek",
    "Zombie/Mummy",
    "Angel",
    "Bird",
    "Canine",
    /* "Ancient Dragon/Wyrm", */
    "Elemental",
    "Dragon Fly",
    "Ghost",
    "Hybrid",
    "Insect",
    "Snake",
    "Killer Beetle",
    "Lich",
    "Multi-Headed Reptile",
    "Mystery Living",
    "Ogre",
    "Giant Humanoid",
    "Quylthulg",
    "Reptile/Amphibian",
    "Spider/Scorpion/Tick",
    "Troll",
    /* "Major Demon", */
    "Vampire",
    "Wight/Wraith/etc",
    "Vein grazer/Plated vein grazer/etc",
    "Yeti",
    "Zephyr Hound",
    "Mimic",
    "Wall/Plant/Gas",
    "Mushroom patch",
    "Ball",
    "Player",
    NULL
};


/*
 * Symbols of monsters in each group. Note the "Uniques" group
 * is handled differently.
 */
static cptr monster_group_char[] =
{
    (char *) -1L,
    (char *) -2L,
    (char *) -3L,
    (char *) -4L,
    (char *) -5L,
    (char *) -6L,
    (char *) -7L,
    "a",
    "b",
    "c",
    "dD",
    "e",
    "f",
    "g",
    "h",
    "i",
    "j",
    "k",
    "l",
    "m",
    "n",
    "o",
    "pt",
    "q",
    "r",
    "s",
    "uU",
    "v",
    "w",
    /* "x", */
    "y",
    "z",
    "A",
    "B",
    "C",
    /* "D", */
    "E",
    "F",
    "G",
    "H",
    "I",
    "J",
    "K",
    "L",
    "M",
    "N",
    "O",
    "P",
    "Q",
    "R",
    "S",
    "T",
    /* "U", */
    "V",
    "W",
    "X",
    "Y",
    "Z",
    "!$&()+./=>?[\\]`{|~x",
    "#%",
    ",",
    "*",
    "@",
    NULL
};


/*
 * hook function to sort monsters by level
 */
static bool ang_sort_comp_monster_level(vptr u, vptr v, int a, int b)
{
    u16b *who = (u16b*)(u);

    int w1 = who[a];
    int w2 = who[b];

    monster_race *r_ptr1 = &r_info[w1];
    monster_race *r_ptr2 = &r_info[w2];

    /* Unused */
    (void)v;

    if (r_ptr2->level > r_ptr1->level) return FALSE;
    if (r_ptr1->level > r_ptr2->level) return TRUE;

    if ((r_ptr2->flags1 & RF1_UNIQUE) && !(r_ptr1->flags1 & RF1_UNIQUE)) return TRUE;
    if ((r_ptr1->flags1 & RF1_UNIQUE) && !(r_ptr2->flags1 & RF1_UNIQUE)) return FALSE;
    return w1 <= w2;
}

/*
 * Build a list of monster indexes in the given group. Return the number
 * of monsters in the group.
 *
 * mode & 0x01 : check for non-empty group
 * mode & 0x02 : visual operation only
 */
static int collect_monsters(int grp_cur, s16b mon_idx[], byte mode)
{
    int i, mon_cnt = 0;
    int dummy_why;

    /* Get a list of x_char in this group */
    cptr group_char = monster_group_char[grp_cur];

    /* XXX Hack -- Check for special groups */
    bool        grp_corpses = (monster_group_char[grp_cur] == (char *) -1L);
    bool        grp_unique = (monster_group_char[grp_cur] == (char *) -2L);
    bool        grp_riding = (monster_group_char[grp_cur] == (char *) -3L);
    bool        grp_wanted = (monster_group_char[grp_cur] == (char *) -4L);
    bool        grp_guardian = (monster_group_char[grp_cur] == (char *) -5L);
    bool        grp_crossling = (monster_group_char[grp_cur] == (char *) -6L);
    bool        grp_god = (monster_group_char[grp_cur] == (char *) -7L);
    int_map_ptr available_corpses = NULL;

    if (grp_corpses)
    {
        available_corpses = int_map_alloc(NULL);

        /* In Pack */
        for (i = 1; i <= pack_max(); i++)
        {
            object_type *o_ptr = pack_obj(i);
            if (!o_ptr) continue;
            if (!object_is_(o_ptr, TV_CORPSE, SV_CORPSE)) continue;
            int_map_add(available_corpses, o_ptr->pval, NULL);
        }

        /* At Home */
        for (i = 1; i <= home_max(); i++)
        {
            object_type *o_ptr = home_obj(i);
            if (!o_ptr) continue;
            if (!object_is_(o_ptr, TV_CORPSE, SV_CORPSE)) continue;
            int_map_add(available_corpses, o_ptr->pval, NULL);
        }

        /* Underfoot */
        if (in_bounds2(py, px))
        {
            cave_type  *c_ptr = &cave[py][px];
            s16b        o_idx = c_ptr->o_idx;

            while (o_idx)
            {
                object_type *o_ptr = &o_list[o_idx];

                if (object_is_(o_ptr, TV_CORPSE, SV_CORPSE))
                    int_map_add(available_corpses, o_ptr->pval, NULL);

                o_idx = o_ptr->next_o_idx;
            }
        }

        /* Current Form for Easier Comparisons */
        if (p_ptr->prace == RACE_MON_POSSESSOR && p_ptr->current_r_idx != MON_POSSESSOR_SOUL)
            int_map_add(available_corpses, p_ptr->current_r_idx, NULL);

    }


    /* Check every race */
    for (i = 1; i < max_r_idx; i++)
    {
        /* Access the race */
        monster_race *r_ptr = &r_info[i];

        /* Skip empty race */
        if (!r_ptr->name) continue;
        if (!p_ptr->wizard && (r_ptr->flagsx & RFX_SUPPRESS)) continue;

        /* Require known monsters */
        if (!(mode & 0x02) && !easy_lore && !r_ptr->r_sights) continue;

        if (grp_corpses)
        {
            if (!int_map_contains(available_corpses, i))
                continue;
        }

        else if (grp_unique)
        {
            if (!(r_ptr->flags1 & RF1_UNIQUE)) continue;
        }

        else if (grp_riding)
        {
            if (!(r_ptr->flags7 & RF7_RIDING)) continue;
        }

        else if (grp_wanted)
        {
            bool wanted = FALSE;
            int j;
            for (j = 0; j < MAX_KUBI; j++)
            {
                if (kubi_r_idx[j] == i || kubi_r_idx[j] - 10000 == i ||
                    (p_ptr->today_mon && p_ptr->today_mon == i))
                {
                    wanted = TRUE;
                    break;
                }
            }
            if (!wanted) continue;
        }

        else if (grp_crossling)
        {
            if (!(r_ptr->flags3 & RF3_AMBERITE)) continue;
        }

        else if (grp_god)
        {
            if (!(r_ptr->flags1 & RF1_UNIQUE)) continue;
            if (!monster_pantheon(r_ptr)) continue;
        }

        else if (grp_guardian)
        {
            if (!(r_ptr->flags7 & RF7_GUARDIAN)) continue;
            if ((d_info[DUNGEON_MYSTERY].final_guardian == i) &&
                (!(d_info[DUNGEON_MYSTERY].flags1 & DF1_SUPPRESSED)) &&
                (d_info[DUNGEON_MYSTERY].maxdepth > max_dlv[DUNGEON_MYSTERY])) continue;
        }

        else
        {
            /* Check for race in the group */
            if (!my_strchr(group_char, r_ptr->d_char)) continue;
        }

        /* Add the race */
        mon_idx[mon_cnt++] = i;

        /* XXX Hack -- Just checking for non-empty group */
        if (mode & 0x01) break;
    }

    /* Terminate the list */
    mon_idx[mon_cnt] = -1;

    /* Select the sort method */
    ang_sort_comp = ang_sort_comp_monster_level;
    ang_sort_swap = ang_sort_swap_hook;

    /* Sort by monster level */
    ang_sort(mon_idx, &dummy_why, mon_cnt);

    if (grp_corpses)
        int_map_free(available_corpses);

    /* Return the number of races */
    return mon_cnt;
}


/*
 * Description of each object group.
 */
static cptr object_group_text[] =
{
    "Food",
    "Potions",
/*  "Flasks", */
    "Scrolls",
/*  "Rings",
    "Amulets", */
/*  "Whistle",
    "Lanterns", */
/*  "Wands",
    "Staves",
    "Rods", */
/*  "Cards",
    "Capture Balls",
    "Parchments",
    "Spikes",
    "Boxs",
    "Figurines",
    "Statues",
    "Junks",
    "Bottles",
    "Skeletons",
    "Corpses", */
    "Swords",
    "Blunt Weapons",
    "Polearms",
    "Diggers",
    "Bows",
    "Shots",
    "Arrows",
    "Bolts",
    "Soft Armor",
    "Hard Armor",
    "Dragon Armor",
    "Shields",
    "Cloaks",
    "Gloves",
    "Helms",
    "Crowns",
    "Boots",
    "Spellbooks",
/*  "Treasure", */
    "Something",
    NULL
};


/*
 * TVALs of items in each group
 */
static byte object_group_tval[] =
{
    TV_FOOD,
    TV_POTION,
/*  TV_FLASK, */
    TV_SCROLL,
/*  TV_RING,
    TV_AMULET, */
/*  TV_WHISTLE,
    TV_LITE, */
/*  TV_WAND,
    TV_STAFF,
    TV_ROD,  */
/*  TV_CARD,
    TV_CAPTURE,
    TV_PARCHMENT,
    TV_SPIKE,
    TV_CHEST,
    TV_FIGURINE,
    TV_STATUE,
    TV_JUNK,
    TV_BOTTLE,
    TV_SKELETON,
    TV_CORPSE, */
    TV_SWORD,
    TV_HAFTED,
    TV_POLEARM,
    TV_DIGGING,
    TV_BOW,
    TV_SHOT,
    TV_ARROW,
    TV_BOLT,
    TV_SOFT_ARMOR,
    TV_HARD_ARMOR,
    TV_DRAG_ARMOR,
    TV_SHIELD,
    TV_CLOAK,
    TV_GLOVES,
    TV_HELM,
    TV_CROWN,
    TV_BOOTS,
    TV_LIFE_BOOK, /* Hack -- all spellbooks */
/*  TV_GOLD, */
    0,
    0,
};

static bool _compare_k_level(vptr u, vptr v, int a, int b)
{
    int *indices = (int*)u;
    int left = indices[a];
    int right = indices[b];
    return k_info[left].level <= k_info[right].level;
}

static void _swap_int(vptr u, vptr v, int a, int b)
{
    int *indices = (int*)u;
    int tmp = indices[a];
    indices[a] = indices[b];
    indices[b] = tmp;
}

/*
 * Build a list of object indexes in the given group. Return the number
 * of objects in the group.
 *
 * mode & 0x01 : check for non-empty group
 * mode & 0x02 : visual operation only
 */
static int collect_objects(int grp_cur, int object_idx[], byte mode)
{
    int i, j, k, object_cnt = 0;

    /* Get a list of x_char in this group */
    byte group_tval = object_group_tval[grp_cur];

    /* Check every object */
    for (i = 0; i < max_k_idx; i++)
    {
        /* Access the object */
        object_kind *k_ptr = &k_info[i];

        /* Skip empty objects */
        if (!k_ptr->name) continue;

        if (mode & 0x02)
        {
            /* Any objects will be displayed */
        }
        else
        {
            if (!k_ptr->flavor)
            {
                if (!k_ptr->counts.found && !k_ptr->counts.bought) continue;
            }

            /* Require objects ever seen */
            if (!k_ptr->aware) continue;

            /* Skip items with no distribution (special artifacts) */
            for (j = 0, k = 0; j < 4; j++) k += k_ptr->chance[j];
            if (!k) continue;
        }

        /* Check for objects in the group */
        if (TV_LIFE_BOOK == group_tval)
        {
            /* Hack -- All spell books */
            if (TV_BOOK_BEGIN <= k_ptr->tval && k_ptr->tval <= TV_BOOK_END)
            {
                /* Add the object */
                object_idx[object_cnt++] = i;
            }
            else continue;
        }
        else if (k_ptr->tval == group_tval)
        {
            /* Add the object */
            object_idx[object_cnt++] = i;
        }
        else continue;

        /* XXX Hack -- Just checking for non-empty group */
        if (mode & 0x01) break;
    }

    /* Sort Results */
    ang_sort_comp = _compare_k_level;
    ang_sort_swap = _swap_int;
    ang_sort(object_idx, NULL, object_cnt);

    /* Terminate the list */
    object_idx[object_cnt] = -1;

    /* Return the number of objects */
    return object_cnt;
}


/*
 * Description of each feature group.
 */
static cptr feature_group_text[] =
{
    "terrains",
    NULL
};


void do_cmd_save_screen_doc(void)
{
    string_ptr s = get_screenshot();
    char       buf[1024];
    FILE      *fff;

    path_build(buf, sizeof(buf), SCOTTISH_DIR_USER, "screen.doc");
    FILE_TYPE(FILE_TYPE_TEXT);
    fff = my_fopen(buf, "w");
    if (fff)
    {
        string_write_file(s, fff);
        my_fclose(fff);
    }
    string_free(s);
}

void save_screen_aux(cptr file, int format)
{
    string_ptr s = get_screenshot();
    doc_ptr    doc = doc_alloc(Term->wid);
    FILE      *fff;

    doc_insert(doc, "<style:screenshot>");
    doc_insert(doc, string_buffer(s));
    doc_insert(doc, "</style>");

    FILE_TYPE(FILE_TYPE_TEXT);
    fff = my_fopen(file, "w");
    if (fff)
    {
        doc_write_file(doc, fff, format);
        my_fclose(fff);
    }
    string_free(s);
    doc_free(doc);
}

static void _save_screen_aux(int format)
{
    char buf[1024];

    if (format == DOC_FORMAT_HTML)
        path_build(buf, sizeof(buf), SCOTTISH_DIR_USER, "screen.html");
    else
        path_build(buf, sizeof(buf), SCOTTISH_DIR_USER, "screen.txt");

    save_screen_aux(buf, format);
}

void do_cmd_save_screen_txt(void)
{
    _save_screen_aux(DOC_FORMAT_TEXT);
}

void do_cmd_save_screen_html(void)
{
    _save_screen_aux(DOC_FORMAT_HTML);
}

void do_cmd_save_screen(void)
{
    string_ptr s = get_screenshot();
    doc_ptr    doc = doc_alloc(Term->wid);

    doc_insert(doc, "<style:screenshot>");
    doc_insert(doc, string_buffer(s));
    doc_insert(doc, "</style>");
    screen_save();
    doc_display(doc, "Current Screenshot", 0);
    screen_load();

    string_free(s);
    doc_free(doc);
}

/************************************************************************
 * Artifact Lore (Standard Arts Only)
 * Note: Check out the Wizard Spoiler Commands for an alternative approach.
 *       ^a"a and ^a"O
 ************************************************************************/
typedef struct {
    object_p filter;
    cptr     name;
} _art_type_t;

static _art_type_t _art_types[] = {
    { object_is_melee_weapon, "Weapons" },
    { object_is_shield, "Shield" },
    { object_is_bow, "Bows" },
    { object_is_ring, "Rings" },
    { object_is_amulet, "Amulets" },
    { object_is_lite, "Lights" },
    { object_is_body_armour, "Body Armor" },
    { object_is_cloak, "Cloaks" },
    { object_is_helmet, "Helmets" },
    { object_is_gloves, "Gloves" },
    { object_is_boots, "Boots" },
    { object_is_ammo, "Ammo" },
    { NULL, NULL },
};

static bool _compare_a_level(vptr u, vptr v, int a, int b)
{
    int *indices = (int*)u;
    int left = indices[a];
    int right = indices[b];
    return a_info[left].level <= a_info[right].level;
}

static int _collect_arts(int grp_cur, int art_idx[], bool show_all)
{
    int i, cnt = 0;

    for (i = 0; i < max_a_idx; i++)
    {
        artifact_type *a_ptr = &a_info[i];
        object_type    forge;

        if (!a_ptr->name) continue;
        if (!a_ptr->found)
        {
            if (!show_all) continue;
            /*if (!a_ptr->generated) continue;*/
            if (!art_has_lore(a_ptr)) continue;
        }
        if (!create_named_art_aux_aux(i, &forge)) continue;
        if (!_art_types[grp_cur].filter(&forge)) continue;

        art_idx[cnt++] = i;
    }

    /* Sort Results */
    ang_sort_comp = _compare_a_level;
    ang_sort_swap = _swap_int;
    ang_sort(art_idx, NULL, cnt);

    /* Terminate the list */
    art_idx[cnt] = -1;

    return cnt;
}


/*
 * Display known uniques
 * With "XTRA HACK UNIQHIST" (Originally from XAngband)
 */
/*
 * Four screens that used to go out through a temporary file.
 *
 * Each of them built its page with fprintf() into a file in the temp
 * directory, closed it, handed the path to show_file() -- the old pager -- and
 * then deleted it. That is how you displayed a list in 1995 when the terminal
 * could not scroll and the program could not hold the text.
 *
 * They build a document now. No file, no round trip through the disk, and no
 * dependency on the pager -- and since a document knows about colour, a
 * creature can be printed in the colour it is actually drawn in, which is the
 * one thing a player is already using to tell them apart.
 */

/* A creature's name, in the colour it is drawn in on the map. */
static void _mon_name(doc_ptr doc, monster_race *r_ptr)
{
    doc_printf(doc, "<color:%c>%s</color>",
               attr_to_attr_char(r_ptr->d_attr), r_name + r_ptr->name);
}

static void do_cmd_knowledge_pets(void)
{
    doc_ptr doc = doc_alloc(MIN(72, ui_map_rect().cx));
    int     i, ct = 0;

    doc_insert(doc, "<color:U>Who is following you</color>\n");
    doc_add_rule(doc);
    doc_newline(doc);

    /* Backwards, because that is the order they were taken in. */
    for (i = m_max - 1; i >= 1; i--)
    {
        monster_type *m_ptr = &m_list[i];
        monster_race *r_ptr = &r_info[m_ptr->r_idx];
        char          name[80];

        if (!m_ptr->r_idx) continue;
        if (!is_pet(m_ptr)) continue;

        ct++;
        monster_desc(name, m_ptr, MD_ASSUME_VISIBLE | MD_INDEF_VISIBLE);
        doc_printf(doc, "  <color:%c>%-40.40s</color>",
                   attr_to_attr_char(r_ptr->d_attr), name);
        if (r_ptr->r_tkills)
            doc_printf(doc, " <color:D>level %d</color>", r_ptr->level);
        doc_printf(doc, "  %s\n", mon_health_desc(m_ptr));
    }

    if (!ct)
        doc_insert(doc, "  <color:D>Nobody. You are travelling alone.</color>\n");
    else
    {
        doc_newline(doc);
        doc_add_rule(doc);
        doc_printf(doc, "  <color:W>%d</color> <color:D>pet%s, costing you"
                        "</color> <color:W>%d%%</color> <color:D>of your"
                        " mana.</color>\n",
                   ct, ct == 1 ? "" : "s", calculate_upkeep());
    }

    doc_display(doc, "Pets", 0);
    doc_free(doc);
}

static void do_cmd_knowledge_kubi(void)
{
    doc_ptr doc = doc_alloc(MIN(72, ui_map_rect().cx));
    int     i, ct = 0;

    doc_insert(doc, "<color:U>Who is worth money</color>\n");
    doc_add_rule(doc);
    doc_newline(doc);

    doc_insert(doc, "  <color:D>Today they are paying for</color> ");
    if (p_ptr->today_mon)
        _mon_name(doc, &r_info[p_ptr->today_mon]);
    else
        doc_insert(doc, "<color:D>nothing in particular</color>");
    doc_insert(doc, "<color:D>.</color>\n\n");

    doc_insert(doc, "<style:heading>Standing bounties</style>\n");
    for (i = 0; i < MAX_KUBI; i++)
    {
        int id = kubi_r_idx[i];

        if (id <= 0 || id >= 10000) continue;
        ct++;
        doc_insert(doc, "  ");
        _mon_name(doc, &r_info[id]);
        doc_newline(doc);
    }

    if (!ct)
        doc_insert(doc, "  <color:G>You have turned in every one of them."
                        "</color>\n");

    doc_display(doc, "Wanted monsters", 0);
    doc_free(doc);
}

static void do_cmd_knowledge_kill_count(void)
{
    doc_ptr doc = doc_alloc(MIN(76, ui_map_rect().cx));
    doc_ptr cols[2];
    s16b   *who;
    u16b    why = 2;
    int     i, k, n = 0;
    s32b    total = 0;

    C_MAKE(who, max_r_idx, s16b);

    for (i = 1; i < max_r_idx; i++)
    {
        monster_race *r_ptr = &r_info[i];

        if (r_ptr->flags1 & RF1_UNIQUE) { if (!r_ptr->max_num) total++; }
        else if (r_ptr->r_pkills > 0)   total += r_ptr->r_pkills;

        if (r_ptr->name) who[n++] = i;
    }

    doc_insert(doc, "<color:U>What you have accounted for</color>\n");
    doc_add_rule(doc);
    doc_newline(doc);

    if (!total)
    {
        doc_insert(doc, "  <color:D>Nothing yet.</color>\n");
        C_KILL(who, max_r_idx, s16b);
        doc_display(doc, "Kill count", 0);
        doc_free(doc);
        return;
    }

    doc_printf(doc, "  <color:W>%d</color> <color:D>%s, in the order you would"
                    " have met them.</color>\n\n",
               total, total == 1 ? "creature" : "creatures");

    ang_sort_comp = ang_sort_comp_hook;
    ang_sort_swap = ang_sort_swap_hook;
    ang_sort(who, &why, n);

    /* Two columns: the list is long and the screen is wide. */
    cols[0] = doc_alloc((doc_width(doc) - 2) / 2);
    cols[1] = doc_alloc(doc_width(doc) - 2 - doc_width(cols[0]));

    total = 0;
    for (k = 0; k < n; k++)
    {
        monster_race *r_ptr = &r_info[who[k]];
        doc_ptr       col;

        if (r_ptr->flags1 & RF1_UNIQUE)
        {
            if (r_ptr->max_num) continue;
            col = cols[total % 2];
            doc_insert(col, "  ");
            _mon_name(col, r_ptr);
            doc_newline(col);
            total++;
        }
        else if (r_ptr->r_pkills > 0)
        {
            s16b ct = r_ptr->r_pkills;
            char name[80];

            col = cols[total % 2];
            if (ct < 2)
            {
                doc_printf(col, "  <color:D>%s</color> ",
                           my_strstr(r_name + r_ptr->name, "coins")
                             ? "1 pile of" : "1");
                _mon_name(col, r_ptr);
            }
            else
            {
                strcpy(name, r_name + r_ptr->name);
                plural_aux(name);
                doc_printf(col, "  <color:W>%d</color> <color:%c>%s</color>",
                           ct, attr_to_attr_char(r_ptr->d_attr), name);
            }
            doc_newline(col);
            total += ct;
        }
    }

    doc_insert_cols(doc, cols, 2, 2);
    doc_free(cols[0]);
    doc_free(cols[1]);

    C_KILL(who, max_r_idx, s16b);
    doc_display(doc, "Kill count", 0);
    doc_free(doc);
}

static void do_cmd_knowledge_uniques(void)
{
    doc_ptr doc = doc_alloc(MIN(76, ui_map_rect().cx));
    doc_ptr cols[2];
    s16b   *who;
    u16b    why = 2;
    int     i, k, n = 0;
    int     band[10], surface = 0, over100 = 0, alive = 0, deepest = -1;

    for (i = 0; i < 10; i++) band[i] = 0;

    C_MAKE(who, max_r_idx, s16b);

    for (i = 1; i < max_r_idx; i++)
    {
        monster_race *r_ptr = &r_info[i];

        if (!r_ptr->name) continue;
        if (!(r_ptr->flags1 & RF1_UNIQUE)) continue;
        if (r_ptr->flagsx & RFX_SUPPRESS) continue;
        if (!easy_lore && !r_ptr->r_sights) continue;
        if (!r_ptr->rarity
         || ((r_ptr->rarity > 100) && !(r_ptr->flagsx & RFX_QUESTOR))) continue;
        if (r_ptr->max_num == 0) continue;

        if (!r_ptr->level) surface++;
        else
        {
            int lev = (r_ptr->level - 1) / 10;
            if (lev < 10) { band[lev]++; if (deepest < lev) deepest = lev; }
            else over100++;
        }
        who[n++] = i;
    }

    doc_insert(doc, "<color:U>Who is still out there</color>\n");
    doc_add_rule(doc);
    doc_newline(doc);

    if (surface) { doc_printf(doc, "  <color:D>%-14.14s</color> <color:W>%3d</color>\n", "On the surface", surface); alive += surface; }
    for (i = 0; i <= deepest; i++)
    {
        doc_printf(doc, "  <color:D>%4d to %4d ft</color> <color:W>%3d</color>\n",
                   (1 + i * 10) * 50, (10 + i * 10) * 50, band[i]);
        alive += band[i];
    }
    if (over100) { doc_printf(doc, "  <color:D>%-14.14s</color> <color:W>%3d</color>\n", "5050 ft and on", over100); alive += over100; }

    if (!alive)
    {
        doc_insert(doc, "  <color:G>None that you know of.</color>\n");
        C_KILL(who, max_r_idx, s16b);
        doc_display(doc, "Remaining uniques", 0);
        doc_free(doc);
        return;
    }

    doc_newline(doc);
    doc_add_rule(doc);
    doc_printf(doc, "  <color:W>%d</color> <color:D>alive that you have"
                    " laid eyes on.</color>\n\n", alive);

    ang_sort_comp = ang_sort_comp_hook;
    ang_sort_swap = ang_sort_swap_hook;
    ang_sort(who, &why, n);

    cols[0] = doc_alloc((doc_width(doc) - 2) / 2);
    cols[1] = doc_alloc(doc_width(doc) - 2 - doc_width(cols[0]));

    for (k = 0; k < n; k++)
    {
        monster_race *r_ptr = &r_info[who[k]];
        doc_ptr       col   = cols[k % 2];

        doc_insert(col, "  ");
        _mon_name(col, r_ptr);
        /* How far down, and whether that is further than you have been. */
        doc_printf(col, " <color:%c>%d ft</color>\n",
                   r_ptr->level * 50 > dun_level * 50 ? 'y' : 'D',
                   r_ptr->level * 50);
    }

    doc_insert_cols(doc, cols, 2, 2);
    doc_free(cols[0]);
    doc_free(cols[1]);

    C_KILL(who, max_r_idx, s16b);
    doc_display(doc, "Remaining uniques", 0);
    doc_free(doc);
}



void do_cmd_knowledge_shooter(void)
{
    doc_ptr doc = doc_alloc(80);

    display_shooter_info(doc);
    if (doc_line_count(doc))
    {
        screen_save();
        doc_display(doc, "Shooting", 0);
        screen_load();
    }
    else
        msg_print("You are not wielding a bow.");

    doc_free(doc);
}

void do_cmd_knowledge_weapon(void)
{
    int i;
    doc_ptr doc = doc_alloc(80);

    for (i = 0; i < MAX_HANDS; i++)
    {
        if (p_ptr->weapon_info[i].wield_how == WIELD_NONE) continue;

        if (p_ptr->weapon_info[i].bare_hands)
            monk_display_attack_info(doc, i);
        else
            display_weapon_info(doc, i);
    }

    for (i = 0; i < p_ptr->innate_attack_ct; i++)
    {
        display_innate_attack_info(doc, i);
    }

    if (doc_line_count(doc))
    {
        screen_save();
        doc_display(doc, "Melee", 0);
        screen_load();
    }
    else
        msg_print("You have no melee attacks.");

    doc_free(doc);
}

void display_weapon_info_aux(int mode)
{
    bool screen_hack = screen_is_saved();
    if (screen_hack) screen_load();

    display_weapon_mode = mode;
    do_cmd_knowledge_weapon();
    display_weapon_mode = 0;

    if (screen_hack) screen_save();
}

static void do_cmd_knowledge_extra(void)
{
    doc_ptr  doc = doc_alloc(80);
    class_t *class_ptr = get_class();
    race_t  *race_ptr = get_race();

    doc_insert(doc, "<style:wide>");

    if (race_ptr->character_dump)
        race_ptr->character_dump(doc);

    if (class_ptr->character_dump)
        class_ptr->character_dump(doc);

    doc_insert(doc, "</style>");

    doc_display(doc, "Race/Class Extra Information", 0);
    doc_free(doc);
}

/*
 * Display weapon-exp.
 */
static int _compare_k_lvl(object_kind *left, object_kind *right)
{
    if (left->level < right->level) return -1;
    if (left->level > right->level) return 1;
    return 0;
}

static vec_ptr _prof_weapon_alloc(int tval)
{
    int i;
    vec_ptr v = vec_alloc(NULL);
    for (i = 0; i < max_k_idx; i++)
    {
        object_kind *k_ptr = &k_info[i];
        if (k_ptr->tval != tval) continue;
        if ((tval == TV_POLEARM) && (k_ptr->sval == (prace_is_(RACE_MON_SWORD) ? SV_DEATH_SCYTHE : SV_DEATH_SCYTHE_HACK))) continue;
        if (tval == TV_BOW && k_ptr->sval == SV_HARP) continue;
        if (tval == TV_BOW && k_ptr->sval == SV_FLUTE) continue;
        if (tval == TV_BOW && k_ptr->sval == SV_CRIMSON) continue;
        if (tval == TV_BOW && k_ptr->sval == SV_RAILGUN) continue;
        vec_add(v, k_ptr);
    }
    vec_sort(v, (vec_cmp_f)_compare_k_lvl);
    return v;
}
 
static cptr _prof_exp_str[5]   = {"[Un]", "[Be]", "[Sk]", "[Ex]", "[Ma]"};
static char _prof_exp_color[5] = {'w',    'G',    'y',    'r',    'v'};
static cptr _prof_weapon_heading(int tval)
{
    switch (tval)
    {
    case TV_SWORD: return "Swords";
    case TV_POLEARM: return "Polearms";
    case TV_HAFTED: return "Hafted";
    case TV_DIGGING: return "Diggers";
    case TV_BOW: return "Bows";
    }
    return "";
}

static void _prof_weapon_doc(doc_ptr doc, int tval, int mode)
{
    vec_ptr v = _prof_weapon_alloc(tval);
    int     i;

    doc_insert_text(doc, TERM_RED, _prof_weapon_heading(tval));
    doc_newline(doc);

    for (i = 0; i < vec_length(v); i++)
    {
        object_kind *k_ptr = vec_get(v, i);
        int          exp = skills_weapon_current(k_ptr->tval, k_ptr->sval);
        int          max = skills_weapon_max(k_ptr->tval, k_ptr->sval);
        int          max_lvl = weapon_exp_level(max);
        int          exp_lvl = weapon_exp_level(exp);
        char         name[MAX_NLEN];

        strip_name(name, k_ptr->idx);
        doc_printf(doc, "<color:%c>%-19s</color> ", equip_find_obj(k_ptr->tval, k_ptr->sval) ? 'B' : 'w', name);
        switch (mode)
        {
            case 1:
                doc_printf(doc, " <color:%c>%-4s</color>", _prof_exp_color[max_lvl], _prof_exp_str[max_lvl]);
                break;
            case 2:
                {
                    s32b pct = 0;
                    int pct_lvl;
                    if (max > 0) pct = ((s32b)exp * 100L) / (s32b)max;
                    pct_lvl = weapon_exp_level((WEAPON_EXP_MASTER / 100) * pct);
                    doc_printf(doc, " <color:%c>%3d%%</color>", _prof_exp_color[pct_lvl], pct);
                    break;
                }
            case 3:
                {
                    s32b pct = ((s32b)exp * 100L) / WEAPON_EXP_MASTER;
                    int pct_lvl = weapon_exp_level((WEAPON_EXP_MASTER / 100) * pct);
                    doc_printf(doc, " <color:%c>%3d%%</color>", _prof_exp_color[pct_lvl], pct);
                    break;
                }
            default:
                doc_printf(doc, "%c<color:%c>%-4s</color>", exp >= max ? '!' : ' ', _prof_exp_color[exp_lvl], _prof_exp_str[exp_lvl]);
                break;
        }
        doc_newline(doc);
    }
    doc_newline(doc);
    vec_free(v);
}

static void _prof_skill_aux(doc_ptr doc, int skill, int mode)
{
    int  exp, max, exp_lvl, max_lvl, pct_lvl;
    cptr name;
    char color = 'w';

    switch (skill)
    {
    case SKILL_MARTIAL_ARTS:
        name = "Martial Arts";
        exp = skills_martial_arts_current();
        max = skills_martial_arts_max();
        max_lvl = weapon_exp_level(max);
        exp_lvl = weapon_exp_level(exp);
        break;
    case SKILL_DUAL_WIELDING:
        name = "Dual Wielding";
        exp = skills_dual_wielding_current();
        max = skills_dual_wielding_max();
        max_lvl = weapon_exp_level(max);
        exp_lvl = weapon_exp_level(exp);
        break;
    case SKILL_RIDING:
    default: /* gcc warnings ... */
        name = "Riding";
        exp = skills_riding_current();
        max = skills_riding_max();
        max_lvl = riding_exp_level(max);
        exp_lvl = riding_exp_level(exp);
        break;
    }
    doc_printf(doc, "<color:%c>%-19s</color> ", color, name);
    switch (mode)
    {
        case 1:
            doc_printf(doc, " <color:%c>%-4s</color>", _prof_exp_color[max_lvl], _prof_exp_str[max_lvl]);
            break;
        case 2:
            {
                s32b pct = 0;
                if (max > 0) pct = ((s32b)exp * 100L) / (s32b)max;
                if (skill == SKILL_RIDING) pct_lvl = riding_exp_level(RIDING_EXP_MASTER / 100 * pct);
                else pct_lvl = weapon_exp_level((WEAPON_EXP_MASTER / 100) * pct);
                doc_printf(doc, " <color:%c>%3d%%</color>", _prof_exp_color[pct_lvl], pct);
                break;
            }
        case 3:
            {
                s32b pct = ((s32b)exp * 100L) / WEAPON_EXP_MASTER;
                if (skill == SKILL_RIDING) pct_lvl = riding_exp_level(RIDING_EXP_MASTER / 100 * pct);
                else pct_lvl = weapon_exp_level((WEAPON_EXP_MASTER / 100) * pct);
                doc_printf(doc, " <color:%c>%3d%%</color>", _prof_exp_color[pct_lvl], pct);
                break;
            }
        default:
            doc_printf(doc, "%c<color:%c>%-4s</color>", exp >= max ? '!' : ' ', _prof_exp_color[exp_lvl], _prof_exp_str[exp_lvl]);
            break;
    }
    doc_newline(doc);
}

static void _prof_skill_doc(doc_ptr doc, int mode)
{
    doc_insert_text(doc, TERM_RED, "Miscellaneous");
    doc_newline(doc);
    _prof_skill_aux(doc, SKILL_MARTIAL_ARTS, mode);
    _prof_skill_aux(doc, SKILL_DUAL_WIELDING, mode);
    _prof_skill_aux(doc, SKILL_RIDING, mode);
    doc_newline(doc);
}

static int _do_cmd_knowledge_weapon_exp_aux(int mode, int *huippu)
{
    doc_ptr doc = doc_alloc(80);
    doc_ptr cols[3] = {0};
    int     i, tulos;

    for (i = 0; i < 3; i++)
        cols[i] = doc_alloc(26);

    _prof_weapon_doc(cols[0], TV_SWORD, mode);
    _prof_weapon_doc(cols[1], TV_POLEARM, mode);
    _prof_weapon_doc(cols[1], TV_BOW, mode);
    _prof_weapon_doc(cols[2], TV_HAFTED, mode);
    _prof_weapon_doc(cols[2], TV_DIGGING, mode);
    _prof_skill_doc(cols[2], mode);

    doc_insert_cols(doc, cols, 3, 1);
    switch (mode)
    {   
        case 1:
        {
            class_t *class_ptr = get_class();
            char buf[64];
            strcpy(buf, class_ptr->name);
            strcat(buf, " Proficiency Caps");
            tulos = weapon_exp_display(doc, buf, huippu); break;
        }
        case 2: tulos = weapon_exp_display(doc, "Current Proficiency as % of Caps", huippu); break;
        case 3: tulos = weapon_exp_display(doc, "Current Proficiency as % of Full Mastery", huippu); break;
        default: tulos = weapon_exp_display(doc, "Current Proficiency", huippu); break;
    }

    doc_free(doc);
    for (i = 0; i < 3; i++)
        doc_free(cols[i]);
    return tulos;
}

static void do_cmd_knowledge_weapon_exp(void)
{
    int mode = 0;
    bool lopeta = FALSE;
    int huippu = 0;

    while (!lopeta)
    {
        if (_do_cmd_knowledge_weapon_exp_aux(mode, &huippu)) mode = ((mode + 1) % 4);
        else lopeta = TRUE;
    } 
}

/*
 * Display spell-exp
 */
static void do_cmd_knowledge_spell_exp(void)
{
    doc_ptr doc = doc_alloc(80);

    doc_insert(doc, "<style:wide>");
    spellbook_character_dump(doc);
    doc_insert(doc, "</style>");
    doc_display(doc, "Spell Proficiency", 0);
    doc_free(doc);
}

/*
 * Pluralize a monster name
 */
static bool _plural_imp(char *name, const char *suffix, const char *replacement)
{
    bool result = FALSE;
    int l1 = strlen(name);
    int l2 = strlen(suffix);

    if (l1 >= l2)
    {
        char *tmp = name + (l1 - l2);
        if (streq(tmp, suffix))
        {
            strcpy(tmp, replacement);
            result = TRUE;
        }
    }
    return result;
}

void plural_aux(char *Name)
{
    int NameLen = strlen(Name);

    if (my_strstr(Name, "Disembodied hand"))
    {
        strcpy(Name, "Disembodied hands that strangled people");
    }
    else if (my_strstr(Name, "Colour out of space"))
    {
        strcpy(Name, "Colours out of space");
    }
    else if (my_strstr(Name, "stairway to hell"))
    {
        strcpy(Name, "stairways to hell");
    }
    else if (my_strstr(Name, "Dweller on the threshold"))
    {
        strcpy(Name, "Dwellers on the threshold");
    }
    else if (my_strstr(Name, " of "))
    {
        cptr aider = my_strstr(Name, " of ");
        char dummy[80];
        int i = 0;
        cptr ctr = Name;

        while (ctr < aider)
        {
            dummy[i] = *ctr;
            ctr++; i++;
        }

        if (dummy[i-1] == 's')
        {
            strcpy(&(dummy[i]), "es");
            i++;
        }
        else
        {
            strcpy(&(dummy[i]), "s");
        }

        strcpy(&(dummy[i+1]), aider);
        strcpy(Name, dummy);
    }
    else if (my_strstr(Name, "coins"))
    {
        char dummy[80];
        strcpy(dummy, "piles of ");
        strcat(dummy, Name);
        strcpy(Name, dummy);
        return;
    }
    else if (my_strstr(Name, "Grave imp"))
    {
        return;
    }
    else if (_plural_imp(Name, "ey", "eys"))
    {
    }
    else if (_plural_imp(Name, "y", "ies"))
    {
    }
    else if (_plural_imp(Name, "ouse", "ice"))
    {
    }
    else if (_plural_imp(Name, "us", "i"))
    {
    }
    else if (_plural_imp(Name, "kelman", "kelmen"))
    {
    }
    else if (_plural_imp(Name, "wordsman", "wordsmen"))
    {
    }
    else if (_plural_imp(Name, "oodsman", "oodsmen"))
    {
    }
    else if (_plural_imp(Name, "eastman", "eastmen"))
    {
    }
    else if (_plural_imp(Name, "izardman", "izardmen"))
    {
    }
    else if (_plural_imp(Name, "geist", "geister"))
    {
    }
    else if (_plural_imp(Name, "ex", "ices"))
    {
    }
    else if (_plural_imp(Name, "lf", "lves"))
    {
    }
    else if (suffix(Name, "ch") ||
         suffix(Name, "sh") ||
             suffix(Name, "nx") ||
             suffix(Name, "s") ||
             suffix(Name, "o"))
    {
        strcpy(&(Name[NameLen]), "es");
    }
    else
    {
        strcpy(&(Name[NameLen]), "s");
    }
}

/*
 * Display current pets
 */



/*
 * Total kill count
 *
 * Note that the player ghosts are ignored. XXX XXX XXX
 */



/*
 * Move the cursor in a browser window
 */
static void browser_cursor(char ch, int *column, int *grp_cur, int grp_cnt,
                           int *list_cur, int list_cnt)
{
    int d;
    int col = *column;
    int grp = *grp_cur;
    int list = *list_cur;

    /* Extract direction */
    if (ch == ' ')
    {
        /* Hack -- scroll up full screen */
        d = 3;
    }
    else if (ch == '-')
    {
        /* Hack -- scroll down full screen */
        d = 9;
    }
    else
    {
        d = get_keymap_dir(ch, FALSE);
    }

    if (!d) return;

    /* Diagonals - hack */
    if ((ddx[d] > 0) && ddy[d])
    {
        int browser_rows;
        int wid, hgt;

        /* Get size */
        Term_get_size(&wid, &hgt);

        browser_rows = hgt - 8;

        /* Browse group list */
        if (!col)
        {
            int old_grp = grp;

            /* Move up or down */
            grp += ddy[d] * (browser_rows - 1);

            /* Verify */
            if (grp >= grp_cnt)    grp = grp_cnt - 1;
            if (grp < 0) grp = 0;
            if (grp != old_grp)    list = 0;
        }

        /* Browse sub-list list */
        else
        {
            /* Move up or down */
            list += ddy[d] * browser_rows;

            /* Verify */
            if (list >= list_cnt) list = list_cnt - 1;
            if (list < 0) list = 0;
        }

        (*grp_cur) = grp;
        (*list_cur) = list;

        return;
    }

    if (ddx[d])
    {
        col += ddx[d];
        if (col < 0) col = 0;
        if (col > 1) col = 1;

        (*column) = col;

        return;
    }

    /* Browse group list */
    if (!col)
    {
        int old_grp = grp;

        /* Move up or down */
        grp += ddy[d];

        /* Verify */
        if (grp >= grp_cnt)    grp = grp_cnt - 1;
        if (grp < 0) grp = 0;
        if (grp != old_grp)    list = 0;
    }

    /* Browse sub-list list */
    else
    {
        /* Move up or down */
        list += ddy[d];

        /* Verify */
        if (list >= list_cnt) list = list_cnt - 1;
        if (list < 0) list = 0;
    }

    (*grp_cur) = grp;
    (*list_cur) = list;
}


/*
 *  Clipboard variables for copy&paste in visual mode
 */
static byte attr_idx = 0;
static byte char_idx = 0;

/* Hack -- for feature lighting */
static byte attr_idx_feat[F_LIT_MAX];
static byte char_idx_feat[F_LIT_MAX];

enum monster_mode_e
{
    MONSTER_MODE_STATS,
    MONSTER_MODE_SKILLS,
    MONSTER_MODE_EXTRA,
    MONSTER_MODE_MAX
};
static int monster_mode = MONSTER_MODE_STATS;

/*
 * Display known monsters.
 */

/*
 * Browsing goes to the new screen; editing, and the "show me this one\n* monster" call the editor makes of itself, go to the old one.
 */
typedef struct {
    u32b id;
    cptr name;
} _ego_type_t;

static _ego_type_t _ego_types[] = {
    { EGO_TYPE_WEAPON, "Weapons" },
    { EGO_TYPE_DIGGER, "Diggers" },

    { EGO_TYPE_SHIELD, "Shields" },
    { EGO_TYPE_BODY_ARMOR, "Body Armor" },
    { EGO_TYPE_ROBE, "Robes" },
    { EGO_TYPE_DRAGON_ARMOR, "Dragon Armor" },
    { EGO_TYPE_CLOAK, "Cloaks" },
    { EGO_TYPE_HELMET, "Helmets" },
    { EGO_TYPE_CROWN, "Crowns" },
    { EGO_TYPE_GLOVES, "Gloves" },
    { EGO_TYPE_BOOTS, "Boots" },

    { EGO_TYPE_BOW, "Bows" },
    { EGO_TYPE_AMMO, "Ammo" },
    { EGO_TYPE_HARP, "Harps" },

    { EGO_TYPE_RING, "Rings" },
    { EGO_TYPE_AMULET, "Amulets" },
    { EGO_TYPE_LITE, "Lights" },
    { EGO_TYPE_DEVICE, "Devices" },

    { EGO_TYPE_NONE, NULL },
};

static bool _compare_e_level(vptr u, vptr v, int a, int b)
{
    int *indices = (int*)u;
    int left = indices[a];
    int right = indices[b];
    return e_info[left].level <= e_info[right].level;
}

static int _collect_egos(int grp_cur, int ego_idx[])
{
    int i, cnt = 0;
    int type = _ego_types[grp_cur].id;

    for (i = 0; i < max_e_idx; i++)
    {
        ego_type *e_ptr = &e_info[i];

        if (!e_ptr->name) continue;
        /*if (!e_ptr->aware) continue;*/
        if (!ego_has_lore(e_ptr) && !e_ptr->counts.found && !e_ptr->counts.bought) continue;
        if (!(e_ptr->type & type)) continue;

        ego_idx[cnt++] = i;
    }

    /* Sort Results */
    ang_sort_comp = _compare_e_level;
    ang_sort_swap = _swap_int;
    ang_sort(ego_idx, NULL, cnt);

    /* Terminate the list */
    ego_idx[cnt] = -1;

    return cnt;
}

/*
 * One browser, five screens.
 *
 * Creatures, artefacts, egos, objects and terrain each had this screen written
 * out separately: a column of groups, a column of things in the selected
 * group, and a key you pressed to go and read about one of them somewhere
 * else. Five copies of the same scrolling arithmetic, five slightly different
 * sets of keys, and in all five the thing you actually came to read was one
 * keypress away and took the list off the screen when you asked for it.
 *
 * This is that screen, once. The third column is the page you used to have to
 * ask for, and it follows the cursor. A caller supplies two lists and a
 * function that writes the third column; scrolling, colour and the arrow keys
 * come with the driver.
 *
 * The tile editors are not part of this. They used to be the same functions
 * with a flag threaded through every branch, which is why those functions were
 * unreadable. They are separate now, still reached by '%', and they can go on
 * being what they are: a tool for somebody editing a tileset.
 */
typedef struct _browse_s _browse_t, *_browse_ptr;

struct _browse_s
{
    cptr  title;      /* names the screen and heads the middle column */
    cptr  what;       /* plural, for "You have not found any ___ yet" */
    int   grp_w;      /* group column; 0 for a browser with one group */
    int   item_w;     /* name column. The rest goes to the detail pane. */

    int   grp_cnt;
    int   grp_idx[100];

    cptr (*grp_name)(_browse_ptr b, int g);
    int  (*collect)(_browse_ptr b, int g, int *idx);
    void (*item_row)(_browse_ptr b, doc_ptr doc, int idx, bool sel);
    void (*detail)(_browse_ptr b, doc_ptr doc, int idx);
    void (*footer)(_browse_ptr b, doc_ptr doc, int *idx, int cnt);

    /* Extra keys. Return TRUE to rebuild the item list. */
    bool (*key)(_browse_ptr b, int cmd, int idx);
    cptr  help;

    int  *idx;        /* item list, allocated by the driver */
    int   idx_max;
    void *aux;        /* whatever the browser needs and the driver does not */
    bool  opt;        /* one toggle, meaning whatever the browser says */

    int   grp_cur, item_cur;
};

static void _browse(_browse_ptr b)
{
    doc_ptr grp_doc = NULL, item_doc, det_doc;
    int     grp_top = 0, item_top = 0, item_cnt = 0;
    int     wid, hgt, rows, list_rows, det_w, item_x;
    bool    done = FALSE, regroup = TRUE;

    if (!b->grp_cnt)
    {
        msg_format("You have not found any %s yet.", b->what);
        return;
    }

    Term_get_size(&wid, &hgt);
    rows   = hgt - 6;
    /* The totals line, where there is one, comes out of the list rather than
     * off the bottom of the screen. */
    list_rows = rows - (b->footer ? 2 : 0);
    item_x = b->grp_w ? b->grp_w + 2 : 0;
    det_w  = wid - item_x - b->item_w - 2;
    if (det_w < 40) det_w = 40;

    if (b->grp_w) grp_doc = doc_alloc(b->grp_w);
    item_doc = doc_alloc(b->item_w);
    det_doc  = doc_alloc(det_w);

    C_MAKE(b->idx, b->idx_max, int);
    b->grp_cur = b->item_cur = 0;

    screen_save();
    ui_screen_layer_push(UI_LAYER_MENU);

    while (!done)
    {
        int cmd, i;

        if (regroup)
        {
            item_cnt = b->collect(b, b->grp_cur, b->idx);
            b->item_cur = item_top = 0;
            regroup  = FALSE;
        }

        /* Keep both cursors on screen. */
        if (b->grp_cur < grp_top) grp_top = b->grp_cur;
        if (b->grp_cur >= grp_top + rows) grp_top = b->grp_cur - rows + 1;
        if (b->item_cur < item_top) item_top = b->item_cur;
        if (b->item_cur >= item_top + list_rows)
            item_top = b->item_cur - list_rows + 1;

        if (grp_doc) doc_clear(grp_doc);
        doc_clear(item_doc);
        doc_clear(det_doc);

        if (grp_doc)
        {
            doc_insert(grp_doc, "<color:U>Groups</color>\n\n");
            for (i = grp_top; i < b->grp_cnt && i < grp_top + rows; i++)
            {
                bool sel = (i == b->grp_cur);
                doc_printf(grp_doc, "%s<color:%c>%-*.*s</color>\n",
                           sel ? "<color:B>></color>" : " ",
                           sel ? 'B' : 'w',
                           b->grp_w - 2, b->grp_w - 2, b->grp_name(b, i));
            }
        }

        doc_printf(item_doc, "<color:U>%s</color>\n\n", b->title);
        if (!item_cnt)
            doc_insert(item_doc, " <color:D>None.</color>\n");
        for (i = item_top; i < item_cnt && i < item_top + list_rows; i++)
            b->item_row(b, item_doc, b->idx[i], i == b->item_cur);
        if (b->footer && item_cnt)
            b->footer(b, item_doc, b->idx, item_cnt);

        if (item_cnt)
            b->detail(b, det_doc, b->idx[b->item_cur]);

        Term_clear();
        prt(format("%s%s%s", b->title,
                   b->grp_w ? " - " : "",
                   b->grp_w ? b->grp_name(b, b->grp_cur) : ""), 0, 0);

        /* The knowledge menu leaves a panel behind and it bleeds through. */
        ui_event_panel_screen();

        /*
         * The detail is synced last. A document carrying hotspots emits a
         * menu-clear when it is synced, and the menu-clear takes the portrait
         * with it -- so anything that might draw one goes on the screen after
         * everything that might clear it.
         */
        if (grp_doc)
            doc_sync_term(grp_doc, doc_range_all(grp_doc), doc_pos_create(0, 2));
        doc_sync_term(item_doc, doc_range_all(item_doc),
                      doc_pos_create(item_x, 2));
        doc_sync_term(det_doc, doc_range_all(det_doc),
                      doc_pos_create(item_x + b->item_w + 2, 2));

        prt(b->help ? b->help : "Arrows to move, ESC to leave.", hgt - 1, 0);
        Term_fresh();

        cmd = inkey_special(TRUE);

        switch (cmd)
        {
        case ESCAPE: case 'q': case 'Q':
            done = TRUE;
            break;

        case SKEY_LEFT: case '4':
            if (b->grp_cur > 0) { b->grp_cur--; regroup = TRUE; }
            break;

        case SKEY_RIGHT: case '6':
            if (b->grp_cur < b->grp_cnt - 1) { b->grp_cur++; regroup = TRUE; }
            break;

        case SKEY_UP: case '8':
            if (b->item_cur > 0) b->item_cur--;
            break;

        case SKEY_DOWN: case '2':
            if (b->item_cur < item_cnt - 1) b->item_cur++;
            break;

        case SKEY_PGUP: case ' ':
            b->item_cur -= list_rows - 1;
            if (b->item_cur < 0) b->item_cur = 0;
            break;

        case SKEY_PGDOWN:
            b->item_cur += list_rows - 1;
            if (b->item_cur > item_cnt - 1) b->item_cur = item_cnt - 1;
            break;

        case SKEY_TOP: case SKEY_BOTTOM:
            b->item_cur = (cmd == SKEY_TOP) ? 0 : item_cnt - 1;
            break;

        default:
            if (b->key && b->key(b, cmd, item_cnt ? b->idx[b->item_cur] : -1))
                regroup = TRUE;
            break;
        }
    }

    ui_screen_layer_pop(UI_LAYER_MENU);
    screen_load();

    if (grp_doc) doc_free(grp_doc);
    doc_free(item_doc);
    doc_free(det_doc);
    C_KILL(b->idx, b->idx_max, int);
}

/*
 * A row of counts, for the browsers that keep them. Zero is printed dark so
 * the eye skips it -- on most of these lists most of the numbers are zero,
 * and the few that are not are the whole point of the column.
 */
static void _count(doc_ptr doc, cptr label, int n)
{
    doc_printf(doc, "  <color:D>%-10s</color> <color:%c>%d</color>\n",
               label, n ? 'W' : 'D', n);
}

/************************************************************************
 * Creatures
 ************************************************************************/
static cptr _mon_grp_name(_browse_ptr b, int g)
{
    return monster_group_text[b->grp_idx[g]];
}

static int _mon_collect(_browse_ptr b, int g, int *idx)
{
    s16b *tmp = (s16b *)b->aux;
    int   n = collect_monsters(b->grp_idx[g], tmp, 0x00);
    int   i;

    for (i = 0; i < n; i++) idx[i] = tmp[i];
    return n;
}

static void _mon_row(_browse_ptr b, doc_ptr doc, int idx, bool sel)
{
    monster_race *r_ptr = &r_info[idx];

    doc_printf(doc, "%s<color:%c>%c</color> <color:%c>%-*.*s</color>",
               sel ? "<color:B>></color>" : " ",
               attr_to_attr_char(r_ptr->d_attr), r_ptr->d_char,
               sel ? 'B' : attr_to_attr_char(r_ptr->d_attr),
               b->item_w - 4, b->item_w - 4, r_name + r_ptr->name);
    /* A unique that is dead is worth a mark, because that one is permanent. */
    if ((r_ptr->flags1 & RF1_UNIQUE) && !r_ptr->max_num)
        doc_insert(doc, "<color:D>+</color>");
    doc_newline(doc);
}

static void _mon_detail(_browse_ptr b, doc_ptr doc, int idx)
{
    mon_display_doc_card(&r_info[idx], doc);
}

static bool _mon_key(_browse_ptr b, int cmd, int idx)
{
    /* The body pane: stats, skills, the rest. Only of use to somebody who is
     * shopping for a body to wear. */
    if (cmd == '=' && idx >= 0
     && (p_ptr->wizard || p_ptr->prace == RACE_MON_POSSESSOR
      || p_ptr->prace == RACE_MON_MIMIC))
    {
        doc_ptr d = doc_alloc(80);
        mon_display_possessor(&r_info[idx], d);
        doc_display(d, "Body", 0);
        doc_free(d);
    }
    return FALSE;
}

static void _browse_monsters(void)
{
    _browse_t b = {0};
    s16b     *scratch;
    int       i;

    C_MAKE(scratch, max_r_idx, s16b);

    b.title    = "Creatures";
    b.what     = "creatures";
    b.grp_w    = 16;
    /* Twenty-four, and not one more: the recall card will not draw a portrait
     * under seventy-two columns and there are only so many to share out. */
    b.item_w   = 24;
    b.idx_max  = max_r_idx;
    b.aux      = scratch;
    b.grp_name = _mon_grp_name;
    b.collect  = _mon_collect;
    b.item_row = _mon_row;
    b.detail   = _mon_detail;
    b.key      = _mon_key;

    for (i = 0; monster_group_text[i] != NULL; i++)
    {
        if (monster_group_char[i] == ((char *)-1L)
         && p_ptr->prace != RACE_MON_POSSESSOR) continue;

        if ((monster_group_char[i] == ((char *)-2L))
         || collect_monsters(i, scratch, 0x01))
            b.grp_idx[b.grp_cnt++] = i;

        if (b.grp_cnt >= 99) break;
    }

    _browse(&b);
    C_KILL(scratch, max_r_idx, s16b);
}

/************************************************************************
 * Artefacts
 ************************************************************************/
static void _art_forge(int idx, object_type *forge)
{
    create_named_art_aux_aux(idx, forge);
    forge->ident = IDENT_KNOWN;
}

static cptr _art_grp_name(_browse_ptr b, int g)
{
    return _art_types[b->grp_idx[g]].name;
}

static int _art_collect(_browse_ptr b, int g, int *idx)
{
    return _collect_arts(b->grp_idx[g], idx, b->opt);
}

static void _art_row(_browse_ptr b, doc_ptr doc, int idx, bool sel)
{
    object_type forge;
    char        name[MAX_NLEN];
    char        attr;

    _art_forge(idx, &forge);
    object_desc(name, &forge, OD_OMIT_INSCRIPTION);

    /* Found and not found are the distinction the player is here for, so it
     * is carried by the colour rather than by a column of its own. */
    if (sel)                    attr = 'B';
    else if (a_info[idx].found) attr = attr_to_attr_char(tval_to_attr[forge.tval % 128]);
    else                        attr = 'D';

    doc_printf(doc, "%s<color:%c>%-*.*s</color>\n",
               sel ? "<color:B>></color>" : " ", attr,
               b->item_w - 2, b->item_w - 2, name);
}

static void _art_detail(_browse_ptr b, doc_ptr doc, int idx)
{
    object_type forge;

    _art_forge(idx, &forge);
    if (a_info[idx].found)
        doc_insert(doc, "<color:G>You have held this.</color>\n\n");
    else
        doc_insert(doc, "<color:D>You have not found this.</color>\n\n");
    obj_display_doc(&forge, doc);
}

static bool _art_key(_browse_ptr b, int cmd, int idx)
{
    if (cmd == 't' || cmd == 'T')
    {
        b->opt = !b->opt;
        b->help = b->opt
            ? "Arrows to move, 't' for only the ones you have found, ESC to leave."
            : "Arrows to move, 't' for all of them, ESC to leave.";
        return TRUE;
    }
    return FALSE;
}

static void _browse_artifacts(void)
{
    _browse_t b = {0};
    int       i;

    if (!random_artifacts && no_artifacts)
    {
        cmsg_print(TERM_L_RED, "You won't find any artifacts this game.");
        return;
    }

    b.title    = "Artifacts";
    b.what     = "artifacts";
    b.grp_w    = 14;
    b.item_w   = 36;
    b.idx_max  = max_a_idx;
    b.opt      = TRUE;
    b.help     = "Arrows to move, 't' for only the ones you have found, ESC to leave.";
    b.grp_name = _art_grp_name;
    b.collect  = _art_collect;
    b.item_row = _art_row;
    b.detail   = _art_detail;
    b.key      = _art_key;

    for (i = 0; _art_types[i].filter; i++)
    {
        int *scratch;
        C_MAKE(scratch, max_a_idx, int);
        if (_collect_arts(i, scratch, TRUE))
            b.grp_idx[b.grp_cnt++] = i;
        C_KILL(scratch, max_a_idx, int);
    }

    _browse(&b);
}

/************************************************************************
 * Egos
 ************************************************************************/
static cptr _ego_grp_name(_browse_ptr b, int g)
{
    return _ego_types[b->grp_idx[g]].name;
}

static int _ego_collect(_browse_ptr b, int g, int *idx)
{
    return _collect_egos(b->grp_idx[g], idx);
}

static void _ego_row(_browse_ptr b, doc_ptr doc, int idx, bool sel)
{
    ego_type *e_ptr = &e_info[idx];
    char      name[255];

    strip_name_aux(name, e_name + e_ptr->name);

    doc_printf(doc, "%s<color:%c>%-*.*s</color>\n",
               sel ? "<color:B>></color>" : " ",
               sel ? 'B' : (e_ptr->counts.found ? 'w' : 'D'),
               b->item_w - 2, b->item_w - 2, name);
}

static void _ego_totals(_browse_ptr b, doc_ptr doc, int *idx, int cnt)
{
    counts_t t = {0};
    int      i;

    for (i = 0; i < cnt; i++)
    {
        t.found     += e_info[idx[i]].counts.found;
        t.bought    += e_info[idx[i]].counts.bought;
        t.destroyed += e_info[idx[i]].counts.destroyed;
    }
    doc_newline(doc);
    doc_printf(doc, " <color:y>%d found, %d bought, %d destroyed</color>\n",
               t.found, t.bought, t.destroyed);
}

static void _ego_detail(_browse_ptr b, doc_ptr doc, int idx)
{
    ego_type *e_ptr = &e_info[idx];

    /* The shared marker used to be " [Shared]" glued onto the name, which
     * meant nothing unless you already knew what it meant. */
    if (e_ptr->type & (~_ego_types[b->grp_idx[b->grp_cur]].id))
        doc_insert(doc, "<color:D>This make also turns up on other kinds"
                        " of gear.</color>\n");

    doc_insert(doc, "<style:heading>Your record</style>\n");
    _count(doc, "found", e_ptr->counts.found);
    _count(doc, "bought", e_ptr->counts.bought);
    _count(doc, "destroyed", e_ptr->counts.destroyed);
    doc_newline(doc);

    ego_display_doc(e_ptr, doc);

    /* ego_display_doc() prints only what you have learned, so for a make you
     * have merely heard of it prints the name and stops -- which reads like a
     * bug rather than like an answer. */
    if (!ego_has_lore(e_ptr))
        doc_insert(doc, "\n<color:D>You have not yet learned what this make"
                        " does.</color>\n");
}

static void _browse_egos(void)
{
    _browse_t b = {0};
    int       i, *scratch;

    b.title    = "Egos";
    b.what     = "egos";
    b.grp_w    = 16;
    b.item_w   = 34;
    b.idx_max  = max_e_idx;
    b.grp_name = _ego_grp_name;
    b.collect  = _ego_collect;
    b.item_row = _ego_row;
    b.detail   = _ego_detail;
    b.footer   = _ego_totals;

    C_MAKE(scratch, max_e_idx, int);
    for (i = 0; _ego_types[i].id != EGO_TYPE_NONE; i++)
    {
        if (_collect_egos(i, scratch))
            b.grp_idx[b.grp_cnt++] = i;
    }
    C_KILL(scratch, max_e_idx, int);

    _browse(&b);
}

/************************************************************************
 * Objects
 ************************************************************************/
static cptr _obj_grp_name(_browse_ptr b, int g)
{
    return object_group_text[b->grp_idx[g]];
}

static int _obj_collect(_browse_ptr b, int g, int *idx)
{
    return collect_objects(b->grp_idx[g], idx, 0x01);
}

/* What the player would call it: the real name once they know it, the
 * flavour -- "a murky potion" -- until then. */
static void _obj_name(char *buf, int k_idx)
{
    object_kind *k_ptr = &k_info[k_idx];

    if (!k_ptr->flavor || k_ptr->aware)
        strip_name(buf, k_idx);
    else
        strcpy(buf, k_name + k_info[k_ptr->flavor].flavor_name);
}

static void _obj_row(_browse_ptr b, doc_ptr doc, int idx, bool sel)
{
    object_kind *k_ptr = &k_info[idx];
    char         name[255];

    _obj_name(name, idx);
    doc_printf(doc, "%s<color:%c>%-*.*s</color>\n",
               sel ? "<color:B>></color>" : " ",
               sel ? 'B' : (k_ptr->aware ? 'w' : 'D'),
               b->item_w - 2, b->item_w - 2, name);
}

static void _obj_detail(_browse_ptr b, doc_ptr doc, int idx)
{
    object_kind *k_ptr = &k_info[idx];
    object_type  forge;

    if (k_ptr->flavor && !k_ptr->aware)
    {
        doc_insert(doc, "<color:D>You have seen these about but have never"
                        " worked out what they are.</color>\n\n");
    }

    doc_insert(doc, "<style:heading>Your record</style>\n");
    _count(doc, "found", k_ptr->counts.found);
    _count(doc, "bought", k_ptr->counts.bought);
    _count(doc, "used", k_ptr->counts.used);
    _count(doc, "destroyed", k_ptr->counts.destroyed);
    doc_newline(doc);

    if (k_ptr->flavor && !k_ptr->aware) return;

    object_wipe(&forge);
    object_prep(&forge, idx);
    forge.ident |= IDENT_KNOWN;
    obj_display_doc(&forge, doc);
}

static void _browse_objects(void)
{
    _browse_t b = {0};
    int       i, *scratch;

    b.title    = "Objects";
    b.what     = "objects";
    b.grp_w    = 18;
    b.item_w   = 32;
    b.idx_max  = max_k_idx;
    b.grp_name = _obj_grp_name;
    b.collect  = _obj_collect;
    b.item_row = _obj_row;
    b.detail   = _obj_detail;

    C_MAKE(scratch, max_k_idx, int);
    for (i = 0; object_group_text[i] != NULL; i++)
    {
        if (collect_objects(i, scratch, 0x01))
            b.grp_idx[b.grp_cnt++] = i;
        if (b.grp_cnt >= 99) break;
    }
    C_KILL(scratch, max_k_idx, int);

    _browse(&b);
}

/************************************************************************
 * Terrain
 *
 * This one is different. The other four browsers have a page to show, written
 * by somebody; terrain has none -- f_info.txt has no description line at all,
 * so the old screen could only ever be a table of symbols. What a terrain IS,
 * though, is written down: it is the flags. Whether you can walk on it, see
 * through it, dig it, drown in it. So the third column is assembled rather
 * than looked up.
 *
 * The groups are new too. There was one group, called "terrains", holding
 * every feature in the game.
 ************************************************************************/
typedef struct {
    cptr name;
    int  flag;      /* the flag that puts a feature in this group */
    int  not_flag;  /* ...unless it also has this one. -1 for none */
} _feat_type_t;

static _feat_type_t _feat_types[] = {
    { "Walls",        FF_WALL,      FF_DOOR },
    { "Doors",        FF_DOOR,      -1 },
    { "Stairs",       FF_STAIRS,    -1 },
    { "Traps",        FF_TRAP,      -1 },
    { "Shops",        FF_STORE,     -1 },
    { "Buildings",    FF_BLDG,      -1 },
    { "Ways in",      FF_ENTRANCE,  -1 },
    { "Water",        FF_WATER,     -1 },
    { "Lava",         FF_LAVA,      -1 },
    { "Snow and ice", FF_SNOW,      -1 },
    { "Trees",        FF_TREE,      -1 },
    { "Plants",       FF_PLANT,     -1 },
    { "Rubble",       FF_HURT_ROCK, FF_WALL },
    { "Floors",       FF_FLOOR,     -1 },
    /* Whatever none of the above claimed. Without this the list would be
     * shorter than the old one, which showed every feature in the game. */
    { "Everything else", -1,        -1 },
    { NULL,           0,            -1 },
};

static bool _feat_in_group(int f_idx, int g);

static bool _feat_in_group(int f_idx, int g)
{
    feature_type *f_ptr = &f_info[f_idx];
    _feat_type_t *t     = &_feat_types[g];

    if (!f_ptr->name) return FALSE;
    if (f_ptr->mimic != f_idx) return FALSE;   /* it pretends to be something else */

    if (t->flag < 0)
    {
        int i;
        for (i = 0; _feat_types[i].name; i++)
        {
            if (_feat_types[i].flag < 0) continue;
            if (_feat_in_group(f_idx, i)) return FALSE;
        }
        return TRUE;
    }

    if (!have_flag(f_ptr->flags, t->flag)) return FALSE;
    if (t->not_flag >= 0 && have_flag(f_ptr->flags, t->not_flag)) return FALSE;
    return TRUE;
}

static cptr _feat_grp_name(_browse_ptr b, int g)
{
    return _feat_types[b->grp_idx[g]].name;
}

static int _feat_collect(_browse_ptr b, int g, int *idx)
{
    int i, n = 0;

    for (i = 0; i < max_f_idx; i++)
    {
        int j;

        if (!_feat_in_group(i, b->grp_idx[g])) continue;

        for (j = 0; j < n; j++)
        {
            if (streq(f_name + f_info[idx[j]].name, f_name + f_info[i].name))
                break;
        }
        if (j < n) continue;

        idx[n++] = i;
    }
    return n;
}

static void _feat_row(_browse_ptr b, doc_ptr doc, int idx, bool sel)
{
    feature_type *f_ptr = &f_info[idx];

    doc_printf(doc, "%s<color:%c>%c</color> <color:%c>%-*.*s</color>\n",
               sel ? "<color:B>></color>" : " ",
               attr_to_attr_char(f_ptr->x_attr[F_LIT_STANDARD]),
               f_ptr->x_char[F_LIT_STANDARD],
               sel ? 'B' : 'w',
               b->item_w - 4, b->item_w - 4, f_name + f_ptr->name);
}

/* One line, if the flag is set. */
static void _feat_says(doc_ptr doc, feature_type *f_ptr, int flag, cptr text)
{
    if (have_flag(f_ptr->flags, flag))
        doc_printf(doc, "  <color:D>%s</color>\n", text);
}

typedef struct { int flag; cptr text; } _feat_line_t;

static _feat_line_t _feat_harm[] = {
    { FF_LAVA,      "It burns." },
    { FF_ACID,      "It is acid." },
    { FF_DEEP,      "Deep enough to drown in, if you are loaded down." },
    { FF_HIT_TRAP,  "Stepping on it sets it off." },
    { 0, NULL },
};

static _feat_line_t _feat_unmake[] = {
    { FF_HURT_FIRE, "Fire." },
    { FF_HURT_ROCK, "Stone to mud." },
    { FF_HURT_DISI, "Disintegration." },
    { 0, NULL },
};

static _feat_line_t _feat_holds[] = {
    { FF_HAS_GOLD,      "There is gold in it." },
    { FF_MAY_HAVE_GOLD, "There may be gold in it." },
    { FF_HAS_ITEM,      "There is something in it." },
    { 0, NULL },
};

/* A heading is only worth printing if something goes under it. */
static void _feat_block(doc_ptr doc, feature_type *f_ptr, cptr heading,
                        _feat_line_t *lines)
{
    int  i;
    bool any = FALSE;

    for (i = 0; lines[i].text; i++)
        if (have_flag(f_ptr->flags, lines[i].flag)) any = TRUE;
    if (!any) return;

    doc_printf(doc, "<style:heading>%s</style>\n", heading);
    for (i = 0; lines[i].text; i++)
        _feat_says(doc, f_ptr, lines[i].flag, lines[i].text);
    doc_newline(doc);
}

static void _feat_detail(_browse_ptr b, doc_ptr doc, int idx)
{
    feature_type *f_ptr = &f_info[idx];
    int           i;

    doc_printf(doc, "<color:U>%s</color>\n\n", f_name + f_ptr->name);


    /* The same square drawn three ways: unlit, lit, and lit by your own
     * light. Worth showing side by side, because telling them apart at a
     * glance is the entire skill of reading a map. */
    doc_insert(doc, "<style:heading>How it is drawn</style>\n");
    for (i = 0; i < F_LIT_MAX; i++)
    {
        doc_printf(doc, "  <color:%c>%c</color> <color:D>%s</color>\n",
                   attr_to_attr_char(f_ptr->x_attr[i]), f_ptr->x_char[i],
                   lighting_level_str[i]);
    }
    doc_newline(doc);

    doc_insert(doc, "<style:heading>Getting past it</style>\n");
    if (have_flag(f_ptr->flags, FF_MOVE))
        doc_insert(doc, "  <color:G>You can walk onto it.</color>\n");
    else
        doc_insert(doc, "  <color:r>You cannot walk onto it.</color>\n");
    _feat_says(doc, f_ptr, FF_CAN_FLY, "Flying gets you over it.");
    _feat_says(doc, f_ptr, FF_CAN_SWIM, "You can swim it.");
    _feat_says(doc, f_ptr, FF_CAN_CLIMB, "You can climb it.");
    _feat_says(doc, f_ptr, FF_CAN_PASS, "Anything that walks through walls"
                                        " goes through it.");
    _feat_says(doc, f_ptr, FF_TUNNEL, "It can be dug out.");
    _feat_says(doc, f_ptr, FF_PERMANENT, "Nothing will shift it.");
    _feat_says(doc, f_ptr, FF_SLIPPERY, "The footing is treacherous.");
    _feat_says(doc, f_ptr, FF_AVOID_RUN, "Running stops when you meet it.");
    _feat_says(doc, f_ptr, FF_TELEPORTABLE, "You can teleport onto it.");
    doc_newline(doc);

    doc_insert(doc, "<style:heading>Seeing past it</style>\n");
    if (have_flag(f_ptr->flags, FF_LOS))
        doc_insert(doc, "  <color:D>You can see through it.</color>\n");
    else
        doc_insert(doc, "  <color:y>It blocks your view.</color>\n");
    if (have_flag(f_ptr->flags, FF_PROJECT))
        doc_insert(doc, "  <color:D>Spells and arrows pass over it.</color>\n");
    else
        doc_insert(doc, "  <color:y>Spells and arrows stop at it.</color>\n");
    _feat_says(doc, f_ptr, FF_GLOW, "It gives off its own light.");
    doc_newline(doc);

    _feat_block(doc, f_ptr, "What it does to you", _feat_harm);
    _feat_block(doc, f_ptr, "What gets rid of it", _feat_unmake);
    _feat_block(doc, f_ptr, "What is in it", _feat_holds);
}

static void _browse_terrain(void)
{
    _browse_t b = {0};
    int       i, j;

    b.title    = "Terrain";
    b.what     = "terrain";
    b.grp_w    = 18;
    b.item_w   = 30;
    b.idx_max  = max_f_idx;
    b.grp_name = _feat_grp_name;
    b.collect  = _feat_collect;
    b.item_row = _feat_row;
    b.detail   = _feat_detail;

    for (i = 0; _feat_types[i].name; i++)
    {
        for (j = 0; j < max_f_idx; j++)
        {
            if (_feat_in_group(j, i)) { b.grp_idx[b.grp_cnt++] = i; break; }
        }
    }

    _browse(&b);
}

/*
 * List wanted monsters
 */


/*
 * List virtues & status
 */

static void do_cmd_knowledge_virtues(void)
{
    doc_ptr doc = doc_alloc(80);

    virtue_display(doc);
    doc_display(doc, "Virtues", 0);
    doc_free(doc);
}

/*
* Dungeon
*
*/
static void do_cmd_knowledge_dungeon(void)
{
    doc_ptr doc = doc_alloc(80);

    py_display_dungeons(doc);
    doc_display(doc, "Dungeons", 0);
    doc_free(doc);
}

static void do_cmd_knowledge_stat(void)
{
    doc_ptr          doc = doc_alloc(80);
    race_t          *race_ptr = get_race();
    class_t         *class_ptr = get_class();
    personality_ptr  pers_ptr = get_personality();
    int              i;

    if (p_ptr->knowledge & KNOW_HPRATE)
        doc_printf(doc, "Your current Life Rating is %s.\n\n", life_rating_desc(TRUE));
    else
        doc_insert(doc, "Your current Life Rating is <color:y>\?\?\?</color>.\n\n");

    doc_insert(doc, "<color:r>Limits of maximum stats</color>\n");

    for (i = 0; i < MAX_STATS; i++)
    {
        if ((p_ptr->knowledge & KNOW_STAT) || p_ptr->stat_max[i] == p_ptr->stat_max_max[i])
        {
            if (decimal_stats)
                doc_printf(doc, "%s <color:G>%d</color>\n", stat_names[i], (p_ptr->stat_max_max[i]-18)/10+18);
            else doc_printf(doc, "%s <color:G>18/%d</color>\n", stat_names[i], p_ptr->stat_max_max[i]-18);
        }
        else
            doc_printf(doc, "%s <color:y>\?\?\?</color>\n", stat_names[i]);
    }
    doc_insert(doc, "\n\n");

    doc_printf(doc, "<color:r>Race:</color> <color:B>%s</color>\n", race_ptr->name);
    doc_insert(doc, race_ptr->desc);
    if (p_ptr->pclass == CLASS_MONSTER)
        doc_printf(doc, " For more information, see <link:MonsterRaces.txt#%s>.\n\n", race_ptr->name);
    else
        doc_printf(doc, " For more information, see <link:Races.txt#%s>.\n\n", race_ptr->name);

    if (race_ptr->subdesc && strlen(race_ptr->subdesc))
    {
        doc_printf(doc, "<color:r>Subrace:</color> <color:B>%s</color>\n", race_ptr->subname);
        doc_insert(doc, race_ptr->subdesc);
        doc_insert(doc, "\n\n");
    }

    if (p_ptr->pclass != CLASS_MONSTER)
    {
        doc_printf(doc, "<color:r>Class:</color> <color:B>%s</color>\n", class_ptr->name);
        doc_insert(doc, class_ptr->desc);
        doc_printf(doc, " For more information, see <link:Classes.txt#%s>.\n\n", class_ptr->name);
    }

    doc_printf(doc, "<color:r>Personality:</color> <color:B>%s</color>\n", pers_ptr->name);
    doc_insert(doc, pers_ptr->desc);
    doc_printf(doc, " For more information, see <link:Personalities.txt#%s>.\n\n", pers_ptr->name);

    if (p_ptr->realm1)
    {
        doc_printf(doc, "<color:r>Realm:</color> <color:B>%s</color>\n", realm_names[p_ptr->realm1]);
        doc_insert(doc, realm_jouhou[technic2magic(p_ptr->realm1)-1]);
        doc_insert(doc, "\n\n");
    }

    if (p_ptr->realm2)
    {
        doc_printf(doc, "<color:r>Realm:</color> <color:B>%s</color>\n", realm_names[p_ptr->realm2]);
        doc_insert(doc, realm_jouhou[technic2magic(p_ptr->realm2)-1]);
        doc_insert(doc, "\n\n");
    }

    doc_display(doc, "Self Knowledge", 0);
    doc_free(doc);
}

/*
 * Interact with "knowledge"
 */
/*
 * The knowledge menu.
 *
 * This was two hand-drawn columns of prt() at fixed rows, which meant it could
 * not be clicked, could not be walked with the arrow keys, and had to be
 * re-counted by hand every time an entry was added or taken away.
 *
 * It is one document now, which buys the theme's colours, the hover highlight
 * and clickable rows for nothing -- and a single column, deliberately: two
 * columns look tidier and cannot be clicked, because doc_insert_cols() builds
 * its columns as separate documents and the hotspots stay behind in them. A
 * column of twenty rows you can click beats two columns of ten you cannot.
 *
 * Each entry also says what it is FOR. "Egos" tells you nothing you did not
 * know from the word; "which ego types you have identified" tells you whether
 * it is worth the keystroke.
 */
static void _know_row(doc_ptr doc, char key, cptr label, cptr what)
{
    doc_printf(doc, "  <color:y>%c</color><color:D>)</color> %-22.22s", key, label);
    if (what)
        doc_printf(doc, " <color:D>%s</color>", what);
    /* Before the newline: doc_add_hotspot() records the row the cursor is on,
     * and doc_newline() has already moved it down. */
    doc_add_hotspot(doc, key);
    doc_newline(doc);
}

void do_cmd_knowledge(void)
{
    int      i;
    bool     need_redraw = FALSE;
    class_t *class_ptr = get_class();
    race_t  *race_ptr = get_race();
    doc_ptr  doc = doc_alloc(MIN(72, ui_map_rect().cx));

    screen_save();

    while (1)
    {
        doc_clear(doc);
        doc_insert(doc, "<style:table>");
        doc_insert(doc, "<color:U>What you know</color>\n");
        doc_add_rule(doc);
        doc_newline(doc);

        doc_insert(doc, "<style:heading>Things</style>\n");
        _know_row(doc, 'a', "Artifacts", "which ones have turned up");
        _know_row(doc, 'o', "Objects", "what you have identified");
        _know_row(doc, 'e', "Egos", "the makes you have seen");
        _know_row(doc, '_', "Loot Judgement", "what to pick up, leave and destroy");
        doc_newline(doc);

        doc_insert(doc, "<style:heading>Creatures</style>\n");
        _know_row(doc, 'm', "Known monsters", "everything you have met");
        _know_row(doc, 'w', "Wanted monsters", "who is worth money");
        _know_row(doc, 'u', "Remaining uniques", "who is still out there");
        _know_row(doc, 'k', "Kill count", "what you have accounted for");
        _know_row(doc, 'p', "Pets", "who is following you");
        doc_newline(doc);

        doc_insert(doc, "<style:heading>Places</style>\n");
        _know_row(doc, 'd', "Dungeons", "how deep you have been in each");
        _know_row(doc, 'q', "Quests", "taken, done and failed");
        _know_row(doc, 't', "Terrain symbols", "what the map is telling you");
        doc_newline(doc);

        doc_insert(doc, "<style:heading>Yourself</style>\n");
        _know_row(doc, '@', "About yourself", "the whole character dump");
        if (p_ptr->prace != RACE_MON_RING)
            _know_row(doc, 'W', "Weapon damage", "what your blows actually do");
        if (equip_find_obj(TV_BOW, SV_ANY) && !prace_is_(RACE_MON_JELLY)
         && p_ptr->shooter_info.tval_ammo != TV_NO_AMMO)
            _know_row(doc, 'S', "Shooter damage", "and what your shots do");
        if (mut_count(NULL))
            _know_row(doc, 'M', "Mutations", "what has happened to you");
        if (enable_virtues)
            _know_row(doc, 'v', "Virtues", "how you have been behaving");
        if (class_ptr->character_dump || race_ptr->character_dump)
            _know_row(doc, 'x', "Extra info", "whatever your kind keeps track of");
        _know_row(doc, 'P', "Proficiency", "how well you handle each weapon");
        if (p_ptr->pclass != CLASS_RAGE_MAGE)
            _know_row(doc, 's', "Spell proficiency", "and each spell");
        _know_row(doc, 'H', "High scores", "everyone who came before");

        doc_newline(doc);
        doc_insert(doc, "  <color:D>ESC) Back</color>\n");
        doc_insert(doc, "</style>");

        Term_clear();
        doc_sync_menu(doc);

        i = ui_menu_inkey(doc);

        if (i == ESCAPE) break;
        if (!i) continue;                 /* a navigation key; redraw and wait */

        switch (i)
        {
        /* Object Knowledge */
        case 'a':
            _browse_artifacts();
            break;
        case 'o':
            _browse_objects();
            break;
        case 'e':
            _browse_egos();
            break;
        case '_':
            do_cmd_loot_rules();
            break;

        /* Monster Knowledge */
        case 'm':
            _browse_monsters();
            break;
        case 'w':
            do_cmd_knowledge_kubi();
            break;
        case 'u':
            do_cmd_knowledge_uniques();
            break;
        case 'k':
            do_cmd_knowledge_kill_count();
            break;
        case 'p':
            do_cmd_knowledge_pets();
            break;

        /* Dungeon Knowledge */
        case 'd':
            do_cmd_knowledge_dungeon();
            break;
        case 'q':
            quests_display();
            break;
        case 't':
            {
                int lighting_level = F_LIT_STANDARD;
                _browse_terrain();
            }
            break;

        /* Self Knowledge */
        case '@':
            do_cmd_knowledge_stat();
            break;
        case 'W':
            if (p_ptr->prace != RACE_MON_RING)
                do_cmd_knowledge_weapon();
            else
                bell();
            break;
        case 'S':
            if (equip_find_obj(TV_BOW, SV_ANY) && !prace_is_(RACE_MON_JELLY) && p_ptr->shooter_info.tval_ammo != TV_NO_AMMO)
                do_cmd_knowledge_shooter();
            else
                bell();
            break;
        case 'M':
            if (mut_count(NULL))
                mut_do_cmd_knowledge();
            else
                bell();
            break;
        case 'v':
            if (enable_virtues)
                do_cmd_knowledge_virtues();
            else
                bell();
            break;
        case 'x':
            if (class_ptr->character_dump || race_ptr->character_dump)
                do_cmd_knowledge_extra();
            else
                bell();
            break;
        case 'H': {
            vec_ptr scores;
            if (check_score())
                scores_update();
            scores = scores_load(NULL);
            scores_display(scores);
            vec_free(scores);
            break; }

        /* Skills */
        case 'P':
            do_cmd_knowledge_weapon_exp();
            break;
        case 's':
            if (p_ptr->pclass != CLASS_RAGE_MAGE)  /* TODO */
                do_cmd_knowledge_spell_exp();
            break;

        default:
            bell();
        }

        /* Flush messages */
        msg_print(NULL);
    }

    /* Restore the screen */
    screen_load();

    doc_free(doc);

    if (need_redraw) do_cmd_redraw();
}

/*
 * Display the time and date
 */
void do_cmd_time(void)
{
    int day, hour, min, full, start, end, num;
    char desc[1024];

    char buf[1024];
    char day_buf[10];

    FILE *fff;

    extract_day_hour_min(&day, &hour, &min);

    full = hour * 100 + min;

    start = 9999;
    end = -9999;

    num = 0;

    strcpy(desc, "It is a strange time.");


    if (day < MAX_DAYS) sprintf(day_buf, "%d", day);
    else strcpy(day_buf, "*****");

    /* Message */
    msg_format("This is day %s. The time is %d:%02d %s.",
           day_buf, (hour % 12 == 0) ? 12 : (hour % 12),
           min, (hour < 12) ? "AM" : "PM");


    /* Find the path */
    if (!randint0(10) || p_ptr->image)
    {
        path_build(buf, sizeof(buf), SCOTTISH_DIR_FILE, "timefun.txt");

    }
    else
    {
        path_build(buf, sizeof(buf), SCOTTISH_DIR_FILE, "timenorm.txt");

    }

    /* Open this file */
    fff = my_fopen(buf, "rt");

    /* Oops */
    if (!fff) return;

    /* Find this time */
    while (!my_fgets(fff, buf, sizeof(buf)))
    {
        /* Ignore comments */
        if (!buf[0] || (buf[0] == '#')) continue;

        /* Ignore invalid lines */
        if (buf[1] != ':') continue;

        /* Process 'Start' */
        if (buf[0] == 'S')
        {
            /* Extract the starting time */
            start = atoi(buf + 2);

            /* Assume valid for an hour */
            end = start + 59;

            /* Next... */
            continue;
        }

        /* Process 'End' */
        if (buf[0] == 'E')
        {
            /* Extract the ending time */
            end = atoi(buf + 2);

            /* Next... */
            continue;
        }

        /* Ignore incorrect range */
        if ((start > full) || (full > end)) continue;

        /* Process 'Description' */
        if (buf[0] == 'D')
        {
            num++;

            /* Apply the randomizer */
            if (!randint0(num)) strcpy(desc, buf + 2);

            /* Next... */
            continue;
        }
    }

    if (p_ptr->prace == RACE_WEREWOLF)
    {
        strcat(desc, werewolf_moon_message());
    }

    /* Message */
    msg_print(desc);

    /* Close the file */
    my_fclose(fff);
}
