/*
 * File: ui_loot.c
 *
 * Mogaminator 2: Loot Judgement.
 *
 * A screen in front of the rules engine in autopick.c. That engine is
 * Mogami's, written for Hengband in 2002, and nothing here touches it: this
 * file writes lines in its language and asks it what they mean.
 *
 * It could always do everything below. What it did not have was a way in that
 * did not require learning the language first. The old screen handed you a
 * text file and a tutorial, and expected you to know that a leading '~' means
 * never pick this up and a trailing '#!kq' inscribes whatever it catches.
 * Everything a player wanted was possible; none of it was discoverable.
 *
 * So a rule is a short form here, and the screen writes the line. Two things
 * sit beside it that the old editor could never show: the rule said out loud
 * in English, and how many of the game's objects it would actually catch.
 * Writing a rule and finding out in the dungeon whether it ate something you
 * wanted is a poor way to learn.
 *
 * Composing text and handing it to autopick_new_entry() -- rather than
 * setting the entry's flags directly -- is deliberate. It means the form can
 * never build a rule the parser would refuse, and that there is exactly one
 * authority on what a rule means, which is his.
 *
 * The text editor is still on 't', for anybody who knows the language and for
 * the corners of it this form does not reach.
 *
 * Copyright (c) 2026 John Horton.
 *
 * Added since the fork and offered on the same terms as the rest of the
 * tree -- the Angband licence in LICENSE.
 */

#include "scottish.h"

#define _LIST_W 46

/************************************************************************
 * The words a rule is made of
 *
 * A subset. Every keyword in autopick.c can appear in a rule; these are the
 * ones that answer "what should happen to this kind of thing", which is the
 * question people actually arrive with. The rest stay in the text editor --
 * a form for what is wanted, a language for everything.
 ************************************************************************/
typedef struct { cptr prefix; cptr name; cptr what; } _action_t;

/*
 * A rule picks things up unless it is told otherwise. '!' destroys, '~'
 * leaves, ';' asks, and '?' adds identification to whatever else it does --
 * so identifying without picking up is the two of them together.
 */
static _action_t _actions[] = {
    { "",   "Pick it up",   "into your pack, without asking" },
    { ";",  "Ask me first", "stop and ask before picking it up" },
    { "~",  "Leave it",     "walk over it and never pick it up" },
    { "!",  "Destroy it",   "break it where it lies" },
    { "~?", "Identify it",  "work out what it is and leave it there" },
    { NULL, NULL, NULL },
};

typedef struct { cptr key; cptr name; } _word_t;

/* Nouns: what kind of thing the rule is about. */
static _word_t _kinds[] = {
    { "items",            "Anything at all" },
    { "weapons",          "Weapons" },
    { "favorite weapons", "Weapons you are good with" },
    { "hafted weapons",   "Blunt weapons" },
    { "diggers",          "Diggers" },
    { "shooters",         "Bows and slings" },
    { "ammo",             "Ammunition" },
    { "armors",           "Armour of any kind" },
    { "shields",          "Shields" },
    { "suits",            "Body armour" },
    { "cloaks",           "Cloaks" },
    { "helms",            "Helmets" },
    { "gloves",           "Gloves" },
    { "boots",            "Boots" },
    { "lights",           "Lights" },
    { "rings",            "Rings" },
    { "amulets",          "Amulets" },
    { "spellbooks",       "Spellbooks" },
    { "wands",            "Wands" },
    { "staves",           "Staves" },
    { "rods",             "Rods" },
    { "potions",          "Potions" },
    { "scrolls",          "Scrolls" },
    { "corpses",          "Corpses" },
    { "skeletons",        "Skeletons" },
    { "junk",             "Junk" },
    { NULL, NULL },
};

/* Adjectives: when the rule should apply. */
static _word_t _onlys[] = {
    { "",             "always" },
    { "unidentified", "you have not identified it" },
    { "identified",   "you have identified it" },
    { "unaware",      "you do not know what it is" },
    { "unsensed",     "you have not sensed it" },
    { "average",      "it turned out ordinary" },
    { "good",         "it turned out good" },
    { "ego",          "it is of a named make" },
    { "artifact",     "it is an artifact" },
    { "nameless",     "it has no make and is no artifact" },
    { "cursed",       "it is cursed" },
    { "worthless",    "it is worth nothing" },
    { "rare",         "it is rare" },
    { "common",       "it is common" },
    { "unusable",     "your kind cannot use it" },
    { "wanted",       "somebody is paying for it" },
    { NULL, NULL },
};

static int _table_len(_word_t *t)
{
    int n = 0;

    while (t[n].name) n++;
    return n;
}

/************************************************************************
 * Writing a rule, and reading one back
 ************************************************************************/
typedef struct {
    int  action;          /* index into _actions */
    int  kind;            /* index into _kinds   */
    int  only;            /* index into _onlys   */
    bool listed;          /* show it in the object list */
    char name[80];
    char insc[80];
} _form_t;

/* The form as a line in the engine's own language. */
static void _form_to_line(_form_t *f, char *buf, int cap)
{
    char body[256];

    body[0] = '\0';

    if (_onlys[f->only].key[0])
    {
        my_strcat(body, _onlys[f->only].key, sizeof(body));
        my_strcat(body, " ", sizeof(body));
    }
    my_strcat(body, _kinds[f->kind].key, sizeof(body));

    /* A name to look for goes after a colon, and narrows whatever precedes. */
    if (f->name[0])
    {
        my_strcat(body, ":", sizeof(body));
        my_strcat(body, f->name, sizeof(body));
    }

    strnfmt(buf, cap, "%s%s%s%s%s",
            _actions[f->action].prefix,
            f->listed ? "" : "(",
            body,
            f->insc[0] ? "#" : "",
            f->insc[0] ? f->insc : "");
}

static bool _form_to_entry(_form_t *f, autopick_type *entry)
{
    char line[512];

    _form_to_line(f, line, sizeof(line));
    return autopick_rule_parse(entry, line);
}

/* Is `word` in `line` as a whole word? */
static bool _has_word(cptr line, cptr word)
{
    int wl = strlen(word);
    cptr p = line;

    if (!wl) return FALSE;

    while ((p = my_strstr(p, word)) != NULL)
    {
        bool left  = (p == line) || !isalpha(p[-1]);
        bool right = !isalpha(p[wl]);

        if (left && right) return TRUE;
        p += wl;
    }
    return FALSE;
}

/*
 * Read an existing rule back into the form.
 *
 * From the written line rather than from the flags, because the line is what
 * the parser produced and what it will consume again -- so the form and the
 * rule cannot drift apart. A rule the form cannot express (there are some:
 * the engine has conditionals and numeric tests this screen does not offer)
 * still opens, showing whatever of it the form does understand.
 */
static void _entry_to_form(autopick_type *entry, _form_t *f)
{
    string_ptr s = autopick_line_from_entry(entry, 0);
    cptr       line = s ? string_buffer(s) : "";
    int        i;

    WIPE(f, _form_t);
    f->listed = TRUE;

    /* The prefix, longest first: "~?" before "~". */
    for (i = 0; _actions[i].name; i++)
    {
        cptr p = _actions[i].prefix;

        if (p[0] && strlen(p) == 2 && !strncmp(line, p, 2)) { f->action = i; break; }
    }
    if (!f->action)
    {
        for (i = 0; _actions[i].name; i++)
        {
            cptr p = _actions[i].prefix;

            if (p[0] && strlen(p) == 1 && line[0] == p[0]) { f->action = i; break; }
        }
    }

    if (my_strchr(line, '(')) f->listed = FALSE;

    /* Longest keyword first, so "favorite weapons" wins over "weapons". */
    {
        int best = 0, best_len = 0;

        for (i = 0; _kinds[i].name; i++)
        {
            int len = strlen(_kinds[i].key);

            if (len > best_len && _has_word(line, _kinds[i].key))
            {
                best = i;
                best_len = len;
            }
        }
        f->kind = best;
    }

    for (i = 1; _onlys[i].name; i++)
    {
        if (_has_word(line, _onlys[i].key)) { f->only = i; break; }
    }

    if (entry->name) my_strcpy(f->name, entry->name, sizeof(f->name));
    if (entry->insc) my_strcpy(f->insc, entry->insc, sizeof(f->insc));

    if (s) string_free(s);
}

/*
 * A rule in a sentence.
 *
 * The written form is exact, compact and unreadable until you know it. This
 * is the same rule said out loud, which is what somebody scanning a list of
 * twenty of them needs.
 */
static void _rule_sentence(doc_ptr doc, autopick_type *entry)
{
    _form_t f;
    bool    said = FALSE;

    _entry_to_form(entry, &f);

    doc_printf(doc, "<color:W>%s</color> <color:B>%s</color>",
               _actions[f.action].name, _kinds[f.kind].name);

    if (f.name[0])
    {
        doc_printf(doc, " <color:D>whose name has</color> <color:y>%s</color>"
                        " <color:D>in it</color>", f.name);
        said = TRUE;
    }

    if (f.only > 0)
    {
        doc_printf(doc, " <color:D>%s</color> <color:y>%s</color>",
                   said ? "and when" : "when", _onlys[f.only].name);
    }

    if (f.insc[0])
        doc_printf(doc, "<color:D>, inscribing it</color> <color:y>%s</color>",
                   f.insc);

    if (!f.listed)
        doc_insert(doc, "<color:D>, and keep it out of the object list"
                        "</color>");

    doc_insert(doc, "<color:D>.</color>\n");
}

/************************************************************************
 * What a rule actually catches
 *
 * The question the old editor could never answer. Every kind of object the
 * game has, made real for a moment and offered to the rule. A few hundred
 * cheap tests, which is nothing beside the keypress that asked for them.
 *
 * "Of the kinds of thing in the game", not "of the things you have seen":
 * a rule is written to deal with what you have not met yet.
 ************************************************************************/
typedef struct {
    int  hits, total, samples;
    char sample[3][MAX_NLEN];
} _catch_t;

static void _rule_catches(autopick_type *entry, _catch_t *out)
{
    int i;

    out->hits = out->total = out->samples = 0;

    for (i = 1; i < max_k_idx; i++)
    {
        object_kind *k_ptr = &k_info[i];
        object_type  forge;

        if (!k_ptr->name) continue;

        object_wipe(&forge);
        object_prep(&forge, i);
        forge.ident |= IDENT_KNOWN;

        out->total++;

        if (!autopick_rule_matches(entry, &forge)) continue;

        out->hits++;
        if (out->samples < 3)
        {
            object_desc(out->sample[out->samples], &forge,
                        OD_NAME_ONLY | OD_OMIT_INSCRIPTION);
            out->samples++;
        }
    }
}

static void _show_catch(doc_ptr doc, _catch_t *c, bool with_samples,
                        bool by_name)
{
    if (!c->hits)
    {
        /*
         * Every kind is tested fresh out of the box, so a rule that
         * matches on a name or an inscription can be perfectly good and
         * still catch none of them. Calling that rule broken would be a
         * confident lie.
         */
        if (by_name)
            doc_insert(doc, "  <color:D>None of the game's object kinds"
                            " under that name -- though a rule matching"
                            " on a name or an\n  inscription may still"
                            " catch things once you meet them.</color>\n");
        else
            doc_insert(doc, "  <color:r>Nothing at all.</color>"
                            " <color:D>Which usually means a condition it"
                            " can never meet.</color>\n");
        return;
    }

    doc_printf(doc, "  <color:W>%d</color> <color:D>of the game's</color>"
                    " <color:W>%d</color> <color:D>kinds of thing",
               c->hits, c->total);

    if (with_samples && c->samples)
    {
        int i;

        doc_insert(doc, "<color:D>, such as</color> ");
        for (i = 0; i < c->samples; i++)
            doc_printf(doc, "%s<color:y>%s</color>",
                       i ? "<color:D>,</color> " : "", c->sample[i]);
    }
    doc_insert(doc, "<color:D>.</color>\n");
}

/************************************************************************
 * The form
 *
 * One screen of fields rather than a run of questions. A question at a time
 * is easy to write and horrible to use: you cannot see what you have already
 * chosen, you cannot go back, and you only find out what you built at the
 * end. Here every choice is visible, and the sentence and the count under it
 * change as you move.
 ************************************************************************/
#define _F_ACTION 0
#define _F_KIND   1
#define _F_ONLY   2
#define _F_NAME   3
#define _F_INSC   4
#define _F_LIST   5
#define _F_MAX    6

static void _cycle(int *v, int delta, int count)
{
    *v += delta;
    if (*v < 0) *v = count - 1;
    if (*v >= count) *v = 0;
}

static bool _edit_rule(_form_t *f, cptr caption)
{
    doc_ptr doc;
    int     field = 0, wid, hgt;
    bool    done = FALSE, keep = FALSE;

    static cptr label[_F_MAX] = {
        "What happens", "What it covers", "Only when",
        "Name contains", "Inscribe it", "In the object list" };

    Term_get_size(&wid, &hgt);
    doc = doc_alloc(MIN(96, wid));

    while (!done)
    {
        autopick_type entry;
        _catch_t      caught;
        char          line[512];
        cptr          value[_F_MAX];
        int           cmd, i;
        bool          ok;

        _form_to_line(f, line, sizeof(line));
        ok = _form_to_entry(f, &entry);
        if (ok) _rule_catches(&entry, &caught);

        value[_F_ACTION] = _actions[f->action].name;
        value[_F_KIND]   = _kinds[f->kind].name;
        value[_F_ONLY]   = _onlys[f->only].name;
        value[_F_NAME]   = f->name[0] ? f->name : "(anything)";
        value[_F_INSC]   = f->insc[0] ? f->insc : "(nothing)";
        value[_F_LIST]   = f->listed ? "show it" : "keep it out";

        doc_clear(doc);
        doc_printf(doc, "<color:U>%s</color>\n", caption);
        doc_add_rule(doc);
        doc_newline(doc);

        for (i = 0; i < _F_MAX; i++)
        {
            doc_printf(doc, "%s<color:%c>%-18s</color> <color:%c>%s</color>\n",
                       i == field ? "<color:B>></color>" : " ",
                       i == field ? 'B' : 'D', label[i],
                       i == field ? 'B' : 'W', value[i]);
        }

        doc_printf(doc, "\n  <color:D>%s</color>\n",
                   _actions[f->action].what);

        doc_newline(doc);
        doc_insert(doc, "<style:heading>Which reads</style>\n  ");
        if (ok) _rule_sentence(doc, &entry);
        else doc_insert(doc, "<color:r>Nothing the engine will accept."
                             "</color>\n");
        doc_newline(doc);

        doc_insert(doc, "<style:heading>Which catches</style>\n");
        if (ok) _show_catch(doc, &caught, TRUE, BOOL(f->name[0]));
        doc_newline(doc);

        doc_printf(doc, "<style:heading>Which is written</style>\n"
                        "  <color:y>%s</color>\n", line);

        Term_clear();
        ui_event_panel_none();
        doc_sync_term(doc, doc_range_all(doc), doc_pos_create(0, 1));

        prt("Arrows move and change. RETURN keeps it, ESC throws it away.",
            hgt - 1, 0);
        Term_fresh();

        cmd = inkey_special(TRUE);
        if (ok) autopick_rule_free(&entry);

        switch (cmd)
        {
        case ESCAPE: case 'q': case 'Q':
            done = TRUE;
            break;

        case '\r': case '\n':
            done = TRUE;
            keep = TRUE;
            break;

        case SKEY_UP:   if (field > 0) field--; break;
        case SKEY_DOWN: if (field < _F_MAX - 1) field++; break;

        case SKEY_LEFT: case SKEY_RIGHT: case ' ':
        {
            int d = (cmd == SKEY_LEFT) ? -1 : 1;
            int n = 0;

            switch (field)
            {
            case _F_ACTION:
                while (_actions[n].name) n++;
                _cycle(&f->action, d, n);
                break;
            case _F_KIND:
                _cycle(&f->kind, d, _table_len(_kinds));
                break;
            case _F_ONLY:
                _cycle(&f->only, d, _table_len(_onlys));
                break;
            case _F_LIST:
                f->listed = !f->listed;
                break;
            case _F_NAME:
                get_string("Only things whose name contains: ",
                           f->name, sizeof(f->name) - 1);
                break;
            default:
                get_string("Inscribe what it catches with: ",
                           f->insc, sizeof(f->insc) - 1);
                break;
            }
            break;
        }

        default:
            /* Typing at a text field edits it, without hunting for an arrow. */
            if (field == _F_NAME)
                get_string("Only things whose name contains: ",
                           f->name, sizeof(f->name) - 1);
            else if (field == _F_INSC)
                get_string("Inscribe what it catches with: ",
                           f->insc, sizeof(f->insc) - 1);
            break;
        }
    }

    doc_free(doc);
    return keep;
}

/************************************************************************
 * The list
 ************************************************************************/
void do_cmd_loot_rules(void)
{
    doc_ptr list_doc, side_doc;
    int     cur = 0, top = 0;
    int     wid, hgt, rows, side_w;
    bool    done = FALSE, dirty = FALSE;

    Term_get_size(&wid, &hgt);
    rows   = hgt - 8;
    side_w = wid - _LIST_W - 4;
    if (side_w < 40) side_w = 40;

    list_doc = doc_alloc(_LIST_W);
    side_doc = doc_alloc(side_w);

    screen_save();

    while (!done)
    {
        int i, cmd;

        if (cur >= max_autopick) cur = MAX(0, max_autopick - 1);
        if (cur < 0) cur = 0;
        if (cur < top) top = cur;
        if (cur >= top + rows) top = cur - rows + 1;

        doc_clear(list_doc);
        doc_clear(side_doc);

        doc_insert(list_doc, "<color:U>Your rules</color>");
        if (max_autopick > rows)
            doc_printf(list_doc, "  <color:D>%d of %d</color>",
                       cur + 1, max_autopick);
        doc_insert(list_doc, "\n\n");

        if (!max_autopick)
            doc_insert(list_doc, " <color:D>None yet.</color>\n");

        for (i = top; i < max_autopick && i < top + rows; i++)
        {
            _form_t f;
            bool    sel = (i == cur);

            _entry_to_form(&autopick_list[i], &f);

            doc_printf(list_doc, "%s<color:%c>%-12.12s</color> "
                                 "<color:%c>%-28.28s</color>\n",
                       sel ? "<color:B>></color>" : " ",
                       sel ? 'B' : 'W', _actions[f.action].name,
                       sel ? 'B' : 'D',
                       f.name[0] ? f.name : _kinds[f.kind].name);
        }

        if (max_autopick)
        {
            autopick_type *e = &autopick_list[cur];
            _catch_t       caught;
            string_ptr     line = autopick_line_from_entry(e, 0);

            _rule_catches(e, &caught);

            doc_printf(side_doc, "<color:U>Rule %d of %d</color>\n\n",
                       cur + 1, max_autopick);

            doc_insert(side_doc, "<style:heading>Which reads</style>\n  ");
            _rule_sentence(side_doc, e);
            doc_newline(side_doc);

            doc_insert(side_doc, "<style:heading>Which catches</style>\n");
            _show_catch(side_doc, &caught, TRUE,
                        BOOL(e->name && e->name[0]));
            doc_newline(side_doc);

            doc_printf(side_doc, "<style:heading>Which is written</style>\n"
                                 "  <color:y>%s</color>\n",
                       line ? string_buffer(line) : "");
            if (line) string_free(line);

            doc_newline(side_doc);
            doc_insert(side_doc,
                       "<color:D>Order decides. The first rule that catches a"
                       " thing settles what\nhappens to it and the rest never"
                       " see it, so a narrow rule belongs\nabove the broad one"
                       " it is an exception to.</color>\n"
                       "<color:D>Use</color> <color:y>< </color>"
                       "<color:D>and</color> <color:y>></color>"
                       " <color:D>to move this one.</color>\n");
        }
        else
        {
            doc_insert(side_doc, "<color:U>Nothing is standing</color>\n\n");
            doc_insert(side_doc,
                       "  <color:D>A rule says what should happen to a kind of"
                       " thing when you\n  come across it: pick it up, leave"
                       " it, break it where it lies,\n  or stop and ask."
                       "</color>\n\n");
            doc_insert(side_doc, "  <color:D>Press</color> <color:y>n</color>"
                                 " <color:D>to write one.</color>\n");
        }

        Term_clear();
        ui_event_panel_none();

        prt("Mogaminator 2: Loot Judgement", 0, 0);
        {
            cptr credit = "the rules engine is Mogami's, Hengband 2002";
            int  x = wid - strlen(credit) - 1;

            if (x > 32) c_put_str(TERM_L_DARK, credit, 0, x);
        }

        doc_sync_term(list_doc, doc_range_all(list_doc), doc_pos_create(0, 2));
        doc_sync_term(side_doc, doc_range_all(side_doc),
                      doc_pos_create(_LIST_W + 4, 2));

        prt("n a new rule,  e change this one,  d remove it,"
            "  < > move it up or down", hgt - 2, 0);
        prt("t the text editor, for the language itself."
            "  ESC keeps what you have done.", hgt - 1, 0);
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
            if (cur < max_autopick - 1) cur++;
            break;

        case SKEY_TOP: case SKEY_PGUP:
            cur = 0;
            break;

        case SKEY_BOTTOM: case SKEY_PGDOWN:
            cur = MAX(0, max_autopick - 1);
            break;

        case 'n': case 'N':
        {
            _form_t f;

            WIPE(&f, _form_t);
            f.listed = TRUE;

            if (_edit_rule(&f, "A new rule"))
            {
                autopick_type entry;

                if (_form_to_entry(&f, &entry))
                {
                    autopick_rule_insert(&entry, cur);
                    dirty = TRUE;
                }
                else
                    msg_print("The engine would not take that rule.");
            }
            break;
        }

        case 'e': case 'E':
            if (max_autopick)
            {
                _form_t f;

                _entry_to_form(&autopick_list[cur], &f);
                if (_edit_rule(&f, "Changing a rule"))
                {
                    autopick_type entry;

                    if (_form_to_entry(&f, &entry))
                    {
                        autopick_rule_free(&autopick_list[cur]);
                        autopick_list[cur] = entry;
                        dirty = TRUE;
                    }
                    else
                        msg_print("The engine would not take that rule.");
                }
            }
            break;

        case 'd': case 'D':
            if (max_autopick && get_check("Remove this rule? "))
            {
                autopick_rule_remove(cur);
                dirty = TRUE;
            }
            break;

        case '<':
            if (cur > 0) { autopick_rule_move(cur, -1); cur--; dirty = TRUE; }
            break;

        case '>':
            if (cur < max_autopick - 1)
            { autopick_rule_move(cur, 1); cur++; dirty = TRUE; }
            break;

        case 't': case 'T':
            /* The language itself. It writes the file on its own way out, so
             * anything done in there is already kept -- and it keeps the
             * comments and blank lines that saving the list would not. */
            do_cmd_edit_autopick();
            dirty = FALSE;
            break;
        }
    }

    if (dirty && !autopick_rules_save())
        msg_print("Could not write your rules to a file.");

    doc_free(list_doc);
    doc_free(side_doc);

    screen_load();
    p_ptr->redraw |= (PR_WIPE | PR_BASIC | PR_EXTRA | PR_MAP | PR_EQUIPPY);
}
