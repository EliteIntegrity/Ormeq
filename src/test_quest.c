/* Actual quest lifecycle, information boundaries and read-only presentation.
 * Headless tests only. Copyright (c) 2026 John Horton; see LICENSE. */
#include "scottish.h"

static int _failures, _checks;
#define CHECK(x) do { ++_checks; if (!(x)) { ++_failures; printf("quest-check FAIL %d: %s\n", __LINE__, #x); } } while (0)

static errr (*_old_hook)(int, int);
static cptr _keys;
static errr _input(int n, int v)
{
    if (n == TERM_XTRA_EVENT)
    {
        if (!v) return 0;
        if (!_keys || !*_keys)
        {
            int x, y;
            for (y = 0; y < 24; ++y)
            {
                char line[81]; byte attr;
                for (x = 0; x < 80; ++x) Term_what(x, y, &attr, &line[x]);
                line[80] = 0; printf("%s\n", line);
            }
            quit("Quest fixture exhausted its prompt answers");
        }
        auto_more_state = AUTO_MORE_SKIP_ALL;
        Term_keypress(*_keys++);
        return 0;
    }
    return _old_hook(n, v);
}

static bool _contains(doc_ptr doc, cptr text)
{
    string_ptr s = string_alloc();
    int x, y;
    bool result;
    bool space = FALSE;
    for (y = 0; y < doc_line_count(doc); ++y)
    {
        for (x = 0; x < doc_width(doc); ++x)
        {
            char c = doc_char(doc, doc_pos_create(x, y))->c;
            if (!c || c == ' ' || c == '\t')
            {
                if (!space) string_append_c(s, ' ');
                space = TRUE;
            }
            else { string_append_c(s, c); space = FALSE; }
        }
        if (!space) string_append_c(s, ' ');
        space = TRUE;
    }
    result = strstr(string_buffer(s), text) != NULL;
    string_free(s);
    return result;
}

static bool _listed(vec_ptr v, quest_ptr q)
{
    int i;
    for (i = 0; i < vec_length(v); ++i) if (vec_get(v, i) == q) return TRUE;
    return FALSE;
}

static int _row_with(doc_ptr doc, cptr text)
{
    int y, x;
    char line[160];
    for (y = 0; y < doc_line_count(doc); ++y)
    {
        int width = MIN(doc_width(doc), 159);
        for (x = 0; x < width; ++x) line[x] = doc_char(doc, doc_pos_create(x, y))->c;
        line[width] = 0;
        if (strstr(line, text)) return y;
    }
    return -1;
}

static void _read_only(quest_ptr q, int card)
{
    quest_t before = *q;
    u32b state[RAND_DEG], value = Rand_value;
    u16b place = Rand_place;
    bool quick = Rand_quick;
    int turn = game_turn, i, rows;
    s32b gold = p_ptr->au;
    memcpy(state, Rand_state, sizeof(state));
    for (i = 36; i <= 100; i += 16)
    for (rows = 12; rows <= 32; rows += 20)
    {
        doc_ptr doc = doc_alloc(i);
        quest_journal_doc(q, doc, card, NULL, rows);
        CHECK(doc_line_count(doc) > 5);
        CHECK(_contains(doc, card == QUEST_CARD_FAILED ? "Response" : "Objective"));
        CHECK(!_contains(doc, "M:"));
        CHECK(!_contains(doc, "R:OBJ"));
        if ((q->status == QS_FAILED || q->status == QS_FAILED_DONE) && rows == 12)
        {
            CHECK(_row_with(doc, "Failure record") >= 0 && _row_with(doc, "Failure record") < 10);
            if (doc->portrait.live) CHECK(doc->portrait.row > _row_with(doc, "Failure record"));
        }
        doc_free(doc);
    }
    CHECK(!memcmp(q, &before, sizeof(before)));
    CHECK(!memcmp(Rand_state, state, sizeof(state)) && value == Rand_value && place == Rand_place && quick == Rand_quick);
    CHECK(turn == game_turn && gold == p_ptr->au);
}

static void _telmora(vec_ptr all)
{
    struct { int id, parent; cptr objective, directions, concealed; } cases[] = {
        {3,3,"Kill the stranger", "northwestern corner", "Anti-paladin"},
        {4,4,"Find the small sword Nettle", "one chamber at a time", "You found Nettle"},
        {5,5,"Clear the creatures", "ancient burial ground", "reduce their service prices"},
        {6,6,"Clear every creature", "eastern part", "cloak of immolation"},
        {15,15,"Kill Cinder", "stronghold", "This reward is yours"},
        {16,16,"defeat its defenders", "Castle Montsalvat", "Take the reward we owe you"},
        {34,34,"forgotten shrine", "nearby mountain", "potion of life"},
        {78,78,"Clear every creature", "eastern part", "Phial of Lava"},
        {88,3,"travellers in the marshes", "northeast", "scouts have confirmed"},
        {94,34,"troll army", "eastern hills", "spared the town an assault"},
        {96,78,"creature beneath the abandoned mine", "eastern territory", "Greater emberlord"}
    };
    int i, j, mode;
    for (i = 0; i < sizeof(cases)/sizeof(cases[0]); ++i)
    {
        quest_ptr entry = NULL, parent = NULL;
        quest_t saved;
        int substitution;
        for (j = 0; j < vec_length(all); ++j)
        {
            quest_ptr candidate = vec_get(all, j);
            if (candidate->id == cases[i].id) entry = candidate;
            if (candidate->id == cases[i].parent) parent = candidate;
        }
        CHECK(entry && parent);
        if (!entry || !parent) continue;
        saved = *entry; substitution = parent->substitute;
        /* Exercise both sides of each substitution using the real quest
         * object: description conditions must resolve to this same instance. */
        parent->substitute = entry == parent ? 0 : -entry->id;
        for (mode = 0; mode < 3; ++mode)
        {
            quest_contact_t contact;
            int stage;
            no_wilderness = mode > 0; coffee_break = mode == 2 ? 2 : 0;
            quest_contact(entry, &contact);
            if (!mode) CHECK(streq(contact.portrait, "Denegor III"));
            for (stage = QS_UNTAKEN; stage <= QS_COMPLETED; ++stage)
            {
                int card = stage == QS_UNTAKEN ? QUEST_CARD_OFFER : QUEST_CARD_PROGRESS;
                doc_ptr doc = doc_alloc(60);
                entry->status = stage;
                _read_only(entry, card);
                quest_journal_doc(entry, doc, card, NULL, 12);
                if (contact.name[0])
                {
                    CHECK(_contains(doc, cases[i].objective));
                    CHECK(_contains(doc, cases[i].directions));
                }
                CHECK(!_contains(doc, cases[i].concealed));
                CHECK(!_contains(doc, "No further briefing was recorded"));
                CHECK(_row_with(doc, "Next:") >= 0 && _row_with(doc, "Next:") < 6);
                CHECK(_row_with(doc, "Objective") >= 0 && _row_with(doc, "Objective") < 10);
                if (doc->portrait.live) CHECK(doc->portrait.row > _row_with(doc, "Objective"));
                doc_free(doc);
            }
            entry->status = QS_FAILED;
            {
                doc_ptr doc = doc_alloc(100);
                quest_journal_doc(entry, doc, QUEST_CARD_PROGRESS, NULL, 32);
                CHECK(!_contains(doc, cases[i].concealed));
                CHECK(_contains(doc, "Not yet reported"));
                doc_free(doc);
            }
            _read_only(entry, QUEST_CARD_FAILED);
            {
                doc_ptr doc = doc_alloc(80);
                quest_journal_doc(entry, doc, QUEST_CARD_FAILED, NULL, 20);
                CHECK(!_contains(doc, "No further briefing was recorded"));
                CHECK(!_contains(doc, "Report back to the questgiver"));
                doc_free(doc);
            }
        }
        *entry = saved; parent->substitute = substitution;
    }
    no_wilderness = FALSE; coffee_break = 0;
}

static void _morivant(vec_ptr all)
{
    struct { int id, parent; cptr objective, directions, concealed; } cases[] = {
        {51,51,"Recover the lost leather whip", "snake-infested ruined building", "field archaeologist"},
        {52,52,"Clear every enemy from the old barracks", "among the ruined buildings", "mummified orc"},
        {53,53,"Clear every enemy from the old shop", "ancient city's ruins", "worldcore golem"},
        {54,54,"Clear every enemy from the old Elven Embassy", "Embassy among the ruins", "master mystic"},
        {55,55,"Clear every enemy from the ancient underground city", "found by the archaeologists", "grave skull idol"},
        {92,52,"Clear every enemy from the collapsed barracks", "great earthquake", "black wraith"},
        {93,53,"Clear every enemy from the old Dwarven Embassy", "Embassy among the ruins", "mature gold dragon"},
        {95,54,"Clear the spider cult", "beneath the ruins of the old Elven Embassy", "undying lamp"}
    };
    int i, j, mode;
    for (i = 0; i < sizeof(cases)/sizeof(cases[0]); ++i)
    {
        quest_ptr entry = NULL, parent = NULL;
        quest_t saved;
        int substitution;
        for (j = 0; j < vec_length(all); ++j)
        {
            quest_ptr candidate = vec_get(all, j);
            if (candidate->id == cases[i].id) entry = candidate;
            if (candidate->id == cases[i].parent) parent = candidate;
        }
        CHECK(entry && parent);
        if (!entry || !parent) continue;
        saved = *entry; substitution = parent->substitute;
        parent->substitute = entry == parent ? 0 : -entry->id;
        CHECK(entry->goal == (entry->id == 51 ? QG_FIND_ART : QG_CLEAR_LEVEL));
        for (mode = 0; mode < 3; ++mode)
        {
            quest_contact_t contact;
            int stage;
            no_wilderness = mode > 0; coffee_break = mode == 2 ? 2 : 0;
            quest_contact(entry, &contact);
            if (!mode) CHECK(streq(contact.name, "Bulin") && streq(contact.building, "Castle") && streq(contact.town, "Morivant"));
            else if (mode == 1 && entry->id == 55) CHECK(streq(contact.portrait, "Uldrik II"));
            else CHECK(!contact.name[0]);
            for (stage = QS_UNTAKEN; stage <= QS_FAILED_DONE; ++stage)
            {
                int card = stage == QS_UNTAKEN ? QUEST_CARD_OFFER : QUEST_CARD_PROGRESS;
                doc_ptr doc = doc_alloc(80);
                entry->status = stage;
                _read_only(entry, card);
                quest_journal_doc(entry, doc, card, NULL, 12);
                if (contact.name[0])
                {
                    CHECK(_contains(doc, cases[i].objective));
                    CHECK(_contains(doc, cases[i].directions));
                }
                CHECK(!_contains(doc, cases[i].concealed));
                CHECK(!_contains(doc, "battle gloves")); /* turn-in response only */
                CHECK(!_contains(doc, "No further briefing was recorded"));
                if (stage == QS_UNTAKEN || stage == QS_TAKEN || stage == QS_IN_PROGRESS)
                {
                    CHECK(_contains(doc, "Leaving the quest site before completing it fails the quest"));
                    CHECK(_row_with(doc, "Objective") >= 0 && _row_with(doc, "Objective") < 10);
                    if (doc->portrait.live) CHECK(doc->portrait.row > _row_with(doc, "Objective"));
                }
                if (stage == QS_COMPLETED && !mode) CHECK(_contains(doc, "Return to Bulin at Castle, Morivant"));
                if (entry->id == 51 && contact.name[0]) CHECK(!_contains(doc, "Defeat all quest enemies"));
                doc_free(doc);
            }
            entry->status = QS_FAILED;
            _read_only(entry, QUEST_CARD_FAILED);
            entry->flags |= QF_INVIS;
            {
                doc_ptr doc = doc_alloc(80);
                quest_journal_doc(entry, doc, QUEST_CARD_PROGRESS, NULL, 12);
                CHECK(_contains(doc, "No known quest")); CHECK(!_contains(doc, cases[i].objective));
                doc_free(doc);
            }
            entry->flags = saved.flags;
        }
        *entry = saved; parent->substitute = substitution;
    }
    /* The underground-city reply must match all three existing reward paths,
     * including races that cannot wear gloves. Reading never rolls the gift. */
    {
        int races[] = {RACE_HUMAN, RACE_MON_CENTIPEDE, RACE_MON_DRAGON};
        int old_race = p_ptr->prace;
        for (i = 0; i < 3; ++i)
        {
            string_ptr s;
            p_ptr->prace = races[i];
            s = quest_get_description_at(quests_get(55), QS_COMPLETED);
            CHECK(strstr(string_buffer(s), i ? "Take this reward" : "Take my old battle gloves") != NULL);
            CHECK(!strstr(string_buffer(s), i ? "battle gloves" : "Take this reward"));
            string_free(s);
        }
        p_ptr->prace = old_race;
    }
    no_wilderness = FALSE; coffee_break = 0;
}

static void _guild_hunt_count(quest_ptr q)
{
    monster_type target = {0}, other = {0}, clone = {0}, second = {0};
    doc_ptr doc;
    CHECK(!quest_id_current() && !dungeon_type);
    q->status = QS_TAKEN; q->goal_current = 0;
    target.r_idx = clone.r_idx = second.r_idx = q->goal_idx;
    other.r_idx = q->goal_idx + 1;
    clone.mflag = MFLAG_BORN2;
    quests_on_kill_mon(&target); /* Outside the quest floor: no progress. */
    CHECK(q->goal_current == 0);
    quests_on_generate(quest_dungeon(q), q->level);
    CHECK(quest_id_current() == q->id && q->status == QS_IN_PROGRESS);
    quests_on_kill_mon(&other); CHECK(q->goal_current == 0);
    quests_on_kill_mon(&target); CHECK(q->goal_current == 1);
    quests_on_kill_mon(&target); CHECK(q->goal_current == 1); /* already counted */
    quests_on_kill_mon(&clone); CHECK(q->goal_current == 1);
    quests_on_kill_mon(&second); CHECK(q->goal_current == 2);
    doc = doc_alloc(80); quest_journal_doc(q, doc, QUEST_CARD_PROGRESS, NULL, 32);
    CHECK(_contains(doc, "Progress: 2/10 defeated")); doc_free(doc);
    /* Clear only the temporary current-floor marker; no completion or reward. */
    q->status = QS_COMPLETED; quests_on_leave();
    CHECK(!quest_id_current());
}

static void _guilds(void)
{
    struct { int id; cptr objective, response, concealed; } cases[] = {
        {56,"Day-troll", "Ten day-trolls accounted for", "steel helm"},
        {57,"Lesser titan", "That took more than a strong arm", "spiked gauntlets"},
        {58,"Ancient gold dragon", "this trophy to carry with you", "gold dragon scale mail"},
        {59,"Emberlord", "Take this trophy with our respect", "broad sword"},
        {79,"Many Colours", "That is a deed the Hall will remember", "dragon shield"},
        {80,"Pit Fiend", "your deeds speak for themselves", "starmetal chain mail"},
        {81,"Lesser titan", "That took more than a strong arm", "spiked gauntlets"},
        {29,"Magic Shop's back treasure room", "A useful piece of work", "barbarian talisman"},
        {30,"Black Market's treasure room", "You kept your side", "potion mimic"},
        {32,"Dusk elf Lords' shop", "Quiet streets suit me", "dweller on the threshold"}
    };
    int i, mode, alternate, stage;
    for (i = 0; i < sizeof(cases)/sizeof(cases[0]); ++i)
    {
        quest_ptr q = quests_get(cases[i].id);
        quest_ptr sibling = q->id == 57 ? quests_get(81) : q->id == 81 ? quests_get(57) : NULL;
        quest_t saved = *q;
        int sibling_status = sibling ? sibling->status : 0;
        bool hunt = q->dungeon != 0;
        int d = q->dungeon, old_alt = d_info[d].alt;
        u32b old_flags = d_info[d].flags1;
        bool replaces = d == DUNGEON_ORC || d == DUNGEON_GIANTS_HALL;
        CHECK(q->goal == (hunt ? QG_KILL_MON : QG_CLEAR_LEVEL));
        if (hunt) CHECK(q->goal_count == 10);
        if (sibling) sibling->status = QS_UNTAKEN;
        for (alternate = 0; alternate < (replaces ? 2 : 1); ++alternate)
        {
            if (replaces)
            {
                d_info[d].flags1 &= ~DF1_SUPPRESSED;
                if (alternate) d_info[d].flags1 |= DF1_SUPPRESSED;
                d_info[d].alt = d == DUNGEON_ORC ? DUNGEON_TROLL : DUNGEON_SNOW;
            }
            for (mode = 0; mode < 3; ++mode)
            {
                quest_contact_t contact;
                no_wilderness = mode > 0; coffee_break = mode == 2 ? 2 : 0;
                quest_contact(q, &contact);
                if (!mode) CHECK(streq(contact.name, hunt ? "Hjolgar" : "Paliro"));
                else if (!hunt && (mode == 1 || q->id == 30)) CHECK(streq(contact.name, "Lardbottom"));
                else CHECK(!contact.name[0]);
                q->goal_current = hunt ? 3 : 0;
                for (stage = QS_UNTAKEN; stage <= QS_FAILED_DONE; ++stage)
                {
                    int card = stage == QS_UNTAKEN ? QUEST_CARD_OFFER : QUEST_CARD_PROGRESS;
                    doc_ptr doc = doc_alloc(80);
                    q->status = stage;
                    _read_only(q, card);
                    quest_journal_doc(q, doc, card, NULL, 12);
                    if (hunt)
                    {
                        char location[160];
                        int expected = mode ? DUNGEON_SINK : alternate ? d_info[d].alt : d;
                        CHECK(quest_dungeon(q) == expected);
                        sprintf(location, "Location: %s, level %d", d_name + d_info[expected].name, q->level);
                        CHECK(_contains(doc, location));
                        CHECK(_contains(doc, cases[i].objective));
                        if (stage != QS_UNTAKEN) CHECK(_contains(doc, "Progress: 3/10 defeated"));
                        if (contact.name[0]) CHECK(_contains(doc, "Only target kills on the active quest floor count"));
                        if (q->id == 56) CHECK(!_contains(doc, "cave trolls"));
                    }
                    else if (contact.name[0])
                    {
                        CHECK(_contains(doc, cases[i].objective));
                        CHECK(_contains(doc, "Return to the Thieves' Guild") || _contains(doc, "return to the Thieves' Guild"));
                        CHECK(!_contains(doc, "Progress:")); /* No count of undiscovered inhabitants. */
                    }
                    CHECK(!_contains(doc, cases[i].concealed));
                    CHECK(!_contains(doc, cases[i].response));
                    CHECK(!_contains(doc, "No further briefing was recorded"));
                    if (stage <= QS_IN_PROGRESS)
                    {
                        CHECK(_contains(doc, "Leaving the quest site before completing it fails the quest"));
                        CHECK(_row_with(doc, "Objective") >= 0 && _row_with(doc, "Objective") < 10);
                    }
                    if (stage == QS_COMPLETED && !mode) CHECK(_contains(doc, hunt ?
                        "Return to Hjolgar at Fighters' Hall, Morivant" : "Return to Paliro at Thieves' Guild, Morivant"));
                    doc_free(doc);
                }
                q->status = QS_COMPLETED;
                {
                    doc_ptr doc = doc_alloc(100);
                    quest_journal_doc(q, doc, QUEST_CARD_REWARD, NULL, 32);
                    CHECK(_contains(doc, cases[i].response)); doc_free(doc);
                }
                q->status = QS_FAILED; _read_only(q, QUEST_CARD_FAILED);
                q->flags |= QF_INVIS;
                {
                    doc_ptr doc = doc_alloc(80);
                    quest_journal_doc(q, doc, QUEST_CARD_PROGRESS, NULL, 12);
                    CHECK(_contains(doc, "No known quest")); CHECK(!_contains(doc, cases[i].objective));
                    doc_free(doc);
                }
                q->flags = saved.flags;
            }
            no_wilderness = FALSE; coffee_break = 0;
            if (hunt) _guild_hunt_count(q);
        }
        d_info[d].flags1 = old_flags; d_info[d].alt = old_alt;
        *q = saved;
        if (sibling) sibling->status = sibling_status;
    }
}

static void _angwil(vec_ptr all)
{
    struct { int id, parent; cptr objective, directions, response, concealed; } cases[] = {
        {19,19,"site beyond the first breach", "northwest of town", "The first breach has been cleared", "pyre-priest"},
        {21,21,"site beyond the second breach", "edge of town", "The second breach has been cleared", "STAFF_HEAL_CURING"},
        {23,23,"Clear every enemy from the spawning pits", "nearby valley", "The pits are cleared", "quylthulg"},
        {24,24,"Destroy the source of the Concern's influence", "building marked '& CO.'", "The presence behind the Concern has been destroyed", "The Chairman"},
        {35,35,"Clear every enemy from the dragons' dungeon", "small dungeon occupied by dragons", "The dungeon is clear, and my people", "dragon wings"},
        {36,36,"Clear every enemy from the worms' sand pit", "sand pit is nearby", "The sand pit is cleared", "blade of chaos"},
        {85,21,"site beyond the second breach", "edge of town", "The second breach has been cleared", "spider-engine"},
        {86,24,"Destroy the source of the warehouse's evil presence", "northern edge of town", "The presence in the warehouse has been destroyed", "Gospel of Mug"},
        {91,36,"Clear every enemy from the old quarry", "old quarry", "The quarry is cleared", "fearmaster"}
    };
    int i, j, mode, stage;
    for (i = 0; i < sizeof(cases)/sizeof(cases[0]); ++i)
    {
        quest_ptr q = NULL, parent = NULL;
        quest_t saved;
        int substitution;
        bool target = cases[i].id == 24 || cases[i].id == 86;
        for (j = 0; j < vec_length(all); ++j)
        {
            quest_ptr candidate = vec_get(all, j);
            if (candidate->id == cases[i].id) q = candidate;
            if (candidate->id == cases[i].parent) parent = candidate;
        }
        CHECK(q && parent);
        if (!q || !parent) continue;
        saved = *q; substitution = parent->substitute;
        parent->substitute = q == parent ? 0 : -q->id;
        CHECK(q->goal == (target ? QG_KILL_MON : QG_CLEAR_LEVEL));
        if (target) CHECK(q->goal_count == 1);
        for (mode = 0; mode < 3; ++mode)
        {
            quest_contact_t contact;
            cptr speaker = !mode ? "Colmanwen" : (q->id == 19 || q->id == 21 || q->id == 85) ? "Odwin" :
                q->id == 35 || (mode == 1 && (q->id == 36 || q->id == 91)) ? "Kimuk" :
                mode == 1 && q->id == 23 ? "Uldrik" : "";
            no_wilderness = mode > 0; coffee_break = mode == 2 ? 2 : 0;
            quest_contact(q, &contact);
            CHECK(streq(contact.name, speaker));
            if (!mode) CHECK(streq(contact.building, "Manor") && streq(contact.town, "Angwil"));
            else if (speaker[0]) CHECK(streq(contact.town, "Outpost"));
            for (stage = QS_UNTAKEN; stage <= QS_FAILED_DONE; ++stage)
            {
                int card = stage == QS_UNTAKEN ? QUEST_CARD_OFFER : QUEST_CARD_PROGRESS;
                doc_ptr doc = doc_alloc(100);
                q->status = stage;
                q->goal_current = target && (stage == QS_COMPLETED || stage == QS_FINISHED) ? 1 : 0;
                _read_only(q, card);
                quest_journal_doc(q, doc, card, NULL, 12);
                if (speaker[0])
                {
                    CHECK(_contains(doc, cases[i].objective));
                    CHECK(_contains(doc, cases[i].directions));
                    if (target)
                    {
                        CHECK(_contains(doc, "Clearing the whole"));
                        CHECK(_contains(doc, stage == QS_UNTAKEN ? "Required: 1 defeated" :
                            q->goal_current ? "Progress: 1/1 defeated" : "Progress: 0/1 defeated"));
                    }
                }
                CHECK(!_contains(doc, cases[i].response));
                CHECK(!_contains(doc, cases[i].concealed));
                CHECK(!_contains(doc, "No further briefing was recorded"));
                if (target) CHECK(!_contains(doc, r_name + r_info[q->goal_idx].name));
                else CHECK(!_contains(doc, "Progress:"));
                if (q->id == 91)
                {
                    CHECK(_contains(doc, mode ? "northwest" : "south"));
                    CHECK(!_contains(doc, mode ? "south" : "northwest"));
                }
                if (q->id == 23) CHECK(!_contains(doc, "where you defeated the orcs"));
                if (q->id == 24) CHECK(!_contains(doc, "account number"));
                if (stage <= QS_IN_PROGRESS)
                {
                    CHECK(_contains(doc, "Leaving the quest site before completing it fails the quest"));
                    CHECK(_row_with(doc, "Objective") >= 0 && _row_with(doc, "Objective") < 10);
                }
                if (stage == QS_COMPLETED && !mode) CHECK(_contains(doc, "Return to Colmanwen at Manor, Angwil"));
                if (stage == QS_COMPLETED && mode && speaker[0])
                    CHECK(_contains(doc, streq(speaker, "Odwin") ? "Return to Odwin at The White Horse Inn, Outpost" :
                        streq(speaker, "Kimuk") ? "Return to Kimuk at Crossing Tower, Outpost" : "Return to Count Uldrik in Outpost"));
                doc_free(doc);
            }
            q->status = QS_COMPLETED;
            {
                doc_ptr doc = doc_alloc(100);
                quest_journal_doc(q, doc, QUEST_CARD_REWARD, NULL, 32);
                CHECK(_contains(doc, cases[i].response));
                if (q->id == 21) CHECK(_contains(doc, mode ? "My wizard friend" : "Our resident wizard"));
                doc_free(doc);
            }
            q->status = QS_FAILED; _read_only(q, QUEST_CARD_FAILED);
            q->flags |= QF_INVIS;
            {
                doc_ptr doc = doc_alloc(100);
                quest_journal_doc(q, doc, QUEST_CARD_PROGRESS, NULL, 12);
                CHECK(_contains(doc, "No known quest")); CHECK(!_contains(doc, cases[i].objective));
                doc_free(doc);
            }
            q->flags = saved.flags; q->status = QS_UNTAKEN;
            {
                doc_ptr doc = doc_alloc(100);
                vec_ptr list = quest_journal_list(0);
                CHECK(!_listed(list, q)); vec_free(list);
                quest_journal_doc(q, doc, QUEST_CARD_PROGRESS, NULL, 12);
                CHECK(_contains(doc, "No known quest")); doc_free(doc);
            }
        }
        *q = saved; parent->substitute = substitution;
    }
    no_wilderness = FALSE; coffee_break = 0;
}

static void _remaining(vec_ptr all)
{
    struct { int id, parent, goal; cptr objective, concealed; } cases[] = {
        {22,22,QG_CLEAR_LEVEL,"orc camp","reward we owe"},
        {60,60,QG_CLEAR_LEVEL,"the tunnels","You have earned this reward"},
        {67,67,QG_KILL_MON,"five great crystal drakes","share of the payment"},
        {68,68,QG_CLEAR_LEVEL,"dinosaur experiment","prehistoric problem"},
        {75,75,QG_CLEAR_LEVEL,"Apina Island","this reward is yours"},
        {83,75,QG_CLEAR_LEVEL,"High Tower","owe you this reward"},
        {99,60,QG_CLEAR_LEVEL,"Scary Rock","Here is my reward"},
        {72,72,QG_CLEAR_LEVEL,"in the cellar","You did what our officers could not"},
        {73,73,QG_CLEAR_LEVEL,"smugglers' den","evidence stores"},
        {74,74,QG_CLEAR_LEVEL,"adventurers' house","now available as your home"},
        {33,33,QG_CLEAR_LEVEL,"Cloning Pits","reward we owe you"},
        {61,61,QG_CLEAR_LEVEL,"from the wreckage","safe to investigate"},
        {69,69,QG_CLEAR_LEVEL,"old watchtower","Here is your reward"},
        {70,70,QG_CLEAR_LEVEL,"hostile creatures","recovered this lamp"},
        {71,71,QG_CLEAR_LEVEL,"fairies' thicket","You have made the forest safer"},
        {89,70,QG_KILL_MON,"man-eating cyclops","cyclops is dead"},
        {63,63,QG_KILL_MON,"five fieldcap patches","signet ring as your reward"},
        {64,64,QG_KILL_MON,"renegade old sorcerer","You passed"},
        {65,65,QG_CLEAR_LEVEL,"from the laboratory","Here is a pair"},
        {66,66,QG_CLEAR_LEVEL,"basilisk cave","Top marks"},
        {76,76,QG_FIND_ART,"wizard's staff","Mokomagi"},
        {90,66,QG_CLEAR_LEVEL,"academy occupying the museum","museum can reopen"},
        {10,10,QG_CLEAR_LEVEL,"Node of Sorcery","earned this spellbook"},
        {11,11,QG_CLEAR_LEVEL,"Node of Chaos","Take this spellbook"},
        {12,12,QG_CLEAR_LEVEL,"Node of Nature","This spellbook is your reward"},
        {77,77,QG_CLEAR_LEVEL,"time mages' site","teleport service is restored"},
        {98,98,QG_CLEAR_LEVEL,"Angwilian stables","Here is your reward"},
        {26,26,QG_KILL_MON,"horned general","Hammerhorn"}
    };
    int i, j, mode, stage;
    for (i = 0; i < sizeof(cases)/sizeof(cases[0]); ++i)
    {
        quest_ptr q = NULL, parent = NULL;
        quest_t saved;
        int substitution;
        for (j = 0; j < vec_length(all); ++j)
        {
            quest_ptr entry = vec_get(all, j);
            if (entry->id == cases[i].id) q = entry;
            if (entry->id == cases[i].parent) parent = entry;
        }
        CHECK(q && parent); if (!q || !parent) continue;
        saved = *q; substitution = parent->substitute;
        parent->substitute = parent == q ? 0 : -q->id;
        CHECK(q->goal == cases[i].goal);
        for (mode = 0; mode < 3; ++mode)
        {
            quest_contact_t contact;
            no_wilderness = mode > 0; coffee_break = mode == 2 ? 2 : 0;
            quest_contact(q, &contact);
            if (!mode && q->id != 98 && q->id != 26) CHECK(contact.name[0]);
            for (stage = QS_UNTAKEN; stage <= QS_COMPLETED; ++stage)
            {
                doc_ptr doc = doc_alloc(64);
                int card = stage == QS_UNTAKEN ? QUEST_CARD_OFFER : QUEST_CARD_PROGRESS;
                q->status = stage; _read_only(q, card);
                quest_journal_doc(q, doc, card, NULL, 12);
                CHECK(_contains(doc, cases[i].objective));
                CHECK(!_contains(doc, cases[i].concealed));
                CHECK(!_contains(doc, "No further briefing was recorded"));
                CHECK(_row_with(doc, "Next:") < 8);
                CHECK(_row_with(doc, "Objective") < 12);
                if (q->goal == QG_CLEAR_LEVEL) CHECK(!_contains(doc, "Progress:"));
                if (q->id == 73) CHECK(_contains(doc, mode ? "south of the Arena" : "north of the police"));
                if (q->id == 69) CHECK(_contains(doc, mode ? "near the river" : "near the shore"));
                if (q->id == 26) CHECK(_contains(doc, "Legacy commission"));
                if (q->goal == QG_KILL_MON && q->id != 26)
                    CHECK(_contains(doc, stage == QS_UNTAKEN ? "Required:" : "Progress:"));
                doc_free(doc);
            }
            q->status = QS_FAILED; _read_only(q, QUEST_CARD_FAILED);
            q->flags |= QF_INVIS;
            {
                doc_ptr doc = doc_alloc(80);
                quest_journal_doc(q, doc, QUEST_CARD_OFFER, NULL, 32);
                CHECK(_contains(doc, "No known quest")); CHECK(!_contains(doc, cases[i].objective));
                doc_free(doc);
            }
            q->flags = saved.flags;
        }
        *q = saved; parent->substitute = substitution;
    }
    no_wilderness = FALSE; coffee_break = 0;
}

static void _outcomes(void)
{
    int ids[] = {5,74,77,QUEST_OBERON,QUEST_SERPENT};
    cptr texts[] = {"Reduced prices", "available as your home", "teleport service is restored",
        "way to the final level is open", "last errand is complete"};
    int i, status;
    for (i = 0; i < 5; ++i)
    {
        quest_ptr q = quests_get(ids[i]);
        int old_status = q->status;
        for (status = QS_TAKEN; status <= QS_FAILED; ++status)
        {
            doc_ptr doc = doc_alloc(80);
            q->status = status;
            quest_journal_doc(q, doc, QUEST_CARD_PROGRESS, NULL, 32);
            CHECK(_contains(doc, "Outcome:") == (status == QS_FINISHED));
            if (status == QS_FINISHED) CHECK(_contains(doc, texts[i]));
            if (q->id == QUEST_SERPENT) CHECK(!_contains(doc, "Metatron"));
            doc_free(doc);
        }
        q->status = old_status;
    }
}

static void _journal_memory(void)
{
    quest_ptr a = quests_get(20), b = quests_get(27), failed = quests_get(50);
    quest_t saved_a = *a, saved_b = *b;
    int flags = failed->flags;
    quest_journal_state_t saved = quest_journal_state;
    memset(&quest_journal_state, 0, sizeof(quest_journal_state));
    a->status = b->status = QS_TAKEN;
    a->danger_level = 1; b->danger_level = 2;
    _keys = "anx"; quests_display();
    CHECK(quest_journal_state.tab == 0 && quest_journal_state.selected[0] == b->id);
    b->status = QS_COMPLETED; /* Same quest moves to the top of the list. */
    _keys = "x"; quests_display();
    CHECK(quest_journal_state.selected[0] == b->id);
    b->flags |= QF_INVIS;
    _keys = "x"; quests_display();
    CHECK(quest_journal_state.selected[0] == a->id);
    _keys = "cx"; quests_display();
    CHECK(quest_journal_state.tab == 1 && quest_journal_state.selected[1] == quests_get(1)->id);
    _keys = "fx"; quests_display();
    CHECK(quest_journal_state.tab == 2 && quest_journal_state.selected[2] == failed->id);
    _keys = "x"; quests_display();
    CHECK(quest_journal_state.tab == 2 && quest_journal_state.selected[2] == failed->id);
    failed->flags |= QF_INVIS; /* A vanished Failed tab falls back safely. */
    _keys = "x"; quests_display();
    CHECK(quest_journal_state.tab == 0 && quest_journal_state.selected[0] == a->id);
    _keys = "\r]]x"; quests_display();
    {
        int i, top = 0;
        for (i = 0; i < 160; ++i)
            if (quest_journal_state.scroll[i].id == a->id) top = quest_journal_state.scroll[i].top;
        CHECK(top == 2);
        _keys = "cx"; quests_display();
        _keys = "a\rx"; quests_display();
        for (i = 0; i < 160; ++i)
            if (quest_journal_state.scroll[i].id == a->id) CHECK(quest_journal_state.scroll[i].top == top);
    }
    *a = saved_a; *b = saved_b; failed->flags = flags;
    quest_journal_state = saved;
}

static void _leave_fixture(quest_ptr q, bool retake, cptr answer)
{
    int flags = q->flags, dungeon = q->dungeon, level = q->level, goal = q->goal;
    q->dungeon = DUNGEON_SINK; q->level = 123; q->goal = QG_CLEAR_LEVEL;
    q->flags = QF_TOWN | (retake ? QF_RETAKE | QF_RANDOM : 0);
    q->status = QS_TAKEN;
    quests_on_generate(DUNGEON_SINK, 123);
    CHECK(quest_id_current() == q->id);
    _keys = answer;
    quests_on_leave();
    CHECK(quest_id_current() == 0);
    q->flags = flags; q->dungeon = dungeon; q->level = level; q->goal = goal;
}

static void _failure_fixture(void)
{
    quest_ptr left = quests_get(20), abandoned = quests_get(27), retake = quests_get(31);
    quest_ptr legacy = quests_get(33), hidden = quest_alloc("Hidden failure fixture");
    quest_ptr other = quests_get(50);
    string_ptr speech;
    doc_ptr doc;
    cptr old_file;
    u32b rng[RAND_DEG], value;
    u16b place;
    bool quick;
    int turn;
    _leave_fixture(left, FALSE, "");
    CHECK(left->failure && streq(left->failure->reason, "Left the quest site before completing the objective."));
    CHECK(!left->failure->reported && !left->failure->response[0]);
    _leave_fixture(abandoned, TRUE, "Y");
    CHECK(abandoned->failure && streq(abandoned->failure->reason, "Chose to abandon the quest when leaving."));
    _leave_fixture(retake, TRUE, "n");
    CHECK(retake->status == QS_TAKEN && !retake->failure);
    hidden->flags = QF_INVIS | QF_TOWN;
    quest_fail(hidden); quest_report_failure(hidden);
    CHECK(!hidden->failure); quest_free(hidden);

    speech = quest_get_description_at(left, QS_FAILED);
    CHECK(string_length(speech) > 0);
    doc = doc_alloc(240); quest_journal_doc(left, doc, QUEST_CARD_PROGRESS, NULL, 32);
    CHECK(_contains(doc, "Not yet reported"));
    CHECK(!_contains(doc, string_buffer(speech))); doc_free(doc);
    turn = game_turn; value = Rand_value; place = Rand_place; quick = Rand_quick;
    memcpy(rng, Rand_state, sizeof(rng));
    quest_report_failure(left);
    CHECK(left->status == QS_FAILED && left->failure->reported);
    CHECK(left->failure->reported_turn == game_turn && streq(left->failure->response, string_buffer(speech)));
    CHECK(game_turn == turn && value == Rand_value && place == Rand_place && quick == Rand_quick && !memcmp(rng, Rand_state, sizeof(rng)));
    _keys = "\033"; quest_card(left, QUEST_CARD_FAILED, NULL);
    left->status = QS_FAILED_DONE; /* Same transition as the town service. */
    old_file = left->file; left->file = NULL;
    _read_only(left, QUEST_CARD_PROGRESS);
    doc = doc_alloc(240); quest_journal_doc(left, doc, QUEST_CARD_PROGRESS, NULL, 32);
    CHECK(_contains(doc, string_buffer(speech))); doc_free(doc);
    quest_report_failure(left); /* A repeat call cannot replace the snapshot. */
    CHECK(streq(left->failure->response, string_buffer(speech)));
    left->flags |= QF_INVIS;
    doc = doc_alloc(240); quest_journal_doc(left, doc, QUEST_CARD_PROGRESS, NULL, 32);
    CHECK(_contains(doc, "No known quest")); CHECK(!_contains(doc, string_buffer(speech))); doc_free(doc);
    left->flags &= ~QF_INVIS; left->file = old_file; string_free(speech);

    /* Old unreported failures can gain a response, never an invented cause.
     * An empty response is distinct from one that was never recorded. */
    legacy->status = QS_FAILED;
    CHECK(!legacy->failure);
    old_file = legacy->file; legacy->file = NULL;
    quest_report_failure(legacy); legacy->status = QS_FAILED_DONE;
    CHECK(legacy->failure->reported && !legacy->failure->reason[0] && !legacy->failure->response[0]);
    doc = doc_alloc(100); quest_journal_doc(legacy, doc, QUEST_CARD_PROGRESS, NULL, 32);
    CHECK(_contains(doc, "Failure reason not recorded"));
    CHECK(_contains(doc, "No closing response was given")); doc_free(doc);
    legacy->file = old_file;
    CHECK(other->failure && !other->failure->reason[0] && !other->failure->reported);
    quest_journal_state.tab = 2;
    quest_journal_state.selected[0] = retake->id;
    quest_journal_state.selected[1] = quests_get(1)->id;
    quest_journal_state.selected[2] = left->id;
    do_cmd_save_game(FALSE);
}

extern bool quest_test_record_rewards;
extern void quest_test_capture_rewards(string_ptr into);

static void _drop_observer_checks(void)
{
    obj_t forge = {0};
    string_ptr observed = string_alloc();
    int x = px, y = py, old_image = p_ptr->image, old_blind = p_ptr->blind;
    u32b info = cave[y][x].info, rng[RAND_DEG], value;
    int old_feat = cave[y][x].feat;
    bool aware;
    u16b place;
    int o_idx;
    object_prep(&forge, lookup_kind(TV_POTION, SV_POTION_HEALING));
    aware = k_info[forge.k_idx].aware; k_info[forge.k_idx].aware = FALSE;
    forge.number = 2;
    quest_test_capture_rewards(observed);
    cave_set_feat(y, x, feat_floor);
    cave[y][x].info |= CAVE_LITE | CAVE_VIEW;
    drop_near_stack_hack = TRUE;
    o_idx = drop_near(&forge, -1, y, x);
    CHECK(o_idx > 0 && string_length(observed));
    CHECK(!strstr(string_buffer(observed), "Healing")); /* unidentified remains unidentified */
    /* Merge two newly delivered objects into an already present stack. */
    forge.number = 2;
    string_clear(observed);
    drop_near_stack_hack = TRUE;
    CHECK(drop_near(&forge, -1, y, x) == 0); /* existing API returns zero for merges */
    {
        doc_ptr doc = doc_alloc(100);
        doc_insert(doc, string_buffer(observed));
        CHECK(_contains(doc, "2 ") && !_contains(doc, "4 ")); /* not the old stack's four */
        doc_free(doc);
    }
    string_clear(observed);
    memcpy(rng, Rand_state, sizeof(rng)); value = Rand_value; place = Rand_place;
    p_ptr->blind = 1; quest_record_reward_drop(&o_list[o_idx], 2);
    p_ptr->blind = 0; p_ptr->image = 1; quest_record_reward_drop(&o_list[o_idx], 2);
    p_ptr->image = 0;
    {
        obj_ptr obj = &o_list[o_idx];
        u32b old_info = cave[obj->loc.y][obj->loc.x].info;
        cave[obj->loc.y][obj->loc.x].info &= ~(CAVE_LITE | CAVE_MNLT | CAVE_VIEW);
        quest_record_reward_drop(obj, 2); /* remembered, but not visible now */
        cave[obj->loc.y][obj->loc.x].info = old_info;
    }
    CHECK(!string_length(observed));
    CHECK(!memcmp(rng, Rand_state, sizeof(rng)) && value == Rand_value && place == Rand_place);
    forge.number = 2;
    CHECK(!drop_near(&forge, 100, y, x)); /* unsuccessful drop adds no record */
    CHECK(!string_length(observed));
    /* One item fits in the old stack, but there is no slot for the remainder.
     * Record that one successful delivery even though drop_near returns zero. */
    {
        int old_max = o_max, old_limit = max_o_idx;
        doc_ptr doc = doc_alloc(100);
        o_list[o_idx].number = 98; forge.number = 3;
        o_max = max_o_idx = 1;
        drop_near_stack_hack = TRUE;
        CHECK(!drop_near(&forge, -1, y, x));
        o_max = old_max; max_o_idx = old_limit;
        CHECK(o_list[o_idx].number == 99 && forge.number == 2);
        doc_insert(doc, string_buffer(observed));
        CHECK(_contains(doc, "a ") && !_contains(doc, "2 ") && !_contains(doc, "99 "));
        doc_free(doc);
    }
    quest_test_capture_rewards(NULL);
    p_ptr->blind = old_blind; p_ptr->image = old_image; cave[y][x].info = info;
    cave_set_feat(y, x, old_feat); k_info[forge.k_idx].aware = aware;
    drop_near_stack_hack = FALSE;
    string_free(observed);
    {
        quest_t q = {0};
        const quest_patron_record_t *record;
        char long_message[1024];
        bool old_statistics = statistics_hack;
        int old_world = world_monster, i;
        q.flags = QF_PURPLE | QF_INVIS;
        quest_patron_begin(&q, -30, TRUE);
        quest_patron_message(TERM_WHITE, "hidden quest record must not exist");
        quest_patron_end();
        CHECK(!quest_patron_record(-30));
        q.flags = QF_PURPLE;
        quest_patron_begin(&q, -30, TRUE);
        cmsg_print(TERM_VIOLET, "visible patron response");
        world_monster = 1;
        cmsg_print(TERM_WHITE, "suppressed by stopped time");
        world_monster = old_world;
        statistics_hack = TRUE;
        cmsg_print(TERM_WHITE, "suppressed by statistics");
        statistics_hack = old_statistics;
        memset(long_message, 'a', sizeof(long_message) - 1); long_message[sizeof(long_message) - 1] = 0;
        for (i = 0; i < 12; ++i) quest_patron_message(TERM_WHITE, long_message);
        quest_patron_end();
        record = quest_patron_record(-30);
        CHECK(record && strlen(record->response) <= 8192);
        CHECK(strstr(record->response, "visible patron response") != NULL);
        CHECK(strstr(record->response, "suppressed") == NULL);
        CHECK(strstr(record->response, "[Further messages omitted") != NULL);
        quest_patron_message(TERM_WHITE, "outside the observation scope");
        CHECK(strstr(record->response, "outside the observation scope") == NULL);
    }
}

static void _special_fixture(int mode)
{
    int i;
    bool record_rewards = mode != 28 && mode != 29;
    bool known_failure = mode == 30 || mode == 31;
    if (mode >= 28 && mode <= 31) mode = 18 + (mode % 2);
    quest_test_record_rewards = record_rewards;
    if (mode == 18 || mode == 19)
    {
        p_ptr->pclass = CLASS_DISCIPLE;
        p_ptr->psubclass = mode == 18 ? DISCIPLE_KARROT : DISCIPLE_TROIKA;
        no_wilderness = TRUE; coffee_break = 0;
        get_class()->birth();
        for (i = 0; i < 7; ++i)
        {
            quest_ptr q;
            quest_t record;
            p_ptr->lev = p_ptr->max_plv = 10 + i * 2;
            q = disciple_get_quest(DUNGEON_SINK, 45 + i * 5);
            CHECK(q != NULL); if (!q) break;
            CHECK(!q->receipt && !q->failure);
            quest_journal_state.scroll[159].id = PURPLE_QUEST;
            quest_journal_state.scroll[159].top = 12;
            quest_journal_state.scroll[159].width = 80;
            q->goal_current = i == 6 ? 0 : q->goal_count;
            if (i == 6 && known_failure)
            {
                q->level = 123; /* isolate from the independently assigned dungeon hunts */
                quests_on_generate(DUNGEON_SINK, 123);
                CHECK(quest_id_current() == PURPLE_QUEST);
                quests_on_leave();
            }
            else if (i == 6) quest_fail(q);
            else quest_complete(q, point(px, py));
            CHECK(q->status == QS_UNTAKEN); /* reused active quest is cleared as before */
            CHECK(!q->failure && !q->receipt && !quest_journal_state.scroll[159].id);
            CHECK(mode == 18 ? karrot_quest_history(i, &record) : troika_quest_history(i, &record));
            CHECK(record.status == (i == 6 ? QS_FAILED_DONE : QS_FINISHED));
            {
                const quest_patron_record_t *observed = quest_patron_record(record.id);
                CHECK((observed != NULL) == record_rewards);
                if (observed)
                {
                    CHECK(observed->response[0]);
                    CHECK(!observed->reason[0] || (i == 6 && known_failure));
                    if (i == 6 && known_failure) CHECK(strstr(observed->reason, "Left the quest site") != NULL);
                    if (mode == 19) CHECK((observed->benefits[0] != 0) == (i == 4));
                }
            }
        }
        /* Read all lists together: their borrowed history entries must remain
         * stable, including after a different tab is populated. */
        {
            vec_ptr complete = quest_journal_list(1), failed = quest_journal_list(2);
            int seen = 0;
            for (i = 0; i < vec_length(complete); ++i)
            {
                quest_ptr q = vec_get(complete, i);
                if (q->id >= 0) continue;
                CHECK(q->name && q->name[0]); _read_only(q, QUEST_CARD_PROGRESS); ++seen;
            }
            CHECK(seen == 6); seen = 0;
            for (i = 0; i < vec_length(failed); ++i)
            {
                quest_ptr q = vec_get(failed, i);
                if (q->id >= 0) continue;
                _read_only(q, QUEST_CARD_PROGRESS); ++seen;
            }
            CHECK(seen == 1);
            vec_free(complete); vec_free(failed);
        }
        _keys = "c\r]]x"; quests_display();
    }
    else
    {
        quest_ptr q = quests_get(40);
        int old_blind = p_ptr->blind;
        quest_test_record_rewards = mode != 16 && mode != 27;
        no_wilderness = mode >= 26; coffee_break = mode >= 26 ? 2 : 0;
        /* A fully lit, empty patch so observation and dropped objects are
         * independently inspectable without depending on a town layout. */
        for (i = py - 4; i <= py + 4; ++i)
        {
            int x;
            for (x = px - 4; x <= px + 4; ++x)
            {
                cave_set_feat(i, x, feat_floor);
                cave[i][x].info |= CAVE_GLOW | CAVE_VIEW | CAVE_LITE;
            }
        }
        if (mode == 17) p_ptr->blind = 1;
        q->status = QS_IN_PROGRESS;
        q->goal_current = q->goal_count;
        _keys = "ay"; /* Small-town completion's existing stat-choice prompt. */
        quest_complete(q, point(px, py));
        CHECK(q->status == QS_FINISHED && q->receipt);
        if (mode == 15 || mode == 26) CHECK(!prefix(q->receipt->reward, "No floor rewards"));
        else CHECK(prefix(q->receipt->reward, "No floor rewards"));
        if (mode >= 26) CHECK(strstr(q->receipt->response, "increased") != NULL);
        p_ptr->blind = old_blind;
        _read_only(q, QUEST_CARD_PROGRESS);
        quest_test_record_rewards = TRUE;
        _keys = "c\r]]x"; quests_display();
    }
    do_cmd_save_game(FALSE);
}

static void _remaining_town_fixture(int mode)
{
    int ids[][3] = {{22,67,68}, {74,73,72}, {33,61,71}, {63,76,66}, {77,10,11}, {98,69,35}};
    int portrait_ids[] = {1,62,3,51,56,29,19,22,74,33,63,77,11,12,98,69};
    int i, row = mode - 20;
    if (mode >= 100) row = mode >= 114 ? 5 : 0;
    vec_ptr all = quests_get_all();
    for (i = 0; i < vec_length(all); ++i)
    {
        quest_ptr q = vec_get(all, i);
        q->status = QS_UNTAKEN; q->substitute = 0;
    }
    vec_free(all);
    memset(&quest_journal_state, 0, sizeof(quest_journal_state));
    no_wilderness = mode == 25 || mode >= 114; coffee_break = no_wilderness ? 2 : 0;
    quests_get(ids[row][1])->status = QS_COMPLETED;
    quests_get(ids[row][2])->status = QS_FAILED;
    if (mode == 20) { quests_get(60)->status = QS_TAKEN; quests_get(75)->status = QS_TAKEN; }
    if (mode == 22) quests_get(70)->status = QS_TAKEN;
    if (mode == 23) { quests_get(64)->status = QS_TAKEN; quests_get(65)->status = QS_TAKEN; }
    if (mode == 24) quests_get(12)->status = QS_TAKEN;
    player_place(30, 84);
    cave_set_feat(30, 85, f_tag_to_index("BUILDING_0"));
    cave[30][85].special = mode >= 100 ? portrait_ids[mode - 100] : ids[row][0];
    if (mode >= 100) quests_get(portrait_ids[mode - 100])->status = QS_UNTAKEN;
    cave_set_feat(30, 86, f_tag_to_index("BUILDING_1")); cave[30][86].special = ids[row][1];
    cave_set_feat(30, 83, f_tag_to_index("BUILDING_2")); cave[30][83].special = ids[row][2];
    do_cmd_save_game(FALSE);
}

void test_quest_probe(int browse)
{
    quest_ptr q = quests_get(1), other = quests_get(50), hidden = quests_get(97);
    int i, mode;
    bool old_wilderness = no_wilderness;
    byte old_coffee = coffee_break;
    int old_more = auto_more_state;
    quest_contact_t contact;
    vec_ptr list;
    doc_ptr doc;
    string_ptr s;
    if ((browse >= 20 && browse <= 25) || (browse >= 100 && browse < 116))
    {
        _remaining_town_fixture(browse);
        printf("quest-check checks %d failures %d\n", _checks, _failures);
        return;
    }
    if ((browse >= 15 && browse <= 19) || (browse >= 26 && browse <= 31))
    {
        _old_hook = Term->xtra_hook; Term->xtra_hook = _input;
        auto_more_state = AUTO_MORE_SKIP_ALL;
        _special_fixture(browse);
        Term->xtra_hook = _old_hook; auto_more_state = old_more;
        printf("quest-check checks %d failures %d\n", _checks, _failures);
        return;
    }
    if (browse == 4 || browse == 5)
    {
        vec_ptr entries = quests_get_all();
        int receipts = 0, failures = 0;
        for (i = 0; i < vec_length(entries); ++i)
        {
            quest_ptr entry = vec_get(entries, i);
            if (entry->failure)
            {
                ++failures;
                CHECK(!(entry->flags & QF_INVIS) && (entry->status == QS_FAILED || entry->status == QS_FAILED_DONE));
                printf("quest-failure %d %s\n", entry->id, entry->failure->reason);
                printf("quest-failure-response %d %d %lu %s\n", entry->id, entry->failure->reported,
                    (unsigned long)entry->failure->reported_turn, entry->failure->response);
                _read_only(entry, QUEST_CARD_PROGRESS);
            }
            else if (!(entry->flags & QF_INVIS) && (entry->status == QS_FAILED || entry->status == QS_FAILED_DONE))
            {
                doc = doc_alloc(90); quest_journal_doc(entry, doc, QUEST_CARD_PROGRESS, NULL, 32);
                CHECK(_contains(doc, "Failure reason not recorded"));
                CHECK(_contains(doc, entry->status == QS_FAILED ? "Not yet reported" : "Closing response not recorded"));
                doc_free(doc);
            }
            if (entry->receipt)
            {
                ++receipts;
                CHECK(entry->status == QS_FINISHED && !(entry->flags & QF_INVIS));
                printf("quest-receipt %d %lu %s\n", entry->id, (unsigned long)entry->receipt->turn, entry->receipt->reward);
                printf("quest-response %d %s\n", entry->id, entry->receipt->response);
                _read_only(entry, QUEST_CARD_PROGRESS);
            }
            else if (entry->status == QS_FINISHED && (entry->flags & QF_TOWN) && !(entry->flags & QF_INVIS))
            {
                doc = doc_alloc(90); quest_journal_doc(entry, doc, QUEST_CARD_PROGRESS, NULL, 32);
                CHECK(_contains(doc, "Reward not recorded")); doc_free(doc);
            }
        }
        vec_free(entries);
        printf("quest-receipts %d\n", receipts);
        printf("quest-failures %d\n", failures);
        printf("quest-journal %d %d %d %d\n", quest_journal_state.tab, quest_journal_state.selected[0],
            quest_journal_state.selected[1], quest_journal_state.selected[2]);
        for (i = 0; i < 160; ++i)
            if (quest_journal_state.scroll[i].id)
                printf("quest-scroll %d %d %d\n", quest_journal_state.scroll[i].id,
                    quest_journal_state.scroll[i].top, quest_journal_state.scroll[i].width);
        for (mode = 1; mode < 3; ++mode)
        {
            vec_ptr history = quest_journal_list(mode);
            for (i = 0; i < vec_length(history); ++i)
            {
                quest_ptr q = vec_get(history, i);
                if (q->id >= 0) continue;
                printf("quest-patron %d %d %d %d %d %lu\n", q->id, q->status, q->goal_idx,
                    q->goal_current, q->goal_count, (unsigned long)q->completed_turn);
                {
                    const quest_patron_record_t *record = quest_patron_record(q->id);
                    if (record)
                    {
                        printf("patron-reason %d %s\n", q->id, record->reason);
                        printf("patron-drops %d %s\n", q->id, record->drops);
                        printf("patron-response %d %s\n", q->id, record->response);
                        printf("patron-benefits %d %s\n", q->id, record->benefits);
                    }
                }
                _read_only(q, QUEST_CARD_PROGRESS);
            }
            vec_free(history);
        }
        if (browse == 5) do_cmd_save_game(FALSE);
        printf("quest-check checks %d failures %d\n", _checks, _failures);
        return;
    }
    /* Resolve substitution caches before snapshotting them. No quest is taken. */
    vec_ptr all = quests_get_all();
    _old_hook = Term->xtra_hook; Term->xtra_hook = _input;
    auto_more_state = AUTO_MORE_SKIP_ALL;
    for (i = 0; i < vec_length(all); ++i) quests_get(((quest_ptr)vec_get(all, i))->id);
    CHECK(q && other && hidden);
    no_wilderness = FALSE; coffee_break = 0;
    q->status = QS_UNTAKEN; other->status = QS_UNTAKEN;
    hidden->status = QS_FINISHED;
    list = quest_journal_list(1); CHECK(!_listed(list, hidden)); vec_free(list);
    hidden->status = QS_FAILED;
    list = quest_journal_list(2); CHECK(!_listed(list, hidden)); vec_free(list);
    hidden->status = QS_UNTAKEN;
    list = quest_journal_list(0); CHECK(!_listed(list, q)); vec_free(list);
    doc = doc_alloc(72); quest_journal_doc(q, doc, QUEST_CARD_PROGRESS, NULL, 32);
    CHECK(_contains(doc, "No known quest")); CHECK(!_contains(doc, "thieves")); doc_free(doc);
    if (browse)
    {
        _read_only(q, QUEST_CARD_OFFER);
        _keys = "\033"; CHECK(!quest_card(q, QUEST_CARD_OFFER, NULL));
        _keys = "a"; CHECK(quest_card(q, QUEST_CARD_OFFER, NULL));
        CHECK(q->status == QS_UNTAKEN);
    }
    quest_take(q);
    CHECK(q->status == QS_TAKEN);
    if (browse) _read_only(q, QUEST_CARD_PROGRESS);
    s = quest_get_description_at(q, QS_COMPLETED);
    CHECK(strstr(string_buffer(s), "Outpost pays its debts") != NULL);
    CHECK(q->status == QS_TAKEN);
    string_free(s);
    s = quest_get_description(q);
    CHECK(strstr(string_buffer(s), "northeastern quarter") != NULL);
    CHECK(strstr(string_buffer(s), "Outpost pays its debts") == NULL);
    string_free(s);
    other->status = QS_COMPLETED;
    list = quest_journal_list(0);
    CHECK(vec_get(list, 0) == other && _listed(list, q)); vec_free(list);
    other->status = QS_UNTAKEN;

    for (mode = 0; mode < 3; ++mode)
    {
        no_wilderness = mode > 0; coffee_break = mode == 2 ? 2 : 0;
        quest_contact(q, &contact);
        CHECK(streq(contact.name, mode ? "Uldrik" : "Uldrik II"));
        CHECK(streq(contact.portrait, "Uldrik II"));
        quest_contact(quests_get(20), &contact);
        CHECK(streq(contact.name, mode ? "Uldrik" : "Goodman Odwin"));
        CHECK(streq(contact.portrait, mode ? "Uldrik II" : "Goodman Odwin"));
        quest_contact(quests_get(14), &contact);
        CHECK(streq(contact.name, mode == 2 ? "Odwin" : mode ? "Uldrik" : "Uldrik II"));
        CHECK(streq(contact.portrait, mode == 2 ? "Goodman Odwin" : "Uldrik II"));
    }
    no_wilderness = FALSE; coffee_break = 0;
    q->status = QS_IN_PROGRESS;
    quest_complete(q, point(px, py));
    CHECK(q->status == QS_COMPLETED);
    if (browse)
    {
        _read_only(q, QUEST_CARD_PROGRESS);
        doc = doc_alloc(90); quest_journal_doc(q, doc, QUEST_CARD_PROGRESS, NULL, 32);
        CHECK(_contains(doc, "Ready to return")); CHECK(_contains(doc, "Return to Count Uldrik II"));
        CHECK(!_contains(doc, "Outpost pays its debts"));
        CHECK(!_contains(doc, "Long Sword")); doc_free(doc);
    }
    _keys = "\033"; /* Dismiss the receipt, not the reward operation. */
    quest_reward(q);
    CHECK(q->status == QS_FINISHED);
    CHECK(q->receipt && q->receipt->turn == game_turn);
    CHECK(q->receipt && strstr(q->receipt->response, "Outpost pays its debts"));
    CHECK(q->receipt && q->receipt->reward[0]);
    list = quest_journal_list(0); CHECK(!_listed(list, q)); vec_free(list);
    list = quest_journal_list(1); CHECK(_listed(list, q)); vec_free(list);
    if (browse) _read_only(q, QUEST_CARD_PROGRESS);
    if (browse)
    {
        /* An earned receipt remains fixed even if the briefing file changes.
         * It must also stay concealed if a quest is made hidden/unrewarded. */
        cptr old_file = q->file;
        int old_flags = q->flags;
        q->file = NULL;
        doc = doc_alloc(100); quest_journal_doc(q, doc, QUEST_CARD_PROGRESS, NULL, 32);
        CHECK(_contains(doc, "Outpost pays its debts")); doc_free(doc);
        q->status = QS_COMPLETED;
        doc = doc_alloc(100); quest_journal_doc(q, doc, QUEST_CARD_PROGRESS, NULL, 32);
        CHECK(!_contains(doc, "Outpost pays its debts")); doc_free(doc);
        q->status = QS_FINISHED; q->flags |= QF_INVIS;
        doc = doc_alloc(100); quest_journal_doc(q, doc, QUEST_CARD_PROGRESS, NULL, 32);
        CHECK(_contains(doc, "No known quest")); CHECK(!_contains(doc, "Reward received")); doc_free(doc);
        q->flags = old_flags; q->file = old_file;
    }
    quest_take(other); other->status = QS_IN_PROGRESS; quest_fail(other);
    CHECK(other->status == QS_FAILED);
    list = quest_journal_list(2); CHECK(_listed(list, other)); vec_free(list);
    if (browse) _read_only(other, QUEST_CARD_FAILED);
    if (browse) _journal_memory();

    /* Exercise every authored briefing, including substituted identities and
     * class/race-dependent prose. Reading must not reveal hidden map contents. */
    if (browse)
    {
        _telmora(all);
        _morivant(all);
        _guilds();
        _angwil(all);
        _remaining(all);
        _outcomes();
        for (i = 0; i < vec_length(all); ++i)
        {
            quest_ptr entry = vec_get(all, i);
            quest_t saved = *entry;
            if (!(entry->flags & QF_TOWN)) continue;
            entry->status = QS_TAKEN;
            _read_only(entry, QUEST_CARD_PROGRESS);
            *entry = saved;
        }
        /* Hidden entries stay hidden even when passed directly to the renderer. */
        hidden->status = QS_COMPLETED;
        doc = doc_alloc(80); quest_journal_doc(hidden, doc, QUEST_CARD_PROGRESS, NULL, 32);
        CHECK(_contains(doc, "No known quest")); CHECK(!_contains(doc, "Metatron")); doc_free(doc);
        hidden->status = QS_UNTAKEN;
        /* Summaries follow the actual variant and use only authored directions.
         * In particular no sewer wording on Compost Heap, nor birds on bears. */
        for (mode = 0; mode < 3; ++mode)
        {
            no_wilderness = mode > 0; coffee_break = mode == 2 ? 2 : 0;
            for (i = 0; i < vec_length(all); ++i)
            {
                quest_ptr entry = vec_get(all, i);
                int old_status = entry->status;
                if (entry->id != 1 && entry->id != 2 && entry->id != 84 && entry->id != 87 && entry->id != 31) continue;
                entry->status = QS_TAKEN;
                doc = doc_alloc(100); quest_journal_doc(entry, doc, QUEST_CARD_PROGRESS, NULL, 32);
                quest_contact(entry, &contact);
                if (streq(contact.town, "Outpost"))
                {
                    CHECK(_contains(doc, "Directions"));
                    if (entry->id == 1) CHECK(_contains(doc, "northeastern quarter"));
                    if (entry->id == 87) { CHECK(_contains(doc, "compost heap")); CHECK(!_contains(doc, "town sewer")); }
                    if (entry->id == 84) { CHECK(_contains(doc, "forest's southern edge")); CHECK(!_contains(doc, "birds' nest")); }
                    if (entry->id == 31) CHECK(_contains(doc, "Kill the Old Willow"));
                }
                doc_free(doc); entry->status = old_status;
            }
        }
        no_wilderness = FALSE; coffee_break = 0;
        _keys = "CFA\r\033x";
        quests_display();
        CHECK(ui_screen_map_owns());
    }
    vec_free(all);
    no_wilderness = old_wilderness; coffee_break = old_coffee;
    if (browse == 8)
    {
        no_wilderness = FALSE; coffee_break = 0;
        _failure_fixture();
        no_wilderness = old_wilderness; coffee_break = old_coffee;
    }
    else if (browse == 2 || browse == 7 || browse == 9 || (browse >= 10 && browse <= 13))
    {
        /* Disposable save for SDL integration checks, never a shipped scenario. */
        memset(&quest_journal_state, 0, sizeof(quest_journal_state));
        q->status = QS_COMPLETED;
        quests_get(14)->status = QS_TAKEN; quests_get(14)->goal_current = 3;
        quests_get(20)->status = QS_TAKEN;
        quests_get(27)->status = QS_TAKEN;
        quests_get(29)->status = QS_TAKEN;
        quests_get(31)->status = QS_TAKEN;
        quests_get(33)->status = QS_FINISHED;
        quests_get(33)->completed_turn = game_turn;
        quests_get(33)->completed_lev = p_ptr->lev;
        player_place(30, 84);
        cave_set_feat(30, 85, f_tag_to_index("BUILDING_0")); cave[30][85].special = 62;
        cave_set_feat(30, 86, f_tag_to_index("BUILDING_1")); cave[30][86].special = 1;
        cave_set_feat(30, 83, f_tag_to_index("BUILDING_2")); cave[30][83].special = 50;
        if (browse == 7 || browse == 9 || (browse >= 10 && browse <= 13))
        {
            vec_ptr entries = quests_get_all();
            for (i = 0; i < vec_length(entries); ++i)
                ((quest_ptr)vec_get(entries, i))->status = QS_UNTAKEN;
            vec_free(entries);
            if (browse == 7)
            {
                quests_get(4)->status = QS_TAKEN;
                quests_get(5)->status = QS_COMPLETED;
                quests_get(16)->status = QS_FAILED;
                cave[30][85].special = 3;
                cave[30][86].special = 5;
                cave[30][83].special = 16;
            }
            else if (browse == 9)
            {
                quests_get(52)->status = QS_TAKEN;
                quests_get(53)->status = QS_TAKEN;
                quests_get(55)->status = QS_COMPLETED;
                quests_get(54)->status = QS_FAILED;
                cave[30][85].special = 51;
                cave[30][86].special = 55;
                cave[30][83].special = 54;
            }
            else if (browse == 10)
            {
                quests_get(29)->status = QS_TAKEN;
                quests_get(81)->status = QS_TAKEN; quests_get(81)->goal_current = 3;
                quests_get(30)->status = QS_COMPLETED;
                quests_get(32)->status = QS_FAILED;
                cave[30][85].special = 56;
                cave[30][86].special = 30;
                cave[30][83].special = 32;
            }
            else
            {
                /* Colmanwen and the actual Odwin/Uldrik/Kimuk assignments.
                 * Force known variants only in this disposable fixture. */
                quest_ptr parent21 = NULL, parent24 = NULL, parent36 = NULL;
                entries = quests_get_all();
                for (i = 0; i < vec_length(entries); ++i)
                {
                    quest_ptr entry = vec_get(entries, i);
                    if (entry->id == 21) parent21 = entry;
                    if (entry->id == 24) parent24 = entry;
                    if (entry->id == 36) parent36 = entry;
                }
                CHECK(parent21 && parent24 && parent36);
                parent21->substitute = 0; parent24->substitute = -86; parent36->substitute = -91;
                quests_get(35)->status = QS_COMPLETED;
                quests_get(21)->status = QS_FAILED;
                if (browse < 13) quests_get(36)->status = QS_TAKEN;
                if (browse == 11) quests_get(24)->status = QS_TAKEN;
                if (browse == 12) quests_get(23)->status = QS_TAKEN;
                cave[30][85].special = 19;
                cave[30][86].special = 35;
                cave[30][83].special = 21;
                no_wilderness = browse > 11; coffee_break = browse == 13 ? 2 : 0;
                vec_free(entries);
            }
        }
        do_cmd_save_game(FALSE);
    }
    else if (browse == 3)
    {
        quest_ptr empty = quests_get(14);
        quest_ptr secret = quest_alloc("Concealed receipt fixture");
        cptr old_file = empty->file;
        empty->file = NULL; empty->status = QS_COMPLETED;
        _keys = "\033"; quest_reward(empty);
        CHECK(empty->receipt && streq(empty->receipt->reward, "No item was awarded."));
        CHECK(empty->receipt && !empty->receipt->response[0]);
        empty->file = old_file;
        secret->flags = QF_TOWN | QF_INVIS; secret->status = QS_COMPLETED;
        quest_reward(secret); CHECK(!secret->receipt); quest_free(secret);
        quests_get(33)->status = QS_FINISHED; /* no historic receipt */
        do_cmd_save_game(FALSE);
    }
    if (browse == 14) _drop_observer_checks();
    Term->xtra_hook = _old_hook;
    auto_more_state = old_more;
    printf("quest-check checks %d failures %d\n", _checks, _failures);
    printf("quest-lifecycle %d %d %u %u\n", q->status, other->status, q->seed, other->seed);
}
