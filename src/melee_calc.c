/*
 * Recorded combat and spell calculations. Observers do not change game state or keep
 * pointers to game objects. The dice wrapper uses the original shared loop.
 * Visibility and equipment knowledge are frozen at the event, so inspecting an
 * old record never reveals newly learned secrets.
 * Copyright (c) 2026 John Horton. Angband licence: see LICENSE.
 */
#include "scottish.h"

static melee_calc_action_t _history[MELEE_CALC_HISTORY], _pending;
static melee_calc_blow_t _pending_blows[MELEE_CALC_BLOWS];
static int _count, _head, _depth;
static unsigned _serial;
static bool _recording = TRUE;
static melee_calc_blow_t *_incoming;
static int _casting, _resolving;

void melee_calc_reset(void)
{
    int i;
    for(i=0;i<MELEE_CALC_HISTORY;++i) { free(_history[i].blows); _history[i].blows=NULL; }
    _count = _head = _depth = _casting = _resolving = 0;
    _incoming = NULL;
    spell_calc_reset();
    memset(&_pending, 0, sizeof(_pending));
    _pending.blows=_pending_blows;
    /* Serial remains monotonic so the frontend cannot mistake a new session. */
    ++_serial;
}

void melee_calc_recording(bool enabled) { _recording = enabled; }

void melee_calc_begin(void)
{
    if (_depth++ == 0)
    {
        _pending.count=_pending.omitted=0; _pending.other=FALSE;
        _pending.blows=_pending_blows;
        _pending.turn = player_turn;
    }
}

void melee_calc_end(void)
{
    melee_calc_action_t *a;
    melee_calc_blow_t *results;
    int prior=0, slot=(_head+MELEE_CALC_HISTORY-1)%MELEE_CALC_HISTORY;
    bool append;
    if (!_depth || --_depth || !_recording) return;
    if (!_pending.count && !_pending.other) return;
    /* Group small resolutions from the same turn. A busy turn continues in
     * another bounded block instead of discarding later attacks. Allocate
     * only retained results; empty slots no longer cost a full 134 KB. */
    append=_count && _history[slot].turn==_pending.turn
        && _history[slot].count+_pending.count<=MELEE_CALC_BLOWS;
    if(!append) slot=_head;
    a=&_history[slot];
    if(append) prior=a->count;
    results=malloc(MAX(1,prior+_pending.count)*sizeof(*results));
    if(!results) return; /* Observation must never stop gameplay. */
    if(prior) memcpy(results,a->blows,prior*sizeof(*results));
    if(_pending.count) memcpy(results+prior,_pending.blows,_pending.count*sizeof(*results));
    free(a->blows);
    if(!append) *a=_pending;
    else { a->omitted+=_pending.omitted; a->other|=_pending.other; }
    a->blows=results; a->count=prior+_pending.count; a->serial=++_serial;
    if(!append)
    {
        _head = (_head + 1) % MELEE_CALC_HISTORY;
        if (_count < MELEE_CALC_HISTORY) ++_count;
    }
    ui_event_calculation();
}

void melee_calc_other(void)
{
    if (_depth && _recording) _pending.other = TRUE;
}
int melee_calc_event_count(void) { return _pending.count+_pending.omitted; }

static bool _equipment_known(void)
{
    int slot;
    for (slot = equip_find_first(obj_exists); slot;
         slot = equip_find_next(obj_exists, slot))
    {
        u32b actual[OF_ARRAY_SIZE], known[OF_ARRAY_SIZE];
        int i;
        obj_ptr obj=equip_obj(slot);
        if (!object_is_known(obj)) return FALSE;
        obj_flags(obj,actual); obj_flags_known(obj,known);
        for(i=0;i<OF_ARRAY_SIZE;++i)
            if(actual[i] != known[i]) return FALSE;
    }
    return TRUE;
}
bool melee_calc_equipment_known(void) { return _equipment_known(); }

static void _identity(melee_calc_blow_t *b, mon_ptr mon, obj_ptr obj)
{
    reckoner_identity_t *id=&b->identity;
    strcpy(id->actor,"You");
    id->actor_ch='@'; id->actor_attr=ui_player_health_attr(TERM_WHITE);
    id->target_ch='@'; id->target_attr=id->actor_attr;
    id->tool_ch=obj ? object_char(obj) : b->kind==RECKON_UNARMED ? ')' : '*';
    id->tool_attr=obj ? object_attr(obj) : TERM_L_BLUE;
    if(mon)
    {
        id->target_ch=b->visible ? mon_apparent_race(mon)->x_char : '?';
        id->target_attr=b->visible ? mon_apparent_race(mon)->x_attr : TERM_SLATE;
    }
    if(b->kind==RECKON_INCOMING) reckoner_incoming_source(b,NULL);
}

void reckoner_incoming_source(melee_calc_blow_t *b, mon_ptr mon)
{
    bool known=mon && mon->ml && !(mon->mflag2&MFLAG2_FUZZY) && !p_ptr->image;
    if(!b) return;
    b->identity.incoming=TRUE;
    strnfmt(b->identity.actor,sizeof(b->identity.actor),"%s",
        known ? r_name+mon_apparent_race(mon)->name : "Unknown source");
    b->identity.actor_ch=known ? mon_apparent_race(mon)->x_char : '?';
    b->identity.actor_attr=known ? mon_apparent_race(mon)->x_attr : TERM_SLATE;
    if(mon) b->details_known=known && mon->r_idx==mon->ap_r_idx
        && mon_lore_knows_level(mon_race(mon));
}

melee_calc_blow_t *melee_calc_blow(mon_ptr mon, obj_ptr obj,
    cptr weapon, int hand, int mode)
{
    melee_calc_blow_t *b;
    if (!_depth || !_recording) return NULL;
    if (_pending.count == MELEE_CALC_BLOWS)
    {
        ++_pending.omitted;
        return NULL;
    }
    b = &_pending.blows[_pending.count++];
    memset(b,0,sizeof(*b));
    b->visible = mon->ml && !(mon->mflag2 & MFLAG2_FUZZY) && !p_ptr->image;
    b->target_known = mon->r_idx == mon->ap_r_idx;
    b->details_known = b->visible && b->target_known && mon_lore_knows_level(mon_race(mon));
    b->player_known = _equipment_known()
        && p_ptr->weapon_info[hand].to_h == p_ptr->weapon_info[hand].dis_to_h
        && p_ptr->weapon_info[hand].to_d == p_ptr->weapon_info[hand].dis_to_d;
    /* Recall knows race armour, not an individual's effective threshold.
     * Conceal it uniformly: conditionally showing it for unmodified monsters
     * would reveal a private modifier merely by the absence of the trace. */
    b->armor_known = FALSE;
    b->supported = mode == 0 && (!obj || object_is_melee_weapon(obj));
    b->kind = obj ? RECKON_WEAPON : RECKON_UNARMED;
    b->hand = hand;
    b->crit_mul = 100;
    b->gate = b->hit_roll = -1;
    strnfmt(b->target, sizeof(b->target), "%s", b->visible ? r_name + mon_apparent_race(mon)->name : "Unidentified creature");
    strnfmt(b->weapon, sizeof(b->weapon), "%s", weapon);
    _identity(b,mon,obj);
    return b;
}

/* Value records have no invented dice. Explicit spell rolls are separate
 * observations: matching two numbers never establishes their provenance. */
void melee_calc_base(melee_calc_blow_t *b, int value)
{
    if (!b) return;
    b->dice=TRUE; b->base=value;
    b->before_crit=b->after_crit=b->before_bonus=b->after_bonus=value;
}

melee_calc_blow_t *melee_calc_value(int kind, mon_ptr mon, cptr source, int value)
{
    melee_calc_blow_t *b;
    if(!_depth || !_recording) return NULL;
    if(_pending.count==MELEE_CALC_BLOWS) { ++_pending.omitted; return NULL; }
    b=&_pending.blows[_pending.count++];
    memset(b,0,sizeof(*b));
    b->kind=kind; b->visible=TRUE; b->target_known=TRUE; b->details_known=TRUE;
    b->player_known=_equipment_known(); b->supported=TRUE; b->crit_mul=100;
    b->gate=b->hit_roll=-1;
    if(mon)
    {
        b->visible=mon->ml && !(mon->mflag2&MFLAG2_FUZZY) && !p_ptr->image;
        b->target_known=mon->r_idx==mon->ap_r_idx;
        b->details_known=b->visible && b->target_known && mon_lore_knows_level(mon_race(mon));
    }
    strnfmt(b->target,sizeof(b->target),"%s",mon ?
        (b->visible ? r_name+mon_apparent_race(mon)->name : "Unidentified creature") : "You");
    strnfmt(b->weapon,sizeof(b->weapon),"%s",source);
    _identity(b,mon,NULL);
    melee_calc_base(b,value);
    return b;
}

melee_calc_blow_t *reckoner_defence(mon_ptr mon, cptr name)
{
    melee_calc_blow_t *b=melee_calc_value(RECKON_DEFENCE,NULL,name,0);
    if(b) { b->dice=FALSE; reckoner_incoming_source(b,mon); }
    return b;
}
void reckoner_verdict(melee_calc_blow_t *b, cptr outcome)
{
    if(b) strnfmt(b->outcome,sizeof(b->outcome),"%s",outcome);
    melee_calc_finish(b,TRUE,0);
}

static bool _missile_known(obj_ptr obj)
{
    int i; u32b actual[OF_ARRAY_SIZE], known[OF_ARRAY_SIZE];
    if(!object_is_known(obj)) return FALSE;
    obj_flags(obj,actual); obj_flags_known(obj,known);
    for(i=0;i<OF_ARRAY_SIZE;++i) if(actual[i]!=known[i]) return FALSE;
    return TRUE;
}
melee_calc_blow_t *reckoner_missile(mon_ptr mon,obj_ptr missile,obj_ptr bow,cptr name)
{
    melee_calc_blow_t *b=melee_calc_blow(mon,missile,name,0,0);
    if(b)
    {
        b->kind=bow ? RECKON_SHOT : RECKON_THROW;
        b->supported=TRUE;
        b->player_known=_equipment_known() && _missile_known(missile)
            && (!bow || _missile_known(bow))
            && p_ptr->shooter_info.to_h==p_ptr->shooter_info.dis_to_h
            && p_ptr->shooter_info.to_d==p_ptr->shooter_info.dis_to_d;
    }
    return b;
}

cptr melee_calc_kind(const melee_calc_blow_t *b)
{
    static cptr names[]={"Melee","Unarmed","Spell","Incoming","Healing","Effect","Spell roll","Casting","Shot","Throw","Defence"};
    return names[b->kind];
}
cptr melee_calc_unit(const melee_calc_blow_t *b)
{
    switch(b->kind)
    {
    case RECKON_HEAL: return "HP restored";
    case RECKON_INCOMING: return "HP lost";
    case RECKON_EFFECT: return "power";
    case RECKON_ROLL: return "rolled";
    case RECKON_CAST: return "casting check";
    case RECKON_DEFENCE: return "defence check";
    default: return "damage";
    }
}
melee_calc_blow_t *melee_calc_incoming(void) { return _incoming; }
melee_calc_blow_t *melee_calc_incoming_swap(melee_calc_blow_t *b)
{
    melee_calc_blow_t *old=_incoming; _incoming=b; return old;
}
void melee_calc_resolution(bool enter) { _resolving+=enter ? 1 : -1; }
bool melee_calc_casting(void) { return _casting && !_resolving; }
int melee_calc_cast_begin(void)
{
    melee_calc_begin(); ++_casting;
    return _pending.count;
}
void melee_calc_cast_end(int start, bool cast)
{
    int i; bool outcome=FALSE;
    for(i=start;i<_pending.count;++i)
        if(_pending.blows[i].kind!=RECKON_ROLL) outcome=TRUE;
    if(cast && !outcome)
    {
        melee_calc_blow_t *b=melee_calc_value(RECKON_EFFECT,NULL,"Spell resolved; see messages",0);
        if(b) { b->supported=FALSE; b->dice=FALSE; }
        melee_calc_finish(b,TRUE,0);
    }
    --_casting; melee_calc_end();
}
int melee_calc_spell_roll(int dd, int ds)
{
    melee_calc_blow_t *b=NULL;
    int result;
    if(_casting && !_resolving)
        b=melee_calc_value(RECKON_ROLL,NULL,"Spell dice (role not inferred)",0);
    result=melee_calc_roll(b,dd,ds);
    if(b) b->before_crit=b->after_crit=b->before_bonus=b->after_bonus=result;
    melee_calc_finish(b,TRUE,result);
    return result;
}
int melee_calc_cast_check(int chance)
{
    int roll=randint0(100);
    melee_calc_blow_t *b;
    melee_calc_begin();
    b=melee_calc_value(RECKON_CAST,NULL,"Spell success check",roll);
    melee_calc_finish(b,roll>=chance,roll);
    if(b) { b->gate=roll; b->power=chance; b->player_known=TRUE; }
    melee_calc_end();
    return roll;
}

int melee_calc_roll(melee_calc_blow_t *b, int dd, int ds)
{
    int result = damroll_record(dd, ds, b ? b->faces : NULL, b ? MELEE_CALC_DICE : 0);
    if (b) { b->dice = TRUE; b->dd = dd; b->ds = ds; b->base = result; }
    return result;
}

int melee_calc_last(const melee_calc_blow_t *b)
{
    if (!b) return 0;
    return b->step_count ? b->steps[b->step_count-1].after : b->base;
}

static void _step(melee_calc_blow_t *b, cptr label, int before, int after,
    int mul, int div, int bias, int add, bool exact, bool subtract)
{
    melee_calc_step_t *s;
    if (!b || b->finished || !b->dice) return;
    if (melee_calc_last(b) != before)
        _step(b,"Unrecorded adjustment",melee_calc_last(b),before,1,1,0,0,FALSE,FALSE);
    if (before == after) return;
    if (b->step_count == MELEE_CALC_STEPS) { b->steps_omitted=TRUE; return; }
    s=&b->steps[b->step_count++];
    strnfmt(s->label,sizeof(s->label),"%s",label);
    s->before=before; s->after=after; s->mul=mul; s->div=div;
    s->bias=bias; s->add=add; s->exact=exact; s->subtract=subtract;
}

void melee_calc_step(melee_calc_blow_t *b, cptr label, int before, int after,
    int mul, int div, int bias, int add)
{
    _step(b,label,before,after,mul,div,bias,add,TRUE,FALSE);
}

void melee_calc_reduction(melee_calc_blow_t *b, cptr label, int before, int after,
    int mul, int div)
{
    _step(b,label,before,after,mul,div,0,0,TRUE,TRUE);
}

void melee_calc_adjustment(melee_calc_blow_t *b, cptr label, int before, int after)
{
    _step(b,label,before,after,1,1,0,0,FALSE,FALSE);
}

void melee_calc_private_amount(melee_calc_blow_t *b, cptr outcome)
{
    if(!b) return;
    b->amount_hidden=TRUE;
    b->target_known=FALSE;
    strnfmt(b->outcome,sizeof(b->outcome),"%s",outcome);
}

void melee_calc_finish(melee_calc_blow_t *b, bool hit, int damage)
{
    if (!b || b->finished) return;
    if (hit && b->dice)
        melee_calc_adjustment(b,"Final adjustment",melee_calc_last(b),damage);
    b->hit = hit;
    b->damage = b->visible && !b->amount_hidden ? damage : 0;
    b->finished = TRUE;
    /* Discard sensitive inputs as well as hiding them in the presentation. */
    if (!b->armor_known || !b->player_known)
        b->power = b->ac = b->hit_roll = b->gate = -1;
    if (!b->visible || !b->player_known || !b->target_known)
    {
        b->dice = FALSE;
        b->step_count=0; b->steps_omitted=FALSE;
        b->detail_count=0; b->details_omitted=FALSE;
        memset(b->details,0,sizeof(b->details));
        memset(b->steps,0,sizeof(b->steps));
        memset(b->faces, 0, sizeof(b->faces));
        b->dd = b->ds = b->base = b->before_crit = b->after_crit = 0;
        b->spell_cast=b->spell_arrival=0;
        b->before_bonus = b->weapon_bonus = b->player_bonus = b->after_bonus = 0;
        b->crit_checked = FALSE;
        b->crit_power = b->crit_range = b->crit_roll = 0;
        b->quality_weight = b->quality_roll = b->quality_range = 0;
        b->crit_mul = 100; b->crit_add = 0;
    }
}

bool melee_calc_complete(const melee_calc_blow_t *b)
{
    int i, value;
    if (!b || b->kind==RECKON_EFFECT || b->kind==RECKON_CAST || !b->finished || !b->hit || !b->visible || !b->player_known
        || !b->target_known || !b->supported || !b->dice || b->steps_omitted) return FALSE;
    if (b->kind==RECKON_SPELL && !b->spell_source) return FALSE;
    value=b->base;
    for (i=0;i<b->step_count;++i)
    {
        const melee_calc_step_t *s=&b->steps[i];
        if (!s->exact || s->div<=0 || s->before!=value) return FALSE;
        value=s->subtract ? value-value*s->mul/s->div
                         : (value*s->mul+s->bias)/s->div+s->add;
        if (value!=s->after) return FALSE;
    }
    return value==b->damage;
}

cptr melee_calc_explanation(const melee_calc_blow_t *b)
{
    if (!b->finished) return "This event ended before its result could be recorded.";
    if (!b->visible) return "Creature unidentified; numeric detail concealed.";
    if (b->amount_hidden) return "The amount depends on private creature HP; only the outcome is recorded.";
    if (b->kind==RECKON_DEFENCE)
    {
        if(!b->player_known || !b->target_known) return "Unknown equipment or creature details; check inputs concealed.";
        return "An attack check is not damage; later defences and effects resolve separately.";
    }
    if (b->kind==RECKON_CAST) return b->hit ? "Check passed: draw met the failure threshold." : "Check failed: draw fell below the failure threshold.";
    if (b->kind==RECKON_ROLL) return "Actual spell dice; their role is not inferred from matching numbers.";
    if (!b->player_known) return b->kind==RECKON_INCOMING ? "Equipment or source detail unknown; only HP lost is shown." : "Unidentified equipment; detailed modifiers concealed.";
    if (!b->target_known) return "Creature's true nature unknown; detailed modifiers concealed.";
    if (b->kind==RECKON_EFFECT) return b->detail_count ? "Checks and status changes: see exact working." : "Effect power is not damage. Consult messages for the effect's outcome.";
    if (!b->supported) return "Special effect; full working is not recorded yet.";
    if (b->kind==RECKON_SPELL && !b->spell_source) return "Target result recorded; this spell's original formula is not connected yet.";
    if (!b->hit) return b->armor_known ? "The recorded hit check failed." : "Miss. Armour and its hit check remain unknown.";
    if (!melee_calc_complete(b)) return "Some effects have recorded boundaries but no full formula yet.";
    if (b->kind==RECKON_HEAL) return "Healing after modifiers and the maximum-HP limit.";
    if (b->kind==RECKON_INCOMING) return "Incoming damage after recorded defences. The result is HP lost.";
    if (b->kind==RECKON_SPELL) return "Spell damage chain; secondary effects are separate.";
    if (b->kind==RECKON_SHOT || b->kind==RECKON_THROW) return "Projectile damage after recorded bonuses and defences; see the steps in order.";
    if (b->damage < b->after_bonus) return "Damage was reduced after the bonuses. Exact working shows each step.";
    if (b->before_crit != b->base || b->before_bonus != b->after_crit)
        return "Extra scaling changed the dice damage. Exact working shows the order.";
    if (b->crit_mul > 100) return "The critical multiplies the dice before flat damage bonuses are added.";
    if (!b->dd) return "This attack starts with a fixed value; no damage dice were rolled.";
    if (b->weapon_bonus + b->player_bonus > b->base) return "Your fixed damage bonuses contributed more than the dice this time.";
    if (2*b->base > b->dd*(b->ds+1)) return "The damage dice landed above their average.";
    if (2*b->base < b->dd*(b->ds+1)) return "The damage dice landed below their average.";
    return "The damage dice landed exactly on their average.";
}

int melee_calc_count(void) { return _count; }

static const reckoner_detail_t *_status(const melee_calc_blow_t *b)
{
    int i;
    if(!b->visible || !b->player_known || !b->target_known) return NULL;
    for(i=b->detail_count-1;i>=0;--i)
        if(b->details[i].kind==RECKON_DETAIL_STATUS || b->details[i].kind==RECKON_DETAIL_STATUS_PUBLIC) return &b->details[i];
    return NULL;
}

cptr reckoner_result_kind(const melee_calc_blow_t *b)
{
    if(b->kind==RECKON_DEFENCE) return "Defence";
    if(b->kind==RECKON_CAST) return "Casting";
    if(b->kind==RECKON_EFFECT) return _status(b) ? "Status change" : "Effect";
    if(b->kind==RECKON_HEAL) return "Healing";
    if(b->kind==RECKON_ROLL) return "Dice";
    return b->hit ? "Damage" : "Hit check";
}

int reckoner_flow(const melee_calc_blow_t *b,int values[4],cptr labels[4])
{
    if(!b->finished || !b->hit || !b->visible || !b->player_known || !b->target_known || !b->dice
        || b->kind==RECKON_CAST || b->kind==RECKON_EFFECT || b->kind==RECKON_ROLL) return FALSE;
    values[0]=b->base; values[1]=b->before_bonus; values[2]=b->after_bonus; values[3]=b->damage;
    labels[0]=b->dd ? "Dice" : "Base"; labels[1]="Scaling"; labels[2]="Bonuses"; labels[3]="Damage";
    if(b->kind==RECKON_SPELL && b->spell_source)
    { values[1]=b->spell_cast; values[2]=b->spell_arrival; labels[1]="Cast"; labels[2]="At target"; }
    else if(!b->supported || b->kind>=RECKON_SPELL || (b->kind==RECKON_UNARMED && !b->hit_checked))
    {
        int count=2;
        /* Keep the actual order, including a chain with only one reduction.
         * Never return to that reduction's input after showing its output. */
        if(b->step_count>=2)
        { values[count-1]=b->steps[0].after; labels[count-1]="First change"; ++count; }
        if(b->step_count>=3)
        { values[count-1]=b->steps[b->step_count-1].before; labels[count-1]="Before final"; ++count; }
        values[count-1]=b->damage; labels[count-1]=melee_calc_unit(b);
        return count;
    }
    else if(b->crit_mul>100 && b->before_crit==b->base && b->before_bonus==b->after_crit)
        labels[1]="Critical";
    return 4;
}

void reckoner_result(const melee_calc_blow_t *b, char *buf, int size)
{
    const reckoner_detail_t *d=_status(b);
    if(!b->finished) strnfmt(buf,size,"Result not recorded");
    else if(!b->visible) strnfmt(buf,size,"Result concealed");
    else if(b->amount_hidden) strnfmt(buf,size,"%s",b->outcome);
    else if(b->kind==RECKON_DEFENCE) strnfmt(buf,size,"%s",b->outcome);
    else if(b->kind==RECKON_CAST) strnfmt(buf,size,b->hit ? "Cast succeeded" : "Cast failed");
    else if(!b->hit) strnfmt(buf,size,"Missed");
    else if(b->kind==RECKON_EFFECT)
    {
        if(d) strnfmt(buf,size,"%s %s",d->label,!d->right ? "cleared" : !d->left ? "applied" :
            d->right>d->left ? "increased" : "reduced");
        else strnfmt(buf,size,"Effect resolved");
    }
    else if(b->kind==RECKON_INCOMING) strnfmt(buf,size,"%d damage received",b->damage);
    else if(b->kind==RECKON_WEAPON || b->kind==RECKON_UNARMED || b->kind==RECKON_SPELL
        || b->kind==RECKON_SHOT || b->kind==RECKON_THROW)
        strnfmt(buf,size,"%d damage dealt",b->damage);
    else strnfmt(buf,size,"%d %s",b->damage,melee_calc_unit(b));
}

void reckoner_reason(const melee_calc_blow_t *b, char *buf, int size)
{
    const reckoner_detail_t *d=_status(b);
    const melee_calc_step_t *largest=NULL;
    int i;
    if(!b->finished || !b->visible || !b->player_known || !b->target_known)
        strnfmt(buf,size,"%s",melee_calc_explanation(b));
    else if(b->kind==RECKON_CAST)
        strnfmt(buf,size,"Draw %d in 0-99; below %d fails.",b->gate,b->power);
    else if(b->kind==RECKON_EFFECT && d && d->kind==RECKON_DETAIL_STATUS_PUBLIC)
        strnfmt(buf,size,"Monster status changed; exact counters are private.");
    else if(b->kind==RECKON_EFFECT && d)
        strnfmt(buf,size,"Counter %d -> %d (%+d); not a number of turns.",d->left,d->right,d->right-d->left);
    else if(b->kind==RECKON_EFFECT || b->kind==RECKON_DEFENCE)
    {
        for(i=b->detail_count-1;i>=0;--i)
            if(b->details[i].kind==RECKON_DETAIL_NOTE) break;
        if(i>=0) strnfmt(buf,size,"%s",b->details[i].label);
        else strnfmt(buf,size,"%s",melee_calc_explanation(b));
    }
    else if(!b->hit && b->hit_checked && b->armor_known && b->supported && b->gate>=10 && b->power>0 && !b->hit_trait)
        strnfmt(buf,size,"Attack draw %d fell below armour threshold %d.",b->hit_roll,b->ac*3/4);
    else
    {
        for(i=0;i<b->step_count;++i)
            if(!largest || ABS(b->steps[i].after-b->steps[i].before)>ABS(largest->after-largest->before))
                largest=&b->steps[i];
        if(largest && b->hit)
            strnfmt(buf,size,"%s: %d -> %d%s",largest->label,largest->before,largest->after,
                largest->exact ? "." : " (formula unrecorded).");
        else strnfmt(buf,size,"%s",melee_calc_explanation(b));
    }
}

int melee_calc_live_blow(const melee_calc_action_t *a)
{
    int i, selected=0;
    if(a) for(i=0;i<a->count;++i) if(a->blows[i].hit && a->blows[i].kind!=RECKON_ROLL) selected=i;
    return selected;
}

const melee_calc_action_t *melee_calc_get(int age)
{
    if (age < 0 || age >= _count) return NULL;
    return &_history[(_head + MELEE_CALC_HISTORY - 1 - age) % MELEE_CALC_HISTORY];
}

static unsigned _live_latest, _live_selected;
static int _live_result;

void reckoner_live_current(int *age,int *blow,int *position,int *total,int *omitted)
{
    const melee_calc_action_t *newest=melee_calc_get(0), *a;
    int last=0,i,j,n=0,pos=0;
    *age=*blow=*position=*total=*omitted=0;
    if(!newest) return;
    if(_live_latest!=newest->serial)
    {
        const melee_calc_action_t *selected=newest;
        int candidate=0;
        _live_latest=newest->serial;
        while(!selected->count && (a=melee_calc_get(++candidate)) && a->turn==newest->turn)
            selected=a;
        _live_selected=selected->serial;
        _live_result=melee_calc_live_blow(selected);
    }
    while((a=melee_calc_get(last+1)) && a->turn==newest->turn) ++last;
    for(i=last;i>=0;--i)
    {
        a=melee_calc_get(i); *omitted+=a->omitted;
        for(j=0;j<a->count;++j)
        {
            ++n;
            if(a->serial==_live_selected && j==_live_result)
            { *age=i; *blow=j; pos=n; }
        }
    }
    *total=n; *position=pos;
}
void reckoner_live_move(int delta)
{
    int age,blow,pos,total,omitted,i,j,n=0,last=0,wanted;
    const melee_calc_action_t *a,*newest=melee_calc_get(0);
    reckoner_live_current(&age,&blow,&pos,&total,&omitted);
    if(!newest || !total) return;
    wanted=MAX(1,MIN(total,pos+delta));
    while((a=melee_calc_get(last+1)) && a->turn==newest->turn) ++last;
    for(i=last;i>=0;--i)
    {
        a=melee_calc_get(i);
        for(j=0;j<a->count;++j) if(++n==wanted)
        { _live_selected=a->serial; _live_result=j; return; }
    }
}

static void _working(const melee_calc_blow_t *b, doc_ptr doc)
{
    int j;
    doc_insert(doc, melee_calc_explanation(b)); doc_newline(doc);
    if(b->amount_hidden) { doc_printf(doc,"%s.\n",b->visible ? b->outcome : "Result concealed"); return; }
    spell_calc_details(b,doc);
    if(b->kind==RECKON_DEFENCE)
    { doc_printf(doc,"\n%s.\n",b->outcome); return; }
    if(b->kind==RECKON_CAST)
    {
        doc_printf(doc,"\nDraw %d in 0..99; failure if draw < %d. %s\n",b->gate,b->power,b->hit ? "Succeeded." : "Failed.");
        return;
    }
    if (b->hit_checked && b->armor_known && b->player_known && b->visible && b->supported)
    {
        if (b->gate >= 0) doc_printf(doc, "\nHit check: first draw <color:y>%d</color> in 0..99.\n", b->gate);
        if (b->gate >= 0 && b->gate < 10)
            doc_insert(doc, b->gate < 5 ? "0..4: automatic hit.\n" : "5..9: automatic miss.\n");
        else if (b->hit_trait)
            doc_insert(doc, "A recorded 1-in-20 character-trait check caused the miss.\n");
        else if (b->power <= 0) doc_insert(doc, "Non-positive attack power: no roll, automatic miss.\n");
        else doc_printf(doc, "Attack draw <color:y>%d</color> in 0..%d %s armour threshold %d.\n"
            "Threshold = %d x 3 / 4 = %d (integer division).\n",
            b->hit_roll, b->power-1, b->hit ? ">=" : "<", b->ac*3/4, b->ac, b->ac*3/4);
    }
    else if(b->kind<=RECKON_UNARMED || b->kind==RECKON_SHOT || b->kind==RECKON_THROW) doc_insert(doc, "\nHit-check inputs concealed or outside this trace.\n");
    if (!b->hit || !b->visible || !b->player_known || !b->target_known)
    {
        if(b->visible && b->hit) doc_printf(doc,"Resolved %s: %d.\n",melee_calc_unit(b),b->damage);
        return;
    }
    if(b->kind==RECKON_EFFECT && !b->spell_source)
    {
        if(b->dice) doc_printf(doc,"Recorded effect power: %d.\n",b->base);
        return;
    }
    if (b->dice && !b->dd) doc_printf(doc,"\nStarting %s: %d.\n",melee_calc_unit(b),b->base);
    if (b->dice && b->dd)
    {
        doc_printf(doc, "\n%s dice: <color:y>%dd%d</color> = ",
            b->kind==RECKON_ROLL ? "Spell" : "Damage", b->dd, b->ds);
        for (j=0; j<MIN(b->dd, MELEE_CALC_DICE); ++j)
            doc_printf(doc, "%s%d", j ? " + " : "", b->faces[j]);
        if (b->dd > MELEE_CALC_DICE) doc_insert(doc, " + [remaining dice omitted]");
        doc_printf(doc, " = <color:y>%d</color>.\n", b->base);
    }
    if(b->kind==RECKON_ROLL)
    {
        doc_printf(doc,"Dice average: %d.%d before modifiers.\n",
            b->dd*(b->ds+1)/2, (b->dd*(b->ds+1)%2)*5);
        return;
    }
    if (b->crit_checked && b->supported)
    {
        if (b->crit_forced) doc_insert(doc, "Critical: guaranteed by the active attack rule.\n");
        else doc_printf(doc, "Critical draw: %d in 1..%d; needs <= %d.\n", b->crit_roll, b->crit_range, b->crit_power);
        if (b->quality_range)
            doc_printf(doc, b->quality_product ? "Quality: weight %d x draw %d (1..%d).\n" : "Quality: adjusted weight %d + draw %d (1..%d).\n",
                b->quality_weight, b->quality_roll, b->quality_range);
    }
    doc_insert(doc,"\n<color:U>Calculation chain, in resolution order</color>\n");
    for (j=0;j<b->step_count;++j)
    {
        const melee_calc_step_t *s=&b->steps[j];
        doc_printf(doc,"%s: ",s->label);
        if (!s->exact)
            doc_printf(doc,"%d -> %d (boundary only; formula not recorded).\n",s->before,s->after);
        else if (s->subtract)
            doc_printf(doc,"%d %s (%d x %d / %d) = %d.\n",s->before,
                s->mul<0 ? "+" : "-",s->before,ABS(s->mul),s->div,s->after);
        else if (s->mul==1 && s->div==1 && !s->bias)
            doc_printf(doc,"%d %+d = %d.\n",s->before,s->add,s->after);
        else if (s->bias)
            doc_printf(doc,"(%d x %d + %d) / %d %+d = %d.\n",s->before,s->mul,s->bias,s->div,s->add,s->after);
        else
            doc_printf(doc,"%d x %d / %d %+d = %d.\n",s->before,s->mul,s->div,s->add,s->after);
    }
    if(b->steps_omitted) doc_insert(doc,"Further steps omitted (record limit).\n");
    doc_printf(doc,"\n<color:%c>%s</color> Resolved %s: %d.\n",
        melee_calc_complete(b)?'G':'y',melee_calc_complete(b)?"Complete recorded chain.":"Partial trace.",melee_calc_unit(b),b->damage);
    doc_insert(doc,"Division uses the engine's integer rounding. Unchanged stages are omitted.\n");
    if(b->dd) doc_printf(doc, "Dice average: %d.%d before modifiers.\n",
        b->dd*(b->ds+1)/2, (b->dd*(b->ds+1)%2)*5);
}

void melee_calc_document(const melee_calc_action_t *a, doc_ptr doc)
{
    int i;
    doc_printf(doc, "<color:U>Past Reckonings - calculations - turn %d</color>\n\n", a->turn);
    doc_insert(doc, "Recorded calculations only; secondary effects may be partial.\nOutgoing damage may exceed remaining HP. Unlinked spell rolls remain separate observations.\n\n");
    for (i=0; i<a->count; ++i)
    {
        const melee_calc_blow_t *b=&a->blows[i];
        doc_printf(doc, "<color:U>%s %d: %s</color>\n%s -> %s\n", reckoner_result_kind(b), i+1, b->weapon, b->identity.actor, b->target);
        if (!b->finished) doc_insert(doc, "Unfinished record.\n");
        else _working(b, doc);
        doc_newline(doc);
    }
    if (a->other) doc_insert(doc, "Some secondary effects may be unrecorded.\n");
    if (a->omitted) doc_printf(doc, "%d additional events omitted (record limit).\n", a->omitted);
}
