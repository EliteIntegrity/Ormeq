/*
 * File: ui_death.h
 *
 * The death card, and the page it is composed on.
 *
 * You die in this game constantly, and until now the game answered with
 * print_tomb(): a stone drawn in hyphens, from a text file, with your class
 * and your gold centred under it. That is the most loaded screen in a
 * roguelike and it was the plainest one we had.
 *
 * ui_card_t is that page as data, and ui_card_show() lays it out and puts it
 * on the screen. Two things fill one in: the death itself, and the graveyard
 * looking a run up years later. **Deliberately one layout and one function**
 * -- a memorial and the record of the same memorial should not be two designs
 * that drifted apart, and the first version of this was exactly that.
 *
 * The page is a doc rather than a drawing, so it works on the terminal
 * frontends as well as ours, and so it can carry a portrait box that a
 * frontend fills in and a terminal leaves blank -- which is the only reason
 * it is safe to put a picture in a game whose display is a grid of
 * characters.
 *
 * Nothing here changes what a player is told: every figure on the page is one
 * the character sheet, the monster list or the score file already had.
 *
 * Copyright (c) 2026 John Horton.
 *
 * Added since the fork and offered on the same terms as the rest of the
 * tree -- the Angband licence in LICENSE.
 */

#ifndef INCLUDED_UI_DEATH_H
#define INCLUDED_UI_DEATH_H

#define UI_CARD_FIGURES 6
#define UI_CARD_LINES   3

typedef struct
{
    cptr          name;         /* who this was                             */
    bool          won;          /* finished it, rather than did not return  */
    cptr          race;
    cptr          class_;
    int           level;

    cptr          killer;       /* "a Kobold", "Poison", "Quitting"         */
    monster_race *plate;        /* whose picture, or NULL for none          */
    cptr          where;        /* the dungeon, or NULL for above ground    */
    int           floor;

    cptr          fig_label[UI_CARD_FIGURES];
    char          fig_value[UI_CARD_FIGURES][40];
    int           fig_ct;

    cptr          lost;         /* the thing never identified, or NULL      */

    char          line[UI_CARD_LINES][192];  /* the last few things said    */
    int           line_ct;

    cptr          date;
} ui_card_t;

/*
 * There was a picture of the floor here, cropped to what the player had
 * explored, and it is gone: a run that ends on level one has seen a room and
 * a corridor, and a corridor drawn in seventy columns is four dots and a
 * hash. It was emptiest in exactly the cases somebody would most often be
 * looking at it. See ui_card_show().
 */

/*
 * Lay the card out, take the screen, show it, and wait for a key.
 *
 * `footer` is a dim row of choices printed under a rule at the foot of the
 * page, or NULL for none, and the key pressed comes back so the caller can act
 * on it. That is what turns the card from a page you dismiss into the hub the
 * whole death sequence hangs off -- see _death_hub() in files.c.
 *
 * The graveyard passes NULL and ignores the answer: a record of a death years
 * ago has nothing to offer but the way out.
 */
extern int ui_card_show(const ui_card_t *card, cptr footer);

/* The race a "killed by" string names, or NULL. Shared so that a live death
 * and a score line resolve the same string to the same picture. */
extern monster_race *ui_card_killer(cptr died_from);

/* 214730 -> "214,730". Nobody reads the first one at a glance. */
extern void ui_card_commas(int n, char *buf, int len);

/*
 * The death card itself, installed as tombstone_aux -- which print_tomb()
 * calls first and which suppresses the old stone when it returns TRUE.
 * FALSE if it cannot compose the page, and the stone happens instead, so a
 * failure here costs the look of one screen and never the run.
 */
extern bool ui_death_card(void);

/*
 * The same card with a row of choices under it, returning the key pressed.
 *
 * This is what the death sequence loops on. Everything that used to be forced
 * on a player between dying and getting out -- a filename prompt, then the
 * character sheet -- hangs off this instead, so it is all reachable and none
 * of it is compulsory.
 */
extern int ui_death_card_menu(cptr footer);

#endif
