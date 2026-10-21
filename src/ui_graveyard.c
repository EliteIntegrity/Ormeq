/*
 * File: ui_graveyard.c
 *
 * The dead, browsable. See ui_graveyard.h.
 *
 * Copyright (c) 2026 John Horton.
 *
 * Added since the fork and offered on the same terms as the rest of the
 * tree -- the Angband licence in LICENSE.
 */

#include "scottish.h"
#include "scores.h"
#include "ui_graveyard.h"
#include "ui_death.h"   /* the page both cards are laid out on */

#define _PLATE_COLS 26
#define _PLATE_ROWS 13
#define _PAGE       14

static cptr _s(cptr s) { return s && *s ? s : "?"; }


void ui_graveyard_card(score_ptr score)
{
    ui_card_t c;
    int       n;

    if (!score) return;

    memset(&c, 0, sizeof(c));

    c.name   = _s(score->name);
    c.won    = BOOL(score->status && !strcmpi(score->status, "Winner"));
    c.race   = _s(score->race);
    c.class_ = _s(score->class_);
    c.level  = score->clvl;
    c.killer = _s(score->killer);
    c.where  = score->dlvl ? _s(score->dungeon) : NULL;
    c.floor  = score->dlvl;

    /*
     * Matched the same way a live death is, and through the same function.
     *
     * This used to be a second copy on the reasoning that a years-old score
     * line should not be tied to today's r_info. It is the same reasoning
     * that produces two layouts which drift apart, and the risk it guards
     * against -- a renamed monster no longer matching -- fails safely
     * already: no match, no picture. One function, one behaviour.
     */
    c.plate = ui_card_killer(_s(score->killer));

    n = 0;
    c.fig_label[n] = "Deepest";
    ui_card_commas(score->max_depth, c.fig_value[n], sizeof(c.fig_value[n]));
    n++;
    c.fig_label[n] = "Turns";
    ui_card_commas(score->turns, c.fig_value[n], sizeof(c.fig_value[n]));
    n++;
    c.fig_label[n] = "Fame";
    ui_card_commas(score->fame, c.fig_value[n], sizeof(c.fig_value[n]));
    n++;
    c.fig_label[n] = "Exp";
    ui_card_commas(score->exp, c.fig_value[n], sizeof(c.fig_value[n]));
    n++;
    c.fig_label[n] = "Gold";
    ui_card_commas(score->gold, c.fig_value[n], sizeof(c.fig_value[n]));
    n++;
    c.fig_label[n] = "Was";
    strnfmt(c.fig_value[n], sizeof(c.fig_value[n]), "%s",
            _s(score->personality));
    n++;
    c.fig_ct = n;

    /* A score line holds no last words and no unopened scroll. Those lines
     * are absent rather than blank: an empty field on a memorial reads as
     * something the game forgot rather than something it never knew. */
    c.date = _s(score->date);

    ui_card_show(&c, NULL);
}


/*
 * The list.
 *
 * One line each, newest first, because the run you want to look at is nearly
 * always the last one. Sorted here rather than trusting the file: scores.txt
 * is appended to and its order is the order things happened, which is the
 * order a table wants and the reverse of what a person does.
 */
static void _list(doc_ptr doc, vec_ptr scores, int top)
{
    int i, ct = vec_length(scores);

    doc_clear(doc);
    doc_insert(doc, "<style:screenshot>");
    doc_newline(doc);

    if (!ct)
    {
        doc_insert(doc, "  <color:D>Nobody has died here yet.</color>\n\n");
        doc_insert(doc, "  <color:D>x) Back</color>\n");
        doc_insert(doc, "</style>");
        doc_sync_menu(doc);
        return;
    }

    doc_printf(doc, "  <color:U>The dead</color>"
                    "   <color:D>%d of them</color>\n\n", ct);

    for (i = top; i < ct && i < top + _PAGE; i++)
    {
        score_ptr s = vec_get(scores, i);
        char      key = (char)('a' + (i - top));

        /* No colour of our own for the selected row: doc_select() draws the
         * bar, and a second highlight the same colour as the first reads as
         * one somebody forgot to rub out. */
        doc_add_hotspot(doc, key);
        doc_printf(doc, "  <color:y>%c)</color> <color:w>%-12.12s</color>",
                   key, _s(s->name));
        doc_printf(doc, " <color:D>%-9.9s %-10.10s</color>",
                   _s(s->race), _s(s->class_));
        doc_printf(doc, " <color:D>L</color><color:w>%-3d</color>", s->clvl);
        /* Wide enough for "a Spectral tyrannosaurus while helpless" to
         * say which monster before it runs out. Sixteen columns cut it
         * at "a Spectral tyran", which names nothing. */
        doc_printf(doc, " <color:D>%-28.28s</color>", _s(s->killer));
        doc_printf(doc, " <color:D>%s</color>\n", _s(s->date));
    }

    doc_newline(doc);

    /* Say which key, not that there is more. "more below" with nothing to
     * press is a sign pointing at a door that is not there. */
    if (top + _PAGE < ct)
        doc_printf(doc, "  <color:y>%s</color> <color:D>%s</color>\n",
                   "space)", "more");
    if (top > 0)
        doc_printf(doc, "  <color:y>%s</color> <color:D>%s</color>\n",
                   "-)", "back a page");
    doc_insert(doc, "  <color:y>x)</color> <color:D>Back</color>\n");
    doc_insert(doc, "</style>");
    doc_sync_menu(doc);
}


void ui_graveyard(void)
{
    vec_ptr scores = scores_load(NULL);
    doc_ptr doc;
    int     top = 0, ct;
    bool    done = FALSE;

    if (!scores) return;

    /* Newest first. */
    vec_sort(scores, (vec_cmp_f)score_cmp_date);
    ct = vec_length(scores);

    doc = doc_alloc(MIN(90, ui_screen_rect().cx));

    while (!done)
    {
        int cmd;

        _list(doc, scores, top);
        cmd = ui_menu_inkey(doc);

        /*
         * ui_menu_inkey() takes the arrows for its own selection bar and
         * hands back 0, and turns Return into the selected row's letter.
         * So the arrows are already spoken for and paging needs keys of its
         * own -- which is why the page says which ones.
         */
        switch (cmd)
        {
        case ESCAPE: case 'x': case 'X': case 'q': case 'Q':
            done = TRUE;
            break;

        case ' ': case SKEY_PGDOWN: case '3':
            if (top + _PAGE < ct) top += _PAGE;
            break;

        case '-': case SKEY_PGUP: case '9':
            top -= _PAGE;
            if (top < 0) top = 0;
            break;

        default:
            if (cmd >= 'a' && cmd < 'a' + _PAGE && top + (cmd - 'a') < ct)
                ui_graveyard_card(vec_get(scores, top + (cmd - 'a')));
            break;
        }
    }

    doc_free(doc);

    /* vec_free is enough: scores_load() built the vector with score_free as
     * its element destructor, so freeing them by hand first would be a double
     * free waiting for whoever read this next. */
    vec_free(scores);
}
