/*
 * File: py_screen.c
 *
 * The character screen: what the player sees when they press C.
 *
 * This is not the character dump. The dump lives in py_info.c and is right for
 * its job -- twenty sections in one linear document, written to a file and
 * posted to a ladder, where being long is a feature.
 *
 * We were showing the player that file. Two hundred and fifty-five lines in a
 * single scroll, laid out for eighty columns by twenty-four rows, on a display
 * that is a hundred and sixteen by thirty-four. Two fifths of it was empty
 * while the reader scrolled.
 *
 * So this pages instead of scrolling. Four pages, one screen each, and each
 * one answers a question a player actually has rather than reciting fields in
 * the order the struct happens to store them:
 *
 *      1  Who you are        identity, condition, aptitude
 *      2  What you carry     equipment, and what it protects you from
 *      3  What you can do    blows, shots, powers, spells
 *      4  What you have done depths, kills, history
 *
 * Only page one is built. The others announce themselves so the shape of the
 * thing is visible before the rest is written.
 *
 * Copyright (c) 2026 John Horton.
 *
 * Added since the fork and offered on the same terms as the rest of the
 * tree -- the Angband licence in LICENSE.
 */

#include "scottish.h"

#define _PAGE_CT      5
#define _PORTRAIT_W  18
#define _PORTRAIT_H  11

/* Defined below, with the rest of the verdict machinery. */
static void _who_verdict(doc_ptr doc);

static cptr _page_name[_PAGE_CT] =
{
    "Who you are",
    "What you carry",
    "What you can do",
    "What you have done",
    "What you owe"
};


/************************************************************************
 * Small helpers
 ***********************************************************************/

/* Skill ratings come out of the same place the dump gets them, so the two
 * can never disagree about how good you are at hitting things. */
static void _skill(doc_ptr doc, cptr name, int amt, int div)
{
    skill_desc_t desc = skills_describe(amt, div);

    doc_printf(doc, " <color:W>%-12.12s</color> <color:%c>%s</color>",
               name, attr_to_attr_char(desc.color), desc.desc);
    if (p_ptr->wizard || display_skill_num)
        doc_printf(doc, " <color:D>(%d)</color>", amt);
    doc_newline(doc);
}

/* A stat, shown drained if it is drained -- the dump's own rule. */
static void _stat(doc_ptr doc, int i)
{
    char buf[16];

    if (p_ptr->stat_use[i] < p_ptr->stat_top[i])
    {
        cnv_stat(p_ptr->stat_use[i], buf);
        doc_printf(doc, " <color:W>%-4.4s</color><color:y>%7.7s</color>"
                        " <color:D>of %.7s</color>\n",
                   stat_names_reduced[i], buf,
                   (cnv_stat(p_ptr->stat_top[i], buf), buf));
    }
    else
    {
        cnv_stat(p_ptr->stat_use[i], buf);
        doc_printf(doc, " <color:W>%-4.4s</color><color:G>%7.7s</color>\n",
                   stat_names[i], buf);
    }
}


/************************************************************************
 * Page 1 -- Who you are
 ***********************************************************************/

/* Repeat the character identity and elapsed time on every sheet page.
 * Keep the heading and content start rows consistent across tabs. */
static void _screen_header(doc_ptr doc)
{
    race_t  *race_ptr  = get_race();
    class_t *class_ptr = get_class();
    char     left[160], right[80];
    int      w = doc_width(doc), pad;

    /* Built as plain text first so the padding can be measured. Colour markup
     * is invisible on screen and very visible to strlen.
     *
     * Bounded, because every one of these strings comes out of a data file:
     * a race with a long name and a class with another is not a crash today,
     * and a plain sprintf into a fixed buffer is not a thing to publish. */
    my_strcpy(left, player_name[0] ? player_name : "Nameless", sizeof(left));
    my_strcat(left, "   ", sizeof(left));
    my_strcat(left, race_ptr ? race_ptr->name : "?", sizeof(left));
    my_strcat(left, " ", sizeof(left));
    my_strcat(left, class_ptr ? class_ptr->name : "?", sizeof(left));
    if (p_ptr->realm1)
    {
        my_strcat(left, " of ", sizeof(left));
        my_strcat(left, realm_names[p_ptr->realm1], sizeof(left));
    }

    {
        int day, hour, min;
        extract_day_hour_min(&day, &hour, &min);
        my_strcpy(right,
                  format("level %d   day %d, %d:%02d   %d kill%s",
                         p_ptr->lev, day, hour, min, (int)p_ptr->count,
                         p_ptr->count == 1 ? "" : "s"),
                  sizeof(right));
    }

    doc_printf(doc, " <color:B>%s</color>",
               player_name[0] ? player_name : "Nameless");

    if (p_ptr->total_winner)     doc_insert(doc, " <color:v>*** WINNER ***</color>");
    else if (p_ptr->is_dead)     doc_insert(doc, " <color:r>*** DEAD ***</color>");

    doc_printf(doc, "   <color:w>%s %s</color>",
               race_ptr ? race_ptr->name : "?",
               class_ptr ? class_ptr->name : "?");
    if (p_ptr->realm1)
        doc_printf(doc, " <color:D>of</color> <color:w>%s</color>",
                   realm_names[p_ptr->realm1]);

    pad = w - 1 - (int)strlen(left) - (int)strlen(right) - 1;
    if (p_ptr->total_winner || p_ptr->is_dead) pad -= 15;
    if (pad < 2) pad = 2;
    doc_printf(doc, "%*s", pad, "");

    doc_printf(doc, "<color:D>level</color> <color:W>%d</color>", p_ptr->lev);
    {
        int day, hour, min;
        extract_day_hour_min(&day, &hour, &min);
        doc_printf(doc, "   <color:D>day %d, %d:%02d   %d kill%s</color>",
                   day, hour, min, (int)p_ptr->count,
                   p_ptr->count == 1 ? "" : "s");
    }
    doc_newline(doc);
    doc_newline(doc);
}

/* The middle column: how you are doing right now. */
static void _who_condition(doc_ptr doc)
{
    doc_insert(doc, "<style:heading>Condition</style>\n");
    doc_newline(doc);

    doc_printf(doc, " <color:W>%-12.12s</color> <color:%c>%d</color>"
                    "<color:D>/%d</color>\n", "Hit points",
               (p_ptr->chp >= p_ptr->mhp) ? 'G'
                 : (p_ptr->chp > p_ptr->mhp / 2) ? 'y' : 'r',
               p_ptr->chp, p_ptr->mhp);

    if (p_ptr->msp)
        doc_printf(doc, " <color:W>%-12.12s</color> <color:%c>%d</color>"
                        "<color:D>/%d</color>\n", "Spell points",
                   (p_ptr->csp >= p_ptr->msp) ? 'G' : 'y',
                   p_ptr->csp, p_ptr->msp);

    doc_printf(doc, " <color:W>%-12.12s</color> <color:G>%d</color>\n",
               "Armour", p_ptr->dis_ac + p_ptr->dis_to_a);

    {
        int speed = p_ptr->pspeed - 110;
        doc_printf(doc, " <color:W>%-12.12s</color> <color:%c>%+d</color>\n",
                   "Speed", speed > 0 ? 'G' : speed < 0 ? 'r' : 'w', speed);
    }

    doc_newline(doc);
    doc_insert(doc, "<style:heading>Standing</style>\n");
    doc_newline(doc);

    doc_printf(doc, " <color:W>%-12.12s</color> <color:G>%d</color>\n",
               "Experience", p_ptr->exp);
    if (p_ptr->lev < PY_MAX_LEVEL)
    {
        s32b need = (s32b)(exp_requirement(p_ptr->lev) * p_ptr->expfact / 100L)
                    - p_ptr->exp;
        if (need < 0) need = 0;
        doc_printf(doc, " <color:W>%-12.12s</color> <color:y>%d</color>\n",
                   "Next level", need);
    }
    doc_printf(doc, " <color:W>%-12.12s</color> <color:y>%d</color>\n",
               "Gold", p_ptr->au);
}

/* The right column: what you are good at. Same numbers as the dump. */
static void _who_aptitude(doc_ptr doc)
{
    skills_t skills = p_ptr->skills;
    int      slot   = equip_find_obj(TV_BOW, SV_ANY);

    doc_insert(doc, "<style:heading>Aptitude</style>\n");
    doc_newline(doc);

    skills.thn += p_ptr->to_h_m * BTH_PLUS_ADJ;
    if (slot)
    {
        object_type *bow = equip_obj(slot);
        if (bow)
            skills.thb += (p_ptr->shooter_info.to_h + bow->to_h) * BTH_PLUS_ADJ;
    }
    if (!skills.stl) skills.stl = -1;   /* forces "Very Bad", as the dump does */

    _skill(doc, "Melee",     skills.thn, 12);
    _skill(doc, "Archery",   skills.thb, 12);
    _skill(doc, "Saving",    skills.sav, 7);
    _skill(doc, "Stealth",   skills.stl, 1);
    _skill(doc, "Perception", skills.fos, 6);
    _skill(doc, "Searching", skills.srh, 6);
    _skill(doc, "Disarming", skills.dis, 8);
    _skill(doc, "Devices",   skills.dev, 7);
}

/* How deep you have been. The day, the clock and the kills are in the header
 * where they are true of every page; this is the one that needs a sentence. */
static void _who_footline(doc_ptr doc)
{
    doc_newline(doc);
    {
        /* There is no single "deepest ever" -- max_dlv is per dungeon -- so
         * the deepest floor reached anywhere is the max across all of them. */
        int i, deepest = 0;
        for (i = 1; i < max_d_idx; i++)
            if (max_dlv[i] > deepest) deepest = max_dlv[i];
        if (deepest)
            doc_printf(doc, " <color:D>Deepest you have been: %d ft.</color>",
                       deepest * 50);
        else
            doc_insert(doc, " <color:D>You have not been down yet.</color>");
    }
    doc_newline(doc);
}

static void _page_who(doc_ptr doc)
{
    doc_ptr cols[4];
    int     w = doc_width(doc);
    int     mid, right;

    /* Four columns: the picture and the stats under it, how you are, what you
     * are good at, and what will kill you. The picture's box is left empty on
     * purpose -- a frontend that can draw fills it, one that cannot shows a
     * blank and the page still reads. */
    mid   = (w - _PORTRAIT_W - 6) / 3;
    right = w - _PORTRAIT_W - 6 - mid * 2;
    if (mid < 18) { mid = 18; right = 18; }

    cols[0] = doc_alloc(_PORTRAIT_W);
    cols[1] = doc_alloc(mid);
    cols[2] = doc_alloc(mid);
    cols[3] = doc_alloc(right);

    {
        int i;
        for (i = 0; i < _PORTRAIT_H; i++) doc_newline(cols[0]);
        doc_insert(cols[0], "<style:heading>Stats</style>\n");
        doc_newline(cols[0]);
        for (i = 0; i < MAX_STATS; i++) _stat(cols[0], i);
    }

    _who_condition(cols[1]);
    _who_aptitude(cols[2]);
    _who_verdict(cols[3]);

    if (doc_cursor(doc).x > 0) doc_newline(doc);

    /*
     * The player's own portrait, looked up by race name. There may be no plate
     * for it -- the collection is monsters -- in which case the box stays
     * empty and nothing is lost. This is the one thing on the screen that the
     * terminal this game grew out of could never have done.
     */
    {
        race_t *race_ptr = get_race();
        doc_add_portrait(doc, UI_ART_MONSTER,
                         race_ptr ? race_ptr->name : "Human",
                         _PORTRAIT_W, _PORTRAIT_H,
                         TERM_WHITE, '@');
    }

    doc_insert_cols(doc, cols, 4, 2);
    doc_newline(doc);

    _who_footline(doc);

    doc_free(cols[0]);
    doc_free(cols[1]);
    doc_free(cols[2]);
    doc_free(cols[3]);
}


/************************************************************************
 * The verdict
 *
 * The one thing on this screen that does something rather than reports.
 *
 * A character sheet full of numbers answers "what are my numbers". A player
 * opening it is usually asking "am I safe going down", and the answer is
 * almost always one of half a dozen well-known holes. Paralysis kills more
 * characters in this family of games than anything else, and it kills them
 * while they are reading a sheet that never mentioned it.
 *
 * So: the holes, in the order they will actually kill you, weighted by how
 * deep you are. Shallow characters are told very little, because at fifty feet
 * very little is true.
 ***********************************************************************/

typedef struct { cptr what; cptr why; int weight; } _gap_t;

static int _depth_now(void)
{
    int i, deepest = 0;
    for (i = 1; i < max_d_idx; i++)
        if (max_dlv[i] > deepest) deepest = max_dlv[i];
    return MAX(deepest, dun_level);
}

static int _collect_gaps(_gap_t *gaps, int max)
{
    int depth = _depth_now();
    int ct = 0;

    #define _GAP(cond, w, a, b)         do { if ((cond) && ct < max) { gaps[ct].what = (a); gaps[ct].why = (b);              gaps[ct].weight = (w); ct++; } } while (0)

    /* The one that does the killing. */
    _GAP(!p_ptr->free_act && depth >= 8, depth >= 25 ? 100 : 60,
         "no Free Action",
         "Held still is how characters die at this depth");

    _GAP(!p_ptr->see_inv && depth >= 12, depth >= 20 ? 70 : 35,
         "no See Invisible",
         "There are things down there you cannot fight back at");

    {
        int missing = 0;
        if (res_pct(RES_ACID) <= 0) missing++;
        if (res_pct(RES_ELEC) <= 0) missing++;
        if (res_pct(RES_FIRE) <= 0) missing++;
        if (res_pct(RES_COLD) <= 0) missing++;
        _GAP(missing >= 3 && depth >= 15, 65,
             "little resistance to the elements",
             "A breath will take most of your hit points and half your pack");
        _GAP(missing > 0 && missing < 3 && depth >= 25, 30,
             "gaps in the elemental resistances",
             "Survivable, but it is what makes a bad turn fatal");
    }

    _GAP(!p_ptr->hold_life && depth >= 30, 45,
         "no Hold Life",
         "Drain takes back levels faster than you earn them");

    _GAP(res_pct(RES_CONF) <= 0 && depth >= 25, 40,
         "no resistance to confusion",
         "Confused, you cannot read the scroll that would save you");

    _GAP(res_pct(RES_BLIND) <= 0 && depth >= 25, 35,
         "no resistance to blindness",
         "The same reason, and you will not see what is doing it");

    _GAP(res_pct(RES_POIS) <= 0 && depth >= 35, 30,
         "no resistance to poison",
         "The deep breathers do not need many turns");

    _GAP(p_ptr->pspeed < 110 && depth >= 10, 55,
         "slower than everything else",
         "You cannot outrun a mistake");

    _GAP(depth >= 20 && p_ptr->mhp < depth * 8, 50,
         "thin for the depth",
         "One unlucky exchange is the whole of your hit points");

    #undef _GAP
    return ct;
}

static void _who_verdict(doc_ptr doc)
{
    _gap_t gaps[12];
    int    ct = _collect_gaps(gaps, 12);
    int    i, j, shown = 0;

    doc_insert(doc, "<style:heading>Going down</style>\n");
    doc_newline(doc);

    if (!ct)
    {
        doc_insert(doc, " <color:G>Nothing obvious will kill you.</color>\n");
        doc_insert(doc, " <color:D>Which is not the same as nothing.</color>\n");
        return;
    }

    /* Worst first. A simple sort: there are never more than a dozen. */
    for (i = 0; i < ct; i++)
        for (j = i + 1; j < ct; j++)
            if (gaps[j].weight > gaps[i].weight)
            { _gap_t t = gaps[i]; gaps[i] = gaps[j]; gaps[j] = t; }

    doc_printf(doc, " <color:%c>You have %s.</color>\n",
               gaps[0].weight >= 70 ? 'r' : gaps[0].weight >= 45 ? 'o' : 'y',
               gaps[0].what);
    doc_printf(doc, " <color:D>%s.</color>\n", gaps[0].why);

    for (i = 1; i < ct && shown < 3; i++, shown++)
        doc_printf(doc, "\n<color:y>Also %s.</color>\n<color:D>%s.</color>\n",
                   gaps[i].what, gaps[i].why);
}


/************************************************************************
 * Pages 2 to 4
 *
 * These show the dump's own sections, built by the dump's own code -- see
 * py_dump_section(). Nothing here restates what a section says; what the
 * screen changes is which of them you are looking at and how much room it
 * gets. A section that has nothing to report says so rather than leaving a
 * hole, because a blank page reads as a fault.
 ***********************************************************************/

static void _nothing(doc_ptr doc, cptr what)
{
    doc_printf(doc, " <color:D>%s</color>\n\n", what);
}

/************************************************************************
 * Page 2 -- What you carry
 *
 * The dump lists every resistance and every ability, including the two dozen
 * you do not have, each as a row of dots. That is right for a file somebody
 * greps and wrong for a screen somebody reads: eighteen rows of nothing to
 * find the two rows of something.
 *
 * So this shows what you HAVE, one to a line, and then names what you lack in
 * a running sentence -- which fits in two lines where the grid took twenty,
 * and which is the form the question is actually asked in. The per-slot
 * breakdown is still in the dump for anyone who wants to know which ring it
 * was.
 ***********************************************************************/

typedef struct { cptr name; bool has; } _ability_t;

static int _abilities(_ability_t *out, int max)
{
    int ct = 0;

    #define _AB(n, c) do { if (ct < max) { out[ct].name = (n); \
                          out[ct].has = (c) ? TRUE : FALSE; ct++; } } while (0)

    _AB("Free Action",   p_ptr->free_act);
    _AB("See Invisible", p_ptr->see_inv);
    _AB("Hold Life",     p_ptr->hold_life);
    _AB("Telepathy",     p_ptr->telepathy);
    _AB("Levitation",    p_ptr->levitation);
    _AB("Reflection",    p_ptr->reflect);
    _AB("Permanent light", p_ptr->lite);
    _AB("Slow digestion", p_ptr->slow_digest);
    _AB("Regeneration",  p_ptr->regen > 100);
    #undef _AB

    return ct;
}

/* "acid, fire and cold" -- a list a person would say out loud. */
static void _run_on(doc_ptr doc, cptr *items, int ct, cptr colour)
{
    int i;

    /* The separator goes *after* the word it follows, not before the word it
     * precedes. Written the other way round the comma is the first thing on
     * the next line every time the list wraps, which it does whenever the
     * list is long enough to be worth reading. */
    for (i = 0; i < ct; i++)
    {
        char buf[64];

        /* The comma is part of the word, not a token of its own. Inserted
         * separately it is a run the wrapper can break in front of, and it
         * spent every long list sitting at the start of a line. */
        my_strcpy(buf, items[i], sizeof(buf) - 1);
        if (i + 2 < ct) my_strcat(buf, ",", sizeof(buf));
        doc_printf(doc, "<color:%s>%s</color>", colour, buf);
        if (i + 2 < ct)       doc_insert(doc, " ");
        else if (i + 2 == ct) doc_insert(doc, " and ");
    }
}

static void _page_carry(doc_ptr doc)
{
    doc_ptr     cols[2];
    cptr        gone[RES_MAX];
    cptr        missing[16];
    _ability_t  ab[16];
    int         i, ct, gone_ct = 0, missing_ct = 0, held = 0;

    if (!equip_count_used())
    {
        doc_insert(doc, "\n <color:D>You are wearing and wielding nothing at "
                        "all.</color>\n");
        return;
    }

    /* What you are actually wearing. Short, and the point of the page. */
    doc_insert(doc, "<style:heading>Worn and wielded</style>\n\n");
    for (i = 1; i <= equip_max(); i++)
    {
        object_type *o_ptr = equip_obj(i);
        char         o_name[MAX_NLEN];

        if (!o_ptr) continue;
        object_desc(o_name, o_ptr, OD_COLOR_CODED);
        doc_printf(doc, " <color:y>%c</color><color:D>)</color> %s\n",
                   i - 1 + 'a', o_name);
    }
    doc_newline(doc);

    cols[0] = doc_alloc((doc_width(doc) - 2) / 2);
    cols[1] = doc_alloc(doc_width(doc) - 2 - doc_width(cols[0]));

    /* Left: what protects you, and then what does not. */
    doc_insert(cols[0], "<style:heading>Protection</style>\n\n");
    for (i = RES_BEGIN; i <= RES_END; i++)
    {
        int pct = res_pct(i);

        if (!pct)
        {
            gone[gone_ct++] = res_name(i);
            continue;
        }
        held++;
        doc_printf(cols[0], " <color:W>%-14.14s</color> <color:%c>%+d%%</color>\n",
                   res_name(i), pct > 0 ? 'G' : 'r', pct);
    }
    if (!held)
        doc_insert(cols[0], " <color:D>Nothing at all.</color>\n");

    if (gone_ct)
    {
        doc_newline(cols[0]);
        doc_insert(cols[0], " <color:D>Nothing against ");
        doc_insert(cols[0], "<indent>");
        _run_on(cols[0], gone, gone_ct, "u");
        doc_insert(cols[0], "</indent>.</color>\n");
    }

    /* Right: what you can do, and then what you cannot. */
    doc_insert(cols[1], "<style:heading>Abilities</style>\n\n");
    ct = _abilities(ab, 16);
    held = 0;
    for (i = 0; i < ct; i++)
    {
        if (ab[i].has)
        {
            held++;
            doc_printf(cols[1], " <color:G>%s</color>\n", ab[i].name);
        }
        else if (missing_ct < 16)
            missing[missing_ct++] = ab[i].name;
    }
    if (!held)
        doc_insert(cols[1], " <color:D>None of the useful ones.</color>\n");

    if (missing_ct)
    {
        doc_newline(cols[1]);
        doc_insert(cols[1], " <color:D>No ");
        doc_insert(cols[1], "<indent>");
        _run_on(cols[1], missing, missing_ct, "u");
        doc_insert(cols[1], "</indent>.</color>\n");
    }

    doc_insert_cols(doc, cols, 2, 2);
    doc_free(cols[0]);
    doc_free(cols[1]);

    /* What your kit is doing to your stats, if anything. */
    {
        bool any = FALSE;
        for (i = 0; i < MAX_STATS; i++) if (p_ptr->stat_add[i]) any = TRUE;
        if (any)
        {
            doc_newline(doc);
            /* stat_add is every modifier, not just the equipment's -- race
             * and class are in there too. Isolating the kit's share means the
             * dump's flagzilla, which is a great deal of machinery to answer
             * one question, so the heading says what the number really is. */
            doc_insert(doc, "<style:heading>Stat modifiers</style>\n");
            doc_insert(doc, " <color:D>race, class and kit together</color>\n\n ");
            for (i = 0; i < MAX_STATS; i++)
            {
                if (!p_ptr->stat_add[i]) continue;
                doc_printf(doc, "<color:W>%s</color> <color:%c>%+d</color>   ",
                           stat_names_reduced[i],
                           p_ptr->stat_add[i] > 0 ? 'G' : 'r',
                           p_ptr->stat_add[i]);
            }
            doc_newline(doc);
        }
    }
}

/*
 * The dump's sections, laid into columns instead of poured down the page.
 *
 * py_info.c writes these for a file: one section after another, eighty
 * columns wide, as long as it needs to be. That is right for a dump and wrong
 * for a screen -- on a hundred-and-thirteen column display it left two fifths
 * of the width empty and still ran off the bottom.
 *
 * The sections themselves are not touched. Each is written into a column of
 * its own and the columns are set side by side, so the dump and the screen
 * cannot disagree about a number and the screen still has a shape.
 */
/*
 * Full width, stacked, and it scrolls.
 *
 * The first attempt set these side by side to use the width, and the tables
 * inside them came apart: py_info.c positions its columns with tab stops
 * measured for a page about eighty wide, so a fifty-six column column wrapped
 * the Powers table's header away from its own numbers. The sections are
 * shared with the character dump and are not ours to restyle. So they get the
 * room they were written for, and the page scrolls, which it now says.
 */
static void _page_do(doc_ptr doc)
{
    int before = doc_cursor(doc).y;

    py_dump_section(doc, PY_SECT_MELEE);
    py_dump_section(doc, PY_SECT_SHOOTING);
    py_dump_section(doc, PY_SECT_POWERS);
    py_dump_section(doc, PY_SECT_SPELLS);
    py_dump_section(doc, PY_SECT_MUTATIONS);

    if (doc_cursor(doc).y == before)
        _nothing(doc, "No blows, no shots, no powers, no spells. Yet.");
}

static void _page_done(doc_ptr doc)
{
    int before = doc_cursor(doc).y;

    /* Quests have a page of their own now, so they are not here twice. */
    py_dump_section(doc, PY_SECT_DUNGEONS);
    py_dump_section(doc, PY_SECT_UNIQUES);
    py_dump_section(doc, PY_SECT_PETS);
    py_dump_section(doc, PY_SECT_HISTORY);
    py_dump_section(doc, PY_SECT_STATISTICS);

    if (doc_cursor(doc).y == before)
        _nothing(doc, "Nothing yet. That is what the game is for.");
}

/*
 * Page five -- what you owe.
 *
 * A compact textual account for the sheet; J opens the full quest journal.
 * Both read the same quest state. See quests_build_doc().
 */
static void _page_owe(doc_ptr doc)
{
    int before;
    doc_insert(doc, "<color:y>J) Open quest journal</color>\n\n");
    doc_add_hotspot_at(doc, 0, 'j');
    before = doc_cursor(doc).y;

    quests_build_doc(doc);

    if (doc_cursor(doc).y == before)
        _nothing(doc, "Nobody has asked you for anything yet.");
}


/************************************************************************
 * The screen
 ***********************************************************************/

/* Use screenshot style to prevent wrapping the tab bar and losing its hint.
 * Tabs are not numbered: keys 4 and 6 already navigate left and right. */
static void _footer(doc_ptr doc, int page, bool more, bool back)
{
    int i;

    doc_insert(doc, "<style:screenshot>");
    doc_newline(doc);

    for (i = 0; i < _PAGE_CT; i++)
    {
        doc_printf(doc, " <color:%s>%s</color>",
                   i == page ? "B" : "D", _page_name[i]);
        if (i < _PAGE_CT - 1) doc_insert(doc, "  <color:D>|</color>");
    }
    doc_newline(doc);

    doc_insert(doc, " <color:D>4 6 or the arrows turn the page");
    if (more || back)
        doc_insert(doc, ", 2 8 scroll it, a letter jumps to its section");
    doc_insert(doc, ", ESC puts it down.</color>\n");
    doc_insert(doc, "</style>");
}

/*
 * One frame for all five pages.
 *
 * It used to draw at the top left corner of the terminal with the tabs pinned
 * three rows off the bottom, so the content floated in the middle of a screen
 * with no edges and page four ran underneath the tab bar. It takes the same
 * box every other full-screen page in the game takes -- see ui_page_rect() --
 * with the header on the first row, the tabs on the last, and the pages in
 * between all starting on the same line.
 */
void py_screen(void)
{
    rect_t  box   = ui_page_rect();
    doc_ptr head  = doc_alloc(box.cx);
    doc_ptr doc   = doc_alloc(box.cx);
    doc_ptr foot  = doc_alloc(box.cx);
    int     page = 0, top = 0;

    screen_save();
    ui_screen_layer_push(UI_LAYER_MENU);

    for (;;)
    {
        /* Three rows of header, three of footer, and the rest is the page. */
        int cmd, avail = box.cy - 6, lines, more;

        if (avail < 4) avail = 4;

        doc_clear(head);
        doc_insert(head, "<style:screenshot>");
        _screen_header(head);
        doc_insert(head, "</style>");

        doc_clear(doc);

        /*
         * Screenshot style means "do not wrap this", which is right for four
         * pages of tables and wrong for the fifth. The quest page is prose --
         * whole paragraphs of somebody explaining what they want -- and
         * unwrapped prose runs off the right-hand edge and is simply gone.
         */
        if (page != 4) doc_insert(doc, "<style:screenshot>");

        switch (page)
        {
        case 0:  _page_who(doc);   break;
        case 1:  _page_carry(doc); break;
        case 2:  _page_do(doc);    break;
        case 3:  _page_done(doc);  break;
        default: _page_owe(doc);   break;
        }

        if (page != 4) doc_insert(doc, "</style>");

        lines = doc_line_count(doc);
        if (top > lines - avail) top = lines - avail;
        if (top < 0) top = 0;
        more = (lines > top + avail);

        /*
         * A rule and a portrait outlive the page that declared them: the doc
         * only ever says "there is one here", never "there is not one any
         * more", so page two was being drawn over page one's picture with
         * page one's rule struck across it. Clearing the menu state first
         * forgets both, and whatever this page has is declared again below.
         */
        ui_event_menu_clear();
        Term_clear();
        ui_event_panel_screen();

        doc_sync_term(head, doc_range_all(head),
                      doc_pos_create(box.x, box.y));
        doc_sync_term(doc, doc_range_lines_from(doc, top, avail),
                      doc_pos_create(box.x, box.y + 3));

        doc_clear(foot);
        _footer(foot, page, more, top > 0);
        doc_sync_term(foot, doc_range_all(foot),
                      doc_pos_create(box.x, box.y + box.cy - 3));

        /* Said here rather than with doc_add_rule() because the rule belongs
         * to the page and a document's rule is only ever as wide as its own
         * longest line -- which is the tab bar, two thirds of the way
         * across. Last, so it is the one the frontend keeps. */
        ui_event_rule(box.x, box.y + box.cy - 3, box.cx);

        /* The page scrolls and the reader has no way of knowing unless we
         * say so; the frontend draws the arrows on the frame. */
        ui_event_scroll(BOOL(top > 0), BOOL(more));
        Term_fresh();

        cmd = inkey_special(TRUE);

        if (cmd == ESCAPE || cmd == 'q' || cmd == 'Q') break;

        if (page == 4 && (cmd == 'j' || cmd == 'J'))
        {
            quests_display();
            continue;
        }

        if (cmd == SKEY_RIGHT || cmd == '6')
            { page = (page + 1) % _PAGE_CT; top = 0; }
        else if (cmd == SKEY_LEFT || cmd == '4')
            { page = (page + _PAGE_CT - 1) % _PAGE_CT; top = 0; }
        else if (cmd == SKEY_DOWN || cmd == '2')
            top++;
        else if (cmd == SKEY_UP || cmd == '8')
            top--;
        else if (cmd == ' ' || cmd == SKEY_PGDOWN)
            top += avail - 2;
        else if (cmd == SKEY_PGUP)
            top -= avail - 2;
        else
        {
            /*
             * Anything else jumps to the section whose name starts with it.
             *
             * Which is where the orange first letter in "Melee", "Dungeons"
             * and the rest comes from: those headings are written for the
             * document viewer, where any unhandled key does exactly this, and
             * the colour marks the key. Borrowed onto a screen that ignored
             * them, the highlight was a promise nothing kept -- and it was on
             * some headings and not others, because pages one and two are
             * ours and pages three and four are the dump's.
             *
             * Cheaper to keep the promise than to erase it.
             */
            doc_pos_t pos = doc_next_bookmark_char(doc, doc_pos_create(1, top),
                                                   cmd);

            if (!doc_pos_is_valid(pos))
                pos = doc_next_bookmark_char(doc, doc_pos_create(0, 0), cmd);
            if (doc_pos_is_valid(pos)) top = pos.y;
        }
    }

    ui_screen_layer_pop(UI_LAYER_MENU);
    screen_load();
    doc_free(foot);
    doc_free(doc);
    doc_free(head);
}
