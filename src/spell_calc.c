/* Explicit spell arithmetic, attached to the effect at its call site.
 * Copyright (c) 2026 John Horton. Angband licence: see LICENSE. */
#include "scottish.h"

static const spell_calc_t *_source, *_projection;
static spell_calc_scope_t _scope;
static int _operands[2];

/* Immutable handles avoid large caller stack temporaries, including /Od spell
 * menus. A generation check prevents a long-lived local from borrowing a
 * newer trace after wraparound. Exhaustion drops provenance, never the value. */
#define SPELL_CALC_POOL 128
static struct { unsigned generation; melee_calc_blow_t trace; } _pool[SPELL_CALC_POOL];
static unsigned _generation;

static const melee_calc_blow_t *_trace(const spell_calc_t *c)
{
    if(!c || !c->trace || _pool[c->trace % SPELL_CALC_POOL].generation!=c->trace) return NULL;
    return &_pool[c->trace % SPELL_CALC_POOL].trace;
}

static melee_calc_blow_t *_copy(spell_calc_t *c, const melee_calc_blow_t *source)
{
    melee_calc_blow_t *b;
    if(!++_generation) ++_generation;
    c->trace=_generation;
    b=&_pool[c->trace % SPELL_CALC_POOL].trace;
    if(source) *b=*source;
    else
    {
        memset(b,0,sizeof(*b));
        b->player_known=melee_calc_equipment_known();
        melee_calc_base(b,c->value);
    }
    _pool[c->trace % SPELL_CALC_POOL].generation=c->trace;
    return b;
}

static melee_calc_blow_t *_edit(spell_calc_t *c)
{
    const melee_calc_blow_t *source=_trace(c);
    melee_calc_blow_t *b=_copy(c,source);
    if(!source) b->steps_omitted=TRUE;
    return b;
}

static spell_calc_t _named(spell_calc_t c, cptr name)
{
    melee_calc_blow_t *b=_edit(&c);
    strnfmt(b->weapon,sizeof(b->weapon),"%s",name);
    return c;
}

void spell_calc_reset(void)
{
    _source=_projection=NULL;
    _scope.record=NULL; _scope.target=-1;
    _operands[0]=_operands[1]=0;
    memset(_pool,0,sizeof(_pool));
}

spell_calc_scope_t spell_calc_scope(melee_calc_blow_t *record, int target)
{
    spell_calc_scope_t old=_scope;
    _scope.record=record; _scope.target=target;
    return old;
}
void spell_calc_unscope(spell_calc_scope_t previous) { _scope=previous; }

static reckoner_detail_t *_detail(int kind, cptr label)
{
    melee_calc_blow_t *b=_scope.record;
    reckoner_detail_t *d;
    /* Also guard observations made after damage was finalized. */
    if(!b || !b->visible || !b->player_known || !b->target_known) return NULL;
    if(!b->details_known && kind!=RECKON_DETAIL_NOTE && kind!=RECKON_DETAIL_STATUS_PUBLIC) return NULL;
    if(b->detail_count==RECKON_DETAILS) { b->details_omitted=TRUE; return NULL; }
    d=&b->details[b->detail_count++];
    memset(d,0,sizeof(*d)); d->kind=kind;
    strnfmt(d->label,sizeof(d->label),"%s",label);
    return d;
}
int spell_calc_operand(int side, int value) { _operands[side]=value; return value; }
bool spell_calc_compare(cptr label, bool result, cptr op)
{
    reckoner_detail_t *d=_detail(RECKON_DETAIL_CHECK,label);
    if(d) { d->left=_operands[0]; d->right=_operands[1]; d->result=result; strnfmt(d->op,sizeof(d->op),"%s",op); }
    return result;
}
bool spell_calc_opposed(cptr label,int left_sides,int left_base,cptr op,int right_sides,int right_base,bool left_first)
{
    /* Preserve each released MSVC call site's order for its randint1 operands.
     * Function-wrapping each operand can silently make MSVC reverse this order.
     * This is the Windows release's canonical draw order on all compilers;
     * odds match the old expression, whose operand order was unspecified in C.
     * The previous-engine fixture checks outcomes as well as final RNG state. */
    int right,left;
    if(left_first)
    {
        left=spell_calc_rand1(left_sides,"Left-hand draw")+left_base;
        right=spell_calc_rand1(right_sides,"Right-hand draw")+right_base;
    }
    else
    {
        right=spell_calc_rand1(right_sides,"Right-hand draw")+right_base;
        left=spell_calc_rand1(left_sides,"Left-hand draw")+left_base;
    }
    bool result=streq(op,">=") ? left>=right : streq(op,"<=") ? left<=right :
        streq(op,">") ? left>right : streq(op,"<") ? left<right :
        streq(op,"==") ? left==right : left!=right;
    _operands[0]=left; _operands[1]=right;
    return spell_calc_compare(label,result,op);
}
static int _draw(int result, int low, int high, cptr label)
{
    reckoner_detail_t *d=_detail(RECKON_DETAIL_ROLL,label);
    if(d) { d->left=result; d->low=low; d->high=high; }
    return result;
}
int spell_calc_rand0(int sides,cptr label) { return _draw(randint0(sides),0,MAX(0,sides-1),label); }
int spell_calc_rand1(int sides,cptr label) { return _draw(randint1(sides),1,MAX(1,sides),label); }
int spell_calc_roll_detail(int dd,int ds,cptr label)
{
    int result=damroll(dd,ds);
    int low=MAX(0,dd), high=0;
    char text[80];
    strnfmt(text,sizeof(text),"%s (%dd%d total)",label,dd,ds);
    /* damroll returns s16b, including the inherited narrowing of large sums.
     * randint1(0) yields 1, so zero-sided legacy calls still add one per die. */
    if(dd>0 && MAX(1,ds)>32767/dd)
    {
        low=-32768; high=32767;
        strnfmt(text,sizeof(text),"%s (%dd%d, 16-bit total)",label,dd,ds);
    }
    else if(dd>0) high=dd*MAX(1,ds);
    return _draw(result,low,high,text);
}
void spell_calc_note(cptr text) { _detail(RECKON_DETAIL_NOTE,text); }
int spell_calc_math(cptr label,int before,int mul,int div,int bias,int add)
{
    int result=(before*mul+bias)/div+add;
    reckoner_detail_t *d=_detail(RECKON_DETAIL_MATH,label);
    if(d) { d->left=before; d->right=result; d->mul=mul; d->div=div; d->bias=bias; d->add=add; }
    return result;
}
void spell_calc_status(int target,cptr name,int before,int after)
{
    reckoner_detail_t *d;
    if(target!=_scope.target || before==after) return;
    d=_detail(target>0 ? RECKON_DETAIL_STATUS_PUBLIC : RECKON_DETAIL_STATUS,name);
    if(d)
    {
        /* Recall never exposes individual timers, even with complete lore.
         * Retain only the public direction of the change for monsters. */
        if(target>0) { d->left=before ? 1 : 0; d->right=!after ? 0 : !before || after>before ? 2 : 1; }
        else { d->left=before; d->right=after; }
    }
}
void spell_calc_player_status(cptr name,int before,int after)
{
    if(before==after) return;
    if(_scope.record && _scope.target==0) spell_calc_status(0,name,before,after);
    else if(_scope.record || melee_calc_casting())
    {
        melee_calc_blow_t *b=melee_calc_value(RECKON_EFFECT,NULL,name,0);
        spell_calc_scope_t old=spell_calc_scope(b,0);
        if(b) { b->dice=FALSE; b->supported=FALSE; }
        spell_calc_status(0,name,before,after);
        melee_calc_finish(b,TRUE,0);
        spell_calc_unscope(old);
    }
}
void spell_calc_details(const melee_calc_blow_t *b,doc_ptr doc)
{
    int i;
    bool checks=FALSE,statuses=FALSE;
    if(!b->detail_count && !b->details_omitted) return;
    doc_insert(doc,"\n<color:U>Checks and effects, in resolution order</color>\n");
    for(i=0;i<b->detail_count;++i)
    {
        const reckoner_detail_t *d=&b->details[i];
        switch(d->kind)
        {
        case RECKON_DETAIL_ROLL:
            doc_printf(doc,"%s: draw %d in %d..%d.\n",d->label,d->left,d->low,d->high); break;
        case RECKON_DETAIL_CHECK:
            checks=TRUE;
            doc_printf(doc,"%s: %d %s %d is %s.\n",d->label,d->left,d->op,d->right,d->result ? "true" : "false"); break;
        case RECKON_DETAIL_STATUS:
            statuses=TRUE;
            doc_printf(doc,"%s: %d -> %d (%+d).\n",d->label,d->left,d->right,d->right-d->left); break;
        case RECKON_DETAIL_STATUS_PUBLIC:
            doc_printf(doc,"%s %s (exact counter concealed).\n",d->label,!d->right ? "cleared" : !d->left ? "applied" : d->right>d->left ? "increased" : "reduced"); break;
        case RECKON_DETAIL_NOTE:
            doc_printf(doc,"%s\n",d->label); break;
        case RECKON_DETAIL_MATH:
            if(d->mul==1 && d->div==1 && !d->bias)
                doc_printf(doc,"%s: %d %+d = %d.\n",d->label,d->left,d->add,d->right);
            else if(d->mul==1 && !d->bias)
                doc_printf(doc,"%s: %d / %d %+d = %d.\n",d->label,d->left,d->div,d->add,d->right);
            else doc_printf(doc,"%s: (%d x %d %+d) / %d %+d = %d.\n",d->label,d->left,d->mul,d->bias,d->div,d->add,d->right);
            break;
        }
    }
    if(b->details_omitted) doc_insert(doc,"Further checks/effects omitted (record limit).\n");
    if(checks) doc_insert(doc,"True means the comparison holds; the outcome depends on that check's rule.\n");
    if(statuses) doc_insert(doc,"Status counters are not HP or a number of player turns.\n");
}
const spell_calc_t *spell_calc_source_swap(const spell_calc_t *source)
{
    const spell_calc_t *old=_source;
    _source=source;
    return old;
}
const spell_calc_t *spell_calc_projection_swap(const spell_calc_t *source)
{
    const spell_calc_t *old=_projection;
    _projection=source;
    return old;
}

spell_calc_t spell_calc_fixed(int value)
{
    spell_calc_t c={value,0};
    melee_calc_blow_t *b=_copy(&c,NULL);
    b->supported=TRUE; b->spell_source=TRUE;
    return c;
}
spell_calc_t spell_calc_input(int value)
{
    spell_calc_t c=spell_calc_fixed(value);
    _pool[c.trace % SPELL_CALC_POOL].trace.spell_source=FALSE;
    return c;
}
spell_calc_t spell_calc_missile(const melee_calc_blow_t *record,int value)
{
    spell_calc_t c={value,0};
    melee_calc_blow_t *b=_copy(&c,record);
    if(record)
    {
        b->spell_source=TRUE; b->kind=RECKON_SHOT;
        b->player_known &= record->visible && record->target_known;
        b->hit_checked=FALSE; b->armor_known=FALSE;
    }
    return c;
}
spell_calc_t spell_calc_dice(int dd, int ds)
{
    spell_calc_t c=spell_calc_fixed(0);
    c.value=melee_calc_roll(&_pool[c.trace % SPELL_CALC_POOL].trace,dd,ds);
    return c;
}
spell_calc_t spell_calc_random(int sides)
{
    spell_calc_t c=spell_calc_fixed(0);
    melee_calc_blow_t *b=&_pool[c.trace % SPELL_CALC_POOL].trace;
    /* randint1 returns int; do not narrow it through damroll's s16b result. */
    c.value=randint1(sides);
    melee_calc_base(b,c.value);
    b->dd=1; b->ds=sides; b->faces[0]=c.value;
    return c;
}
spell_calc_t spell_calc_add(spell_calc_t c, int bonus, cptr label)
{
    melee_calc_blow_t *b=_edit(&c);
    int before=c.value;
    c.value+=bonus;
    melee_calc_step(b,label,before,c.value,1,1,0,bonus);
    return c;
}
spell_calc_t spell_calc_scale(spell_calc_t c, int mul, int div)
{
    melee_calc_blow_t *b=_edit(&c);
    int before=c.value;
    c.value=c.value*mul/div;
    melee_calc_step(b,"Spell scaling",before,c.value,mul,div,0,0);
    return c;
}
spell_calc_t spell_calc_power(spell_calc_t c)
{
    c.value=spell_power_record(c.value,_edit(&c));
    return c;
}
spell_calc_t spell_calc_observed(spell_calc_t c, int value)
{
    /* A mismatch is an explicit boundary, never an invented formula. */
    melee_calc_adjustment(_edit(&c),"Before projection",c.value,value);
    c.value=value;
    return c;
}
spell_calc_t spell_calc_cap(spell_calc_t c,int value,cptr label)
{
    melee_calc_step(_edit(&c),label,c.value,value,0,1,0,value);
    c.value=value;
    return c;
}

static melee_calc_blow_t *_record(const spell_calc_t *source, int kind,
    mon_ptr mon, cptr fallback, int value)
{
    const melee_calc_blow_t *trace=_trace(source);
    melee_calc_blow_t *b=melee_calc_value(kind,mon,
        trace && trace->weapon[0] ? trace->weapon : fallback,value);
    if(b && trace)
    {
        melee_calc_blow_t identity=*b;
        *b=*trace;
        b->kind=kind; b->finished=FALSE;
        b->visible=identity.visible; b->target_known=identity.target_known && !trace->amount_hidden;
        b->details_known=identity.details_known;
        b->player_known=identity.player_known && trace->player_known;
        if(!b->crit_checked) b->crit_mul=100;
        b->gate=b->hit_roll=-1;
        strcpy(b->target,identity.target); strcpy(b->weapon,identity.weapon);
        b->identity=identity.identity;
        if(trace->quality_product || trace->kind==RECKON_SHOT)
        {
            b->identity.tool_ch=trace->identity.tool_ch;
            b->identity.tool_attr=trace->identity.tool_attr;
        }
        melee_calc_adjustment(b,"Before target",melee_calc_last(b),value);
    }
    return b;
}
melee_calc_blow_t *spell_calc_target(mon_ptr mon, int value, int type)
{
    gf_info_ptr info=gf_lookup(type);
    melee_calc_blow_t *b=_record(_projection,info && !(info->flags&GFF_ATTACK) ? RECKON_EFFECT : RECKON_SPELL,
        mon,info ? info->name : "Spell effect",value);
    if(b) { b->spell_cast=_projection ? _projection->value : value; b->spell_arrival=value; }
    return b;
}
melee_calc_blow_t *spell_calc_heal(int value)
{
    return _record(_source,RECKON_HEAL,NULL,"Healing",value);
}
melee_calc_blow_t *spell_calc_direct(spell_calc_t c,mon_ptr mon,cptr name)
{
    melee_calc_blow_t *b=_record(&c,RECKON_SPELL,mon,name,c.value);
    if(b) b->spell_cast=b->spell_arrival=c.value;
    return b;
}

int spell_calc_resistance(melee_calc_blow_t *b, int damage, int multiplier)
{
    int scaled=damage*multiplier;
    int roll=randint1(6), divisor=roll+6;
    char label[32];
    strnfmt(label,sizeof(label),"Resistance (d6=%d)",roll);
    melee_calc_step(b,label,damage,scaled/divisor,multiplier,divisor,0,0);
    return scaled/divisor;
}

/* Scope provenance around the unchanged effect implementation. project()
 * consumes it while resolving, so reflections, auras and death callbacks
 * cannot silently borrow the parent's dice. A multi-target effect reuses its
 * original value; it never rolls again for the history. */
#define START() const spell_calc_t *old; bool result; int events; \
    c=_named(c,name); \
    melee_calc_begin(); events=melee_calc_event_count(); \
    old=spell_calc_source_swap(&c)
#define END() spell_calc_source_swap(old); \
    if(melee_calc_event_count()==events) { \
        melee_calc_blow_t *b=_record(&c,RECKON_EFFECT,NULL,name,c.value); \
        if(b) { strnfmt(b->target,sizeof(b->target),"No creature damage recorded"); \
            b->identity.target_ch='.'; b->identity.target_attr=TERM_SLATE; } \
        melee_calc_finish(b,b && b->player_known,b && b->player_known ? c.value : 0); \
    } \
    melee_calc_end(); return result

bool reckoner_fire_ball(int type,int dir,spell_calc_t c,int rad,cptr name)
{ START(); result=fire_ball(type,dir,c.value,rad); END(); }
bool reckoner_fire_ball_aux(int type,int dir,spell_calc_t c,int rad,int flags,cptr name)
{ START(); result=fire_ball_aux(type,dir,c.value,rad,flags); END(); }
bool reckoner_fire_ball_hide(int type,int dir,spell_calc_t c,int rad,cptr name)
{ START(); result=fire_ball_hide(type,dir,c.value,rad); END(); }
bool reckoner_fire_bolt(int type,int dir,spell_calc_t c,cptr name)
{ START(); result=fire_bolt(type,dir,c.value); END(); }
bool reckoner_fire_beam(int type,int dir,spell_calc_t c,cptr name)
{ START(); result=fire_beam(type,dir,c.value); END(); }
bool reckoner_fire_bolt_or_beam(int prob,int type,int dir,spell_calc_t c,cptr name)
{ START(); result=fire_bolt_or_beam(prob,type,dir,c.value); END(); }
bool reckoner_project(int who,int rad,int y,int x,spell_calc_t c,int type,int flags,cptr name)
{ START(); result=project(who,rad,y,x,c.value,type,flags); END(); }
bool reckoner_project_hack(int type,spell_calc_t c,cptr name)
{ START(); result=project_hack(type,c.value); END(); }
bool reckoner_project_hook(int type,int dir,spell_calc_t c,int flags,cptr name)
{ START(); result=project_hook(type,dir,c.value,flags); END(); }
bool reckoner_project_m(int who,int r,int y,int x,spell_calc_t c,int type,int flags,bool see,cptr name)
{
    const spell_calc_t *previous;
    START(); previous=spell_calc_projection_swap(&c);
    result=project_m(who,r,y,x,c.value,type,flags,see);
    spell_calc_projection_swap(previous); END();
}
bool reckoner_binding_field(spell_calc_t c)
{
    const spell_calc_t *previous;
    cptr name="Binding Field";
    START(); previous=spell_calc_projection_swap(&c);
    result=binding_field(c.value);
    spell_calc_projection_swap(previous); END();
}
bool reckoner_lite_area(spell_calc_t c,int rad,cptr name)
{ START(); result=lite_area(c.value,rad); END(); }
bool reckoner_drain_life(int dir,spell_calc_t c,cptr name)
{ START(); result=drain_life(dir,c.value); END(); }
bool reckoner_hp_player(spell_calc_t c,cptr name)
{ START(); result=hp_player(c.value); END(); }
bool reckoner_dispel_monsters(spell_calc_t c,cptr name)
{ START(); result=dispel_monsters(c.value); END(); }
bool reckoner_dispel_evil(spell_calc_t c,cptr name)
{ START(); result=dispel_evil(c.value); END(); }
bool reckoner_dispel_good(spell_calc_t c,cptr name)
{ START(); result=dispel_good(c.value); END(); }
bool reckoner_dispel_undead(spell_calc_t c,cptr name)
{ START(); result=dispel_undead(c.value); END(); }
bool reckoner_dispel_demons(spell_calc_t c,cptr name)
{ START(); result=dispel_demons(c.value); END(); }
bool reckoner_dispel_living(spell_calc_t c,cptr name)
{ START(); result=dispel_living(c.value); END(); }
