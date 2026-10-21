/* Isolated headless checks. The fixture is also compiled against the previous
 * engine, so identical observer-on/off results alone cannot hide a regression.
 * Copyright (c) 2026 John Horton. Angband licence: see LICENSE. */
#include "scottish.h"

extern void reckoner_test_rage(int which);
extern bool _evaporate_aux(obj_ptr obj);
extern void reckoner_test_psion(int which, int power);
extern void reckoner_test_borrowed(int which, mon_ptr target);

static u32b _rng(void)
{
    int i; u32b h=2166136261U;
    h=(h^Rand_quick)*16777619U; h=(h^Rand_value)*16777619U;
    h=(h^Rand_place)*16777619U;
    for(i=0;i<RAND_DEG;++i) h=(h^Rand_state[i])*16777619U;
    return h;
}

static int _throw_test_damage(py_throw_ptr c,int dam) { return dam*3/2+7; }
static int _inspect(int recording, mon_ptr mon, int *checks, int *statuses);

/* Actual monster blows, HP defences and projection decisions. This fixture is
 * also built against the previous engine; only observer assertions are gated. */
void test_reckoner_defence(int recording)
{
    extern mon_spell_cast_t _current;
    int i,j,idx,failures=0,checks=0,statuses=0,hits=0,misses=0,repelled=0;
    int reflected=0,dodged=0,turned=0,blocked=0,hidden=0,exact=0,escaped=0,equal=0;
    int origin_y=py,origin_x=px,old_more=auto_more_state;
    bool old_lore=easy_lore;
    monster_race race=r_info[4];
    mon_spell_cast_t old_cast=_current;
    obj_ptr weapon;
    mon_ptr mon;
    for(i=m_max-1;i>0;--i) if(m_list[i].r_idx) delete_monster_idx(i);
    for(i=-4;i<=4;++i) for(j=-8;j<=8;++j)
    { cave_set_feat(py+i,px+j,feat_floor); cave[py+i][px+j].info|=CAVE_GLOW|CAVE_MARK; }
    idx=place_monster_one(0,py,px+1,4,0,PM_NO_KAGE);
    weapon=equip_obj(equip_find_first(object_is_melee_weapon));
    if(!idx || !weapon) { printf("defence failures  1\n"); return; }
    mon=&m_list[idx];
    for(i=1;i<=equip_max();++i)
    { obj_ptr obj=equip_obj(i); if(obj) { obj->ident|=IDENT_KNOWN; obj_flags(obj,obj->known_flags); } }
    handle_stuff();
    melee_calc_recording((recording&1)!=0);
    for(i=0;i<1800;++i)
    {
        int group=i/40,old_hp,lost=0,prior_failures=failures;
        mon_spell_t spell={0};
        auto_more_state=AUTO_MORE_SKIP_ALL; easy_lore=TRUE;
        py=origin_y; px=origin_x;
        p_ptr->pclass=CLASS_WARRIOR; p_ptr->lev=35;
        p_ptr->chp=9000; p_ptr->mhp=10000; p_ptr->csp=p_ptr->msp=500;
        p_ptr->ac=p_ptr->dis_ac=30; p_ptr->to_a=p_ptr->dis_to_a=0;
        p_ptr->image=p_ptr->blind=p_ptr->confused=p_ptr->paralyzed=p_ptr->stun=0;
        p_ptr->protevil=p_ptr->invuln=p_ptr->wraith_form=p_ptr->multishadow=0;
        p_ptr->tim_foresight=p_ptr->tim_transcendence=p_ptr->tim_spell_turning=0;
        p_ptr->tim_nimble_dodge=p_ptr->tim_spell_reaction=0;
        p_ptr->reflect=p_ptr->shero=p_ptr->sutemi=0;
        p_ptr->special_defense=p_ptr->special_attack=0;
        p_ptr->duelist_target_idx=0; p_ptr->magic_num1[9]=p_ptr->magic_num2[9]=0;
        p_ptr->spell_turned=FALSE; game_turn=30;
        p_ptr->update=p_ptr->redraw=p_ptr->window=0;
        weapon->ident|=IDENT_KNOWN;
        r_info[4]=race; r_info[4].level=12; r_info[4].flags3&=~RF3_EVIL;
        memset(r_info[4].blows,0,sizeof(r_info[4].blows));
        r_info[4].blows[0].method=RBM_HIT; r_info[4].blows[0].power=30;
        r_info[4].blows[0].effects[0].effect=RBE_HURT;
        r_info[4].blows[0].effects[0].dd=3; r_info[4].blows[0].effects[0].ds=6;
        mon->hp=mon->maxhp=30000; mon->mpower=1000; mon->ml=TRUE;
        mon->ap_r_idx=mon->r_idx; mon->mflag2&=~MFLAG2_FUZZY;
        mon->anger=0; set_hostile(mon); set_monster_csleep(idx,0); set_monster_stunned(idx,0);
        p_ptr->update=p_ptr->redraw=p_ptr->window=0;
        memset(&_current,0,sizeof(_current));
        if(group==1) p_ptr->ac=180;
        if(group==2)
        { r_info[4].level=4; r_info[4].blows[0].power=0; set_monster_stunned(idx,100); p_ptr->ac=6; } /* 4 > 4 is false. */
        if(group==3) p_ptr->ac=3000;
        if(group==4) set_monster_stunned(idx,70);
        if(group==5) { p_ptr->pclass=CLASS_DUELIST; p_ptr->duelist_target_idx=idx; }
        if(group==6) p_ptr->special_attack=ATTACK_SUIKEN;
        if(group==7) { mon->ml=FALSE; mon->mflag2|=MFLAG2_FUZZY; }
        if(group==8) p_ptr->image=10;
        if(group==9) mon->ap_r_idx=5;
        if(group==10) { easy_lore=FALSE; r_info[4].r_tkills=r_info[4].r_pkills=0; }
        if(group==11 || group==29 || group==34) weapon->ident&=~IDENT_KNOWN;
        if(group==12) mon->mpower=1700;
        if(group==13 || group==14 || group==15 || group==16)
        {
            p_ptr->protevil=10;
            if(group!=15) r_info[4].flags3|=RF3_EVIL;
            if(group==14) r_info[4].level=100;
            if(group==16) { easy_lore=FALSE; r_info[4].r_flags3&=~RF3_EVIL; }
        }
        if(group==17) r_info[4].blows[0].effects[0].effect=0;
        if(group==18 || group==29) p_ptr->tim_foresight=10;
        if(group==19)
        { p_ptr->pclass=CLASS_PSION; p_ptr->magic_num1[9]=10; p_ptr->magic_num2[9]=3; p_ptr->tim_foresight=10; }
        if(group==20 || group==21 || group==28) p_ptr->invuln=10;
        if(group==22 || group==23) p_ptr->multishadow=10;
        if(group==23) game_turn=31;
        if(group==24) p_ptr->wraith_form=10;
        if(group==25) p_ptr->special_defense=KATA_MUSOU;
        if(group==26) { p_ptr->tim_transcendence=10; p_ptr->csp=3; }
        if(group==27) { p_ptr->wraith_form=10; p_ptr->tim_transcendence=10; p_ptr->csp=3; }
        if(group>=30 && group<=39)
        {
            spell.id.type=MST_BOLT; spell.id.effect=GF_FIRE;
            _current.mon=mon; _current.race=&r_info[4]; _current.spell=&spell;
            _current.flags=MSC_SRC_MONSTER|MSC_DEST_PLAYER; _current.dest=point(px,py);
            strcpy(_current.name,"Test attacker");
            if(group<=34) p_ptr->reflect=TRUE;
            if(group==31) { mon->ml=FALSE; mon->mflag2|=MFLAG2_FUZZY; }
            if(group==32) p_ptr->image=10;
            if(group==33) { p_ptr->reflect=FALSE; p_ptr->special_defense=KATA_FUUJIN; }
            if(group==35 || group==36)
            { spell.flags=MSF_INNATE; p_ptr->tim_nimble_dodge=10; p_ptr->open_terrain_ct=group==35 ? 7 : 0; }
            if(group>=37) { p_ptr->tim_spell_turning=10; p_ptr->shero=group==38 ? 10 : 0; }
            if(group==39) p_ptr->lev=50;
        }
        if(group>=40)
        {
            p_ptr->special_defense=NINJA_KAWARIMI;
            if(group==41) p_ptr->blind=10;
            if(group==42) p_ptr->stun=190;
        }
        old_hp=p_ptr->chp;
        melee_calc_reset(); melee_calc_begin();
        if(group<18 || (group>=40 && group<=42)) make_attack_normal(idx);
        else if(group<30)
            take_hit(group==21 ? DAMAGE_FORCE : group==28 ? DAMAGE_LOSELIFE : DAMAGE_ATTACK,
                group==24 || group==25 ? 1 : 31,"Defence fixture");
        else project(group==43 ? 0 : idx,0,py,px,37,GF_FIRE,PROJECT_KILL|PROJECT_JUMP|PROJECT_REFLECTABLE);
        melee_calc_end();
        printf("defence-step %d hp=%d sp=%d mon=%d energy=%d pos=%d,%d rng=%08lx state=%lu,%d,%d,%d,%d,%d,%ld\n",
            i,p_ptr->chp,p_ptr->csp,mon->hp,energy_use,py,px,(unsigned long)_rng(),
            (unsigned long)p_ptr->special_defense,p_ptr->spell_turned,p_ptr->stun,p_ptr->cut,p_ptr->slow,mon->anger,(long)p_ptr->exp);
#ifdef RECKONER_DEFENCE
        if(recording&1)
        {
            const melee_calc_action_t *a=melee_calc_get(0);
            failures+=_inspect(recording,mon,&checks,&statuses);
            if(a) for(j=0;j<a->count;++j)
            {
                const melee_calc_blow_t *b=&a->blows[j];
                if(b->kind==RECKON_INCOMING) { lost+=b->damage; if(!b->damage) ++blocked; }
                if(melee_calc_complete(b)) ++exact;
                if(!b->visible || !b->player_known || !b->target_known)
                {
                    ++hidden;
                    if(b->dice || b->base || b->step_count || b->detail_count) ++failures;
                }
                if(b->kind==RECKON_DEFENCE)
                {
                    int values[4]; cptr labels[4]; char text[120];
                    if(group==34 && streq(b->weapon,"Reflection")) ++failures;
                    if(b->damage || b->dice || reckoner_flow(b,values,labels)) ++failures;
                    reckoner_result(b,text,sizeof(text));
                    if(!streq(text,b->outcome)) ++failures;
                    if(streq(text,"Hit allowed")) ++hits;
                    if(streq(text,"Attack missed")) ++misses;
                    if(streq(text,"Attack repelled")) ++repelled;
                    if(streq(text,"Attack reflected")) ++reflected;
                    if(streq(text,"Attack dodged")) ++dodged;
                    if(streq(text,"Spell turned back")) ++turned;
                    if(streq(text,"Escaped before attack")) ++escaped;
                    /* Recompute verdicts from the captured draws, separately
                     * from the observer's comparison result and UI wording. */
                    if(b->player_known && b->target_known)
                    {
                        int n,gate=-1,attack_draws=0,power=-1;
                        for(n=0;n<b->detail_count;++n)
                        {
                            const reckoner_detail_t *d=&b->details[n];
                            cptr success=NULL;
                            if(streq(d->label,"Melee hit gate")) gate=d->left;
                            if(streq(d->label,"Level + blow power") || streq(d->label,"Stun penalty")) power=d->right;
                            if(streq(d->label,"Melee attack draw"))
                            { ++attack_draws; if(d->low!=1 || d->high!=power) ++failures; }
                            if(streq(d->label,"Attack draw exceeds armour")) success="Hit allowed";
                            if(streq(d->label,"Protection repels")) success="Attack repelled";
                            if(streq(d->label,"Reflection succeeds")) success="Attack reflected";
                            if(streq(d->label,"Nimble dodge succeeds")) success="Attack dodged";
                            if(streq(d->label,"Spell turning succeeds")) success="Spell turned back";
                            if(success)
                            {
                                bool expected=streq(d->op,">") ? d->left>d->right :
                                    streq(d->op,"!=") ? d->left!=d->right :
                                    streq(d->op,"<") ? d->left<d->right : d->left<=d->right;
                                if(streq(text,success)!=expected) ++failures;
                                printf("effect-check %d %s %d %d\n",d->left,d->op,d->right,streq(text,success));
                            }
                        }
                        if(gate>=0 && gate<10)
                        {
                            if(attack_draws || streq(text,"Hit allowed")!=(gate<5)) ++failures;
                            printf("effect-check %d < 5 %d\n",gate,streq(text,"Hit allowed"));
                        }
                    }
                    if(group>=7 && group<=12 && streq(b->weapon,"Melee hit check") && b->detail_count) ++failures;
                    if(group==2 && b->detail_count)
                    {
                        int n;
                        for(n=0;n<b->detail_count;++n)
                            if(streq(b->details[n].label,"Attack draw exceeds armour"))
                            { if(b->details[n].result) ++failures; if(b->details[n].left==b->details[n].right) ++equal; }
                    }
                }
            }
            if(lost!=old_hp-p_ptr->chp)
            { printf("defence-mismatch %d recorded=%d actual=%d\n",i,lost,old_hp-p_ptr->chp); ++failures; }
            if(melee_calc_incoming()) ++failures;
        }
#endif
        if(failures!=prior_failures) printf("defence-check-failure %d count=%d\n",i,failures-prior_failures);
    }
    printf("defence coverage  hit %d miss %d repelled %d reflected %d dodged %d turned %d blocked %d concealed %d exact %d escaped %d checks %d equality %d\n",
        hits,misses,repelled,reflected,dodged,turned,blocked,hidden,exact,escaped,checks,equal);
    printf("defence failures  %d\n",failures);
    _current=old_cast; r_info[4]=race; auto_more_state=old_more; easy_lore=old_lore;
    weapon->ident|=IDENT_KNOWN;
}

#ifdef RECKONER_RANGED
#ifdef RECKONER_REVIEW
static int _live_selection_checks(void)
{
    int i,age,blow,pos,total,omitted,failures=0;
    s32b turn=player_turn;
    unsigned first;
    u32b rng=_rng();
    melee_calc_reset();
    for(i=0;i<3;++i)
    {
        melee_calc_blow_t *b;
        melee_calc_begin();
        b=melee_calc_value(RECKON_INCOMING,NULL,"Selection fixture",i+1);
        melee_calc_finish(b,TRUE,i+1); melee_calc_end();
    }
    first=melee_calc_get(0)->serial;
    if(melee_calc_count()!=1) ++failures;
    reckoner_live_current(&age,&blow,&pos,&total,&omitted);
    if(total!=3 || pos!=3 || age!=0) ++failures;
    reckoner_live_move(-1); reckoner_live_current(&age,&blow,&pos,&total,&omitted);
    if(age!=0 || blow!=1 || pos!=2) ++failures;
    reckoner_live_move(-999); reckoner_live_current(&age,&blow,&pos,&total,&omitted);
    if(age!=0 || blow!=0 || pos!=1 || melee_calc_get(age)->serial!=first) ++failures;
    melee_calc_begin(); melee_calc_other(); melee_calc_end();
    reckoner_live_current(&age,&blow,&pos,&total,&omitted);
    if(age!=0 || pos!=3 || total!=3) ++failures;
    ++player_turn;
    melee_calc_begin();
    for(i=0;i<30;++i)
        melee_calc_finish(melee_calc_value(RECKON_INCOMING,NULL,"Overflow fixture",i),TRUE,i);
    melee_calc_end();
    reckoner_live_current(&age,&blow,&pos,&total,&omitted);
    if(total!=24 || omitted!=6 || age!=0) ++failures;
    reckoner_live_move(-999); reckoner_live_current(&age,&blow,&pos,&total,&omitted);
    if(age!=0 || blow!=0 || pos!=1) ++failures;
    for(i=0;i<50;++i)
    { ++player_turn; melee_calc_begin(); melee_calc_finish(melee_calc_value(RECKON_HEAL,NULL,"Ring fixture",i),TRUE,i); melee_calc_end(); }
    reckoner_live_move(-999); reckoner_live_current(&age,&blow,&pos,&total,&omitted);
    if(melee_calc_count()!=48 || age!=0 || total!=1 || pos!=1 || melee_calc_get(47)->serial<=first) ++failures;
    for(i=1;i<48;++i) if(melee_calc_get(i)->serial>=melee_calc_get(i-1)->serial) ++failures;
    player_turn=turn;
    if(_rng()!=rng) ++failures;
    return failures;
}
#else
static int _live_selection_checks(void)
{
    int i,age,blow,pos,total,omitted,failures=0;
    s32b turn=player_turn;
    unsigned first;
    u32b rng=_rng();
    melee_calc_reset();
    for(i=0;i<3;++i)
    {
        melee_calc_blow_t *b;
        melee_calc_begin();
        b=melee_calc_value(RECKON_INCOMING,NULL,"Selection fixture",i+1);
        melee_calc_finish(b,TRUE,i+1); melee_calc_end();
    }
    first=melee_calc_get(2)->serial;
    reckoner_live_current(&age,&blow,&pos,&total,&omitted);
    if(total!=3 || pos!=3 || age!=0) ++failures;
    reckoner_live_move(-1); reckoner_live_current(&age,&blow,&pos,&total,&omitted);
    if(age!=1 || pos!=2) ++failures;
    reckoner_live_move(-999); reckoner_live_current(&age,&blow,&pos,&total,&omitted);
    if(age!=2 || pos!=1 || melee_calc_get(age)->serial!=first) ++failures;
    melee_calc_begin(); melee_calc_other(); melee_calc_end();
    reckoner_live_current(&age,&blow,&pos,&total,&omitted);
    if(age!=1 || pos!=3 || total!=3) ++failures;
    ++player_turn;
    melee_calc_begin();
    for(i=0;i<30;++i)
        melee_calc_finish(melee_calc_value(RECKON_INCOMING,NULL,"Overflow fixture",i),TRUE,i);
    melee_calc_end();
    reckoner_live_current(&age,&blow,&pos,&total,&omitted);
    if(total!=24 || omitted!=6 || age!=0) ++failures;
    reckoner_live_move(-999); reckoner_live_current(&age,&blow,&pos,&total,&omitted);
    if(age!=0 || blow!=0 || pos!=1) ++failures;
    for(i=0;i<50;++i)
    { melee_calc_begin(); melee_calc_finish(melee_calc_value(RECKON_HEAL,NULL,"Ring fixture",i),TRUE,i); melee_calc_end(); }
    reckoner_live_move(-999); reckoner_live_current(&age,&blow,&pos,&total,&omitted);
    if(age!=47 || total!=48 || pos!=1 || melee_calc_get(age)->serial<=first) ++failures;
    for(i=1;i<48;++i) if(melee_calc_get(i)->serial>=melee_calc_get(i-1)->serial) ++failures;
    player_turn=turn;
    if(_rng()!=rng) ++failures;
    return failures;
}
#endif

#endif

void test_reckoner_ranged(int recording)
{
    int i,j,idx,failures=0,hits=0,misses=0,exact=0,hidden=0,criticals=0,checks=0,statuses=0;
    int old_more=auto_more_state,old_class=p_ptr->pclass,old_level=p_ptr->lev,old_sub=p_ptr->psubclass;
    bool old_lore=easy_lore;
    monster_race race=r_info[4];
    mon_ptr mon;
    obj_t bow,ammo;
    for(i=m_max-1;i>0;--i) if(m_list[i].r_idx) delete_monster_idx(i);
    for(i=-4;i<=4;++i) for(j=-8;j<=8;++j)
    { cave_set_feat(py+i,px+j,feat_floor); cave[py+i][px+j].info|=CAVE_GLOW|CAVE_MARK; }
    idx=place_monster_one(0,py,px+3,4,0,PM_NO_KAGE);
    if(!idx) { printf("ranged failures  1\n"); return; }
    mon=&m_list[idx];
    for(i=1;i<=equip_max();++i)
    { obj_ptr obj=equip_obj(i); if(obj) { obj->ident|=IDENT_KNOWN; obj_flags(obj,obj->known_flags); } }
    handle_stuff();
    melee_calc_recording((recording&1)!=0);
    for(i=0;i<800;++i)
    {
        int group=i/20,damage=0,old_hp;
        bool shooting=group<20 || (group>=32 && group<38);
        py_throw_t c={0};
        if(!mon->r_idx)
        {
            idx=place_monster_one(0,py,px+3,4,0,PM_NO_KAGE);
            if(!idx) { ++failures; break; }
            mon=&m_list[idx];
        }
        auto_more_state=AUTO_MORE_SKIP_ALL; easy_lore=TRUE;
        p_ptr->pclass=CLASS_WARRIOR; p_ptr->psubclass=0; p_ptr->lev=35;
        p_ptr->chp=9000; p_ptr->mhp=10000; p_ptr->csp=p_ptr->msp=500;
        p_ptr->stun=p_ptr->image=p_ptr->confused=p_ptr->blind=p_ptr->afraid=0;
        p_ptr->concent=0; p_ptr->ambush=FALSE; p_ptr->painted_target=FALSE;
        p_ptr->skills.thb=220; p_ptr->skill_tht=240;
        p_ptr->shooter_info.to_h=p_ptr->shooter_info.dis_to_h=8;
        p_ptr->shooter_info.to_d=p_ptr->shooter_info.dis_to_d=3;
        p_ptr->shooter_info.base_shot=100; p_ptr->shooter_info.xtra_shot=0;
        p_ptr->update=p_ptr->redraw=p_ptr->window=0;
        r_info[4]=race; r_info[4].ac=40;
        mon->hp=mon->maxhp=30000; mon->ml=TRUE; mon->mpower=1000; mon->ac_adj=0;
        mon->ap_r_idx=mon->r_idx; mon->mflag2&=~MFLAG2_FUZZY;
        set_hostile(mon); set_monster_csleep(idx,0); set_monster_invulner(idx,0,FALSE);
        p_ptr->update=p_ptr->redraw=p_ptr->window=0;
        shoot_hack=SHOOT_NONE; shoot_count=0;
        object_prep(&bow,lookup_kind(TV_BOW,SV_LONG_BOW));
        object_prep(&ammo,lookup_kind(shooting ? TV_ARROW : TV_SWORD,shooting ? 1 : SV_DAGGER));
        bow.ident|=IDENT_KNOWN; bow.to_d=4; bow.to_h=5; obj_flags(&bow,bow.known_flags);
        ammo.ident|=IDENT_KNOWN; ammo.dd=2; ammo.ds=6; ammo.to_d=4; ammo.to_h=5;
        ammo.weight=shooting ? 200 : 900; ammo.number=40;
        if(group==1) { bow.sval=SV_SLING; ammo.tval=TV_SHOT; }
        if(group==2 || group==11) { bow.sval=SV_HEAVY_XBOW; ammo.tval=TV_BOLT; }
        if(group==3 || group==21) add_flag(ammo.flags,OF_BRAND_FIRE);
        if(group==4 || group==22) p_ptr->stun=45;
        if(group==5 || group==23) ammo.to_d=-60;
        if(group==6 || group==24) ammo.ident&=~IDENT_KNOWN;
        if(group==7 || group==25) { p_ptr->image=10; mon->ml=FALSE; }
        if(group==8) p_ptr->concent=4;
        if(group==9) { shoot_hack=SHOOT_SNIPING; set_monster_csleep(idx,100); }
        if(group==10) { p_ptr->painted_target=TRUE; p_ptr->painted_target_idx=idx; p_ptr->painted_target_ct=4; }
        if(group==11) { p_ptr->pclass=CLASS_WEAPONMASTER; p_ptr->psubclass=WEAPONMASTER_CROSSBOWS; p_ptr->lev=45; mon->hp=20000; }
        if(group==12) { p_ptr->concent=3; shoot_hack=SP_DOUBLE; }
        if(group==13) { shoot_hack=SHOOT_PIERCE; }
        if(group==14) { shoot_hack=SHOOT_RAMA; }
        if(group==15) ammo.name2=EGO_AMMO_EXPLODING;
        if(group==16) shoot_hack=SHOOT_ELEMENTAL;
        if(group==17) shoot_hack=SHOOT_SHATTER;
        if(group==18) shoot_hack=SHOOT_TRANQUILIZE;
        if(group==19 || group==26) set_monster_invulner(idx,10,FALSE);
        if(group==27) add_flag(ammo.flags,OF_VORPAL2);
        if(group==28) add_flag(ammo.flags,OF_BRAND_ORDER);
        if(group==29) { p_ptr->ambush=TRUE; set_monster_csleep(idx,100); }
        if(group==30) c.mod_damage_f=_throw_test_damage;
        if(group==31) { c.type=THROW_BOOMERANG; c.back_chance=100; }
        if(group==32) shoot_hack=SHOOT_NEEDLE;
        if(group==33) { shoot_hack=SP_NEEDLE; p_ptr->concent=3; }
        if(group==34) bow.ident&=~IDENT_KNOWN;
        if(group==36) { easy_lore=FALSE; r_info[4].r_pkills=0; }
        if(group==37) p_ptr->skills.thb=-1000;
        if(group==38) p_ptr->skill_tht=-1000;
        if(group==39) { ammo.tval=TV_CORPSE; ammo.sval=SV_CORPSE; }
        obj_flags(&ammo,ammo.known_flags);
        if(group==35) add_flag(ammo.flags,OF_BRAND_FIRE); /* Unlearned flag. */
        target_who=idx; target_row=py; target_col=px+3; command_dir=6;
        old_hp=mon->hp; p_ptr->update=p_ptr->redraw=p_ptr->window=0;
        melee_calc_reset();
        if(shooting) do_cmd_fire_aux2(&bow,&ammo,px,py,px+3,py);
        else
        {
            c.obj=&ammo; c.dir=6; c.range=6; c.mult=235; c.to_d=7;
            if(!py_throw(&c)) ++failures;
        }
        printf("ranged-step %d group=%d hp=%d mon=%d sp=%d energy=%d ammo=%d objects=%d status=%d,%d rng=%08lx\n",
            i,group,p_ptr->chp,mon->hp,p_ptr->csp,energy_use,ammo.number,o_cnt,
            MON_CSLEEP(mon),MON_STUNNED(mon),(unsigned long)_rng());
#ifdef RECKONER_RANGED
        if(recording&1)
        {
            const melee_calc_action_t *a=melee_calc_get(0);
            if(!a || !a->count) { ++failures; continue; }
            for(j=0;j<a->count;++j)
            {
                const melee_calc_blow_t *b=&a->blows[j];
                if(!b->finished) ++failures;
                if(b->kind==RECKON_SHOT || b->kind==RECKON_THROW || b->kind==RECKON_SPELL) damage+=b->damage;
                if(b->hit) ++hits; else ++misses;
                if(b->crit_mul>100) ++criticals;
                if(!b->visible || !b->player_known || !b->target_known)
                { ++hidden; if(b->dice || b->step_count || b->detail_count || b->crit_checked) ++failures; }
                else if(b->hit && (b->kind==RECKON_SHOT || b->kind==RECKON_THROW))
                {
                    if(melee_calc_complete(b)) ++exact;
                    else if(group!=11 && group!=30) { printf("ranged-incomplete %d result=%d\n",i,j); ++failures; }
                }
                if(b->hit_checked && b->armor_known && b->player_known && b->gate>=10 && !b->hit_trait)
                    if(b->hit!=(b->hit_roll>=b->ac*3/4)) ++failures;
                if(b->crit_checked)
                {
                    if(b->crit_roll<1 || b->crit_roll>5000) ++failures;
                    if((b->crit_roll<=b->crit_power)!=(b->crit_mul>100)) ++failures;
                    if(b->quality_range)
                    {
                        int mult=b->quality_product ? 150+b->quality_weight*b->quality_roll*200/2000 :
                            b->quality_weight+b->quality_roll<400 ? 150 : b->quality_weight+b->quality_roll<700 ? 200 : 250;
                        if(mult!=b->crit_mul) ++failures;
                    }
                }
                if(group==32 || group==33)
                    if(b->hit && (b->kind!=RECKON_EFFECT || b->dice || b->damage)) ++failures;
            }
            failures+=_inspect(recording,mon,&checks,&statuses);
            if(group!=7 && group!=25 && group!=32 && group!=33 && damage!=old_hp-mon->hp)
            { printf("ranged-mismatch %d recorded=%d actual=%d\n",i,damage,old_hp-mon->hp); ++failures; }
            /* Browsing all same-turn results never changes the engine or RNG. */
            {
                int age,blow,pos,total,omitted; u32b rng=_rng();
                reckoner_live_current(&age,&blow,&pos,&total,&omitted);
                if(total!=a->count) ++failures;
                reckoner_live_move(-1000); reckoner_live_current(&age,&blow,&pos,&total,&omitted);
                if(pos!=1 || blow!=0) ++failures;
                reckoner_live_move(1000); reckoner_live_current(&age,&blow,&pos,&total,&omitted);
                if(pos!=total || blow!=a->count-1 || _rng()!=rng) ++failures;
            }
        }
#endif
    }
    r_info[4]=race; p_ptr->pclass=old_class; p_ptr->psubclass=old_sub; p_ptr->lev=old_level;
    p_ptr->image=p_ptr->stun=0; easy_lore=old_lore; auto_more_state=old_more; shoot_hack=SHOOT_NONE;
    printf("ranged coverage  hits %d misses %d exact %d hidden %d criticals %d\n",hits,misses,exact,hidden,criticals);
#ifdef RECKONER_RANGED
    if(recording&1) failures+=_live_selection_checks();
#endif
    printf("ranged failures  %d\n",failures);
}
static int _inspect(int recording, mon_ptr mon, int *checks, int *statuses)
{
    int failures=0;
#ifdef RECKONER_EFFECT_CHECKS
    int i,j;
    const melee_calc_action_t *a=melee_calc_get(0);
    if(!(recording&1)) return 0;
    if(!a) return 0; /* a cancelled or irrelevant effect need not make an entry */
    for(i=0;i<a->count;++i)
    {
        const melee_calc_blow_t *b=&a->blows[i];
        if(!b->finished) ++failures;
        if((!b->visible || !b->player_known || !b->target_known) && b->detail_count) ++failures;
        if(b->dice && b->dd>0 && b->dd<=MELEE_CALC_DICE)
        {
            printf("effect-dice %d %d %d",b->dd,b->ds,b->base);
            for(j=0;j<b->dd;++j) printf(" %d",b->faces[j]);
            printf("\n");
        }
        for(j=0;j<b->step_count;++j)
        {
            const melee_calc_step_t *s=&b->steps[j];
            if(s->exact) printf("effect-arith %d %d %d %d %d %d %d\n",
                s->before,s->after,s->mul,s->div,s->bias,s->add,s->subtract);
        }
        for(j=0;j<b->detail_count;++j)
        {
            const reckoner_detail_t *d=&b->details[j];
            if(d->kind==RECKON_DETAIL_CHECK)
            {
                ++*checks;
                printf("effect-check %d %s %d %d\n",d->left,d->op,d->right,d->result);
            }
            if(d->kind==RECKON_DETAIL_STATUS)
            {
                int actual=-1,k;
                bool last=TRUE;
                ++*statuses;
                printf("effect-status %d %d %s\n",d->left,d->right,d->label);
                for(k=j+1;k<b->detail_count;++k)
                    if(streq(d->label,b->details[k].label)) last=FALSE;
                for(k=i+1;k<a->count;++k)
                {
                    int n;
                    for(n=0;n<a->blows[k].detail_count;++n)
                        if(streq(d->label,a->blows[k].details[n].label)) last=FALSE;
                }
                if(streq(d->label,"Confused counter")) actual=MON_CONFUSED(mon);
                if(streq(d->label,"Stunned counter")) actual=MON_STUNNED(mon);
                if(streq(d->label,"Sleep counter")) actual=MON_CSLEEP(mon);
                if(streq(d->label,"Paralyzed counter")) actual=MON_PARALYZED(mon);
                if(streq(d->label,"Slow counter")) actual=MON_SLOW(mon);
                if(streq(d->label,"Antimagic duration")) actual=mon->anti_magic_ct;
                if(last && actual>=0 && mon->r_idx && d->right!=actual) ++failures;
            }
            if(d->kind==RECKON_DETAIL_ROLL)
                printf("effect-roll %d %d %d\n",d->left,d->low,d->high);
            if(d->kind==RECKON_DETAIL_MATH)
                printf("effect-arith %d %d %d %d %d %d 0\n",d->left,d->right,d->mul,d->div,d->bias,d->add);
        }
    }
    { u32b before=_rng(); doc_ptr doc=doc_alloc(72); melee_calc_document(a,doc); doc_free(doc); if(before!=_rng()) ++failures; }
#endif
    return failures;
}

static ang_spell _class_spell(int cls,int index,bool power)
{
    class_t *c;
    p_ptr->pclass=cls;
    c=get_class();
    if(power)
    {
        power_info *p=c->get_powers_fn ? c->get_powers_fn() : c->get_powers;
        return p ? p[index].spell.fn : NULL;
    }
    else
    {
        spell_info *p=c->get_spells_fn ? c->get_spells_fn() : c->get_spells;
        return p ? p[index].fn : NULL;
    }
}

static int _presentation(void)
{
    int failures=0,i,values[4];
    cptr labels[4];
    char buf[240];
    u32b rng=_rng();
    melee_calc_blow_t b={0},copy;
    mon_t mon={0};
    melee_calc_blow_t *record;
    byte glyph=r_info[4].x_char;
    b.kind=RECKON_SPELL; b.visible=b.player_known=b.target_known=TRUE;
    b.finished=b.hit=b.dice=b.spell_source=TRUE;
    b.dd=4; b.ds=6; b.base=12; b.spell_cast=23; b.spell_arrival=12; b.damage=4;
    if(!reckoner_flow(&b,values,labels) || values[0]!=12 || values[1]!=23 || values[2]!=12 || values[3]!=4) ++failures;
    if(!streq(labels[2],"At target")) ++failures;
    reckoner_result(&b,buf,sizeof(buf)); if(!streq(buf,"4 damage dealt")) ++failures;
    copy=b;
    b.kind=RECKON_WEAPON; b.supported=TRUE; b.crit_mul=200;
    b.before_crit=b.base; b.after_crit=b.before_bonus=24; b.after_bonus=b.damage=29;
    if(reckoner_flow(&b,values,labels)!=4 || !streq(labels[1],"Critical")) ++failures;
    b.before_crit=18; /* another modifier precedes the critical */
    if(reckoner_flow(&b,values,labels)!=4 || !streq(labels[1],"Scaling")) ++failures;
    b=copy;
    b.kind=RECKON_HEAL; b.damage=8;
    reckoner_result(&b,buf,sizeof(buf)); if(!streq(buf,"8 HP restored")) ++failures;
    b.kind=RECKON_INCOMING;
    reckoner_result(&b,buf,sizeof(buf)); if(!streq(buf,"8 damage received")) ++failures;
    b.base=22; b.damage=11; b.step_count=1;
    b.steps[0].before=22; b.steps[0].after=11;
    if(reckoner_flow(&b,values,labels)!=2 || values[0]!=22 || values[1]!=11) ++failures;
    b.step_count=2; b.damage=7; b.steps[1].before=11; b.steps[1].after=7;
    if(reckoner_flow(&b,values,labels)!=3 || values[0]!=22 || values[1]!=11 || values[2]!=7) ++failures;
    b.step_count=3; b.damage=5; b.steps[2].before=7; b.steps[2].after=5;
    if(reckoner_flow(&b,values,labels)!=4 || values[0]!=22 || values[1]!=11 || values[2]!=7 || values[3]!=5) ++failures;
    b.kind=RECKON_EFFECT; b.dice=FALSE; b.detail_count=1;
    strcpy(b.details[0].label,"Slow"); b.details[0].kind=RECKON_DETAIL_STATUS;
    b.details[0].left=0; b.details[0].right=8;
    reckoner_result(&b,buf,sizeof(buf)); if(!streq(buf,"Slow applied")) ++failures;
    if(reckoner_flow(&b,values,labels)) ++failures; /* no damage ribbon for counters */
    b.details[0].left=8; b.details[0].right=0;
    reckoner_result(&b,buf,sizeof(buf)); if(!streq(buf,"Slow cleared")) ++failures;
    b.kind=RECKON_SPELL; b.dice=TRUE; b.player_known=FALSE;
    if(reckoner_flow(&b,values,labels)) ++failures;
    reckoner_reason(&b,buf,sizeof(buf)); if(!strstr(buf,"Unidentified equipment")) ++failures;
    b.visible=FALSE;
    reckoner_result(&b,buf,sizeof(buf)); if(!streq(buf,"Result concealed")) ++failures;
    /* Observation snapshots survive later appearances and must never use a
     * disguised creature's real race for presentation. */
    mon.r_idx=5; mon.ap_r_idx=4; mon.ml=TRUE;
    melee_calc_reset(); melee_calc_begin();
    record=melee_calc_value(RECKON_SPELL,&mon,"Snapshot test",12);
    if(record->identity.target_ch!=glyph) ++failures;
    r_info[4].x_char='!';
    if(record->identity.target_ch!=glyph) ++failures;
    reckoner_incoming_source(record,&mon);
    if(record->identity.actor_ch!='!' || !record->identity.incoming) ++failures;
    mon.ml=FALSE; reckoner_incoming_source(record,&mon);
    if(record->identity.actor_ch!='?' || !streq(record->identity.actor,"Unknown source")) ++failures;
    r_info[4].x_char=glyph;
    copy=*record;
    for(i=0;i<10;++i)
    { reckoner_result(record,buf,sizeof(buf)); reckoner_reason(record,buf,sizeof(buf)); reckoner_flow(record,values,labels); }
    if(memcmp(&copy,record,sizeof(copy)) || _rng()!=rng) ++failures;
    melee_calc_end();
    return failures;
}

static int _detail_limits(void)
{
    int failures=0;
#ifdef RECKONER_EFFECT_CHECKS
    int i;
    melee_calc_blow_t *outer,*inner;
    spell_calc_scope_t a,b;
    melee_calc_reset(); melee_calc_recording(TRUE); melee_calc_begin();
    outer=melee_calc_value(RECKON_EFFECT,NULL,"Outer test",0);
    a=spell_calc_scope(outer,1);
    spell_calc_note("Outer only");
    inner=melee_calc_value(RECKON_EFFECT,NULL,"Inner test",0);
    b=spell_calc_scope(inner,2);
    spell_calc_status(1,"Wrong target",0,9);
    spell_calc_note("Inner only");
    if(inner->detail_count!=1 || outer->detail_count!=1) ++failures;
    inner->player_known=FALSE;
    melee_calc_finish(inner,TRUE,0);
    spell_calc_note("Must stay concealed after finish");
    if(inner->detail_count || inner->details[0].label[0]) ++failures;
    spell_calc_unscope(b);
    if(spell_calc_roll_detail(3,0,"Legacy zero sides")!=3) ++failures;
    if(outer->details[1].low!=3 || outer->details[1].high!=3) ++failures;
    for(i=0;i<RECKON_DETAILS+5;++i) spell_calc_note("Bounded observation");
    if(outer->detail_count!=RECKON_DETAILS || !outer->details_omitted) ++failures;
    melee_calc_finish(outer,TRUE,0); spell_calc_unscope(a); melee_calc_end();
#endif
    return failures;
}

void test_reckoner_effects(int recording)
{
    static int types[]={GF_CONFUSION,GF_SOUND,GF_PLASMA,GF_FORCE,GF_INERT,
        GF_SLOW,GF_STUN,GF_OLD_CONF,GF_OLD_SLEEP,GF_OLD_SLOW,GF_PARALYSIS,
        GF_STASIS,GF_STASIS_EVIL,GF_ANTIMAGIC,GF_PSI_BRAIN_SMASH,GF_PSI_EGO_WHIP,
        GF_ELDRITCH_STUN,GF_ELDRITCH_CONFUSE,GF_ELDRITCH_HOWL,GF_PSI,GF_PSI_DRAIN,
        GF_PSI_STORM,GF_DOMINATION,GF_SUBJUGATION,GF_TIME,GF_MIND_BLAST,GF_BRAIN_SMASH,
        GF_CAUSE_1,GF_CAUSE_2,GF_CAUSE_3,GF_CAUSE_4,GF_HAND_DOOM,GF_MANA_CLASH,
        GF_BOMB,GF_ROCKET,GF_ICE,GF_DRAINING_TOUCH,GF_OLD_DRAIN,GF_ELDRITCH_DRAIN,
        GF_LITE_WEAK,GF_LITE,GF_DARK,GF_HOLY_FIRE,GF_HELL_FIRE,GF_DISINTEGRATE,
        GF_CONTROL_ANIMAL,GF_CONTROL_UNDEAD,GF_CONTROL_DEMON,GF_CHARM,
        GF_TURN_ALL,GF_DISP_ALL,GF_DISP_EVIL,GF_OLD_SPEED,GF_OLD_HEAL,GF_DEATH_RAY,GF_DEATH_TOUCH};
    static int incoming[]={GF_CONFUSION,GF_SOUND,GF_PLASMA,GF_FORCE,GF_INERT,
        GF_CHAOS,GF_NEXUS,GF_ICE,GF_LITE,GF_DARK,GF_OLD_CONF,GF_OLD_SLEEP,GF_TURN_ALL,GF_UNLIFE};
    static int classes[][3]={{CLASS_MINDCRAFTER,0,0},{CLASS_MINDCRAFTER,4,0},
        {CLASS_MINDCRAFTER,5,0},{CLASS_MINDCRAFTER,8,0},{CLASS_MINDCRAFTER,11,0},
        {CLASS_MINDCRAFTER,12,0},{CLASS_FORCETRAINER,0,0},
        {CLASS_MIRROR_MASTER,2,0},{CLASS_MIRROR_MASTER,8,0},
        {CLASS_NINJA,6,0},{CLASS_NINJA,13,0},
        {CLASS_CHRONOMANCER,0,0},{CLASS_CHRONOMANCER,5,0},{CLASS_CHRONOMANCER,8,0},
        {CLASS_WARLOCK,0,1},{CLASS_WARLOCK,1,1},{CLASS_WARLOCK,2,1},
        {CLASS_WARLOCK,3,1},{CLASS_WARLOCK,4,1},{CLASS_WARLOCK,5,1}};
    int i,j,k,step=0,failures=0,checks=0,statuses=0;
    int old_class=p_ptr->pclass,old_sub=p_ptr->psubclass,old_level=p_ptr->lev;
    int old_more=auto_more_state;
    bool old_lore=easy_lore;
    monster_race race=r_info[4];
    mon_spells_t dummy_spells={0};
    u32b seed=_rng();
    dummy_spells.freq=37;
    for(i=m_max-1;i>0;--i) if(m_list[i].r_idx) delete_monster_idx(i);
    for(i=-5;i<=5;++i) for(j=-5;j<=5;++j)
    { cave_set_feat(py+i,px+j,feat_floor); cave[py+i][px+j].info|=CAVE_GLOW|CAVE_MARK; }
    for(i=1;i<=equip_max();++i)
    { obj_ptr o=equip_obj(i); if(o) { o->ident|=IDENT_KNOWN; obj_flags(o,o->known_flags); } }
    for(i=0;i<8;++i) for(j=0;j<(i==0 ? sizeof(types)/sizeof(*types) : i==1 ? sizeof(incoming)/sizeof(*incoming) : i==2 ? sizeof(classes)/sizeof(*classes) : i==3 ? 7 : i==4 ? 9 : i==5 ? 4 : i==6 ? 5 : 4);++j)
    for(k=0;k<12;++k)
    {
        int idx;
        mon_ptr mon;
        Rand_quick=FALSE; Rand_place=0; Rand_state_init(seed+step*2654435761U);
        for(idx=0;idx<6;++idx) p_ptr->stat_cur[idx]=p_ptr->stat_max[idx]=18;
        for(idx=m_max-1;idx>0;--idx) if(m_list[idx].r_idx) delete_monster_idx(idx);
        r_info[4]=race; r_info[4].flags1&=~RF1_UNIQUE;
        r_info[4].flags2&=~(RF2_EMPTY_MIND|RF2_WEIRD_MIND);
        r_info[4].flags3=RF3_ANIMAL|RF3_EVIL;
        r_info[4].flagsr=0; r_info[4].level=1+(k%6)*17;
        r_info[4].spells=&dummy_spells;
        p_ptr->pclass=CLASS_MAGE; p_ptr->psubclass=0; p_ptr->lev=40;
        p_ptr->chp=20000; p_ptr->mhp=25000; p_ptr->csp=p_ptr->msp=2000;
        p_ptr->spell_power=k%3-1; p_ptr->to_d_spell=7; p_ptr->tim_blood_rite=0;
        p_ptr->blind=p_ptr->confused=p_ptr->stun=p_ptr->image=p_ptr->paralyzed=p_ptr->slow=0;
        p_ptr->tim_no_spells=p_ptr->multishadow=p_ptr->afraid=p_ptr->invuln=0;
        p_ptr->free_act=k%4; p_ptr->skills.sav=30+k*4;
        for(idx=0;idx<RES_MAX;++idx) p_ptr->resist[idx]=k%3;
        auto_more_state=AUTO_MORE_SKIP_ALL; easy_lore=TRUE;
        if(k==6) r_info[4].flags3|=RF3_NO_CONF|RF3_NO_SLEEP|RF3_NO_STUN|RF3_NO_FEAR;
        if(k==7) r_info[4].flagsr|=RFR_RES_ALL;
        if(k==8) r_info[4].flags2|=RF2_WEIRD_MIND;
        if(k==9) r_info[4].flags2|=RF2_EMPTY_MIND;
        if(k==3) r_info[4].flagsr|=RFR_RES_SHAR|RFR_RES_SOUN|RFR_RES_PLAS;
        idx=place_monster_one(0,py,px+1,4,0,PM_NO_KAGE);
        if(!idx) { ++failures; continue; }
        mon=&m_list[idx]; mon->ml=TRUE; mon->mflag2&=~MFLAG2_FUZZY;
        mon->hp=mon->maxhp=30000; mon->ap_r_idx=mon->r_idx;
        mon->mpower=1000; mon->ac_adj=0;
        if(k==4) r_info[4].flags1|=RF1_UNIQUE;
        memset(mon->mtimed,0,sizeof(mon->mtimed));
        if(k==5) { set_monster_confused(idx,5); set_monster_stunned(idx,25); }
        if(k==10) mon->ml=FALSE;
        if(k==11) equip_obj(equip_find_first(object_is_melee_weapon))->ident&=~IDENT_KNOWN;
        target_who=idx; target_row=py; target_col=px+1; command_dir=6;
        project_length=0;
        melee_calc_reset(); melee_calc_recording((recording&1)!=0); melee_calc_begin();
        if(i==0)
        {
            int power=types[j]==GF_PSI_BRAIN_SMASH || types[j]==GF_PSI_EGO_WHIP ? 1+k%5 : 37+k*13;
#ifdef RECKONER_SPELL_CHAIN
            reckoner_project(0,0,py,px+1,spell_calc_fixed(power),types[j],PROJECT_KILL|PROJECT_JUMP,"Effect verification");
#else
            project(0,0,py,px+1,power,types[j],PROJECT_KILL|PROJECT_JUMP);
#endif
            if(types[j]==GF_PSI_EGO_WHIP) psion_process_monster(idx);
        }
        else if(i==1) gf_affect_p(idx,incoming[j],31+k*3,GF_AFFECT_SPELL);
        else if(i==2)
        {
            ang_spell fn=_class_spell(classes[j][0],classes[j][1],classes[j][2]);
            if(fn) cast_spell(fn); else ++failures;
        }
        else if(i==3) { p_ptr->pclass=CLASS_PSION; reckoner_test_psion(j,1+k%5); }
        else if(i==4) { p_ptr->pclass=CLASS_MONSTER; reckoner_test_borrowed(j,mon); }
        else if(i==5)
        {
            static int spells[]={0,4,7,6};
            int shield_slot=equip_find_obj(TV_SHIELD,SV_ANY), glove_slot=equip_find_obj(TV_GLOVES,SV_ANY);
            obj_ptr shield=shield_slot ? equip_obj(shield_slot) : NULL;
            obj_ptr gloves=glove_slot ? equip_obj(glove_slot) : NULL;
            obj_t saved_shield,saved_gloves;
            if(shield) { saved_shield=*shield; shield->k_idx=0; }
            if(gloves) { saved_gloves=*gloves; gloves->k_idx=0; }
            p_ptr->pclass=CLASS_NECROMANCER;
            do_necromancy_spell(spells[j],SPELL_CAST);
            if(shield) *shield=saved_shield;
            if(gloves) *gloves=saved_gloves;
        }
        else if(i==6) { p_ptr->pclass=CLASS_RAGE_MAGE; reckoner_test_rage(j); }
        else
        {
            static int potions[]={SV_POTION_WATER,SV_POTION_BERSERK_STRENGTH,SV_POTION_CONFUSION,SV_POTION_DETONATIONS};
            obj_t obj;
            p_ptr->pclass=CLASS_ALCHEMIST;
            object_prep(&obj,lookup_kind(TV_POTION,potions[j])); obj.number=1;
            _evaporate_aux(&obj);
        }
        melee_calc_end();
        printf("effect-step %d group=%d case=%d variant=%d hp=%d sp=%d mon=%d,%d status=%d,%d,%d,%d,%d,%d,%d,%d whip=%d,%d anti=%d stats=%d,%d rng=%08lx energy=%d\n",
            step++,i,j,k,p_ptr->chp,p_ptr->csp,mon->r_idx,mon->hp,
            MON_CONFUSED(mon),MON_STUNNED(mon),MON_CSLEEP(mon),MON_PARALYZED(mon),MON_SLOW(mon),
            p_ptr->confused,p_ptr->stun,p_ptr->paralyzed,mon->ego_whip_ct,mon->ego_whip_pow,mon->anti_magic_ct,
            p_ptr->stat_cur[A_INT],p_ptr->stat_cur[A_WIS],(unsigned long)_rng(),energy_use);
        printf("effect-step-state %d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n",
            p_ptr->slow,p_ptr->blind,p_ptr->image,p_ptr->poisoned,p_ptr->cut,p_ptr->afraid,
            p_ptr->invuln,p_ptr->tim_no_spells,MON_FAST(mon),MON_MONFEAR(mon),
            p_ptr->stat_cur[0],p_ptr->stat_cur[1],p_ptr->stat_cur[2],p_ptr->stat_cur[3],p_ptr->stat_cur[4],p_ptr->stat_cur[5]);
        failures+=_inspect(recording,mon,&checks,&statuses);
        equip_obj(equip_find_first(object_is_melee_weapon))->ident|=IDENT_KNOWN;
    }
    r_info[4]=race; p_ptr->pclass=old_class; p_ptr->psubclass=old_sub; p_ptr->lev=old_level;
    p_ptr->image=0; auto_more_state=old_more; easy_lore=old_lore;
    printf("effect coverage  steps %d checks %d statuses %d\n",step,checks,statuses);
    if(recording&1) { failures+=_detail_limits(); failures+=_presentation(); }
    printf("effect failures  %d\n",failures);
}


/* Review regressions run with lore both on and off, and against the saved
 * pre-fix engine. Only observer assertions are conditional on the new API. */
void test_reckoner_review(int recording)
{
    int i,j,idx,failures=0,checks=0,statuses=0,hidden=0,menus=0;
    bool old_lore=easy_lore, old_wizard=p_ptr->wizard;
    int old_class=p_ptr->pclass,old_more=auto_more_state;
    monster_race race=r_info[4];
    static int effects[]={GF_OLD_SLEEP,GF_AWAY_ALL,GF_OLD_CONF,GF_STUN,GF_HAND_DOOM,GF_CAPTURE};
    for(i=1;i<=equip_max();++i)
    { obj_ptr o=equip_obj(i); if(o) { o->ident|=IDENT_KNOWN; obj_flags(o,o->known_flags); } }
    p_ptr->pclass=CLASS_MAGE; p_ptr->lev=40;
    p_ptr->wizard=FALSE; auto_more_state=AUTO_MORE_SKIP_ALL;
    melee_calc_recording((recording&1)!=0);
    /* Listing every magic realm reaches the formerly overflowing Armageddon
     * frame without relying on cast selection or a high-level save file. */
    for(i=1;i<=MAX_MAGIC;++i) for(j=0;j<32;++j)
    {
        do_spell(i,j,SPELL_NAME); do_spell(i,j,SPELL_DESC); do_spell(i,j,SPELL_INFO);
        ++menus;
    }
    for(j=0;j<32;++j)
    {
        do_spell(REALM_MUSIC,j,SPELL_NAME); do_spell(REALM_MUSIC,j,SPELL_DESC);
        do_spell(REALM_MUSIC,j,SPELL_INFO); ++menus;
    }
    printf("review-menu %d rng=%08lx\n",menus,(unsigned long)_rng());
    for(i=-4;i<=4;++i) for(j=-4;j<=4;++j)
    { cave_set_feat(py+i,px+j,feat_floor); cave[py+i][px+j].info|=CAVE_MARK|CAVE_GLOW; }
    for(i=0;i<240;++i)
    {
        mon_ptr mon;
        int which=i%14, variant=(i/14)%4;
        for(idx=m_max-1;idx>0;--idx) if(m_list[idx].r_idx) delete_monster_idx(idx);
        r_info[4]=race; r_info[4].level=37; r_info[4].r_tkills=0; r_info[4].r_xtra1&=~MR1_LORE;
        r_info[4].flags1&=~RF1_UNIQUE; r_info[4].flags3=RF3_ANIMAL; r_info[4].flagsr=0;
        idx=place_monster_one(0,py,px+1,4,0,PM_NO_KAGE);
        if(!idx) { ++failures; continue; }
        mon=&m_list[idx]; mon->hp=137; mon->maxhp=mon->max_maxhp=30000;
        mon->mpower=1000; mon->ml=TRUE; mon->mflag2&=~MFLAG2_FUZZY;
        memset(mon->mtimed,0,sizeof(mon->mtimed)); mon->mtimed[MTIMED_CSLEEP]=37;
        mon->ap_r_idx=variant==2 ? 5 : 4;
        if(variant==3) mon->ml=FALSE;
        easy_lore=variant!=1;
        p_ptr->pclass=CLASS_MAGE; p_ptr->chp=p_ptr->mhp=20000; p_ptr->csp=p_ptr->msp=2000;
        p_ptr->blind=p_ptr->confused=p_ptr->image=p_ptr->stun=0;
        target_who=idx; target_row=py; target_col=px+1; command_dir=6;
        melee_calc_reset(); melee_calc_begin();
        if(which<6) reckoner_project(0,0,py,px+1,spell_calc_fixed(120),effects[which],PROJECT_KILL|PROJECT_JUMP,"Lore review");
        else if(which==6)
        {
            melee_calc_blow_t *b=melee_calc_value(RECKON_EFFECT,mon,"Save review",0);
            spell_calc_scope_t scope=spell_calc_scope(b,idx);
            mon_save_aux(4,61); psion_mon_save_p(4,2); mon_stun(mon,31);
            set_monster_csleep(idx,0);
            melee_calc_finish(b,TRUE,0); spell_calc_unscope(scope);
        }
        else if(which==7) do_spell(REALM_ARMAGEDDON,0,SPELL_CAST);
        else if(which==8) do_spell(REALM_CHAOS,0,SPELL_CAST);
        else if(which==9) py_attack(py,px+1,HISSATSU_KYUSHO);
        else if(which==10) do_spell(REALM_MUSIC,2,SPELL_CAST);
        else if(which==11) do_spell(REALM_MUSIC,3,SPELL_CONT);
        else
        {
            obj_t device={0};
            device.activation.type=which==12 ? EFFECT_BOLT_FIRE : EFFECT_HEAL;
            device.activation.power=40;
            if(which==13) p_ptr->chp=19000;
            do_device(&device,SPELL_CAST,17);
        }
        melee_calc_end();
        printf("review-step %d hp=%d mon=%d,%d sleep=%d stun=%d sp=%d rng=%08lx\n",i,p_ptr->chp,mon->r_idx,mon->hp,MON_CSLEEP(mon),MON_STUNNED(mon),p_ptr->csp,(unsigned long)_rng());
        failures+=_inspect(recording,mon,&checks,&statuses);
#ifdef RECKONER_REVIEW
        if(recording&1)
        {
            const melee_calc_action_t *a=melee_calc_get(0);
            if(a) for(j=0;j<a->count;++j)
            {
                const melee_calc_blow_t *b=&a->blows[j];
                int k;
                if(!b->details_known) for(k=0;k<b->detail_count;++k)
                    if(b->details[k].kind!=RECKON_DETAIL_NOTE && b->details[k].kind!=RECKON_DETAIL_STATUS_PUBLIC) ++failures;
                if(b->amount_hidden)
                { ++hidden; if(b->damage || b->dice || b->step_count || b->detail_count || b->base) ++failures; }
                for(k=0;k<b->detail_count;++k)
                    if(b->details[k].kind==RECKON_DETAIL_STATUS_PUBLIC && (b->details[k].left>1 || b->details[k].right>2)) ++failures;
            }
        }
#endif
    }
#ifdef RECKONER_REVIEW
    if(recording&1)
    {
        melee_calc_blow_t *b;
        spell_calc_t original=spell_calc_fixed(7),branch=spell_calc_add(original,3,"Branch");
        melee_calc_reset(); melee_calc_begin();
        /* reset invalidates handles without changing values */
        b=spell_calc_direct(original,NULL,"Expired reset");
        if(b->spell_source || original.value!=7 || branch.value!=10) ++failures;
        original=spell_calc_fixed(7); branch=spell_calc_add(original,3,"Branch");
        b=spell_calc_direct(original,NULL,"Original");
        if(b->base!=7 || b->step_count || !b->spell_source) ++failures;
        b=spell_calc_direct(branch,NULL,"Branch");
        if(b->base!=7 || b->step_count!=1 || melee_calc_last(b)!=10) ++failures;
        for(i=0;i<300;++i) spell_calc_fixed(99);
        b=spell_calc_direct(original,NULL,"Expired pool");
        if(b->spell_source || b->base!=7) ++failures;
        branch=spell_calc_add(original,5,"Expired branch");
        b=spell_calc_direct(branch,NULL,"Expired branch");
        if(b->spell_source || !b->steps_omitted || branch.value!=12) ++failures;
        melee_calc_end();
        if(!hidden || sizeof(spell_calc_t)>16) ++failures;
        /* The presence of a hit formula must not reveal individual armour
         * adjustments, even when race lore is complete. */
        for(i=0;i<2;++i)
        {
            monster_type subject={0};
            subject.r_idx=subject.ap_r_idx=4; subject.ml=TRUE;
            subject.mpower=i ? 1250 : 1000; subject.ac_adj=i ? 7 : 0;
            easy_lore=TRUE;
            melee_calc_reset(); melee_calc_begin();
            b=melee_calc_blow(&subject,NULL,"Armour privacy",0,0);
            b->hit_checked=TRUE; b->gate=12; b->ac=40; b->power=100; b->hit_roll=17;
            melee_calc_finish(b,TRUE,2); melee_calc_end();
            b=(melee_calc_blow_t *)&melee_calc_get(0)->blows[0];
            if(b->armor_known || b->ac!=-1 || b->power!=-1 || b->gate!=-1 || b->hit_roll!=-1) ++failures;
        }
    }
#endif
    r_info[4]=race; easy_lore=old_lore; p_ptr->wizard=old_wizard;
    p_ptr->pclass=old_class; auto_more_state=old_more;
    printf("review coverage  menus %d hidden %d checks %d\n",menus,hidden,checks);
    printf("review failures  %d\n",failures);
}
