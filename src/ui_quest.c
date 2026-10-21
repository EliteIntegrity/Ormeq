/* Quest journal and conversations. Copyright (c) 2026 John Horton.
 * Angband licence: see LICENSE. Presentation never generates quest rewards,
 * scans a quest map, or counts creatures the player has not discovered. */
#include "scottish.h"

quest_journal_state_t quest_journal_state;
static quest_t _patron_history[45];
static char _patron_titles[45][64];

bool quest_journal_history_id(int id)
{
    return id >= -45 && id <= -1;
}

static quest_contact_t *_contact;
static int _contact_id;
static bool _contact_match;

static errr _parse_contact(char *line, int options)
{
    (void)options;
    if (prefix(line, "Q:"))
    {
        char *ids[32];
        int i, n = z_string_split(line + 2, ids, 32, ",");
        _contact_match = FALSE;
        for (i = 0; i < n; ++i)
            if (atoi(ids[i]) == _contact_id) _contact_match = TRUE;
    }
    else if (_contact_match && prefix(line, "C:"))
    {
        char *parts[3];
        if (z_string_split(line + 2, parts, 3, "|") == 3)
        {
            my_strcpy(_contact->name, parts[0], sizeof(_contact->name));
            my_strcpy(_contact->portrait, parts[0], sizeof(_contact->portrait));
            my_strcpy(_contact->building, parts[1], sizeof(_contact->building));
            my_strcpy(_contact->town, parts[2], sizeof(_contact->town));
        }
    }
    else if (_contact_match && prefix(line, "P:"))
        my_strcpy(_contact->portrait, line + 2, sizeof(_contact->portrait));
    return 0;
}

void quest_contact(quest_ptr q, quest_contact_t *contact)
{
    memset(contact, 0, sizeof(*contact));
    if (!(q->flags & QF_TOWN) || (q->flags & QF_INVIS)) return;
    _contact = contact;
    _contact_id = q->id;
    _contact_match = FALSE;
    parse_edit_file("quest_contacts.txt", _parse_contact, INIT_SILENT);
    _contact = NULL;
}

void quest_journal_title(quest_ptr q, char *buf, int size)
{
    char *suffix;
    int number;
    if ((q->flags & QF_RANDOM) && sscanf(q->name, "Random %d", &number) == 1)
    {
        strnfmt(buf, size, "Dungeon hunt %d", number);
        return;
    }
    my_strcpy(buf, q->name, size);
    if (no_wilderness && (suffix = strstr(buf, " (")) != NULL) *suffix = 0;
}

cptr quest_journal_status(quest_ptr q)
{
    switch (q->status)
    {
    case QS_UNTAKEN: return "Quest offered";
    case QS_TAKEN: return "Accepted";
    case QS_IN_PROGRESS: return "In progress";
    case QS_COMPLETED: return (q->flags & QF_TOWN) ? "Ready to return" : "Objective complete";
    case QS_FINISHED: return q->completed_turn ? "Finished" : "Closed";
    default: return "Failed";
    }
}

static int _compare(quest_ptr a, quest_ptr b)
{
    bool ready_a = a->status == QS_COMPLETED, ready_b = b->status == QS_COMPLETED;
    if (ready_a != ready_b) return ready_a ? -1 : 1;
    if (a->status >= QS_FINISHED && b->status >= QS_FINISHED && a->completed_turn != b->completed_turn)
        return a->completed_turn > b->completed_turn ? -1 : 1;
    if (a->danger_level != b->danger_level) return a->danger_level < b->danger_level ? -1 : 1;
    return a->id < b->id ? -1 : a->id != b->id;
}

vec_ptr quest_journal_list(int tab)
{
    vec_ptr source = tab == 0 ? quests_get_active() : tab == 1 ? quests_get_finished() : quests_get_failed();
    vec_ptr result = vec_alloc(NULL);
    int i;
    for (i = 0; i < vec_length(source); ++i)
    {
        quest_ptr q = vec_get(source, i);
        if (!(q->flags & QF_INVIS)) vec_add(result, q);
    }
    vec_free(source);
    if (tab)
    {
        for (i = 0; i < 45; ++i)
        {
            quest_ptr q = &_patron_history[i];
            bool known = i < 30 ? karrot_quest_history(i, q) : troika_quest_history(i - 30, q);
            if (!known) continue;
            strnfmt(_patron_titles[i], sizeof(_patron_titles[i]), "%s: assignment %d",
                i < 30 ? "Karrot" : "Troika", i < 30 ? i + 1 : i - 29);
            q->name = _patron_titles[i];
            if ((tab == 1) != (q->status == QS_FINISHED)) continue;
            vec_add(result, q);
        }
    }
    vec_sort(result, (vec_cmp_f)_compare);
    return result;
}

static void _return_to(quest_ptr q, quest_contact_t *contact, char *buf, int size)
{
    if (q->id == 26)
        my_strcpy(buf, "Legacy commission: its former quest service is no longer offered.", size);
    else if (streq(contact->building, "Count"))
        strnfmt(buf, size, "Return to Count %s in %s.", contact->name, contact->town);
    else if (contact->name[0])
        strnfmt(buf, size, "Return to %s at %s, %s.", contact->name, contact->building, contact->town);
    else if (q->flags & QF_TOWN)
        my_strcpy(buf, "Return to the building that offered this quest.", size);
    else
        my_strcpy(buf, "This quest is resolved here; no town turn-in is needed.", size);
}

void quest_notify_complete(quest_ptr q)
{
    quest_contact_t contact;
    char title[160], next[256];
    quest_contact(q, &contact);
    quest_journal_title(q, title, sizeof(title));
    _return_to(q, &contact, next, sizeof(next));
    cmsg_format(TERM_L_BLUE, "Objective complete: %s. %s [Ctrl+Q: journal]", title, next);
}

typedef struct {
    char objective[256], directions[256];
} _guidance_t;
static _guidance_t *_guidance;
static cptr _guidance_file;
static bool _guidance_match;

static errr _parse_guidance(char *line, int options)
{
    (void)options;
    if (prefix(line, "F:")) _guidance_match = streq(line + 2, _guidance_file);
    else if (_guidance_match && prefix(line, "O:"))
        my_strcpy(_guidance->objective, line + 2, sizeof(_guidance->objective));
    else if (_guidance_match && prefix(line, "D:"))
        my_strcpy(_guidance->directions, line + 2, sizeof(_guidance->directions));
    return 0;
}

static void _quest_guidance(quest_ptr q, quest_contact_t *contact, _guidance_t *guidance)
{
    memset(guidance, 0, sizeof(*guidance));
    if (!q->file) return;
    (void)contact;
    _guidance = guidance;
    _guidance_file = q->file;
    _guidance_match = FALSE;
    parse_edit_file("quest_briefings.txt", _parse_guidance, INIT_SILENT);
    _guidance = NULL;
}

static void _objective(quest_ptr q, doc_ptr doc, _guidance_t *guidance)
{
    /* Only explicit mission targets, never the hidden contents of a map.
     * Single-target town briefings do not always disclose the target's name. */
    if (guidance->objective[0])
    {
        doc_printf(doc, "%s\n", guidance->objective);
        if (q->goal == QG_KILL_MON)
        {
            if (q->status == QS_UNTAKEN)
                doc_printf(doc, "Required: %d defeated\n", q->goal_count);
            else doc_printf(doc, "<color:G>Progress: %d/%d defeated</color>\n", q->goal_current, q->goal_count);
        }
    }
    else if (q->goal == QG_KILL_MON && (q->goal_count > 1 || (q->flags & (QF_RANDOM | QF_PURPLE))))
    {
        char name[MAX_NLEN];
        my_strcpy(name, r_name + r_info[q->goal_idx].name, sizeof(name));
        if (q->goal_count > 1) plural_aux(name);
        doc_printf(doc, "Defeat %d %s.\n", q->goal_count, name);
        if (q->status != QS_UNTAKEN)
            doc_printf(doc, "<color:G>Progress: %d/%d defeated</color>\n", q->goal_current, q->goal_count);
    }
    else if (q->goal == QG_KILL_MON)
        doc_insert(doc, "Defeat the target described in the briefing.\n");
    else if (q->goal == QG_FIND_ART)
        doc_insert(doc, "Recover the item described in the briefing.\n");
    else
        doc_insert(doc, "Defeat all quest enemies.\n");
}

static void _failure_summary(quest_ptr q, doc_ptr doc)
{
    quest_failure_ptr f = q->failure;
    const quest_patron_record_t *patron = quest_patron_record(q->id);
    cptr reason = patron ? patron->reason : f ? f->reason : "";
    doc_insert(doc, "<color:r>Failure record</color>\n");
    doc_printf(doc, "%s\n", reason[0] ? reason : "Failure reason not recorded.");
    if (q->completed_turn)
    {
        int day, hour, minute;
        extract_day_hour_min_imp(q->completed_turn, &day, &hour, &minute);
        doc_printf(doc, "<color:s>Failed on day %d, %d:%02d, at character level %d.</color>\n",
            day, hour, minute, q->completed_lev);
    }
}

void quest_journal_doc(quest_ptr q, doc_ptr doc, int card, cptr reward, int visible_rows)
{
    quest_contact_t contact;
    _guidance_t guidance;
    string_ptr briefing;
    char title[160], next[256];
    bool finished, failed, early_objective;
    bool compact = doc_width(doc) < 60 || visible_rows < 24 ||
        (ui_screen_pixel_width() && ui_screen_pixel_width() < 960);
    int portrait_w = 12;
    int portrait_h = 4;
    doc_ptr cols[2];
    int i;
    if ((q->flags & QF_INVIS) || (q->status == QS_UNTAKEN && card != QUEST_CARD_OFFER))
    {
        doc_insert(doc, "No known quest.\n");
        return;
    }
    finished = q->status == QS_FINISHED;
    failed = q->status == QS_FAILED || q->status == QS_FAILED_DONE;
    early_objective = !finished && !failed && card != QUEST_CARD_REWARD && card != QUEST_CARD_FAILED;
    quest_contact(q, &contact);
    _quest_guidance(q, &contact, &guidance);
    quest_journal_title(q, title, sizeof(title));
    doc_printf(doc, "<color:U>%s</color>\n", title);
    doc_printf(doc, "<color:%c>%s</color>  |  Danger level %d\n\n",
        failed ? 'r' : q->status == QS_COMPLETED || finished ? 'G' : 'y',
        card == QUEST_CARD_REWARD ? "Quest finished" : quest_journal_status(q), q->danger_level);

    if (card == QUEST_CARD_REWARD || finished)
        my_strcpy(next, "No further action required.", sizeof(next));
    else if (failed)
        my_strcpy(next, q->id == 26 ? "Legacy commission: its former quest service is no longer offered." :
            card != QUEST_CARD_FAILED && q->status == QS_FAILED && (q->flags & QF_TOWN) ? "Report back to the questgiver." : "This quest has ended.", sizeof(next));
    else if (q->status == QS_COMPLETED)
        _return_to(q, &contact, next, sizeof(next));
    else if (card == QUEST_CARD_OFFER)
        my_strcpy(next, "Accept when you are ready to take this on.", sizeof(next));
    else
        my_strcpy(next, q->status == QS_IN_PROGRESS ? "Finish the objective below." : "Travel to the quest site described below.", sizeof(next));

    /* Every screen puts actionable text first. Artwork never determines the
     * layout, so a missing plate cannot move the objective or controls. */
    doc_printf(doc, "<color:y>Next:</color> %s\n\n", next);
    if (failed)
    {
        _failure_summary(q, doc);
        doc_newline(doc);
    }
    if (early_objective)
    {
        doc_insert(doc, "<color:U>Objective</color>\n");
        _objective(q, doc, &guidance);
        if (q->dungeon)
            doc_printf(doc, "Location: %s, level %d\n", d_name + d_info[quest_dungeon(q)].name, q->level);
        if (guidance.directions[0])
            doc_printf(doc, "\n<color:U>Directions</color>\n%s\n", guidance.directions);
        doc_newline(doc);
    }

    if (!finished && !failed && q->status != QS_COMPLETED)
    {
        bool retake = (q->flags & QF_RETAKE) && (!coffee_break || q->level >= 99);
        doc_printf(doc, "<color:%c>%s</color>\n\n", retake ? 's' : 'y', retake ?
            "You may leave and return to this quest." :
            "Leaving the quest site before completing it fails the quest.");
    }

    if (contact.name[0])
    {
        cols[0] = doc_alloc(portrait_w);
        cols[1] = doc_alloc(MAX(16, doc_width(doc) - portrait_w - 3));
        /* Keep a small identity band even without art. Do not substitute a
         * random shopkeeper's face for a named questgiver. */
        for (i = 0; i < portrait_h; ++i)
        {
#ifndef USE_SDL3
            if (i == portrait_h / 2)
                doc_printf(cols[0], "<tab:%d><color:B>p</color>", portrait_w / 2);
#endif
            if (i + 1 < portrait_h) doc_newline(cols[0]);
        }
        if (streq(contact.building, "Count"))
            doc_printf(cols[1], "<color:U>Count %s</color>\n%s\n", contact.name, contact.town);
        else if (compact)
            doc_printf(cols[1], "<color:U>%s</color>\n%s, %s\n", contact.name, contact.building, contact.town);
        else
            doc_printf(cols[1], "<color:U>%s</color>\n%s\n%s\n", contact.name, contact.building, contact.town);
        doc_add_portrait(doc, UI_ART_SHOPKEEPER, contact.portrait, portrait_w, portrait_h, TERM_L_BLUE, 'p');
        doc_insert_cols(doc, cols, 2, 3);
        doc_free(cols[0]); doc_free(cols[1]);
    }

    if (finished && (q->flags & QF_TOWN))
    {
        if (q->receipt)
        {
            int day, hour, minute;
            extract_day_hour_min_imp(q->receipt->turn, &day, &hour, &minute);
            doc_insert(doc, "<color:G>Reward received</color>\n");
            doc_printf(doc, "%s\n<color:s>Collected on day %d, %d:%02d.</color>\n",
                q->receipt->reward, day, hour, minute);
            doc_insert(doc, "\n<color:U>Questgiver's response</color>\n");
            doc_printf(doc, "%s\n\n", q->receipt->response[0] ? q->receipt->response : "No response was recorded.");
        }
        else doc_insert(doc, "<color:s>Reward not recorded.</color>\nNo turn-in receipt was saved for this quest.\n\n");
    }
    else if (card == QUEST_CARD_REWARD)
        doc_printf(doc, "<color:G>Reward received</color>\n%s\n\n", reward && reward[0] ? reward : "No item was awarded.");
    else if (finished && !(q->flags & QF_PURPLE))
    {
        doc_insert(doc, "<color:G>Dungeon reward record</color>\n");
        if (q->receipt)
        {
            doc_printf(doc, "%s\n", q->receipt->reward);
            if (q->receipt->response[0]) doc_printf(doc, "%s\n", q->receipt->response);
        }
        else doc_insert(doc, "Reward not recorded: this quest predates dungeon reward records.\n");
        doc_insert(doc, "Observed at completion; left on the floor. Collection and later identification are not tracked.\n\n");
    }
    if ((q->flags & QF_PURPLE) && (finished || failed))
    {
        const quest_patron_record_t *record = quest_patron_record(q->id);
        if (record)
        {
            if (finished && q->id >= -30)
            {
                doc_insert(doc, "<color:G>Observed reward drops</color>\n");
                doc_printf(doc, "%s\n", record->drops[0] ? record->drops : "No floor rewards were visible at completion.");
                doc_insert(doc, "Left on the floor; collection and later identification are not tracked.\n\n");
            }
            if (finished && q->id <= -31)
                doc_printf(doc, "<color:G>Gift from this assignment</color>\n%s\n\n",
                    record->benefits[0] ? record->benefits : "No new patron gift on this assignment.");
            doc_insert(doc, "<color:U>Patron response and witnessed effects</color>\n");
            doc_printf(doc, "%s\n", record->response[0] ? record->response : "No response was heard.");
        }
        else doc_insert(doc, "<color:s>Outcome details not recorded for this earlier assignment.</color>\n\n");
    }
    if (finished)
    {
        if (q->id == 5)
            doc_insert(doc, no_wilderness ? "<color:G>Outcome:</color> Reduced prices for item and monster research, weapon comparison and armour evaluation in Outpost.\n\n" :
                "<color:G>Outcome:</color> Reduced prices for item research, monster research and armour evaluation in Telmora.\n\n");
        if (q->id == 74) doc_insert(doc, "<color:G>Outcome:</color> The former adventurers' house in Anambar is now available as your home.\n\n");
        if (q->id == 77) doc_insert(doc, "<color:G>Outcome:</color> Zul's teleport service is restored.\n\n");
        if (q->id == QUEST_OBERON) doc_insert(doc, "<color:G>Outcome:</color> The way to the final level is open.\n\n");
        if (q->id == QUEST_SERPENT) doc_insert(doc, "<color:G>Outcome:</color> The last errand is complete.\n\n");
    }
    if (failed)
    {
        quest_failure_ptr f = q->failure;
        if ((q->flags & QF_TOWN) && f && f->reported &&
            (q->status == QS_FAILED_DONE || card == QUEST_CARD_FAILED))
        {
            int day, hour, minute;
            extract_day_hour_min_imp(f->reported_turn, &day, &hour, &minute);
            doc_insert(doc, card == QUEST_CARD_FAILED ? "\n<color:U>Response</color>\n" :
                "\n<color:U>Questgiver's response</color>\n");
            doc_printf(doc, "%s\n<color:s>Reported on day %d, %d:%02d.</color>\n",
                f->response[0] ? f->response : "No closing response was given.", day, hour, minute);
            if (card == QUEST_CARD_FAILED) return;
        }
        else if ((q->flags & QF_TOWN) && card != QUEST_CARD_FAILED)
            doc_insert(doc, q->status == QS_FAILED ?
                "<color:s>Not yet reported to the questgiver.</color>\n" :
                "<color:s>Closing response not recorded.</color>\n");
        doc_newline(doc);
    }
    if (card != QUEST_CARD_REWARD && card != QUEST_CARD_FAILED)
    {
        if (!early_objective)
        {
            doc_insert(doc, "<color:U>Objective</color>\n");
            _objective(q, doc, &guidance);
        }
        if (!early_objective && q->dungeon)
            doc_printf(doc, "Location: %s, level %d\n", d_name + d_info[quest_dungeon(q)].name, q->level);
        else if (!early_objective && contact.town[0] && !guidance.directions[0])
            doc_printf(doc, "Location: %s - see the briefing for directions.\n", contact.town);
        if (!early_objective && guidance.directions[0])
            doc_printf(doc, "\n<color:U>Directions</color>\n%s\n", guidance.directions);
    }

    if (card == QUEST_CARD_REWARD && finished && (q->flags & QF_TOWN))
        return; /* The saved receipt above is the entire turn-in conversation. */
    if (card == QUEST_CARD_REWARD || card == QUEST_CARD_FAILED)
    {
        briefing = quest_get_description_at(q, card == QUEST_CARD_REWARD ? QS_COMPLETED : QS_FAILED);
        doc_insert(doc, "\n<color:U>Response</color>\n");
    }
    else
    {
        /* Finished entries keep the original briefing, not a future reward
         * response. The parser overrides expression evaluation, not q->status. */
        briefing = (q->flags & (QF_RANDOM | QF_PURPLE)) ? string_alloc() : quest_get_description_at(q, QS_TAKEN);
        doc_insert(doc, "\n<color:U>Briefing</color>\n");
    }
    if (q->flags & QF_PURPLE)
    {
        if (finished || failed)
            doc_insert(doc, "Patron assignment ended. No town turn-in is needed.\n");
        else doc_insert(doc, "A patron assignment: defeat the named targets on its assigned floor. No town turn-in is needed.\n");
        troika_quest_benefits(doc);
    }
    else if (q->flags & QF_RANDOM)
        doc_insert(doc, finished ? "Dungeon hunt completed. Rewards were dropped near the final target. No town turn-in is needed.\n" :
            failed ? "The dungeon hunt has ended in failure.\n" :
            "Only target kills on this floor count. Rewards appear near the final target; collect them before departing. No town turn-in is needed.\n");
    else doc_printf(doc, "%s\n", string_length(briefing) ? string_buffer(briefing) : "No further briefing was recorded.");
    string_free(briefing);
    if (finished)
    {
        if (q->completed_turn)
        {
            int day, hour, minute;
            extract_day_hour_min_imp(q->completed_turn, &day, &hour, &minute);
            doc_printf(doc, "\n<color:s>Recorded on day %d, %d:%02d, at character level %d.</color>\n",
                day, hour, minute, q->completed_lev);
        }
    }
}

static void _button(int x, int y, cptr text, int key)
{
    c_put_str(TERM_YELLOW, text, y, x);
    ui_event_menu_item(x, y, (int)strlen(text), key);
}

static void _clear(void)
{
    ui_event_map_hidden();
    ui_event_menu_clear();
    ui_event_portrait_none();
    ui_event_scroll(FALSE, FALSE);
    Term_clear_rect(ui_screen_rect());
    ui_event_panel_screen();
}

static int _scroll(int cmd, int top, int rows, int limit)
{
    if (cmd == SKEY_UP || cmd == '8' || cmd == '[') --top;
    if (cmd == SKEY_DOWN || cmd == '2' || cmd == ']') ++top;
    if (cmd == SKEY_PGUP || cmd == '9') top -= MAX(1, rows - 1);
    if (cmd == SKEY_PGDOWN || cmd == '3' || cmd == ' ') top += MAX(1, rows - 1);
    if (cmd == SKEY_TOP || cmd == '7') top = 0;
    if (cmd == SKEY_BOTTOM || cmd == '1') top = limit;
    return MAX(0, MIN(top, limit));
}

static int _scroll_memory(int id, int width)
{
    int i, empty = -1;
    if (!id) return -1;
    for (i = 0; i < 160; ++i)
    {
        if (!quest_journal_state.scroll[i].id && empty < 0) empty = i;
        if (quest_journal_state.scroll[i].id == id) break;
    }
    if (i == 160) i = empty;
    if (i < 0) return -1;
    quest_journal_state.scroll[i].id = id;
    if (quest_journal_state.scroll[i].width != width)
        quest_journal_state.scroll[i].top = 0; /* Text has rewrapped. */
    quest_journal_state.scroll[i].width = width;
    return i;
}

bool quest_card(quest_ptr q, int card, cptr reward)
{
    int top = 0;
    bool accept = FALSE;
    if (q->flags & QF_INVIS) return FALSE;
    /* The conversation is the acknowledgement. Its message history already
     * exists; do not put an old -more- prompt in front of the receipt. */
    msg_line_clear();
    screen_save_aux();
    sound(SOUND_UI_OPEN);
    for (;;)
    {
        rect_t page = ui_page_rect();
        doc_ptr doc;
        int cmd, rows = MAX(1, page.cy - 4), limit;
        if (page.cx > 86) { page.x += (page.cx - 86) / 2; page.cx = 86; }
        doc = doc_alloc(page.cx);
        quest_journal_doc(q, doc, card, reward, rows);
        limit = MAX(0, doc_line_count(doc) - rows);
        top = MAX(0, MIN(top, limit));
        _clear();
        doc_sync_term(doc, doc_range_lines_from(doc, top, rows), doc_pos_create(page.x, page.y));
        ui_event_rule(page.x, page.y + page.cy - 3, page.cx);
        ui_event_scroll(top > 0, top < limit);
        if (card == QUEST_CARD_OFFER)
        {
            _button(page.x, page.y + page.cy - 2, "[A Accept]", 'a');
            _button(page.x + 13, page.y + page.cy - 2, "[Esc Later]", ESCAPE);
        }
        else _button(page.x, page.y + page.cy - 2, "[Enter Continue]", '\r');
        if (limit)
        {
            _button(page.x + 25, page.y + page.cy - 2, "[Up]", '[');
            _button(page.x + 30, page.y + page.cy - 2, "[Down]", ']');
        }
        c_put_str(TERM_SLATE, "Up/Down: scroll  |  PgUp/PgDn: page", page.y + page.cy - 1, page.x);
        Term_fresh();
        cmd = inkey_special(TRUE);
        doc_free(doc);
        if (cmd == ESCAPE) break;
        if ((cmd == 'a' || cmd == 'A') && card == QUEST_CARD_OFFER) { accept = TRUE; break; }
        if (card != QUEST_CARD_OFFER && (cmd == '\r' || cmd == '\n')) break;
        top = _scroll(cmd, top, rows, limit);
    }
    sound(accept ? SOUND_UI_ACCEPT : SOUND_UI_BACK);
    screen_load();
    return accept;
}

void quests_display(void)
{
    int tab = quest_journal_state.tab, selected[3] = {0}, top = 0;
    int t, j;
    bool detail = FALSE;
    /* Resolve identities against today's visible lists. Status changes may
     * reorder a list or remove a quest; neither may select a hidden entry. */
    for (t = 0; t < 3; ++t)
    {
        vec_ptr list = quest_journal_list(t);
        for (j = 0; j < vec_length(list); ++j)
            if (((quest_ptr)vec_get(list, j))->id == quest_journal_state.selected[t])
            { selected[t] = j; break; }
        vec_free(list);
    }
    screen_save();
    sound(SOUND_UI_OPEN);
    for (;;)
    {
        rect_t page = ui_page_rect(), body;
        vec_ptr lists[3];
        quest_ptr q = NULL;
        int i, cmd, count, tabs, start, rows, limit = 0, old_selected, memory = -1;
        bool wide = page.cx >= 92 && page.cy >= 20 &&
            (!ui_screen_pixel_width() || ui_screen_pixel_width() >= 960);
        bool listing = wide || !detail;
        doc_ptr doc;
        char buf[180];
        for (i = 0; i < 3; ++i) lists[i] = quest_journal_list(i);
        tabs = vec_length(lists[2]) ? 3 : 2;
        if (tab >= tabs) tab = 0;
        count = vec_length(lists[tab]);
        selected[tab] = MAX(0, MIN(selected[tab], count - 1));
        old_selected = selected[tab];
        if (count) q = vec_get(lists[tab], selected[tab]);
        quest_journal_state.tab = tab;
        quest_journal_state.selected[tab] = q ? q->id : 0;
        body = rect(page.x + (wide ? 33 : 0), page.y + 4, page.cx - (wide ? 33 : 0), MAX(1, page.cy - 8));
        rows = MAX(1, MIN(24, body.cy / 3));
        start = MIN(MAX(0, selected[tab] - rows / 2), MAX(0, count - rows));
        doc = doc_alloc(body.cx);
        _clear();
        c_put_str(TERM_L_BLUE, "Quest journal", page.y, page.x);
        for (i = 0; i < tabs; ++i)
        {
            int tab_x = page.x + i * (page.cx < 60 ? page.cx / tabs : 20);
            strnfmt(buf, sizeof(buf), "%s%s (%d)%s", tab == i ? "[" : " ",
                i == 0 ? "Active" : i == 1 ? "Completed" : "Failed", vec_length(lists[i]), tab == i ? "]" : " ");
            if (page.cx < 60)
                strnfmt(buf, sizeof(buf), "%s%s%s", tab == i ? "[" : " ",
                    i == 0 ? "Active" : i == 1 ? "Completed" : "Failed", tab == i ? "]" : " ");
            c_put_str(tab == i ? TERM_YELLOW : TERM_SLATE, buf, page.y + 2, tab_x);
            ui_event_menu_item(tab_x, page.y + 2, (int)strlen(buf), "acf"[i]);
        }
        if (listing && count)
        {
            int width = wide ? 30 : page.cx;
            for (i = 0; i < rows && start + i < count; ++i)
            {
                quest_ptr entry = vec_get(lists[tab], start + i);
                char title[160];
                char *suffix;
                int y = body.y + i * 3;
                quest_journal_title(entry, title, sizeof(title));
                if ((suffix = strstr(title, " (")) != NULL) *suffix = 0;
                strnfmt(buf, sizeof(buf), "%c %s", selected[tab] == start + i ? '>' : ' ', title);
                if ((int)strlen(buf) > width) { buf[width] = 0; memcpy(buf + width - 3, "...", 3); }
                c_put_str(selected[tab] == start + i ? TERM_YELLOW : TERM_WHITE, buf, y, page.x);
                strnfmt(buf, sizeof(buf), "  %s | L%d", quest_journal_status(entry), entry->danger_level);
                buf[MIN((int)strlen(buf), width)] = 0;
                c_put_str(entry->status == QS_COMPLETED ? TERM_L_GREEN : TERM_SLATE, buf, y + 1, page.x);
                ui_event_menu_item(page.x, y, width, SPECIAL_KEY_QUEST_ROW + i);
                ui_event_menu_item(page.x, y + 1, width, SPECIAL_KEY_QUEST_ROW + i);
                if (selected[tab] == start + i) ui_event_menu_select(page.x, y, width);
            }
        }
        if (wide || detail || !count)
        {
            memory = _scroll_memory(q ? q->id : 0, body.cx);
            top = memory >= 0 ? quest_journal_state.scroll[memory].top : 0;
            if (q) quest_journal_doc(q, doc, QUEST_CARD_PROGRESS, NULL, body.cy);
            else doc_printf(doc, "<color:U>%s</color>\n\n%s\n",
                tab == 0 ? "No active quests" : tab == 1 ? "No completed quests" : "No failed quests",
                tab == 0 ? "Ask for work at a town's quest buildings. Only quests you have accepted appear here." :
                "Your quest history will appear here.");
            limit = MAX(0, doc_line_count(doc) - body.cy);
            top = MAX(0, MIN(top, limit));
            if (memory >= 0) quest_journal_state.scroll[memory].top = top;
            doc_sync_term(doc, doc_range_lines_from(doc, top, body.cy), doc_pos_create(body.x, body.y));
            ui_event_scroll(top > 0, top < limit);
        }
        else ui_event_scroll(start > 0, start + rows < count);
        i = page.y + page.cy - 3;
        if (count && listing)
        {
            strnfmt(buf, sizeof(buf), "%d/%d", selected[tab] + 1, count);
            c_put_str(TERM_SLATE, buf, i - 1, page.x);
        }
        ui_event_rule(page.x, i, page.cx);
        if (page.cx >= 64)
        {
            _button(page.x, i + 1, "[P Previous]", 'p');
            _button(page.x + 14, i + 1, "[N Next]", 'n');
            if (!wide) _button(page.x + 25, i + 1, detail ? "[Esc List]" : "[Enter Open]", detail ? ESCAPE : '\r');
            _button(page.x + 40, i + 1, "[? Help]", '?');
            _button(page.x + 51, i + 1, "[X Close]", 'x');
        }
        else
        {
            _button(page.x, i + 1, "[P]", 'p');
            _button(page.x + 5, i + 1, "[N]", 'n');
            _button(page.x + 10, i + 1, detail ? "[List]" : "[Open]", detail ? ESCAPE : '\r');
            _button(page.x + 18, i + 1, "[?]", '?');
            _button(page.x + 23, i + 1, "[X]", 'x');
        }
        if ((wide || detail) && limit)
        {
            _button(page.x, i + 2, "[Scroll up]", '[');
            _button(page.x + 14, i + 2, "[Scroll down]", ']');
            if (page.cx >= 58) c_put_str(TERM_SLATE, "PgUp/PgDn: page | Tab: lists", i + 2, page.x + 30);
        }
        else c_put_str(TERM_SLATE, "Up/Down: select  |  Tab: lists", i + 2, page.x);
        Term_fresh();
        cmd = inkey_special(TRUE);
        doc_free(doc);
        for (i = 0; i < 3; ++i) vec_free(lists[i]);
        if (cmd >= 'A' && cmd <= 'Z') cmd += 'a' - 'A';
        if (cmd == 'x' || cmd == 'X' || (cmd == ESCAPE && (wide || !detail))) break;
        if (cmd == ESCAPE || cmd == SKEY_LEFT) { detail = FALSE; }
        else if (cmd == '\t' || cmd == 'a' || cmd == 'c' || (cmd == 'f' && tabs == 3))
        {
            tab = cmd == '\t' ? (tab + 1) % tabs : cmd == 'a' ? 0 : cmd == 'c' ? 1 : 2;
            detail = FALSE; sound(SOUND_UI_TAB);
        }
        else if (cmd == '?' ) doc_display_help("quests.txt", NULL);
        else if (cmd == '\r' || cmd == '\n' || cmd == SKEY_RIGHT) { detail = TRUE; }
        else if (listing && cmd >= SPECIAL_KEY_QUEST_ROW && cmd < SPECIAL_KEY_QUEST_ROW + rows && start + cmd - SPECIAL_KEY_QUEST_ROW < count)
        { selected[tab] = start + cmd - SPECIAL_KEY_QUEST_ROW; detail = TRUE; sound(SOUND_UI_ACCEPT); }
        else if (cmd == 'p' || (listing && (cmd == SKEY_UP || cmd == '8'))) --selected[tab];
        else if (cmd == 'n' || (listing && (cmd == SKEY_DOWN || cmd == '2'))) ++selected[tab];
        else if (!wide && listing && (cmd == SKEY_PGDOWN || cmd == SKEY_PGUP || cmd == ' '))
            selected[tab] += cmd == SKEY_PGUP ? -rows : rows;
        else if (wide || detail)
        {
            top = _scroll(cmd, top, body.cy, limit);
            if (memory >= 0) quest_journal_state.scroll[memory].top = top;
        }
        selected[tab] = MAX(0, selected[tab]);
        if (selected[tab] != old_selected) { top = 0; sound(SOUND_UI_NAVIGATE); }
    }
    sound(SOUND_UI_CLOSE);
    screen_load();
}
