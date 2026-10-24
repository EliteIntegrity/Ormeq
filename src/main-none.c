/*
 * File: main-none.c
 *
 * A frontend with no display, for playing the game without watching it.
 *
 * Everything we have built so far verifies that the data is well *formed*:
 * every reference resolves, every portrait matches a creature, every option
 * has a paragraph. Not one of those checks can tell you that the game can be
 * *played* -- that a quest can be finished, that a level generates, that a
 * shop opens. The only thing that can answer that is playing it, and a human
 * playing it takes weeks.
 *
 * So this runs the game with a Term that draws nothing, takes its keys from
 * the command line, and prints one line saying where it got to. It is the
 * same game: the same dungeon generation, the same monsters, the same code
 * path. It simply has nobody looking at it.
 *
 * Two things make a run worth having:
 *
 *   -s<n> seeds the RNG, so the same number is the same game every time. A
 *   crash you cannot reproduce is a rumour; a crash with a seed is a bug.
 *
 *   The run always ends. When the script runs out the Term feeds a bounded
 *   run of Escapes to back out of whatever is open, then stops. A headless
 *   game that blocks forever waiting for a key nobody will press is worse
 *   than no headless mode at all.
 *
 * Copyright (c) 2026 John Horton.
 *
 * Added since the fork and offered on the same terms as the rest of the
 * tree -- the Angband licence in LICENSE.
 */

#include "scottish.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
static int _test_crt_report(int type, char *message, int *result)
{
    if(type==_CRT_ASSERT || type==_CRT_ERROR)
    { fputs(message,stderr); exit(3); }
    return FALSE;
}
#endif

/************************************************************************
 * The script
 *
 * Same convention as the SDL3 port's -x, because a script written for one
 * should mean the same thing to the other: '~' is Return, '^' is Escape, a
 * backslash sends the next character exactly as it is, and the arrows have
 * no representation here because there is nothing to arrow around.
 ************************************************************************/
static cptr none_script = NULL;
static int  none_at = 0;

/* How many Escapes to try before giving up, once the script is spent. */
#define NONE_UNWIND 64
static int none_unwind = 0;
static bool none_done = FALSE;

static u32b none_seed = 0;
static bool none_strict = FALSE;    /* -Q: no guessed keys or Escape unwind */
static bool none_identities = FALSE; /* -I: list live race/class identities */
static cptr none_expressions = NULL; /* -E<file>: exercise both rule readers */
static bool none_home = FALSE;      /* -H: exercise the start-screen loop */
static int none_host_settings = 0;  /* -U1/2/3: rows/key/both host callbacks */
static int none_host_shape = 0;
static int none_sessions = 0;
static bool none_quiet = FALSE;
static bool none_verbose = FALSE;   /* -v: say every key as it goes in */
static bool none_debug = FALSE;     /* -d: allow the debug commands */
static int  none_depth = 0;         /* -D<n>: begin on this dungeon floor */
static int  none_dungeon = 1;       /* -Y<n>: which dungeon (1 is The Sink) */
static int  none_floors = 1;        /* -N<n>: how many floors to visit */
static int  none_stride = 400;      /* -T<n>: game turns spent on each */
static bool none_dived = FALSE;
static s32b none_floor_at = 0;      /* the turn the next descent is due */
static int  none_visited = 0;
static bool none_immortal = FALSE;  /* -i: do not let the test die */
static bool none_repeat = FALSE;    /* -r: play the script round again */
static int  none_rewind = -1;       /* where round again starts from */
static int  none_laps = 0;
static s32b none_seen_turn = -1;    /* the last turn we saw time move */
static int  none_idle = 0;          /* keys fed since it last moved */
static int  none_stalls = 0;        /* how many times it stopped moving */
static int  none_knocks = 0;        /* keys spent getting past prompts */

/* Everything the run is asked to report. Collected as it goes, because after
 * quit() there is nothing left to ask. */
static struct {
    long turns;
    int  depth, deepest, clev;
    int  chp, mhp;
    int  x, y;
    s32b gold, exp;
    int  stat[6];
    int  race, cls;
    u32b flavor, town;
    int  noscore, wiz, dtype;
    bool dead;
    bool started;
    char killer[80];
} none_log;


/*
 * What is on the screen.
 *
 * There is no screen -- but Term keeps its own grid of characters whatever
 * the display does with them, so a run with no window can still be asked
 * what it would have shown. It is the only way to find out why a headless
 * game has stopped: something is asking a question, and this is the question.
 */
static void none_screen(cptr why)
{
    int y, x, w, h;

    Term_get_size(&w, &h);
    if (w > 250) w = 250;
    printf("--- screen (%s) ---\n", why);
    for (y = 0; y < h; y++)
    {
        char line[256];
        int  last = -1;

        for (x = 0; x < w; x++)
        {
            byte a;
            char c;

            if (Term_what(x, y, &a, &c) != 0) c = ' ';
            if (!c) c = ' ';
            line[x] = c;
            if (c != ' ') last = x;
        }
        line[last + 1] = 0;
        if (last >= 0) printf("%2d| %s\n", y, line);
    }
    printf("--- end screen ---\n");
}

/* A simulated embedding host, used only by the headless regression frontend.
 * The playable desktop executables leave the public hooks NULL. */
static void none_settings_rows(doc_ptr doc)
{
    int before = doc_hotspot_count(doc);
    printf("settings-host-after %c\n", doc_hotspot_key(doc, before - 1));
    doc_newline(doc);
    ui_settings_section(doc, "Touch");
    ui_settings_row(doc, 'p', "D-pad shape", none_host_shape ? "cross" : "square");
    printf("settings-host-row %c %d\n", doc_hotspot_key(doc, before),
        doc_hotspot_count(doc) - before);
}

static bool none_settings_key(int key)
{
    bool handled = key == 'p' || key == 'P';
    if (handled) none_host_shape = !none_host_shape;
    printf("settings-host-key %d %d\n", key, handled);
    return handled;
}

static bool none_map_test;
static int none_quest_test = -1;
static bool none_quest_done;
extern void test_quest_probe(int browse);
extern void test_map_probe(void);
static int none_melee = -1;
static bool none_melee_done;
extern void test_melee_probe(int recording);
static int none_audio = -1; /* -A0/1: mute/events; -A2/3: also exercise hooks */
static bool none_audio_done;
extern void test_audio_probe(void);
extern void test_audio_event(int event);
extern void test_audio_report(void);

static void none_note(void)
{
    /*
     * No -more- prompts, asserted on every key rather than once at
     * startup, because the preference files are read after that and put
     * it back. They exist so a person can read a message before the next
     * arrives; nobody is reading. Left on they swallow the whole script:
     * a run that asked for twenty steps took none of them, having spent
     * every keystroke dismissing the messages from being born.
     */
    quick_messages = TRUE;
    if (none_audio >= 0) use_sound = (none_audio & 1) != 0;

    /* And the debug commands, if this run asked for them. Same reason
     * they are set here: the preference files would put them back. */
    if (none_debug) allow_debug_opts = TRUE;

    if (!character_generated) return;
    if (none_quest_test >= 0 && !none_quest_done && character_dungeon && ui_screen_map_owns())
    {
        none_quest_done = TRUE;
        test_quest_probe(none_quest_test == 6 ? 4 : none_quest_test);
        if (none_quest_test != 6) quit(NULL);
    }
    if (none_map_test && character_dungeon && ui_screen_map_owns())
    { test_map_probe(); quit(NULL); }
    if (none_audio >= 2 && !none_audio_done && character_dungeon && ui_screen_map_owns())
    {
        none_audio_done = TRUE;
        test_audio_probe();
        /* The fixture creates an artificial combat arena and stair departure.
         * Report its final state without resuming that synthetic world. */
        quit(NULL);
    }
    if (none_melee >= 0 && !none_melee_done && character_dungeon && ui_screen_map_owns())
    {
        none_melee_done = TRUE;
        test_melee_probe(none_melee);
    }

    /*
     * Start on a floor, and then keep going down.
     *
     * The debug menu can do this, and it is what a person would use --
     * but Ctrl-A never reaches the command loop from a script, and a
     * harness that has to walk a character to a staircase it cannot see
     * is not a harness. These are the same lines the wizard jump runs;
     * the level itself is built by the game exactly as it would be for
     * anybody else, which is the part being tested.
     *
     * Descending is worth testing separately from arriving. Arriving
     * runs the level builder once, on a clean slate. Going down runs it
     * on a floor that has to be taken apart first -- the old level saved
     * or discarded, its monsters and objects released, the character
     * carried across. There is more to go wrong there than in building.
     */
    if (none_depth > 0 && !none_dived)
    {
        none_dived = TRUE;
        none_visited = 1;
        none_floor_at = game_turn + none_stride;
        set_dungeon_type(none_dungeon);
        dun_level = none_depth;
        p_ptr->inside_arena = FALSE;
        p_ptr->wild_mode = FALSE;
        p_ptr->energy_need = 0;
        prepare_change_floor_mode(CFM_RAND_PLACE | CFM_FIRST_FLOOR);
        p_ptr->leaving = TRUE;
    }
    else if (none_dived
          && none_visited < none_floors
          && !p_ptr->is_dead
          && !p_ptr->leaving
          && dun_level > 0
          && game_turn >= none_floor_at
          && dun_level < d_info[dungeon_type].maxdepth)
    {
        none_visited++;
        none_floor_at = game_turn + none_stride;
        /* No dun_level++ here: change_floor() does the descending itself
         * when it is handed CFM_DOWN, and doing it twice went down two. */
        prepare_change_floor_mode(CFM_DOWN | CFM_RAND_PLACE);
        p_ptr->leaving = TRUE;
    }

    /*
     * Where the script proper starts.
     *
     * Everything before this made a character; replaying that would try to
     * make a second one. So a repeating run rewinds to here, and not to the
     * beginning -- which is the difference between walking around for an
     * hour and creating the same character over and over.
     */
    if (none_rewind < 0) none_rewind = none_at;

    /*
     * Has time stopped?
     *
     * A character wedged in a dead end bumps walls for ever: the keys are
     * accepted, nothing happens, and the run spins until something kills
     * it. That is the one failure a headless run must not have, so we
     * watch the clock rather than the keyboard. Standing still for two
     * hundred keys is not a game in progress.
     *
     * When it happens on a dive we go down anyway. Being stuck is worth
     * recording, but it is not worth abandoning the other thirty floors
     * we came to test.
     */
    if (game_turn != none_seen_turn)
    {
        none_seen_turn = game_turn;
        none_idle = 0;
    }
    else if (++none_idle > 200)
    {
        none_idle = 0;
        none_stalls++;
        if (none_stalls == 1 || none_verbose)
        {
            printf("STALL turn %ld depth %d leaving %d energy %d "
                   "para %d stun %d conf %d afraid %d pos %d,%d feat %d\n",
                   (long)game_turn, dun_level, (int)p_ptr->leaving,
                   (int)p_ptr->energy_need, (int)p_ptr->paralyzed,
                   (int)p_ptr->stun, (int)p_ptr->confused, (int)p_ptr->afraid,
                   px, py, (int)cave[py][px].feat);
            if (none_stalls == 1) none_screen("time stopped");
        }
        if (none_visited < none_floors) none_floor_at = game_turn;
        else none_repeat = FALSE;
    }

    /* Enough. Let the script run dry and the run will end itself. */
    if (none_repeat
     && (p_ptr->is_dead
      || (none_visited >= none_floors && game_turn >= none_floor_at)))
    {
        none_repeat = FALSE;
    }

    /*
     * Keep the test alive, if it asked to be.
     *
     * A soak run is not playing the game well and is not meant to: it is
     * there to make forty floors and walk about on them. A first-level
     * character dropped on floor thirty dies to the first thing it meets,
     * and then we have tested one floor. This is a harness switch, not a
     * cheat -- nothing writes a score, and the game is generating those
     * floors exactly as it would for anybody.
     */
    if (none_immortal)
    {
        p_ptr->chp = p_ptr->mhp;
        p_ptr->chp_frac = 0;
        p_ptr->csp = p_ptr->msp;
        if (p_ptr->food < PY_FOOD_ALERT) p_ptr->food = PY_FOOD_FULL - 1;

        /*
         * Topping the hit points up is not enough on its own. It happens
         * when the game asks for a key, and a great deal can happen to a
         * first level character between two keys at depth thirty. So the
         * timer goes on as well, renewed every key -- the same protection
         * a potion gives, just never running out.
         */
        p_ptr->invuln = 100;
    }

    none_log.x = px;
    none_log.y = py;
    none_log.gold = p_ptr->au;
    none_log.exp = p_ptr->exp;
    none_log.started = TRUE;
    none_log.turns   = game_turn;
    none_log.depth   = dun_level;
    none_log.clev    = p_ptr->lev;
    none_log.chp     = p_ptr->chp;
    none_log.mhp     = p_ptr->mhp;
    none_log.dead    = p_ptr->is_dead;
    if (p_ptr->is_dead && p_ptr->died_from[0])
    {
        strncpy(none_log.killer, p_ptr->died_from, sizeof(none_log.killer) - 1);
        none_log.killer[sizeof(none_log.killer) - 1] = 0;
    }
    none_log.race    = p_ptr->prace;
    none_log.cls     = p_ptr->pclass;
    none_log.flavor  = seed_flavor;
    none_log.town    = seed_town;
    none_log.noscore = p_ptr->noscore;
    none_log.wiz     = p_ptr->wizard;
    none_log.dtype   = dungeon_type;
    {
        int i;
        for (i = 0; i < 6; i++) none_log.stat[i] = p_ptr->stat_cur[i];
    }
    if (dun_level > none_log.deepest) none_log.deepest = dun_level;
}


/* Fingerprint persistent item properties and terrain, without reading struct
 * padding, pointer addresses, or consuming gameplay randomness. */
static u32b none_hash_value(u32b hash, u32b value)
{
    return (hash ^ value) * 16777619U;
}

static u32b none_hash_item(u32b hash, obj_ptr obj)
{
    if (!obj) return none_hash_value(hash, 0);
    hash = none_hash_value(hash, obj->k_idx);
    hash = none_hash_value(hash, obj->number);
    hash = none_hash_value(hash, obj->pval);
    hash = none_hash_value(hash, obj->name1);
    hash = none_hash_value(hash, obj->name2);
    hash = none_hash_value(hash, obj->to_h);
    hash = none_hash_value(hash, obj->to_d);
    hash = none_hash_value(hash, obj->to_a);
    hash = none_hash_value(hash, obj->timeout);
    return hash;
}

static void none_fingerprints(void)
{
    u32b items = 2166136261U, terrain = 2166136261U;
    int i, x, y;
    for (i = 1; i <= pack_max(); i++) items = none_hash_item(items, pack_obj(i));
    for (i = 1; i <= equip_max(); i++) items = none_hash_item(items, equip_obj(i));
    for (i = 1; i <= quiver_max(); i++) items = none_hash_item(items, quiver_obj(i));
    terrain = none_hash_value(terrain, cur_wid);
    terrain = none_hash_value(terrain, cur_hgt);
    for (y = 0; y < cur_hgt; y++)
        for (x = 0; x < cur_wid; x++)
        {
            terrain = none_hash_value(terrain, cave[y][x].feat);
            terrain = none_hash_value(terrain, cave[y][x].mimic);
        }
    {
        u32b rng=none_hash_value(2166136261U,Rand_quick);
        rng=none_hash_value(rng,Rand_value); rng=none_hash_value(rng,Rand_place);
        for(i=0;i<RAND_DEG;++i) rng=none_hash_value(rng,Rand_state[i]);
        printf("randomness  %08lx\n",(unsigned long)rng);
    }
    printf("inventory   %08lx\n", (unsigned long)items);
    printf("terrain     %08lx\n", (unsigned long)terrain);
    printf("map owner   %d\n", (int)ui_screen_map_owns());
}


static void none_report(void)
{
    /*
     * Once only, and from wherever the run actually ends.
     *
     * The first version printed this after play_game() returned, and it
     * never printed anything at all: the game leaves through quit(),
     * which exits the process, so control does not come back. Anything
     * a run has to say has to be said from the quit hook as well.
     */
    if (none_done) return;
    none_done = TRUE;
    none_note();

    if (none_quiet) return;
    if (none_audio >= 0) test_audio_report();

    printf("\n--- headless run ---\n");
    printf("keys        %d used, %d escapes to unwind\n",
           none_at, none_unwind);
    if (none_seed) printf("seed        %lu\n", (unsigned long)none_seed);

    if (!none_log.started)
    {
        printf("result      no character was made\n");
        return;
    }

    none_fingerprints();
    printf("sessions    %d\n", none_sessions);
    printf("position    %d,%d\n", none_log.x, none_log.y);
    printf("gold        %ld\n", (long)none_log.gold);
    printf("experience  %ld\n", (long)none_log.exp);
    printf("turns       %ld\n", none_log.turns);
    printf("level       %d\n", none_log.clev);
    printf("hp          %d of %d\n", none_log.chp, none_log.mhp);
    printf("knocks      %d\n", none_knocks);
    printf("stalls      %d\n", none_stalls);
    printf("laps        %d\n", none_laps);
    printf("floors      %d\n", none_visited);
    printf("depth       %d in dungeon %d (deepest %d)\n",
           none_log.depth, none_log.dtype, none_log.deepest);
    printf("noscore     %d, wizard %d\n",
           none_log.noscore, none_log.wiz);
    if (none_log.dead && none_log.killer[0])
        printf("killed by   %s\n", none_log.killer);
    printf("result      %s\n", none_log.dead ? "dead" : "alive");
    printf("who         race %d class %d\n",
           none_log.race, none_log.cls);
    printf("stats       %d %d %d %d %d %d\n",
           none_log.stat[0], none_log.stat[1], none_log.stat[2],
           none_log.stat[3], none_log.stat[4], none_log.stat[5]);
    printf("world       flavours %08lx town %08lx\n",
           (unsigned long)none_log.flavor,
           (unsigned long)none_log.town);
}


/************************************************************************
 * A terminal that draws nothing
 *
 * Term still wants somewhere to put its characters -- it tracks what changed
 * and only sends the difference -- so the hooks have to exist and succeed.
 * They simply do not have to do anything.
 ************************************************************************/
static void Term_init_none(term *t)   { (void)t; }
static void Term_nuke_none(term *t)   { (void)t; }

static errr Term_curs_none(int x, int y)
{
    (void)x; (void)y;
    return 0;
}

static errr Term_wipe_none(int x, int y, int n)
{
    (void)x; (void)y; (void)n;
    return 0;
}

static errr Term_text_none(int x, int y, int n, byte a, cptr s)
{
    (void)x; (void)y; (void)n; (void)a; (void)s;
    return 0;
}


/*
 * One key per call, exactly as the SDL3 port does it -- the game gets to
 * finish whatever it was doing between keys, which is what makes a script
 * mean the same thing in both.
 */
static errr none_feed(void)
{
    none_note();

    /*
     * Nothing is happening: knock.
     *
     * A prompt is up and the script is talking straight past it. The low
     * hitpoint warning is the one that taught us this -- it wants Space,
     * and only Space, and a script busy walking north will sit in front of
     * it until the heat death of the universe. There are others like it.
     *
     * So when the clock stops we stop reading the script for a moment and
     * try the three keys that dismiss things: space, escape, return. One
     * of them is what it wanted. This is what a person does when a game
     * appears to have frozen, and it is right for the same reason.
     */
    if (!none_strict && none_idle >= 20 && ((none_idle - 20) % 12) < 6)
    {
        /*
         * Space, escape, return, and then the answers.
         *
         * The low hitpoint warning wants Space and nothing else. "Which
         * stat do you want to raise?" wants a letter and will not take no
         * for an answer. Between them they cover what a stuck run meets:
         * a message to acknowledge, a menu to leave, a question to answer.
         *
         * Knock on every key while it lasts, rather than now and then.
         * The stat prompt is a while(1) that takes only a letter, and
         * behind it sits "Are you sure?" -- so the answer has to follow
         * the letter immediately or the question comes round again. One
         * knock every few keys got asked the same thing eighty thousand
         * times.
         *
         * But in bursts of six, not for ever. Knocking without pause is
         * its own deadlock: the walk keys never go in, so no time passes,
         * so we knock again -- a run that marched down forty floors on
         * game turn one and called it a test. Six knocks, six keys of the
         * script, repeat. The script resumes fully the moment time moves.
         *
         * Note what is not in the list. "n" repeats the last command, so
         * knocking with it walked straight back into the targeting screen
         * it had just escaped, over and over. A key that means "again" is
         * the worst possible thing to press when you are stuck.
         */
        static const char knock[6] = { ' ', 0, 13, 'a', 'y', 0 };
        int which = (none_idle - 20) % 6;

        none_knocks++;
        Term_keypress(knock[which] ? knock[which] : ESCAPE);
        return 0;
    }

    if (none_script && none_script[none_at])
    {
        char c = none_script[none_at++];

        if (c == '\\' && none_script[none_at])
        {
            Term_keypress(none_script[none_at++]);
            return 0;
        }

        /*
         * A control key. There was no way to say one: '^' is already
         * Escape and a backslash sends the next character as itself. It
         * matters because Ctrl-A is the debug menu, which is the only
         * way to reach a dungeon floor without walking a character to a
         * staircase it has no way to see.
         */
        if (c == '%' && none_script[none_at])
        {
            char k = none_script[none_at++];

            if (islower(k)) k = toupper(k);
            Term_keypress(k & 0x1F);
            return 0;
        }

        if (none_verbose)
            printf("key %3d %-4s turn %ld depth %d %s\n",
                   none_at, c == '~' ? "RET" : (c == '^' ? "ESC" : (char[2]){c, 0}),
                   (long)game_turn, dun_level,
                   character_generated ? "in play" : "in birth");
        Term_keypress(c == '~' ? 13 : (c == '^' ? ESCAPE : c));
        return 0;
    }

    /*
     * Round again.
     *
     * A run that visits forty floors needs tens of thousands of keystrokes,
     * and nobody wants that on a command line. So a short script can be told
     * to repeat, and the run ends on its own terms -- floors visited, or the
     * character dead -- rather than when the typing runs out.
     */
    if (none_repeat && none_rewind >= 0 && none_laps < 10000
     && none_script && none_script[none_rewind])
    {
        none_laps++;
        none_at = none_rewind;
        return none_feed();
    }

    /*
     * The script is spent. Back out of whatever is open -- a menu, a prompt,
     * a -more- -- and then stop. Bounded, because a screen that eats Escape
     * and asks again would otherwise keep this going for ever, which is the
     * one thing a headless run must never do.
     */
    if (!none_strict && none_unwind < NONE_UNWIND)
    {
        none_unwind++;
        Term_keypress(ESCAPE);
        return 0;
    }

    if (none_verbose) none_screen("end of script");
    none_report();
    quit(NULL);
    return 0;
}


static errr Term_xtra_none(int n, int v)
{
    (void)v;

    switch (n)
    {
    case TERM_XTRA_EVENT:
        return none_feed();

    case TERM_XTRA_SOUND:
        if (none_audio >= 0) test_audio_event(v);
        return 0;

    /* Nothing to draw on, nothing to wait for, nothing to make a noise. */
    case TERM_XTRA_FLUSH:
    case TERM_XTRA_CLEAR:
    case TERM_XTRA_FRESH:
    case TERM_XTRA_SHAPE:
    case TERM_XTRA_REACT:
    case TERM_XTRA_ALIVE:
    case TERM_XTRA_LEVEL:
    case TERM_XTRA_NOISE:
    case TERM_XTRA_BORED:
        return 0;

    /* A delay is a courtesy to somebody watching. Nobody is. */
    case TERM_XTRA_DELAY:
        return 0;
    }

    return 1;
}


static term none_term;

static void none_term_init(void)
{
    term *t = &none_term;

    /* Eighty by twenty-four: the size everything in this codebase was laid
     * out for, and the size at which a screen is least likely to behave
     * differently from how it does for a player. */
    term_init(t, 80, 24, 256);

    t->soft_cursor = TRUE;
    t->attr_blank  = TERM_DARK;
    t->char_blank  = ' ';

    t->init_hook = Term_init_none;
    t->nuke_hook = Term_nuke_none;
    t->xtra_hook = Term_xtra_none;
    t->curs_hook = Term_curs_none;
    t->wipe_hook = Term_wipe_none;
    t->text_hook = Term_text_none;

    ormeq_term[0] = t;
    Term_activate(t);
}


/************************************************************************
 * Entry
 ************************************************************************/
static void none_usage(cptr name)
{
    printf("Usage: %s [options]\n", name);
    printf("  -s<n>      seed the game, so the same number is the same game\n");
    printf("  -x<keys>   the keys to play ('~' Return, '^' Escape,"
           " backslash sends the next as itself, %%a is Ctrl-A)\n");
    printf("  -u<name>   the character to play\n");
    printf("  -n         start a new character\n");
    printf("  -A0/1     mute/capture sound events (headless, no audio device)\n");
    printf("  -A2/3     exercise audio hooks muted/enabled, then report and exit\n");
    printf("  -q         say nothing; the exit code is the answer\n");
    printf("  -Q         stop when the exact script ends; never guess prompt answers\n");
    printf("  -H         use the start screen and allow multiple play sessions\n");
    printf("  -U1/2/3    simulate Settings host rows/key/both callbacks\n");
    printf("  -K0/1      quest lifecycle fixture, without/with journal browsing\n");
    printf("  -K2/3      disposable journal/receipt save fixtures\n");
    printf("  -K7        disposable Telmora quest-card save fixture\n");
    printf("  -K8        failure history and journal selection save fixture\n");
    printf("  -K9        disposable Morivant castle quest-card save fixture\n");
    printf("  -K4/5/6    inspect receipts / resave / inspect each title-screen session\n");
    printf("  -M<0|1>    run the isolated melee fixture, recording disabled/enabled\n");
    printf("  -v         say every key as it goes in\n");
    printf("  -d         allow the debug commands (needs a -Wizard build)\n");
    printf("  -D<n>      begin on dungeon floor n, rather than in town\n");
    printf("  -i         keep the character alive (for soak runs)\n");
    printf("  -r         play the script round again until the run is done\n");
    printf("  -N<n>      visit n floors, going down one at a time\n");
    printf("  -T<n>      game turns to spend on each floor (default 400)\n");
    printf("  -Y<n>      which dungeon to begin in (1 is The Sink)\n");
    printf("  -I         list live race/class identity names and rule tokens\n");
    printf("  -E<file>   check identity expressions (race class expected expression)\n");
    printf("  -h         this message\n");
}


static void none_init_paths(void)
{
    char path[1024];

    /* Relative to the working directory, which is where the harness runs it
     * from. There is no SDL here to ask for the executable's own directory. */
    strnfmt(path, sizeof(path), "lib%s", PATH_SEP);
    init_file_paths(path, path, path);
}


static void none_quit_hook(cptr s)
{
    /* Every way out of the game comes through here. */
    if (s && *s && !none_quiet) printf("quit: %s\n", s);
    none_report();
}


/* Diagnostic runs use the initialized game data, without creating a player.
 * Input rows: race-id class-id expected-result expression. Both production
 * expression readers must agree with the independently supplied expectation. */
static int none_identity_checks(void)
{
    int i;
    char token[80];
    birth_hack = TRUE; /* report base identities, not an evolved form */
    p_ptr->lev = 1;
    p_ptr->mimic_form = MIMIC_NONE;
    if (none_identities)
    {
        for (i = 0; i < MAX_RACES; i++)
        {
            race_t *race = get_race_aux(i, 0);
            pref_name_token(token, sizeof(token), race->name);
            printf("identity|RACE|%d|%s|%s\n", i, race->name, token);
        }
        for (i = 0; i < MAX_CLASS; i++)
        {
            class_t *cls;
            if (class_is_deprecated(i)) continue;
            cls = get_class_aux(i, 0);
            pref_name_token(token, sizeof(token), cls->name);
            printf("identity|CLASS|%d|%s|%s\n", i, cls->name, token);
        }
    }
    if (none_expressions)
    {
        FILE *fp = my_fopen(none_expressions, "r");
        char line[2048];
        int count = 0, failed = 0;
        if (!fp) return 1;
        while (fgets(line, sizeof(line), fp))
        {
            int race, cls, expected, offset = 0;
            char a[2048], b[2048], *cursor, end;
            bool pref_ok, dungeon_ok;
            if (line[0] == '#' || line[0] == '\n') continue;
            count++;
            if (sscanf(line, "%d %d %d %n", &race, &cls, &expected, &offset) != 3
                || race < 0 || race >= MAX_RACES || cls < 0 || cls >= MAX_CLASS
                || class_is_deprecated(cls) || (expected != 0 && expected != 1)
                || line[offset] != '[')
            {
                printf("invalid identity case %d\n", count);
                failed++;
                continue;
            }
            p_ptr->prace = race;
            p_ptr->pclass = cls;
            p_ptr->psubrace = 0;
            p_ptr->psubclass = cls == CLASS_DISCIPLE ? DISCIPLE_TROIKA : 0;
            my_strcpy(a, line + offset, sizeof(a));
            my_strcpy(b, line + offset, sizeof(b));
            cursor = a;
            pref_ok = streq(process_pref_file_expr(&cursor, &end), expected ? "1" : "0");
            cursor = b;
            dungeon_ok = streq(process_dungeon_file_expr(&cursor, &end), expected ? "1" : "0");
            if (!pref_ok || !dungeon_ok)
            {
                printf("identity case %d failed (pref %d, dungeon %d): %s",
                    count, pref_ok, dungeon_ok, line);
                failed++;
            }
        }
        my_fclose(fp);
        printf("identity checks: %d cases, %d failures\n", count, failed);
        return failed ? 1 : 0;
    }
    return 0;
}

int main(int argc, char *argv[])
{
    bool new_game = FALSE;
    int  i;

#if defined(_MSC_VER) && defined(_DEBUG)
    _CrtSetReportHook(_test_crt_report);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
#endif
    setvbuf(stdout,NULL,_IONBF,0);
    argv0 = argv[0];

    for (i = 1; i < argc; i++)
    {
        if (argv[i][0] != '-') continue;

        switch (argv[i][1])
        {
        case 'J': none_map_test=TRUE; break;
        case 'K': none_quest_test=atoi(&argv[i][2]); break;
        case 'A':
            none_audio = atoi(&argv[i][2]);
            break;

        case 'M':
            none_melee = atoi(&argv[i][2]);
            break;

        case 'I':
            none_identities = TRUE;
            break;

        case 'E':
            none_expressions = &argv[i][2];
            break;

        case 's':
            none_seed = (u32b)strtoul(&argv[i][2], NULL, 10);
            break;

        case 'x':
            none_script = &argv[i][2];
            break;

        case 'u':
            my_strcpy(player_name, &argv[i][2], sizeof(player_name));
            break;

        case 'n':
            new_game = TRUE;
            break;

        case 'Q':
            none_strict = TRUE;
            break;

        case 'H':
            none_home = TRUE;
            break;

        case 'U':
            none_host_settings = atoi(&argv[i][2]);
            break;

        case 'q':
            none_quiet = TRUE;
            break;

        case 'v':
            none_verbose = TRUE;
            break;

        case 'd':
            none_debug = TRUE;
            break;

        case 'D':
            none_depth = atoi(&argv[i][2]);
            break;

        case 'Y':
            none_dungeon = atoi(&argv[i][2]);
            break;

        case 'N':
            none_floors = atoi(&argv[i][2]);
            if (none_floors < 1) none_floors = 1;
            break;

        case 'T':
            none_stride = atoi(&argv[i][2]);
            if (none_stride < 1) none_stride = 1;
            break;

        case 'r':
            none_repeat = TRUE;
            break;

        case 'i':
            none_immortal = TRUE;
            break;

        case 'h':
            none_usage(argv[0]);
            return 0;

        default:
            printf("Unknown option '%s'\n", argv[i]);
            none_usage(argv[0]);
            return 1;
        }
    }

    /*
     * The seed, before anything can spend it.
     *
     * play_game() seeds from time(NULL) for a new character. arg_seed, when
     * it is not zero, is used instead -- see the note in dungeon.c. Every
     * other seed in the game (the flavours, the town layout) is drawn from
     * this one, so one number fixes the lot.
     */
    arg_seed = none_seed;

    none_init_paths();
    process_player_name(TRUE);
    create_needed_dirs();

    quit_aux = none_quit_hook;

    none_term_init();
    init_ormeq();

    if (none_host_settings & 1) ui_settings_host_rows = none_settings_rows;
    if (none_host_settings & 2) ui_settings_host_key = none_settings_key;

    if (none_identities || none_expressions)
        return none_identity_checks();

    /*
     * No -more- prompts.
     *
     * They exist so a person can read a message before the next one
     * arrives. Nobody is reading. Left on, they swallow the script: a
     * run that walked twenty steps took none of them, because every
     * keystroke was spent dismissing the messages from being born.
     * Twenty Returns in front of the script fixed it and made the
     * script a lie about what it was testing.
     */

    do
    {
        if (none_home && !new_game)
            new_game = start_screen();
        else if (new_game)
            savefile[0] = '\0';

        play_game(new_game);
        none_sessions++;
        if (none_quest_test == 6) none_quest_done = FALSE;
        new_game = FALSE;
    }
    while (none_home);

    quit(NULL);
    return 0;
}
