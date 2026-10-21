#include "scottish.h"

#define _MAX_PACK_SLOTS 12

static int _bearskin_form = 0;
static inv_ptr _bearskin_pack = NULL;
static bool _pack_initialized = FALSE;
static bool _do_init_pack = FALSE;

void _bearskin_pack_init(void)
{
    if ((_pack_initialized) && (!_do_init_pack)) return;
    inv_free(_bearskin_pack);
    _bearskin_pack = inv_alloc("Satchel", INV_SPECIAL1, _MAX_PACK_SLOTS);
    _pack_initialized = TRUE;
    _do_init_pack = FALSE;
}

equip_template_ptr _bearskin_equip_template(int hahmo)
{
    if (hahmo == BEARSKIN_FORM_BEAR) return &b_info[111];
    else return &b_info[0];
}

static void _bearskin_equip_on_change_form(void)
{
    equip_template_ptr old_template = _bearskin_equip_template(1 - _bearskin_form);
    equip_template_ptr new_template = _bearskin_equip_template(_bearskin_form);

    slot_t  slot;
    inv_ptr _equipment = get_equipment();
    inv_ptr temp = inv_copy(_equipment);
    inv_ptr temp2 = inv_copy(_bearskin_pack);

//    inv_clear(_equipment);
    inv_clear(_bearskin_pack);
    set_equip_template(new_template);

    /* We need to not just clear the equipment, but also make sure the items
     * aren't tracked as equipped */
    for (slot = 1; slot <= old_template->max; slot++)
    {
        obj_ptr src = inv_obj(temp, slot);
        if (!src) continue;
        equip_remove(slot);
        src->loc.where = 0;
    }

    for (slot = 1; slot <= old_template->max; slot++)
    {
        obj_ptr src = inv_obj(temp, slot);
        slot_t  new_slot;

        if (!src) continue;
        src->marked |= OM_BEING_SHUFFLED;
        new_slot = equip_first_empty_slot(src);
        if (new_slot)
//            equip_wield(src, new_slot);
            inv_add_at(_equipment, src, new_slot);
        else
            inv_add(_bearskin_pack, src);
        obj_release(src, OBJ_RELEASE_QUIET);
        src->marked &= ~OM_BEING_SHUFFLED;
    }

    for (slot = 1; slot <= _MAX_PACK_SLOTS; slot++)
    {
        obj_ptr src = inv_obj(temp2, slot);
        slot_t  new_slot;

        if (!src) continue;
        src->marked |= OM_BEING_SHUFFLED;
        new_slot = equip_first_empty_slot(src);

        /* Assume lone shields are in the left hand */
        if ((_bearskin_form == BEARSKIN_FORM_HUMAN) && (slot == 1) && (new_slot == 1) && (src->tval == TV_SHIELD) && (!(weaponmaster_is_(WEAPONMASTER_SHIELDS))) && (!equip_obj(2)))
        {
            obj_ptr tmp_obj = inv_obj(temp2, 2);
            if ((!tmp_obj) || ((tmp_obj->tval != TV_SHIELD) && (!object_is_melee_weapon(tmp_obj)))) new_slot = 2;
        }

        if (new_slot)
            equip_wield(src, new_slot);
        else
            inv_add(_bearskin_pack, src);
        obj_release(src, OBJ_RELEASE_QUIET);
        src->marked &= ~OM_BEING_SHUFFLED;
    }

    inv_free(temp);
    inv_free(temp2);

    if (_bearskin_form == BEARSKIN_FORM_BEAR) p_ptr->shero = 1; /* show AC right */
    else p_ptr->shero = 0; /* Don't be berserk for 1 more turn... */

    p_ptr->update |= PU_BONUS | PU_TORCH | PU_MANA | PU_HP;
    p_ptr->redraw |= PR_EQUIPPY | PR_EFFECTS | PR_STATUS;
    p_ptr->window |= PW_INVEN | PW_EQUIP;
}

/**********************************************************************
 * bearskin Start
 **********************************************************************/
static void _birth(void)
{
    _do_init_pack = TRUE;
    _bearskin_pack_init();

    _bearskin_form = 0;
    skills_innate_init("Claw", WEAPON_EXP_BEGINNER, WEAPON_EXP_MASTER);
    py_birth_food();
    py_birth_light();
}

void _bearskin_calc_innate_attacks(void)
{
    int l = p_ptr->lev;
    int to_d = l/6 + 5;
    int to_h = l/6 + 10;

    /* Claws */
    {
        innate_attack_t    a = {0};

        a.dd = 3 + l / 12;
        a.ds = 4 + l / 6;
        a.to_d += to_d;
        a.to_h += to_h;
        a.to_d += 3;

        a.weight = 100;
        calc_innate_blows(&a, 386 + (l * 2));
        a.msg = "You claw.";
        a.name = "Claw";

        if (psion_combat()) psion_combat_innate_blows(&a);
        p_ptr->innate_attacks[p_ptr->innate_attack_ct++] = a;
    }
}

void bearskin_change_shape_spell(int cmd, variant *res)
{
    switch (cmd)
    {
    case SPELL_NAME:
        var_set_string(res, "Change Skin");
        break;
    case SPELL_DESC:
        if (_bearskin_form == BEARSKIN_FORM_HUMAN) var_set_string(res, "Assume the shape of a bear. Changing shape costs two turns.");
        else var_set_string(res, "Assume the shape of a human. Changing shape costs two turns.");
        break;
    case SPELL_CAST:
        if (p_ptr->cursed & 0x0000000F)
        {
            msg_print("Your cursed equipment prevents you from changing shape!");
            var_set_bool(res, FALSE);
            break;
        }
        if (psion_weapon_graft())
        {
            msg_print("You cannot change shape with a weapon fused to your arm!");
            var_set_bool(res, FALSE);
            break;
        }
        _bearskin_form = 1 - _bearskin_form;
        _bearskin_equip_on_change_form();
        if (_bearskin_form == BEARSKIN_FORM_BEAR)
        {
            msg_format("You turn into a bear!");
            stop_mouth();
        }
        else msg_format("You turn into a human!");
        var_set_bool(res, TRUE);
        handle_stuff();
        break;
    case SPELL_ENERGY:
        var_set_int(res, 200);
        break;
    default:
        default_spell(cmd, res);
        break;
    }
}

void _bear_swipe_spell(int cmd, variant *res)
{
    switch (cmd)
    {
    case SPELL_NAME:
        var_set_string(res, "Swipe");
        break;
    case SPELL_DESC:
        var_set_string(res, "Strike all adjacent monsters with one swiping blow.");
        break;
    case SPELL_CAST:
    {
        int              dir, x, y;
        cave_type       *c_ptr;
        monster_type    *m_ptr;

        for (dir = 0; dir < 8; dir++)
        {
            y = py + ddy_ddd[dir];
            x = px + ddx_ddd[dir];
            c_ptr = &cave[y][x];

            m_ptr = &m_list[c_ptr->m_idx];

            if (c_ptr->m_idx && (m_ptr->ml || cave_have_flag_bold(y, x, FF_PROJECT)))
                py_attack(y, x, BEARSKIN_SWIPE);
        }
        var_set_bool(res, TRUE);
        break;
    }
    default:
        default_spell(cmd, res);
        break;
    }
}

void _bear_charge_spell(int cmd, variant *res)
{
    switch (cmd)
    {
    case SPELL_NAME:
        var_set_string(res, "Mauling Charge");
        break;
    case SPELL_DESC:
        var_set_string(res, "Charge at a nearby monster.");
        break;
    case SPELL_CAST:
        var_set_bool(res, rush_attack((p_ptr->lev >= 40 ? 3 : 2), NULL));
        break;
    default:
        default_spell(cmd, res);
        break;
    }
}

void _bear_sniff_spell(int cmd, variant *res)
{
    switch (cmd)
    {
    case SPELL_NAME:
        var_set_string(res, "Sniff");
        break;
    case SPELL_DESC:
        var_set_string(res, "Attempt to smell nearby monsters.");
        break;
    case SPELL_CAST:
        detect_monsters_living(DETECT_RAD_DEFAULT, "You smell nearby monsters.");
        var_set_bool(res, TRUE);
        break;
    default:
        default_spell(cmd, res);
        break;
    }
}

void _raging_swipe_spell(int cmd, variant *res)
{
    switch (cmd)
    {
    case SPELL_NAME:
        var_set_string(res, "Raging Swipe");
        break;
    case SPELL_DESC:
        var_set_string(res, "Strike all adjacent monsters with a mighty blow, attempting to knock them back.");
        break;
    case SPELL_CAST:
    {
        int              dir, x, y;
        cave_type       *c_ptr;
        monster_type    *m_ptr;

        for (dir = 0; dir < 8; dir++)
        {
            y = py + ddy_ddd[dir];
            x = px + ddx_ddd[dir];
            c_ptr = &cave[y][x];

            m_ptr = &m_list[c_ptr->m_idx];

            if (c_ptr->m_idx && (m_ptr->ml || cave_have_flag_bold(y, x, FF_PROJECT)))
                py_attack(y, x, BEARSKIN_BIG_SWIPE);
        }
        var_set_bool(res, TRUE);
        break;
    }
    default:
        default_spell(cmd, res);
        break;
    }
}

static power_info _bear_powers[] = {
    { A_DEX, { 20, 5, 25, _bear_swipe_spell } },
    { A_STR, { 25, 10, 30, _bear_charge_spell } },
    { A_INT, { 35, 1, 25, _bear_sniff_spell } },
    {    -1, { -1, -1, -1, NULL}}
};
static power_info _man_powers[] = {
    { A_DEX, { 1, 1, 30, create_food_spell } },
    {    -1, { -1, -1, -1, NULL}}
};
static power_info _default_power[] = {
    { A_DEX, {  1,  0,  0, bearskin_change_shape_spell}},
    {    -1, { -1, -1, -1, NULL}}
};

static power_info *_bearskin_powers(void)
{
    static power_info spells[8] = {0};
    int max = 7;
    int ct = get_powers_aux(spells, max, _default_power, FALSE);
    if (_bearskin_form == BEARSKIN_FORM_BEAR)
        ct += get_powers_aux(spells + ct, max - ct, _bear_powers, FALSE);
    else ct += get_powers_aux(spells + ct, max - ct, _man_powers, FALSE);
    if ((_bearskin_form == BEARSKIN_FORM_BEAR) && (p_ptr->pclass != CLASS_SORCERER) && (p_ptr->pclass != CLASS_DUELIST))
    {
        static power_info _raging_swipe[2] = /* ugly but, hey, it works */
        {
            {A_DEX, {42, 25, 30, _raging_swipe_spell}},
            {-1,    {-1, -1, -1, NULL}},
        };
        ct += get_powers_aux(spells + ct, max - ct, _raging_swipe, FALSE);
    }
    spells[ct].spell.fn = NULL;
    return spells;
}

static void _calc_bonuses(void)
{
    int to_a = py_prorata_level_aux(80, 1, 1, 1) + 15;

    p_ptr->sustain_str = TRUE;
    p_ptr->regen += 100;
    p_ptr->skill_dig += 50 + p_ptr->lev*4;
    if (p_ptr->lev >= 30) p_ptr->free_act++;

    if (_bearskin_form == BEARSKIN_FORM_BEAR)
    {
        res_add_immune(RES_FEAR);
        if (p_ptr->lev >= 40) res_add(RES_NEXUS);
        p_ptr->to_a += to_a;
        p_ptr->dis_to_a += to_a;
        p_ptr->pspeed += p_ptr->lev / 7;
        p_ptr->shero = 1;
        if (p_ptr->lev > 34) p_ptr->no_stun = TRUE;
    }
}

static void _get_flags(u32b flgs[OF_ARRAY_SIZE]) {
    add_flag(flgs, OF_REGEN);
    add_flag(flgs, OF_SUST_STR);
        
    if (p_ptr->lev >= 30) add_flag(flgs, OF_FREE_ACT);
        
    if (_bearskin_form == BEARSKIN_FORM_BEAR)
    {
        add_flag(flgs, OF_IM_FEAR);
        if (p_ptr->lev >= 40) add_flag(flgs, OF_RES_NEXUS);
        add_flag(flgs, OF_SPEED);
    }
}

static void _bearskin_save(savefile_ptr file)
{
    inv_save(_bearskin_pack, file);
    savefile_write_byte(file, _bearskin_form);
}

static void _bearskin_load(savefile_ptr file)
{
    _bearskin_pack_init();
    inv_load(_bearskin_pack, file);
    _bearskin_form = savefile_read_byte(file);
}

void bearskin_init(void)
{
    _bearskin_form = 0;
}

bool bearskin_shape_is_(int which)
{
    return (_bearskin_form == which);
}

static void _dump_satchel(doc_ptr doc)
{
    int laskuri = 0;
    slot_t slot;

    /* We need to not just clear the equipment, but also make sure the items
     * aren't tracked as equipped */
    for (slot = 1; slot <= _MAX_PACK_SLOTS; slot++)
    {
        obj_ptr src = inv_obj(_bearskin_pack, slot);
        if (!src) continue;
        laskuri++;
    }

    if (!laskuri) return;

    else
    {
        char o_name[MAX_NLEN];

        doc_insert(doc, "<topic:Satchel>============================= <color:keypress>S</color>hape-Shift Satchel =============================\n\n");
        for (slot = 1; slot <= _MAX_PACK_SLOTS; slot++)
        {
            object_type *o_ptr = inv_obj(_bearskin_pack, slot);
            if (!o_ptr) continue;

            object_desc(o_name, o_ptr, OD_COLOR_CODED);
            doc_printf(doc, " %c) <indent><style:indent>%s</style></indent>\n", slot - 1 + 'a', o_name);
            if (((always_dump_origins) || ((final_dump_origins) && ((p_ptr->total_winner) || (p_ptr->is_dead))))
              && (o_ptr->origin_type != ORIGIN_NONE) && (o_ptr->origin_type != ORIGIN_MIXED))
            {
                doc_printf(doc, "    <indent><style:indent><color:W>");
                (void)display_origin(o_ptr, doc);
                doc_printf(doc, "</color></style></indent>\n");
            }
        }
        doc_newline(doc);
    }
}

inv_ptr _bearskin_get_pack(void)
{
    if (!_pack_initialized) return NULL;
    return _bearskin_pack;
}

/* Account for the weight of the items in the bearskin pack */
int _bearskin_pack_weight(obj_p p)
{
    if (!_bearskin_get_pack()) return 0;

    return inv_weight(_bearskin_pack, p);
}

race_t *bearskin_get_race(void)
{
    static race_t me = {0};
    static bool   init = FALSE;
    static byte   init_form = 0;

    if ((!init) || (init_form != _bearskin_form))
    {
        if (!init)
        {
            me.name = "Bearskin";
            me.desc = "Bearskins are a hardy, northern race of men, renowned for their ability "
                    "to assume the shape of a bear. Bear-formed Bearskins greatly resemble "
                    "Berserkers: they are extremely strong in melee and very hard to kill, but "
                    "incapable of reading scrolls, casting spells or using magical devices. "
                    "Man-shaped Bearskins are still stronger in combat than a regular human, "
                    "but somewhat lacking in magical skills.\n\nThe equipment slots available "
                    "to a Bearskin depend on their shape, and like Werewolves, they carry a "
                    "special satchel for keeping the items not needed in their current shape.";

            me.infra = 5;
            me.exp = 140;
            me.calc_bonuses = _calc_bonuses;
            me.get_flags = _get_flags;
            me.get_powers_fn = _bearskin_powers;
            me.birth = _birth;
            me.load_player = _bearskin_load;
            me.save_player = _bearskin_save;
            me.character_dump = _dump_satchel;
            me.calc_extra_weight = _bearskin_pack_weight;
        }
        if (_bearskin_form == BEARSKIN_FORM_HUMAN)
        {
            me.stats[A_STR] =  1;
            me.stats[A_INT] =  0;
            me.stats[A_WIS] =  0;
            me.stats[A_DEX] =  -1;
            me.stats[A_CON] =  1;
            me.stats[A_CHR] =  -1;
            me.life = 104;
            me.shop_adjust = 105;
            me.base_hp = 27;

            me.skills.dis = 0;
            me.skills.dev = -8;
            me.skills.sav = 4;
            me.skills.stl = -1;
            me.skills.srh = 0;
            me.skills.fos = 10;
            me.skills.thn = 10;
            me.skills.thb = -5;
            me.calc_innate_attacks = NULL;
            me.flags = RACE_NO_POLY;
        }
        else
        {
            me.stats[A_STR] =  3;
            me.stats[A_INT] =  -1;
            me.stats[A_WIS] =  -2;
            me.stats[A_DEX] =  -1;
            me.stats[A_CON] =  3;
            me.stats[A_CHR] =  -2;
            me.shop_adjust = 120;
            me.base_hp = 32;

            me.skills.dis = -8;
            me.skills.dev = -24;
            me.skills.sav = 10;
            me.skills.stl = -1;
            me.skills.srh = 3;
            me.skills.fos = 3;
            me.skills.thn = 20;
            me.skills.thb = -15;
            me.calc_innate_attacks = _bearskin_calc_innate_attacks;
            me.flags = (RACE_NO_POLY | RACE_IS_ILLITERATE);
        }

        init_form = _bearskin_form;
        init = TRUE;
    }

    if (_bearskin_form == BEARSKIN_FORM_BEAR)
    {
        me.life = (p_ptr->pclass == CLASS_BERSERKER) ? 115 + (p_ptr->lev / 3) : 155 + (p_ptr->lev / 3);
    }

    me.equip_template = _bearskin_equip_template(_bearskin_form);
    me.bonus_pack = _bearskin_get_pack();

    return &me;
}
