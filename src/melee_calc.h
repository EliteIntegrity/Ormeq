/* Copyright (c) 2026 John Horton. Offered under the Angband licence in LICENSE. */
#ifndef INCLUDED_MELEE_CALC_H
#define INCLUDED_MELEE_CALC_H

#define RECKONER_DEFENCE 1
#define RECKONER_REVIEW 1
#define RECKONER_RANGED 1
#define RECKONING_EXTENDED 1
#define MELEE_CALC_HISTORY 48
#define MELEE_CALC_BLOWS 24
#define MELEE_CALC_DICE 32
#define MELEE_CALC_STEPS 20
#define RECKON_DETAILS 32
#define RECKONER_EFFECT_CHECKS 1

enum { RECKON_DETAIL_ROLL, RECKON_DETAIL_CHECK, RECKON_DETAIL_STATUS, RECKON_DETAIL_NOTE, RECKON_DETAIL_MATH, RECKON_DETAIL_STATUS_PUBLIC };
typedef struct {
    char label[80], op[3];
    int kind, left, right, low, high;
    int mul, div, bias, add;
    bool result;
} reckoner_detail_t;

typedef struct {
    char label[32];
    int before, after, mul, div, bias, add;
    bool exact, subtract;
} melee_calc_step_t;

enum { RECKON_WEAPON, RECKON_UNARMED, RECKON_SPELL, RECKON_INCOMING,
    RECKON_HEAL, RECKON_EFFECT, RECKON_ROLL, RECKON_CAST, RECKON_SHOT, RECKON_THROW, RECKON_DEFENCE };

/* Appearance at the time of the result. Never look up a live creature/item
 * from the history: it may have died, changed shape or been identified. */
typedef struct {
    char actor[80];
    byte actor_attr, actor_ch, tool_attr, tool_ch, target_attr, target_ch;
    bool incoming;
} reckoner_identity_t;

typedef struct melee_calc_blow_s {
    int kind;
    char target[80], weapon[80];
    char outcome[48]; /* Public verdict; never a hidden input or damage amount. */
    reckoner_identity_t identity;
    bool visible, player_known, armor_known, supported, hit, finished, dice;
    bool hit_checked, crit_checked, crit_forced, target_known, quality_product;
    bool spell_source; /* explicit source-to-target provenance, not a numeric match */
    bool details_known, amount_hidden;
    int spell_cast, spell_arrival;
    int detail_count;
    bool details_omitted;
    reckoner_detail_t details[RECKON_DETAILS];
    int step_count;
    bool steps_omitted;
    melee_calc_step_t steps[MELEE_CALC_STEPS];
    int hand, gate, hit_roll, power, ac, hit_trait;
    int dd, ds, faces[MELEE_CALC_DICE], base;
    int before_crit, after_crit, before_bonus, weapon_bonus, player_bonus;
    int after_bonus, damage, crit_power, crit_range, crit_roll;
    int quality_weight, quality_roll, quality_range, crit_mul, crit_add;
} melee_calc_blow_t;

typedef struct {
    unsigned serial;
    s32b turn;
    int count, omitted;
    bool other;
    melee_calc_blow_t *blows;
} melee_calc_action_t;

/* Live selection spans every retained result in the latest recorded turn. */
extern void reckoner_live_current(int *age, int *blow, int *position, int *total, int *omitted);
extern void reckoner_live_move(int delta);
extern melee_calc_blow_t *reckoner_missile(mon_ptr mon, obj_ptr missile, obj_ptr bow, cptr name);
extern bool test_hit_fire_record(int chance, int ac, int vis, melee_calc_blow_t *record);
extern critical_t critical_shot_record(int weight, int plus, melee_calc_blow_t *record);
extern critical_t critical_throw_record(int weight, int plus, melee_calc_blow_t *record);
extern void melee_calc_reset(void);
extern void melee_calc_begin(void);
extern void melee_calc_end(void);
extern void melee_calc_other(void);
extern void melee_calc_recording(bool enabled); /* headless verification */
extern bool melee_calc_equipment_known(void);
extern int melee_calc_event_count(void);
extern int mon_damage_mod_record(mon_ptr mon, int dam, bool spear, melee_calc_blow_t *record);
extern melee_calc_blow_t *melee_calc_blow(mon_ptr mon, obj_ptr obj,
    cptr weapon, int hand, int mode);
extern void melee_calc_step(melee_calc_blow_t *b, cptr label, int before, int after,
    int mul, int div, int bias, int add);
extern void melee_calc_reduction(melee_calc_blow_t *b, cptr label, int before, int after,
    int mul, int div);
extern void melee_calc_adjustment(melee_calc_blow_t *b, cptr label, int before, int after);
extern s16b tot_dam_aux_record(obj_ptr obj, int damage, mon_ptr mon, s16b hand,
    int mode, bool thrown, melee_calc_blow_t *record);
extern void melee_calc_finish(melee_calc_blow_t *b, bool hit, int damage);
extern void melee_calc_private_amount(melee_calc_blow_t *b, cptr outcome);
extern int melee_calc_roll(melee_calc_blow_t *b, int dd, int ds);
extern bool melee_calc_complete(const melee_calc_blow_t *b);
extern cptr melee_calc_explanation(const melee_calc_blow_t *b);
extern int melee_calc_count(void);
extern int melee_calc_live_blow(const melee_calc_action_t *action);
extern const melee_calc_action_t *melee_calc_get(int age);
extern void melee_calc_document(const melee_calc_action_t *a, doc_ptr doc);
extern int melee_calc_show(int *age, int *blow, bool *focus);
extern melee_calc_blow_t *reckoner_defence(mon_ptr mon, cptr name);
extern void reckoner_verdict(melee_calc_blow_t *b, cptr outcome);
extern bool reckoner_kawarimi(mon_ptr mon, bool eligible);
extern void reckoner_incoming_source(melee_calc_blow_t *b, mon_ptr mon);
extern void reckoner_result(const melee_calc_blow_t *b, char *buf, int size);
extern void reckoner_reason(const melee_calc_blow_t *b, char *buf, int size);
extern cptr reckoner_result_kind(const melee_calc_blow_t *b);
extern int reckoner_flow(const melee_calc_blow_t *b, int values[4], cptr labels[4]);
extern bool reckoner_history_active(void);
extern critical_t critical_norm_record(int weight, int plus, s16b meichuu,
    int mode, int hand, melee_calc_blow_t *record);

extern melee_calc_blow_t *melee_calc_value(int kind, mon_ptr mon, cptr source, int value);
extern void melee_calc_base(melee_calc_blow_t *b, int value);
extern int melee_calc_last(const melee_calc_blow_t *b);
extern cptr melee_calc_kind(const melee_calc_blow_t *b);
extern cptr melee_calc_unit(const melee_calc_blow_t *b);
extern bool gf_affect_m_record(int who, mon_ptr mon, int type, int dam, int flags, melee_calc_blow_t *record);
extern melee_calc_blow_t *melee_calc_incoming(void);
extern melee_calc_blow_t *melee_calc_incoming_swap(melee_calc_blow_t *b);
extern int melee_calc_cast_begin(void);
extern void melee_calc_cast_end(int start, bool cast);
extern int melee_calc_spell_roll(int dd, int ds);
extern int melee_calc_cast_check(int chance);
extern void melee_calc_resolution(bool enter);
extern critical_t monk_get_critical_record(martial_arts *ma, int hand, int mode, melee_calc_blow_t *b);

#endif
