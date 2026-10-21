#ifndef INCLUDED_QUEST_H
#define INCLUDED_QUEST_H

/* Ormeq Quests
 * [1] Specified in q_info.txt
 * [2] Defined in lib/edit/<quest_file>.txt (q.file)
 * [3] Sequenced in town files
 * [4] Rewards now specified in q_*.txt file rather than town file */

enum {
    SEWER_QUEST = 2,
    QUEST_OBERON = 8,
    QUEST_SERPENT = 9,
    QUEST_GREAT_WOLF = 14,
    PURPLE_QUEST = 82,
    QUEST_METATRON = 97
};

#define QF_GENERATE 0x01 /* quest generates its own level (from q.file) */
#define QF_RETAKE   0x02 /* player can leave quest without failing it (e.g. Wender) (IN_PROGRESS -> TAKEN rather than FAILED) */
#define QF_TOWN     0x04 /* quest is assigned and rewarded from a town building */
#define QF_RANDOM   0x08 /* KILL(*): quest randomized 'on_birth' */
#define QF_ANYWHERE 0x10 /* reserved ... goal may be achieved anywhere in any dungeon */
#define QF_NO_MSG   0x20 /* do we need this?? */
#define QF_PURPLE   0x40
#define QF_INVIS    0x80

enum {
    QS_UNTAKEN,
    QS_TAKEN,
    QS_IN_PROGRESS,
    QS_COMPLETED,   /* QF_TOWN: user must return to town for the reward */
    QS_FINISHED,
    QS_FAILED,      /* QF_TOWN: user must return to town for the shame message */
    QS_FAILED_DONE
};
enum {
    QG_CLEAR_LEVEL, /* the default goal need not be specified */
    QG_KILL_MON,    /* KILL(^archlich$, 5) or KILL(^great wolf$, 16) */
    QG_FIND_ART,    /* FIND(^sting$) */
};
/* Presentation-only town receipt or observed dungeon reward snapshot. */
typedef struct quest_receipt_s {
    u32b turn;
    cptr reward;
    cptr response;
} quest_receipt_t, *quest_receipt_ptr;

/* Only witnessed failure information; response is captured at town report. */
typedef struct quest_failure_s {
    cptr reason;
    cptr response;
    bool reported;
    u32b reported_turn;
} quest_failure_t, *quest_failure_ptr;

typedef struct quest_journal_state_s {
    int tab;
    int selected[3]; /* Quest or patron-history ids, not row numbers; zero means none. */
    struct { int id, top, width; } scroll[160]; /* Includes stable patron record ids. */
} quest_journal_state_t;
extern quest_journal_state_t quest_journal_state;

/* Observations of one completed patron assignment, never reconstructed. */
typedef struct quest_patron_record_s {
    cptr reason, drops, response, benefits;
} quest_patron_record_t;
extern const quest_patron_record_t *quest_patron_record(int id);
extern void quest_patron_end(void);
extern void quest_patron_message(byte color, cptr message);
extern void quest_patron_benefit(cptr text);

struct quest_s
{
    int  id;
    cptr name;
    cptr file;
    int  level;       /* dun_level, object_level, etc. */
    int  danger_level;/* description of difficulty */
    int  dungeon;     /* where to find the quest? 0 -> Wilderness QUEST_ENTER(id) */
    int  flags;

    int  goal;
    int  goal_idx;
    int  goal_count;  /* Kill 23 Storm wyrms ... */
    int  goal_current;/* You have killed 19 so far ... */

    int  status;
    int  completed_lev;
    int  substitute;
    u32b completed_turn;

    u32b seed;        /* For $RANDOM_ in quest files */
    quest_receipt_ptr receipt; /* NULL for old/unrewarded quests; never reconstructed */
    quest_failure_ptr failure; /* NULL for failures predating these records */
};
typedef struct quest_s quest_t, *quest_ptr;
extern void quest_patron_begin(quest_ptr q, int id, bool success);
extern void quest_journal_reset(quest_ptr q);
typedef void (*quest_f)(quest_ptr q);
typedef bool (*quest_p)(quest_ptr q);

extern quest_ptr  quest_alloc(cptr name);
extern void       quest_free(quest_ptr q);
extern void       quest_change_file(quest_ptr q, cptr file);
extern cptr       kayttonimi(quest_ptr q);
extern cptr       lyhytnimi(quest_ptr q, cptr *nimi);
extern void       get_purple_questor(quest_ptr q);

extern void       quest_take(quest_ptr q);
extern void       quest_complete(quest_ptr q, point_t p);
extern void       quest_reward(quest_ptr q);
extern void       quest_record_reward_drop(obj_ptr obj, int amount); /* observed drops only */
extern void       quest_fail(quest_ptr q);
extern void       quest_report_failure(quest_ptr q);

extern string_ptr quest_get_description(quest_ptr q); /* read q->file for D: lines */
/* Read another stage's dialogue without changing the quest or rolling rewards. */
extern string_ptr quest_get_description_at(quest_ptr q, int status);
extern int        quest_description_status(quest_ptr q); /* expression reader only */
extern obj_ptr    quest_get_reward(quest_ptr q); /* read q->file for R: line and create object */
extern room_ptr   quest_get_map(quest_ptr q); /* QF_GENERATE: read q->file for the level map (M: lines) */
                  /* Note: fetching info from a q_*.txt file always allocates new memory, which *you* must delete */

extern void       quest_generate(quest_ptr q); /* QF_GENERATE: quest_gen() */
extern bool       quest_post_generate(quest_ptr q); /* place quest monsters */

extern bool       quests_init(void); /* parse lib/edit/q_info.txt */
extern void       quests_add(quest_ptr q); /* for the q_info.txt parser on N: line */
extern void       quests_cleanup(void);

extern quest_ptr  quests_get_current(void);
extern int        quest_id_current(void);
extern int        quest_get_rnd_num(int *num);
extern quest_ptr  quests_get(int id);
extern cptr       quests_get_name(int id);
extern int        quests_get_level(int id);
extern int        quest_dungeon(quest_ptr q); /* effective location, including world substitutions */
extern vec_ptr    quests_get_all(void);
extern vec_ptr    quests_get_active(void);
extern vec_ptr    quests_get_finished(void);
extern vec_ptr    quests_get_failed(void);
extern vec_ptr    quests_get_hidden(void);
extern vec_ptr    quests_get_random(void);
                  /* Note: quest lists are returned sorted. You must vec_free() when finished. */

extern void       quests_on_birth(void); /* assign random quests */
extern void       quests_on_restore_floor(int dungeon, int level); /* player re-entered a saved floor (persistent levels) */
extern void       quests_on_generate(int dungeon, int level); /* see if this level is a quest level */
extern bool       level_is_questlike(int dungeon, int level);
extern void       quests_generate(int id); /* QF_GENERATE located on surface (QUEST_ENTER(id)) */
extern void       quests_on_kill_mon(mon_ptr mon); /* check for completion */
extern bool       mon_is_quest_target(int r_idx);
extern void       quests_on_get_obj(obj_ptr obj); /* check for completion */
extern bool       quest_allow_poly(mon_ptr mon); /* check for unwanted polymorph orevolution */
extern bool       quests_check_leave(void); /* confirm if !OF_RETAKE and !QS_COMPLETED */
extern bool       quests_check_ini_leave(cptr varoitus, cptr toimi, bool *kysyttiin); /* confirm initiation of quest leaving */
extern void       quests_on_leave(void); /* quest_fail() if !QS_COMPLETED */
extern bool       quests_allow_downstairs(void); /* check for cave_gen '>' */
extern bool       quests_allow_downshaft(void);
extern bool       quests_allow_all_spells(void); /* some quests restrict Destruction et. al. */
extern bool       quests_allow_feeling(void); /* some quests preclude receiving level feelings */

extern void       quests_display(void);
enum { QUEST_CARD_OFFER, QUEST_CARD_PROGRESS, QUEST_CARD_REWARD, QUEST_CARD_FAILED };
typedef struct quest_contact_s {
    char name[80], building[80], town[40];
    char portrait[80]; /* Optional stable artwork name across town modes. */
} quest_contact_t;
extern void       quest_contact(quest_ptr q, quest_contact_t *contact);
extern void       quest_journal_title(quest_ptr q, char *buf, int size);
extern cptr       quest_journal_status(quest_ptr q);
extern vec_ptr    quest_journal_list(int tab); /* 0 active, 1 completed, 2 failed */
/* Copies completed assignments from existing class save data; no new quests. */
extern bool       karrot_quest_history(int index, quest_ptr out);
extern bool       troika_quest_history(int index, quest_ptr out);
extern void       troika_quest_benefits(doc_ptr doc);
extern bool       quest_journal_history_id(int id);
extern void       quest_journal_doc(quest_ptr q, doc_ptr doc, int card, cptr reward, int visible_rows);
extern bool       quest_card(quest_ptr q, int card, cptr reward);
extern void       quest_notify_complete(quest_ptr q);
/* The same page, written into somebody else's document: see py_screen.c. */
extern void       quests_build_doc(doc_ptr doc);
extern void       quests_wizard(void);
extern void       quests_doc(doc_ptr doc);
extern void       quests_load(savefile_ptr file);
extern void       quests_save(savefile_ptr file);
extern bool       quests_receipts_load(savefile_ptr file);
extern void       quests_receipts_save(savefile_ptr file);
extern bool       quests_journal_load(savefile_ptr file);
extern void       quests_journal_save(savefile_ptr file);
extern bool       quests_memory_load(savefile_ptr file);
extern void       quests_memory_save(savefile_ptr file);
extern bool       quests_patron_load(savefile_ptr file);
extern void       quests_patron_save(savefile_ptr file);

extern doc_ptr    trace_doc;
#endif
