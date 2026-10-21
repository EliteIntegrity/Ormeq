/*
 * File: ui_death.c
 *
 * The death card, and the page both it and the graveyard are composed on.
 * See ui_death.h.
 *
 * Copyright (c) 2026 John Horton.
 *
 * Added since the fork and offered on the same terms as the rest of the
 * tree -- the Angband licence in LICENSE.
 */

#include "scottish.h"
#include "ui_death.h"
#include "ui_event.h"   /* UI_ART_MONSTER; ui_art.h is the frontend's */

#include <time.h>

/*
 * The page, in terminal cells.
 *
 * A fixed block, centred on whatever screen it lands on, rather than lines
 * each centred on their own. Centring every line separately is what made the
 * first version look like things scattered across a screen instead of one
 * composed thing: the picture ended up in one place, the name in another, and
 * the figures somewhere between them with no edge shared by any of it.
 *
 * Sixty-eight is the picture, a gap, and a column of text -- and it still
 * fits an eighty-column terminal, which is the narrowest thing that has to
 * work.
 */
#define _W          72
#define _PLATE_COLS 30
#define _PLATE_ROWS 15
#define _TEXT_COLS  (_W - _PLATE_COLS - 4)


/* A dim line across the block. doc holds one rule per page and this page
 * wants two, so they are drawn rather than declared. */
static void _rule(doc_ptr doc)
{
    char buf[_W + 1];

    memset(buf, '-', _W);
    buf[_W] = '\0';
    doc_printf(doc, "<color:D>%s</color>\n", buf);
}


static void _compose(const ui_card_t *c, doc_ptr b)
{
    int i;

    /* --- the picture, and who this was --------------------------------- */
    {
        doc_ptr plate = c->plate ? doc_alloc(_PLATE_COLS) : NULL;
        doc_ptr text  = doc_alloc(c->plate ? _TEXT_COLS : _W);

        /* Left column left empty on purpose: the frontend draws into it, and
         * a terminal shows the gap and the page still reads. */
        if (plate)
            for (i = 0; i < _PLATE_ROWS; i++) doc_newline(plate);

        doc_printf(text, "<color:%c>%s</color>\n",
                   c->won ? 'v' : 'w', c->name);
        doc_printf(text, "<color:D>%s</color>\n",
                   c->won ? "who finished it" : "who did not come back");
        doc_newline(text);
        doc_printf(text, "%s <color:B>%s</color>", c->race, c->class_);
        doc_printf(text, "<color:D>, level</color> <color:W>%d</color>\n",
                   c->level);
        doc_newline(text);
        doc_printf(text, "<color:D>killed by</color> <color:r>%s</color>\n",
                   c->killer);
        if (c->where)
            doc_printf(text, "<color:D>on floor</color> <color:W>%d</color>"
                             " <color:D>of</color> %s\n", c->floor, c->where);
        else
            doc_insert(text, "<color:D>above ground</color>\n");

        if (plate)
        {
            doc_ptr cols[2];

            cols[0] = plate;
            cols[1] = text;

            doc_add_portrait(b, UI_ART_MONSTER, r_name + c->plate->name,
                             _PLATE_COLS, _PLATE_ROWS,
                             c->plate->d_attr, c->plate->d_char);
            doc_insert_cols(b, cols, 2, 4);
            doc_free(plate);
        }
        else
        {
            doc_insert_cols(b, &text, 1, 0);
        }

        doc_free(text);
    }

    doc_newline(b);
    _rule(b);
    doc_newline(b);

    /* --- what the run came to ------------------------------------------ */
    for (i = 0; i < c->fig_ct; i++)
    {
        doc_printf(b, "  <color:D>%-9s</color><color:W>%-11s</color>",
                   c->fig_label[i], c->fig_value[i]);
        if ((i % 3) == 2 || i == c->fig_ct - 1) doc_newline(b);
    }

    if (c->lost)
    {
        doc_newline(b);
        doc_printf(b, "  <color:D>%s</color>  %s\n", "Never identified",
                   c->lost);
    }

    /* --- how it ended --------------------------------------------------- */
    if (c->line_ct)
    {
        doc_newline(b);
        _rule(b);
        doc_newline(b);

        for (i = 0; i < c->line_ct; i++)
        {
            /* The last one brighter: it is the one that finished the run. */
            doc_printf(b, "  <color:%c>%.*s</color>\n",
                       i == c->line_ct - 1 ? 'y' : 'D', _W - 2, c->line[i]);
        }
    }

}


static void _date(const ui_card_t *c, doc_ptr b)
{
    int pad = (_W - (int)strlen(c->date)) / 2;

    if (pad < 0) pad = 0;

    doc_newline(b);
    doc_printf(b, "%*s<color:D>%s</color>\n", pad, "", c->date);
}


int ui_card_show(const ui_card_t *card, cptr footer)
{
    doc_ptr page, block, foot;
    rect_t  box;
    int     rows, i, cmd, foot_rows, avail;

    if (!card) return ESCAPE;

    /*
     * screen_save() first, then the modal layer.
     *
     * The layer alone was not enough: something repainted the level into the
     * terminal while this page sat in inkey(), and a scatter of the floor you
     * died on showed up underneath your own memorial. screen_save() is the
     * signal every well-behaved takeover in this codebase uses, and it is the
     * one ui_screen_map_owns() actually looks at.
     */
    screen_save();
    ui_screen_layer_push(UI_LAYER_MODAL);
    ui_event_map_hidden();

    /*
     * The ground, edge to edge.
     *
     * This said panel_none() and drew on bare canvas, which is the letterbox
     * every other full-screen page in the game stopped doing on 7 September
     * 2026. A page that owns the screen says so and gets the theme's ground
     * under all of it -- and saying it also clears whatever panel the screen
     * underneath had, which is what panel_none() was here for.
     */
    ui_event_panel_screen();
    ui_event_menu_clear();

    /* And the scroll marks. The frontend holds those until told otherwise,
     * so the "more below" arrows from whatever menu was up last were still
     * being drawn over the middle of this page. */
    ui_event_scroll(FALSE, FALSE);

    Term_clear();

    box = ui_page_rect();

    block = doc_alloc(_W);

    /* Screenshot style: no word wrap, and the whole block as the right
     * margin. The default wraps at column 72 *and counts the colour markup
     * towards it*, so three <color> spans add fifty invisible characters and
     * a laid-out line arrives in two halves, the second against the margin.
     * Nothing in the text being written shows why. */
    doc_insert(block, "<style:screenshot>");
    _compose(card, block);

    /* Omit the explored-map preview: short runs provide little useful map
     * detail, and the preview would displace the character information. */

    _date(card, block);

    doc_insert(block, "</style>");

    /* Pin death actions to the page footer and clip the variable-height card
     * above it. A tall portrait or last message must never hide the exit.
     * Graveyard records pass NULL because their characters are not loaded. */
    foot      = NULL;
    foot_rows = 0;

    if (footer && footer[0])
    {
        foot = doc_alloc(_W);
        doc_insert(foot, "<style:screenshot>");
        _rule(foot);
        doc_printf(foot, "<color:D>%s</color>\n", footer);
        doc_insert(foot, "</style>");
        foot_rows = doc_cursor(foot).y;
    }

    /* One blank row between the card and the choices, when there are any. */
    avail = box.cy - (foot_rows ? foot_rows + 1 : 0);
    if (avail < 1) avail = 1;

    page = doc_alloc(box.cx);
    doc_insert(page, "<style:screenshot>");

    /*
     * Centred down the page as well as across it -- a card pinned to the top
     * of an otherwise empty screen is a receipt -- but inside the page's
     * margins, and never above them. With the map on the card the block was
     * tall enough that this arithmetic came out at zero and the name sat on
     * the very first row of the window.
     */
    rows = doc_cursor(block).y + 1;
    for (i = (avail - rows) / 2; i > 0; i--) doc_newline(page);

    doc_insert_doc(page, block, (box.cx - _W) / 2);
    doc_insert(page, "</style>");

    doc_sync_term(page, doc_range_top_lines(page, avail),
                  doc_pos_create(box.x, box.y));

    if (foot)
    {
        doc_ptr fpage = doc_alloc(box.cx);

        doc_insert(fpage, "<style:screenshot>");
        doc_insert_doc(fpage, foot, (box.cx - _W) / 2);
        doc_insert(fpage, "</style>");

        doc_sync_term(fpage, doc_range_all(fpage),
                      doc_pos_create(box.x, box.y + box.cy - foot_rows));

        doc_free(fpage);
        doc_free(foot);
    }

    flush();
    cmd = inkey();

    doc_free(page);
    doc_free(block);

    ui_screen_layer_pop(UI_LAYER_MODAL);
    screen_load();

    return cmd;
}


/* What ui_death_card() should put at the foot of the page, and what was
 * pressed on it. Module state because tombstone_aux is a bool(void) hook
 * and cannot carry either through. */
static cptr _footer = NULL;
static int  _last_key = ESCAPE;


/*
 * The race that killed you, if a creature did.
 *
 * p_ptr->died_from is a sentence fragment -- "a Green jelly", "Grip, Farmer
 * Bulley's dog", "Poison" -- and by the time it is written the monster is
 * gone: take_hit() is handed a string, not a creature, at every one of its
 * hundred call sites.
 *
 * So it is matched back. Exactly, after dropping a leading article, and the
 * whole of what is left has to be a monster's name -- not a substring of one.
 * A loose match would put a picture of something that did not kill you at the
 * top of the page, which is worse than no picture. "Poison" matches nothing
 * and gets none, which is right.
 */
monster_race *ui_card_killer(cptr died_from)
{
    cptr s = died_from;
    int  i;

    if (!s || !*s) return NULL;

    if (!strncmp(s, "a ", 2)) s += 2;
    else if (!strncmp(s, "an ", 3)) s += 3;
    else if (!strncmp(s, "the ", 4)) s += 4;
    else if (!strncmp(s, "The ", 4)) s += 4;

    for (i = 1; i < max_r_idx; i++)
    {
        monster_race *r_ptr = &r_info[i];

        if (!r_ptr->name) continue;
        if (!strcmpi(r_name + r_ptr->name, s)) return r_ptr;
    }

    return NULL;
}


/* A number with thousands separators, because 214730 is not a length of time
 * anybody reads at a glance. */
void ui_card_commas(int n, char *buf, int len)
{
    char raw[32];
    int  i, j, ct;

    strnfmt(raw, sizeof(raw), "%d", n);
    ct = (int)strlen(raw);

    for (i = 0, j = 0; raw[i] && j < len - 1; i++)
    {
        if (i && ((ct - i) % 3) == 0) buf[j++] = ',';
        if (j < len - 1) buf[j++] = raw[i];
    }
    buf[j] = '\0';
}


/* The deepest floor reached, across every dungeon. */
static int _deepest(void)
{
    int i, deep = 0;

    for (i = 1; i < max_d_idx; i++)
        if (max_dlv[i] > deep) deep = max_dlv[i];

    return deep;
}

/*
 * The most impressive thing killed this life.
 *
 * The highest-level race with a kill against it. Not the most kills -- eighty
 * white worm masses is a story about patience, and this line is meant to be
 * the story about the one time it went right.
 */
static monster_race *_best_kill(void)
{
    monster_race *best = NULL;
    int           i;

    for (i = 1; i < max_r_idx; i++)
    {
        monster_race *r_ptr = &r_info[i];

        if (!r_ptr->name || !r_ptr->r_pkills) continue;
        if (!best || r_ptr->level > best->level) best = r_ptr;
    }

    return best;
}

static int _total_kills(void)
{
    int i, ct = 0;

    for (i = 1; i < max_r_idx; i++)
        ct += r_info[i].r_pkills;

    return ct;
}

/*
 * Something in the pack that was never identified.
 *
 * The small ache at the end of a run: the scroll you were saving in case it
 * was something good. Deliberately the first one found rather than the most
 * valuable -- picking the best of them would be telling the player what it
 * was after all, which is the one thing this page must not do.
 */
static obj_ptr _unknown_obj = NULL;

static void _note_unknown(obj_ptr obj)
{
    if (_unknown_obj) return;
    if (obj_is_known(obj)) return;
    _unknown_obj = obj;
}


bool ui_death_card(void)
{
    ui_card_t     c;
    monster_race *best;
    static char   lost_buf[128];
    static char   date[32];
    time_t        now = time(0);
    int           i, n;

    memset(&c, 0, sizeof(c));

    c.name   = player_name;
    c.won    = BOOL(p_ptr->total_winner);
    c.race   = get_race()->name;
    c.class_ = get_class()->name;
    c.level  = p_ptr->lev;
    c.killer = p_ptr->died_from;
    c.plate  = ui_card_killer(p_ptr->died_from);
    c.where  = dun_level ? d_name + d_info[dungeon_type].name : NULL;
    c.floor  = dun_level;


    best = _best_kill();

    n = 0;
    c.fig_label[n] = "Deepest";
    ui_card_commas(_deepest(), c.fig_value[n], sizeof(c.fig_value[n])); n++;
    c.fig_label[n] = "Turns";
    ui_card_commas(game_turn, c.fig_value[n], sizeof(c.fig_value[n])); n++;
    c.fig_label[n] = "Slain";
    ui_card_commas(_total_kills(), c.fig_value[n], sizeof(c.fig_value[n])); n++;
    c.fig_label[n] = "Exp";
    ui_card_commas(p_ptr->exp, c.fig_value[n], sizeof(c.fig_value[n])); n++;
    c.fig_label[n] = "Gold";
    ui_card_commas(p_ptr->au, c.fig_value[n], sizeof(c.fig_value[n])); n++;
    c.fig_label[n] = "Best";
    strnfmt(c.fig_value[n], sizeof(c.fig_value[n]), "%s",
            best ? r_name + best->name : "nothing at all"); n++;
    c.fig_ct = n;

    _unknown_obj = NULL;
    pack_for_each(_note_unknown);
    if (_unknown_obj)
    {
        object_desc(lost_buf, _unknown_obj, OD_COLOR_CODED);
        c.lost = lost_buf;
    }

    /* Display recent messages oldest first by reversing the log traversal.
     * Skip multiline messages, including tiny map screenshots; retain those
     * messages in the log and character dump rather than squeezing them here. */
    {
        char stack[UI_CARD_LINES][192];
        int  ct = 0;

        n = msg_count();

        for (i = 0; i < n && ct < UI_CARD_LINES; i++)
        {
            char buf[192];

            if (msg_get_plain_text(i, buf, sizeof(buf)) <= 0) continue;
            if (!buf[0]) continue;
            if (strpbrk(buf, "\r\n")) continue;   /* a picture, not a line */

            strnfmt(stack[ct], sizeof(stack[0]), "%s", buf);
            ct++;
        }

        for (i = ct - 1; i >= 0; i--)
        {
            strnfmt(c.line[c.line_ct], sizeof(c.line[0]), "%s", stack[i]);
            c.line_ct++;
        }
    }

    strnfmt(date, sizeof(date), "%-.24s", ctime(&now));
    c.date = date;

    _last_key = ui_card_show(&c, _footer);
    return TRUE;
}


/*
 * The card, shown with a row of choices, and the key that was pressed.
 *
 * ui_death_card() is a bool because tombstone_aux is a bool: print_tomb()
 * only wants to know whether the old hyphen stone should happen instead.
 * The hub in files.c wants the page composed fresh each time round and the
 * key back, so it goes through here.
 *
 * Composed from scratch on every call rather than cached, because the
 * figures on it can change while the hub is open -- dumping a record
 * identifies the pack, so an item listed as never identified stops being
 * one, and a page that said otherwise would be stale on its own screen.
 */
int ui_death_card_menu(cptr footer)
{
    _footer = footer;
    _last_key = ESCAPE;
    ui_death_card();
    _footer = NULL;
    return _last_key;
}
