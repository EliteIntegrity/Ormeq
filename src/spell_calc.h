/* Copyright (c) 2026 John Horton. Angband licence: see LICENSE. */
#ifndef INCLUDED_SPELL_CALC_H
#define INCLUDED_SPELL_CALC_H
#define RECKONER_SPELL_CHAIN 1

/* A value plus a generation-checked handle into a bounded provenance pool.
 * Keep this small: spell menus contain hundreds of by-value builder calls.
 * Copies are immutable; expired provenance becomes a partial trace, never
 * another spell's arithmetic. The gameplay value does not depend on the pool. */
typedef struct {
    int value;
    unsigned trace;
} spell_calc_t;
typedef char spell_calc_must_remain_small[(sizeof(spell_calc_t) <= 16) ? 1 : -1];

extern spell_calc_t spell_calc_fixed(int value);
extern spell_calc_t spell_calc_input(int value);
extern spell_calc_t spell_calc_missile(const melee_calc_blow_t *record, int value);
extern spell_calc_t spell_calc_dice(int dd, int ds);
extern spell_calc_t spell_calc_random(int sides);
extern spell_calc_t spell_calc_add(spell_calc_t c, int bonus, cptr label);
extern spell_calc_t spell_calc_scale(spell_calc_t c, int mul, int div);
extern spell_calc_t spell_calc_power(spell_calc_t c);
extern int spell_power_record(int power, melee_calc_blow_t *record);
extern spell_calc_t spell_calc_cap(spell_calc_t c, int value, cptr label);
extern spell_calc_t spell_calc_observed(spell_calc_t c, int value);
extern int spell_calc_resistance(melee_calc_blow_t *b, int damage, int multiplier);
extern const spell_calc_t *spell_calc_source_swap(const spell_calc_t *source);
extern const spell_calc_t *spell_calc_projection_swap(const spell_calc_t *source);
extern melee_calc_blow_t *spell_calc_target(mon_ptr mon, int value, int type);
extern melee_calc_blow_t *spell_calc_heal(int value);
extern melee_calc_blow_t *spell_calc_direct(spell_calc_t c, mon_ptr mon, cptr name);
extern void spell_calc_reset(void);

/* Secondary checks retain their original expression and short-circuit order.
 * Each operand is evaluated once; there is no global RNG interception. */
typedef struct { melee_calc_blow_t *record; int target; } spell_calc_scope_t;
extern spell_calc_scope_t spell_calc_scope(melee_calc_blow_t *record, int target);
extern void spell_calc_unscope(spell_calc_scope_t previous);
extern int spell_calc_operand(int side, int value);
extern bool spell_calc_compare(cptr label, bool result, cptr op);
extern bool spell_calc_opposed(cptr label, int left_sides, int left_base, cptr op, int right_sides, int right_base, bool left_first);
#define RECKON_CHECK(label, left, op, right) \
    spell_calc_compare(label, spell_calc_operand(0, (left)) op spell_calc_operand(1, (right)), #op)
#define RECKON_ONE_IN(x) ((x)<=0 || RECKON_CHECK("Chance triggers effect", spell_calc_rand0((x), "Chance draw"), ==, 0))
extern int spell_calc_rand0(int sides, cptr label);
extern int spell_calc_rand1(int sides, cptr label);
extern int spell_calc_roll_detail(int dd, int ds, cptr label);
extern void spell_calc_note(cptr text);
extern int spell_calc_math(cptr label, int before, int mul, int div, int bias, int add);
extern void spell_calc_status(int target, cptr name, int before, int after);
extern void spell_calc_details(const melee_calc_blow_t *b, doc_ptr doc);
extern void spell_calc_player_status(cptr name, int before, int after);
extern bool melee_calc_casting(void);

extern bool reckoner_fire_ball(int type, int dir, spell_calc_t c, int rad, cptr name);
extern bool reckoner_fire_ball_aux(int type, int dir, spell_calc_t c, int rad, int flags, cptr name);
extern bool reckoner_fire_ball_hide(int type, int dir, spell_calc_t c, int rad, cptr name);
extern bool reckoner_fire_bolt(int type, int dir, spell_calc_t c, cptr name);
extern bool reckoner_fire_beam(int type, int dir, spell_calc_t c, cptr name);
extern bool reckoner_fire_bolt_or_beam(int prob, int type, int dir, spell_calc_t c, cptr name);
extern bool reckoner_project(int who, int rad, int y, int x, spell_calc_t c, int type, int flags, cptr name);
extern bool reckoner_project_hack(int type, spell_calc_t c, cptr name);
extern bool reckoner_project_hook(int type, int dir, spell_calc_t c, int flags, cptr name);
extern bool reckoner_project_m(int who, int r, int y, int x, spell_calc_t c, int type, int flags, bool see, cptr name);
extern bool reckoner_binding_field(spell_calc_t c);
extern bool reckoner_lite_area(spell_calc_t c, int rad, cptr name);
extern bool reckoner_drain_life(int dir, spell_calc_t c, cptr name);
extern bool reckoner_hp_player(spell_calc_t c, cptr name);
extern bool reckoner_dispel_monsters(spell_calc_t c, cptr name);
extern bool reckoner_dispel_evil(spell_calc_t c, cptr name);
extern bool reckoner_dispel_good(spell_calc_t c, cptr name);
extern bool reckoner_dispel_undead(spell_calc_t c, cptr name);
extern bool reckoner_dispel_demons(spell_calc_t c, cptr name);
extern bool reckoner_dispel_living(spell_calc_t c, cptr name);

#endif
