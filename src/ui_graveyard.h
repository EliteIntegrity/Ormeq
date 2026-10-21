/*
 * File: ui_graveyard.h
 *
 * The dead, browsable.
 *
 * A roguelike is a place you come back to rather than a game you finished,
 * and the thing that makes it one is being able to look at who you were last
 * time. The game has always recorded every run in lib/apex/scores.txt -- two
 * hundred lines here already -- and offered them only as a high-score table
 * sorted by a number, which is the least interesting question you can ask of
 * them.
 *
 * This is the same records read as a list of people: who they were, how far
 * they got, and what finally did it. Choose one and it opens the card that
 * run ended on, with a picture of whatever killed them.
 *
 * Nothing new is written. The score file is upstream's, it is already kept up
 * to date on every death, and it has more history in it than anything we
 * could start recording today.
 *
 * Copyright (c) 2026 John Horton.
 *
 * Added since the fork and offered on the same terms as the rest of the
 * tree -- the Angband licence in LICENSE.
 */

#ifndef INCLUDED_UI_GRAVEYARD_H
#define INCLUDED_UI_GRAVEYARD_H

/* Browse the dead. Returns when the player backs out. */
extern void ui_graveyard(void);

/*
 * Show the card for one past run, full screen.
 *
 * The death card's page, filled from a score record instead of a live game --
 * literally ui_card_show(), not a second layout that looks like it. The page
 * you get when you die and the page you get looking that run up years later
 * are the same page, or the second reads as a summary of the first rather
 * than as the thing itself.
 *
 * A score record holds less than a live game does: no best kill, no unopened
 * scroll, no last words. Those lines are simply absent rather than blank.
 */
extern void ui_graveyard_card(score_ptr score);

#endif
