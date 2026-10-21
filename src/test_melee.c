/* Headless regression fixture. Never linked into a playable frontend.
 * Copyright (c) 2026 John Horton. Angband licence: see LICENSE. */
#include "scottish.h"
#include <math.h>

static u32b _rng_hash(void)
{
    int i;
    u32b h=2166136261U;
    h=(h^Rand_quick)*16777619U;
    h=(h^Rand_value)*16777619U;
    h=(h^Rand_place)*16777619U;
    for(i=0;i<RAND_DEG;++i) h=(h^Rand_state[i])*16777619U;
    return h;
}


static int _camera_checks(void)
{
    int size,zoom,focus,placement,failures=0;
    for(size=240;size<=1920;size+=240) for(zoom=2;zoom<=160;zoom+=7)
    for(focus=0;focus<200;focus+=3) for(placement=0;placement<4;++placement)
    {
        float low=(placement&1) ? size*.3f : 0;
        float high=(placement&2) ? size*.65f : (float)size;
        float scale=(float)zoom, origin=size*.5f-(focus+.5f)*scale;
        float actual=ui_map_safe_axis(origin,focus,scale,low,high);
        float pixel=actual+focus*scale, margin=MIN(3*scale,MAX(0,high-low-scale)*.25f);
        if(high-low>=scale && (pixel<low+margin-.01f || pixel+scale>high-margin+.01f)) ++failures;
        if(fabsf(actual-ui_map_safe_axis(actual,focus,scale,low,high))>.01f) ++failures;
        if((int)floorf((pixel+scale*.5f-actual)/scale)!=focus) ++failures;
    }
    printf("camera failures  %d\n",failures);
    return failures;
}

static void _test_reckoning_probe(int recording)
{
    int i,j,k,idx,slot,hand,old_class=p_ptr->pclass,old_more=auto_more_state;
    int failures=_camera_checks(),counts[16]={0},exact=0,hidden=0,dice=0;
    bool old_lore=easy_lore;
    mon_ptr mon;
    obj_ptr weapon;
    mon_blow_t old_blow;
    int old_ac=r_info[4].ac;
    for(i=m_max-1;i>0;--i) if(m_list[i].r_idx) delete_monster_idx(i);
    for(i=-3;i<=3;++i) { cave_set_feat(py,px+i,feat_floor); cave[py][px+i].info|=CAVE_GLOW|CAVE_MARK; }
    idx=place_monster_one(0,py,px+1,4,0,PM_NO_KAGE);
    slot=equip_find_first(object_is_melee_weapon);
    if(!idx || !slot) { printf("reckoning failures  1\n"); return; }
    weapon=equip_obj(slot); hand=equip_which_hand(weapon); mon=&m_list[idx];
    for(i=1;i<=equip_max();++i) { obj_ptr obj=equip_obj(i); if(obj) { obj->ident|=IDENT_KNOWN; obj_flags(obj,obj->known_flags); } }
    old_blow=r_info[4].blows[0];
    memset(&r_info[4].blows[0],0,sizeof(mon_blow_t));
    r_info[4].blows[0].method=RBM_HIT; r_info[4].blows[0].power=80;
    r_info[4].blows[0].effects[0].effect=RBE_HURT;
    r_info[4].blows[0].effects[0].dd=3; r_info[4].blows[0].effects[0].ds=6;
    r_info[4].ac=40;
    p_ptr->pclass=CLASS_MONK;
    p_ptr->update|=PU_BONUS|PU_VIEW|PU_LITE|PU_MONSTERS; handle_stuff();
    melee_calc_reset(); melee_calc_recording((recording&1)!=0);
    for(i=0;i<170;++i)
    {
        int group=i/10, old_hp,old_mon_hp,serial;
        const melee_calc_action_t *a;
        ++player_turn;
        auto_more_state=AUTO_MORE_SKIP_ALL; easy_lore=TRUE;
        mon->hp=mon->maxhp=30000; mon->ml=TRUE; mon->mflag2&=~MFLAG2_FUZZY;
        mon->ap_r_idx=mon->r_idx; mon->mpower=1000; mon->ac_adj=0;
        set_hostile(mon); set_monster_csleep(idx,0); set_monster_stunned(idx,0);
        p_ptr->chp=9000; p_ptr->mhp=10000; p_ptr->csp=p_ptr->msp=500;
        p_ptr->image=p_ptr->stun=p_ptr->wraith_form=p_ptr->tim_transcendence=0;
        p_ptr->skills.thn=180; p_ptr->ac=40; p_ptr->to_a=0;
        p_ptr->weapon_info[hand].slot=equip_find_empty_hand();
        p_ptr->weapon_info[hand].wield_how=WIELD_ONE_HAND;
        p_ptr->weapon_info[hand].bare_hands=TRUE;
        p_ptr->weapon_info[hand].base_blow=150; p_ptr->weapon_info[hand].xtra_blow=0;
        p_ptr->weapon_info[hand].to_d=p_ptr->weapon_info[hand].dis_to_d=4;
        p_ptr->weapon_info[hand].to_h=p_ptr->weapon_info[hand].dis_to_h=5;
        p_ptr->monk_lvl=group==0 ? 0 : 20;
        p_ptr->resist[RES_FIRE]=1;
        target_who=idx; target_row=py; target_col=px+1; command_dir=6;
        weapon->ident|=IDENT_KNOWN;
        if(group==10) weapon->ident&=~IDENT_KNOWN;
        if(group==11) { mon->ml=FALSE; p_ptr->image=10; }
        old_hp=p_ptr->chp; old_mon_hp=mon->hp;
        a=melee_calc_get(0); serial=a ? a->serial : 0;
        p_ptr->innate_attack_ct=0;
        if(group==12)
        {
            innate_attack_ptr attack=&p_ptr->innate_attacks[0];
            memset(attack,0,sizeof(innate_attack_t));
            attack->dd=2; attack->ds=6; attack->blows=200;
            attack->weight=100; attack->to_d=3; attack->to_h=20;
            attack->name="Claw"; attack->msg="You claw %s.";
            p_ptr->innate_attack_ct=1; p_ptr->weapon_info[hand].wield_how=WIELD_NONE;
        }
        switch(group)
        {
        case 0: case 1: case 10: case 11: case 12: py_attack(py,px+1,0); break;
        case 2: do_spell(REALM_CHAOS,0,SPELL_CAST); break;
        case 3: cast_spell(magic_missile_spell); break;
        case 4: gf_affect_m(GF_WHO_PLAYER,mon,GF_FIRE,37+i,GF_AFFECT_SPELL); break;
        case 5: do_spell(REALM_LIFE,0,SPELL_CAST); hp_player(2300); break;
        case 6: gf_affect_p(idx,GF_FIRE,41+i,GF_AFFECT_SPELL); break;
        case 7:
            p_ptr->wraith_form=10; p_ptr->tim_transcendence=10; p_ptr->csp=3;
            take_hit(DAMAGE_ATTACK,21+i,"test attack"); break;
        case 8: make_attack_normal(idx); break;
        case 9: project(0,2,py,px+1,43,GF_FIRE,PROJECT_KILL|PROJECT_JUMP); break;
        case 13:
            r_info[4].flagsr|=RFR_RES_FIRE;
            gf_affect_m(GF_WHO_PLAYER,mon,GF_FIRE,57,GF_AFFECT_SPELL);
            r_info[4].flagsr&=~RFR_RES_FIRE; break;
        case 15:
            melee_calc_begin();
            for(j=0;j<30;++j) gf_affect_m(GF_WHO_PLAYER,mon,GF_FIRE,5,GF_AFFECT_SPELL);
            melee_calc_end(); break;
        case 16:
            r_info[4].flags2|=RF2_AURA_FIRE;
            py_attack(py,px+1,0);
            r_info[4].flags2&=~RF2_AURA_FIRE; break;
        case 14:
            p_ptr->resist[RES_FIRE]=100;
            gf_affect_p(idx,GF_FIRE,70,GF_AFFECT_SPELL); break;
        }
        printf("reckoning-step %d hp=%d mon=%d sp=%d energy=%d rng=%08lx status=%d,%d,%d exp=%ld\n",
            i,p_ptr->chp,mon->hp,p_ptr->csp,energy_use,(unsigned long)_rng_hash(),p_ptr->stun,p_ptr->image,MON_STUNNED(mon),(long)p_ptr->exp);
#ifdef RECKONING_EXTENDED
        a=melee_calc_get(0);
        if((recording&1) && a && (int)a->serial!=serial)
        {
            int dealt=0,lost=0;
            if(group==15 && (a->count!=MELEE_CALC_BLOWS || a->omitted!=6)) ++failures;
            for(j=0;j<a->count;++j)
            {
                const melee_calc_blow_t *b=&a->blows[j];
                ++counts[b->kind];
                if(!b->finished) ++failures;
                if(melee_calc_complete(b)) ++exact;
                if(!b->visible || !b->player_known || !b->target_known)
                {
                    ++hidden;
                    if(b->dice || b->step_count || b->base || b->crit_checked) ++failures;
                }
                if(b->dice && b->dd)
                {
                    ++dice;
                    int sum=0;
                    for(k=0;k<MIN(b->dd,MELEE_CALC_DICE);++k)
                    {
                        if(b->faces[k]<1 || b->faces[k]>b->ds) ++failures;
                        sum+=b->faces[k];
                    }
                    if(b->dd<=MELEE_CALC_DICE && sum!=b->base) ++failures;
                }
                if(b->kind==RECKON_UNARMED || b->kind==RECKON_SPELL) dealt+=b->damage;
                if(b->kind==RECKON_INCOMING) lost+=b->damage;
            }
            if((group<5 || group==12 || group==13 || group==16) && dealt!=old_mon_hp-mon->hp) ++failures;
            if((group==6 || group==7 || group==8 || group==16) && lost!=old_hp-p_ptr->chp) ++failures;
            {
                u32b hash=_rng_hash(); doc_ptr doc=doc_alloc(80);
                melee_calc_document(a,doc); doc_free(doc);
                if(hash!=_rng_hash()) ++failures;
            }
        }
#endif
    }
    printf("reckoning coverage  unarmed %d spell %d incoming %d healing %d dice %d exact %d hidden %d\n",
        counts[1],counts[2],counts[3],counts[4],dice,exact,hidden);
    printf("reckoning failures  %d\n",failures);
    printf("reckoning history  %d\n",melee_calc_count());
    p_ptr->innate_attack_ct=0;
    p_ptr->pclass=old_class; p_ptr->weapon_info[hand].slot=slot;
    p_ptr->image=p_ptr->stun=p_ptr->wraith_form=p_ptr->tim_transcendence=0;
    weapon->ident|=IDENT_KNOWN; r_info[4].blows[0]=old_blow; r_info[4].ac=old_ac;
    easy_lore=old_lore; auto_more_state=old_more;
    p_ptr->update|=PU_BONUS|PU_HP|PU_MANA; handle_stuff();
}

/* Real spell resolutions, also compiled against the pre-observer engine. */
static void _test_spell_probe(int recording)
{
    int i,j,k,ids[2],failures=0,complete=0,hidden=0,falloff=0,resistance=0;
    int repeated=0,old_more=auto_more_state,old_class=p_ptr->pclass,old_level=p_ptr->lev;
    u32b old_res=r_info[4].flagsr;
    u32b old_flags3=r_info[4].flags3;
    bool old_lore=easy_lore;
    for(i=m_max-1;i>0;--i) if(m_list[i].r_idx) delete_monster_idx(i);
    for(i=-4;i<=4;++i) for(j=-4;j<=4;++j)
    { cave_set_feat(py+i,px+j,feat_floor); cave[py+i][px+j].info|=CAVE_GLOW|CAVE_MARK; }
    ids[0]=place_monster_one(0,py,px+1,4,0,PM_NO_KAGE);
    ids[1]=place_monster_one(0,py+1,px+1,4,0,PM_NO_KAGE);
    if(!ids[0] || !ids[1]) { printf("spell failures  1\n"); return; }
    for(i=1;i<=equip_max();++i)
    { obj_ptr obj=equip_obj(i); if(obj) { obj->ident|=IDENT_KNOWN; obj_flags(obj,obj->known_flags); } }
    p_ptr->update|=PU_VIEW|PU_LITE|PU_MONSTERS; handle_stuff();
    melee_calc_reset(); melee_calc_recording((recording&1)!=0);
    for(i=0;i<195;++i)
    {
        int group=i/5,old_hp,old_mon_hp,type=GF_FIRE;
        const melee_calc_action_t *a;
        ++player_turn;
        p_ptr->pclass=old_class; p_ptr->lev=35;
        p_ptr->chp=7000; p_ptr->mhp=10000; p_ptr->csp=p_ptr->msp=500;
        p_ptr->spell_power=3; p_ptr->to_d_spell=7; p_ptr->tim_blood_rite=0;
        p_ptr->image=0; auto_more_state=AUTO_MORE_SKIP_ALL; easy_lore=TRUE;
        r_info[4].flagsr=old_res;
        r_info[4].flags3=old_flags3;
        for(j=0;j<2;++j)
        {
            mon_ptr mon=&m_list[ids[j]];
            mon->hp=mon->maxhp=30000; mon->ml=TRUE; mon->mflag2&=~MFLAG2_FUZZY;
            mon->ap_r_idx=mon->r_idx; mon->mpower=1000; mon->ac_adj=0;
            set_hostile(mon); set_monster_csleep(mon->id,0); set_monster_invulner(mon->id,0,FALSE);
        }
        target_who=ids[0]; target_row=py; target_col=px+1; command_dir=6;
        if(group==7) r_info[4].flagsr|=RFR_RES_FIRE;
        if(group==8) r_info[4].flagsr|=RFR_IM_FIRE;
        if(group==9) { type=GF_PLASMA; r_info[4].flagsr|=RFR_RES_PLAS; }
        if(group==10) { type=GF_SOUND; r_info[4].flagsr|=RFR_RES_SOUN; }
        if(group==11) { type=GF_NETHER; r_info[4].flagsr|=RFR_RES_NETH; }
        if(group==12) { type=GF_CHAOS; r_info[4].flagsr|=RFR_RES_CHAO; }
        if(group==13) { type=GF_SHARDS; r_info[4].flagsr|=RFR_RES_SHAR; }
        if(group==14) { type=GF_NEXUS; r_info[4].flagsr|=RFR_RES_NEXU; }
        if(group==15) { type=GF_WATER; r_info[4].flagsr|=RFR_RES_WATE; }
        if(group==16) { type=GF_DISENCHANT; r_info[4].flagsr|=RFR_RES_DISE; }
        if(group==17) for(j=0;j<2;++j) set_monster_invulner(ids[j],50,FALSE);
        if(group==18) { p_ptr->spell_power=-20; p_ptr->to_d_spell=-3; }
        if(group==19) p_ptr->tim_blood_rite=10;
        if(group==23 || group==31) equip_obj(equip_find_first(object_is_melee_weapon))->ident&=~IDENT_KNOWN;
        if(group==24) { p_ptr->image=10; for(j=0;j<2;++j) m_list[ids[j]].ml=FALSE; }
        if(group==25) p_ptr->chp=p_ptr->mhp-2;
        if(group==26) p_ptr->pclass=CLASS_BLOOD_MAGE;
        if(group==32) { type=GF_LITE_WEAK; r_info[4].flags3&=~RF3_HURT_LITE; }
        if(group==33) { type=GF_LITE; r_info[4].flags3|=RF3_HURT_LITE; }
        if(group==34) { type=GF_LITE; r_info[4].flagsr|=RFR_RES_LITE; }
        if(group==35) { type=GF_DARK; r_info[4].flagsr|=RFR_RES_DARK; }
        old_hp=p_ptr->chp; old_mon_hp=m_list[ids[0]].hp+m_list[ids[1]].hp;
        melee_calc_begin();
        switch(group)
        {
        case 0: do_spell(REALM_CHAOS,0,SPELL_CAST); break;
        case 1: cast_spell(magic_missile_spell); break;
        case 2: cast_spell(acid_bolt_spell); break;
        case 3: cast_spell(fire_ball_spell); break;
        case 4: cast_spell(mana_storm_I_spell); break;
        case 5: case 25: case 26: cast_spell(cure_wounds_III_spell); break;
        case 27: cast_spell(mana_bolt_I_spell); break;
        case 28: cast_spell(holy_lance_spell); break;
        case 36: cast_spell(shoot_arrow_spell); break;
        case 37: do_spell(REALM_DEATH,23,SPELL_CAST); break;
        case 38: do_spell(REALM_CRUSADE,22,SPELL_CAST); break;
        default:
        {
            int n=group==20 || group==21 ? 2 : group==29 ? 30 : 1;
#ifdef RECKONER_SPELL_CHAIN
            spell_calc_t c=spell_calc_power(spell_calc_add(spell_calc_dice(4,6),p_ptr->to_d_spell,"Spell damage bonus"));
            int value=c.value;
#else
            int value=spell_power(damroll(4,6)+p_ptr->to_d_spell);
#endif
            for(j=0;j<n;++j)
            {
                if(group==21 && j)
                {
#ifdef RECKONER_SPELL_CHAIN
                    c=spell_calc_power(spell_calc_add(spell_calc_dice(4,6),p_ptr->to_d_spell,"Spell damage bonus"));
                    value=c.value;
#else
                    value=spell_power(damroll(4,6)+p_ptr->to_d_spell);
#endif
                }
#ifdef RECKONER_SPELL_CHAIN
                if(group!=22) reckoner_project(0,group==30 || group==31 ? 0 : 2,
                    group==30 || group==31 ? py-3 : py,group==30 || group==31 ? px-3 : px+1,c,type,PROJECT_KILL|PROJECT_JUMP,"Test burst");
                else
#endif
                    project(0,group==30 || group==31 ? 0 : 2,group==30 || group==31 ? py-3 : py,
                        group==30 || group==31 ? px-3 : px+1,value,type,PROJECT_KILL|PROJECT_JUMP);
            }
            break;
        }
        }
        melee_calc_end();
        printf("spell-step %d hp=%d mon=%d,%d sp=%d rng=%08lx energy=%d virtue=%d,%d\n",i,
            p_ptr->chp,m_list[ids[0]].hp,m_list[ids[1]].hp,p_ptr->csp,(unsigned long)_rng_hash(),energy_use,
            virtue_current(VIRTUE_COMPASSION),virtue_current(VIRTUE_VITALITY));
#ifdef RECKONER_SPELL_CHAIN
        a=melee_calc_get(0);
        if(recording&1)
        {
            int dealt=0,healed=0;
            if(!a) { ++failures; continue; }
            if((group==30 || group==31) && (a->count!=1 || a->blows[0].kind!=RECKON_EFFECT)) ++failures;
            if(group==29 && (!a->omitted || a->count!=MELEE_CALC_BLOWS)) ++failures;
            for(j=0;j<a->count;++j)
            {
                const melee_calc_blow_t *b=&a->blows[j];
                if(!b->finished) ++failures;
                if(b->kind==RECKON_SPELL) dealt+=b->damage;
                if(b->kind==RECKON_HEAL) healed+=b->damage;
                if(melee_calc_complete(b)) ++complete;
                if(group==22 && b->kind==RECKON_SPELL && melee_calc_complete(b)) ++failures;
                if(group==23 || group==24 || group==31)
                {
                    ++hidden;
                    if(b->dice || b->base || b->step_count || b->spell_cast || b->spell_arrival) ++failures;
                }
                else if(group!=22 && b->kind==RECKON_SPELL && !melee_calc_complete(b))
                { printf("spell incomplete  group=%d source=%s\n",group,b->weapon); ++failures; }
                for(k=0;k<b->step_count;++k)
                {
                    if(streq(b->steps[k].label,"Distance falloff")) ++falloff;
                    if(strstr(b->steps[k].label,"Resistance (d6=")) ++resistance;
                }
                if(group==20 && j>=2 && b->dd)
                {
                    if(memcmp(b->faces,a->blows[j-2].faces,sizeof(b->faces))) ++failures;
                    ++repeated;
                }
            }
            if(group!=24 && group!=29 && dealt!=old_mon_hp-m_list[ids[0]].hp-m_list[ids[1]].hp) ++failures;
            if(healed!=p_ptr->chp-old_hp) ++failures;
            { u32b hash=_rng_hash(); doc_ptr doc=doc_alloc(80); melee_calc_document(a,doc); doc_free(doc); if(hash!=_rng_hash()) ++failures; }
        }
#endif
        equip_obj(equip_find_first(object_is_melee_weapon))->ident|=IDENT_KNOWN;
    }
    printf("spell coverage  complete %d concealed %d distance %d randomresist %d reused %d\n",complete,hidden,falloff,resistance,repeated);
    printf("spell failures  %d\n",failures);
    r_info[4].flagsr=old_res; p_ptr->pclass=old_class; p_ptr->lev=old_level;
    r_info[4].flags3=old_flags3;
    p_ptr->image=p_ptr->tim_blood_rite=0; p_ptr->spell_power=p_ptr->to_d_spell=0;
    easy_lore=old_lore; auto_more_state=old_more;
    p_ptr->update|=PU_BONUS|PU_HP|PU_MANA; handle_stuff();
}

void test_melee_probe(int recording)
{
    extern void test_reckoner_review(int recording);
    if(recording>=14) { test_reckoner_review(recording); return; }
    extern void test_reckoner_defence(int recording);
    if(recording>=12) { test_reckoner_defence(recording); return; }
    extern void test_reckoner_ranged(int recording);
    extern void test_reckoner_effects(int recording);
    if(recording>=10) { test_reckoner_ranged(recording); return; }
    if(recording>=8) { test_reckoner_effects(recording); return; }
    if(recording>=6) { _test_spell_probe(recording); return; }
    obj_ptr weapon;
    mon_ptr mon;
    if(recording>=4) { _test_reckoning_probe(recording); return; }
    int i,j,k,idx,slot,hand,hits=0,misses=0,criticals=0,complete=0,partial=0;
    int concealed=0,armor_hidden=0,failures=0,multiple=0,old_more=auto_more_state;
    bool old_lore=easy_lore, extended=recording>=2;
    int old_class=p_ptr->pclass;
    int slays=0,cuts=0,ambushes=0,penalties=0,clamps=0,defences=0;
    recording=recording&1;
    int tx=px+1,ty=py;
    u32b before;
    /* A deterministic arena with an ordinary weapon. Real py_attack resolves
     * every blow; the fixture never calculates its expected damage for it. */
    for(i=m_max-1;i>0;--i) if(m_list[i].r_idx) delete_monster_idx(i);
    cave_set_feat(ty,tx,feat_floor);
    idx=place_monster_one(0,ty,tx,4,0,PM_NO_KAGE);
    slot=equip_find_first(object_is_melee_weapon);
    if(!idx || !slot) { printf("melee error  fixture needs a weapon and creature\n"); return; }
    mon=&m_list[idx]; weapon=equip_obj(slot); hand=equip_which_hand(weapon);
    for(i=1;i<=equip_max();++i)
    {
        obj_ptr obj=equip_obj(i);
        if(obj) { obj->ident|=IDENT_KNOWN; obj_flags(obj,obj->known_flags); }
    }
    weapon->dd=2; weapon->ds=6; weapon->weight=900;
    weapon->to_h=5; weapon->to_d=4;
    p_ptr->update|=PU_BONUS; handle_stuff();
    p_ptr->skills.thn=100;
    p_ptr->weapon_info[hand].base_blow=250;
    p_ptr->weapon_info[hand].xtra_blow=0;
    p_ptr->weapon_info[hand].to_h=p_ptr->weapon_info[hand].dis_to_h=8;
    p_ptr->weapon_info[hand].to_d=p_ptr->weapon_info[hand].dis_to_d=3;
    mon->hp=mon->maxhp=30000;
    mon->mpower=1000; mon->ac_adj=0;
    r_info[4].ac=40;
    set_hostile(mon);
    set_monster_csleep(idx,0);
    melee_calc_reset(); melee_calc_recording(recording!=0);
    for(i=0;i<90;++i)
    {
        int mode=0,old_hp=mon->hp,total=0;
        const melee_calc_action_t *a;
        ++player_turn;
        auto_more_state=AUTO_MORE_SKIP_ALL;
        mon->ml=TRUE; mon->mflag2&=~MFLAG2_FUZZY;
        mon->ap_r_idx=mon->r_idx;
        p_ptr->image=0; p_ptr->stun=0;
        weapon->ident|=IDENT_KNOWN;
        easy_lore=TRUE;
        if(i>=30 && i<40) p_ptr->stun=30;
        if(i>=40 && i<50) weapon->ident&=~IDENT_KNOWN;
        if(i>=50 && i<60) { easy_lore=FALSE; r_info[4].r_pkills=0; }
        if(i>=60 && i<65) p_ptr->image=10;
        if(i>=65 && i<70) { mon->ml=FALSE; mon->mflag2|=MFLAG2_FUZZY; }
        if(i>=70 && i<80) mode=HISSATSU_MAJIN;
        if(i>=80 && i<85) weapon->to_d=-50;
        else weapon->to_d=4;
        if(extended)
        {
            p_ptr->pclass=old_class;
            remove_flag(weapon->flags,OF_BRAND_FIRE);
            remove_flag(weapon->known_flags,OF_BRAND_FIRE);
            remove_flag(weapon->flags,OF_VORPAL2);
            remove_flag(weapon->known_flags,OF_VORPAL2);
            set_monster_invulner(idx,0,FALSE);
            if(i<12) { add_flag(weapon->flags,OF_BRAND_FIRE); add_flag(weapon->known_flags,OF_BRAND_FIRE); }
            if(i>=12 && i<24) { add_flag(weapon->flags,OF_VORPAL2); add_flag(weapon->known_flags,OF_VORPAL2); }
            if(i>=24 && i<30) { p_ptr->pclass=CLASS_ROGUE; set_monster_csleep(idx,20); }
            if(i>=30 && i<40) p_ptr->pclass=CLASS_MAULER;
            if(i>=85 && i<89) set_monster_invulner(idx,10,FALSE);
        }
        if(i==89) mon->hp=old_hp=1;
        py_attack(ty,tx,mode);
        printf("melee-step %d hp=%d energy=%d rng=%08lx player=%d,%d,%d exp=%ld skill=%d\n",
            i,mon->hp,energy_use,(unsigned long)_rng_hash(),p_ptr->chp,p_ptr->stun,
            p_ptr->image,(long)p_ptr->exp,p_ptr->weapon_exp[weapon->tval-TV_WEAPON_BEGIN][weapon->sval]);
        a=melee_calc_get(0);
        if(recording && a)
        {
            if(a->count>1) ++multiple;
            for(j=0;j<a->count;++j)
            {
                const melee_calc_blow_t *b=&a->blows[j];
                int sum=0;
                if(!b->finished) ++failures;
                if(b->hit) ++hits; else ++misses;
                total+=b->damage;
                if(b->crit_mul>100) ++criticals;
                if(melee_calc_complete(b)) ++complete;
                else if(b->hit) ++partial;
                if(!b->visible || !b->player_known)
                {
                    ++concealed;
                    if(b->dice || b->crit_checked || b->base || b->crit_power) ++failures;
                }
                if(!b->armor_known)
                {
                    ++armor_hidden;
                    if(b->ac!=-1 || b->hit_roll!=-1 || b->power!=-1) ++failures;
                }
                if(b->dice)
                {
                    for(k=0;k<b->dd;++k)
                    {
                        if(b->faces[k]<1 || b->faces[k]>b->ds) ++failures;
                        sum+=b->faces[k];
                    }
                    if(sum!=b->base) ++failures;
                }
                if(!b->visible || !b->player_known || !b->target_known)
                {
                    if(b->step_count || b->steps_omitted) ++failures;
                }
                for(k=0;k<b->step_count;++k)
                {
                    const melee_calc_step_t *step=&b->steps[k];
                    if(streq(step->label,"Slays / brands")) ++slays;
                    if(streq(step->label,"Deep cut")) ++cuts;
                    if(streq(step->label,"Ambush")) ++ambushes;
                    if(streq(step->label,"Stun penalty") || streq(step->label,"Class / race scaling")) ++penalties;
                    if(streq(step->label,"Minimum damage")) ++clamps;
                    if(streq(step->label,"Target defence")) ++defences;
                }
            }
            if(i<60 && old_hp-mon->hp!=total) ++failures;
            /* Reading/exporting never advances randomness or modifies history. */
            before=_rng_hash();
            {
                doc_ptr doc=doc_alloc(80);
                melee_calc_document(a,doc);
                doc_free(doc);
            }
            if(before!=_rng_hash()) ++failures;
        }
        if(!mon->r_idx) break;
    }
    printf("melee coverage  hits %d misses %d criticals %d complete %d partial %d concealed %d armour %d multiple %d\n",
        hits,misses,criticals,complete,partial,concealed,armor_hidden,multiple);
    if(extended) printf("melee stages  slays %d cuts %d ambush %d penalties %d minimum %d defence %d\n",
        slays,cuts,ambushes,penalties,clamps,defences);
    printf("melee history  %d\n",melee_calc_count());
    printf("melee failures  %d\n",failures);
    p_ptr->pclass=old_class;
    p_ptr->image=p_ptr->stun=0;
    weapon->ident|=IDENT_KNOWN; weapon->to_d=4;
    easy_lore=old_lore; auto_more_state=old_more;
    p_ptr->update|=PU_BONUS; handle_stuff();
}
