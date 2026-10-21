/* Headless integration checks for real action/status sound hooks.
 * Copyright (c) 2026 John Horton. Source terms: LICENSE.
 * This file is linked only into the test frontend, never the SDL3 game. */
#include "scottish.h"

extern bool audio_test_trade(shop_ptr shop, obj_ptr obj, bool buying);
extern void audio_test_buyout(shop_ptr shop, obj_ptr stock);

static int _events[SOUND_MAX], _totals[SOUND_MAX], _failures, _checks;
static bool _enabled;
#define CHECK(x) do { ++_checks; if (!(x)) { ++_failures; printf("audio failure line %d: %s\n", __LINE__, #x); } } while (0)

static errr (*_old_hook)(int, int);
static char _keys[16];
static int _key_at;
static errr _input_hook(int n, int v)
{
    if (n == TERM_XTRA_EVENT)
    {
        if (!v) return 0; /* Flush/poll must not consume the next prompt's answer. */
        if (!_keys[_key_at])
        {
            int y, x;
            CHECK(FALSE);
            for (y = 0; y < 24; ++y)
            {
                char line[81]; byte a;
                for (x = 0; x < 80; ++x) Term_what(x, y, &a, &line[x]);
                line[80] = 0; printf("%s\n", line);
            }
            quit("Audio fixture exhausted its prompt answers");
        }
        auto_more_state = AUTO_MORE_SKIP_ALL;
        Term_keypress(_keys[_key_at++]);
        return 0;
    }
    return _old_hook(n, v);
}
static void _key(int key)
{
    auto_more_state = AUTO_MORE_SKIP_ALL;
    _keys[0] = key; _keys[1] = 0; _key_at = 0;
}

void test_audio_event(int event)
{
    if (event > 0 && event < SOUND_MAX) { ++_events[event]; ++_totals[event]; }
    else ++_failures;
}
void test_audio_report(void)
{
    int i;
    for (i = 1; i < SOUND_MAX; ++i)
        if (_totals[i]) printf("audio-event %s %d\n", ormeq_sound_name[i], _totals[i]);
}
static void _clear(void) { memset(_events, 0, sizeof(_events)); }
static void _expect(int event, int count) { CHECK(_events[event] == (_enabled ? count : 0)); }
static void _silent(void)
{
    int i;
    for (i = 1; i < SOUND_MAX; ++i)
        if (i < SOUND_UI_NAVIGATE || i > SOUND_UI_TAB) CHECK(!_events[i]);
}
static u32b _rng(void)
{
    int i; u32b h = 2166136261U;
    h = (h ^ Rand_quick) * 16777619U; h = (h ^ Rand_value) * 16777619U;
    h = (h ^ Rand_place) * 16777619U;
    for (i = 0; i < RAND_DEG; ++i) h = (h ^ Rand_state[i]) * 16777619U;
    return h;
}
static void _state(cptr label)
{
    printf("audio-state %s rng=%08lx hp=%d sp=%d energy=%d gold=%ld pack=%d quiver=%d stats=%d,%d,%d,%d,%d,%d\n",
        label, (unsigned long)_rng(), p_ptr->chp, p_ptr->csp, energy_use,
        (long)p_ptr->au, pack_count(NULL), quiver_count(NULL),
        p_ptr->stat_cur[0], p_ptr->stat_cur[1], p_ptr->stat_cur[2],
        p_ptr->stat_cur[3], p_ptr->stat_cur[4], p_ptr->stat_cur[5]);
}

/* Exercise each setter, rather than only testing the transition helper. */
#define STATUS(field, setter, cue, end) do { \
    p_ptr->field = 0; _clear(); setter(10, TRUE); \
    CHECK(p_ptr->field == 10); _expect(cue, 1); \
    _clear(); setter(10, TRUE); setter(20, TRUE); setter(19, TRUE); _silent(); \
    CHECK(p_ptr->field == 19); _clear(); setter(0, TRUE); \
    CHECK(!p_ptr->field); _expect(end, 1); _clear(); setter(0, TRUE); _silent(); \
    _state(#field); \
} while (0)

void test_audio_probe(void)
{
    int i, j, slot, before, old_more = auto_more_state;
    obj_t obj, bow, ammo;
    shop_ptr shop;
    _enabled = use_sound;
    _old_hook = Term->xtra_hook; Term->xtra_hook = _input_hook;
    for (i = 1; i < SOUND_MAX; ++i)
    {
        CHECK(ormeq_sound_name[i][0]);
        for (j = 1; j < i; ++j) CHECK(!streq(ormeq_sound_name[i], ormeq_sound_name[j]));
    }
    auto_more_state = AUTO_MORE_SKIP_ALL;
    for (i = m_max - 1; i > 0; --i) if (m_list[i].r_idx) delete_monster_idx(i);
    p_ptr->pclass = CLASS_WARRIOR; p_ptr->lev = 35;
    p_ptr->chp = p_ptr->mhp = 10000;
    p_ptr->no_stun = p_ptr->no_cut = p_ptr->no_slow = FALSE;
    STATUS(blind, set_blind, SOUND_BLIND, SOUND_RECOVER);
    STATUS(confused, set_confused, SOUND_CONFUSED, SOUND_RECOVER);
    STATUS(poisoned, set_poisoned, SOUND_POISONED, SOUND_RECOVER);
    /* Existing paralysis cannot be extended except by its countdown path. */
    STATUS(paralyzed, set_paralyzed, SOUND_PARALYZED, SOUND_RECOVER);
    STATUS(image, set_image, SOUND_DRUGGED, SOUND_RECOVER);
    STATUS(stun, set_stun, SOUND_STUN, SOUND_RECOVER);
    STATUS(cut, set_cut, SOUND_CUT, SOUND_RECOVER);
    STATUS(slow, set_slow, SOUND_SLOW, SOUND_RECOVER);
    STATUS(fast, set_fast, SOUND_SPEED, SOUND_BUFF_END);
    STATUS(lightspeed, set_lightspeed, SOUND_SPEED, SOUND_BUFF_END);
    STATUS(shield, set_shield, SOUND_SHIELD, SOUND_BUFF_END);
    STATUS(magicdef, set_magicdef, SOUND_SHIELD, SOUND_BUFF_END);
    STATUS(blessed, set_blessed, SOUND_BLESSED, SOUND_BUFF_END);
    STATUS(hero, set_hero, SOUND_HERO, SOUND_BUFF_END);
    STATUS(shero, set_shero, SOUND_BERSERK, SOUND_BUFF_END);
    STATUS(protevil, set_protevil, SOUND_PROTECTION, SOUND_BUFF_END);
    STATUS(wraith_form, set_wraith_form, SOUND_PROTECTION, SOUND_BUFF_END);
    STATUS(invuln, set_invuln, SOUND_INVULN, SOUND_BUFF_END);
    STATUS(tim_invis, set_tim_invis, SOUND_SEE_INVIS, SOUND_BUFF_END);
    STATUS(tim_infra, set_tim_infra, SOUND_INFRARED, SOUND_BUFF_END);
    STATUS(oppose_acid, set_oppose_acid, SOUND_RES_ACID, SOUND_BUFF_END);
    STATUS(oppose_elec, set_oppose_elec, SOUND_RES_ELEC, SOUND_BUFF_END);
    STATUS(oppose_fire, set_oppose_fire, SOUND_RES_FIRE, SOUND_BUFF_END);
    STATUS(oppose_cold, set_oppose_cold, SOUND_RES_COLD, SOUND_BUFF_END);
    STATUS(oppose_pois, set_oppose_pois, SOUND_RES_POIS, SOUND_BUFF_END);
    _clear(); fear_set_p(50); _expect(SOUND_AFRAID, 1);
    _clear(); fear_set_p(100); fear_set_p(50); _silent();
    _clear(); fear_set_p(0); _expect(SOUND_RECOVER, 1);
    _state("fear");

    /* Prevented statuses, ignored reductions and duplicate invulnerability. */
    p_ptr->no_stun = p_ptr->no_cut = p_ptr->no_slow = TRUE;
    _clear(); set_stun(50, FALSE); set_cut(50, FALSE); set_slow(50, FALSE);
    _silent(); CHECK(!p_ptr->stun && !p_ptr->cut && !p_ptr->slow);
    p_ptr->no_stun = p_ptr->no_cut = p_ptr->no_slow = FALSE;
    set_poisoned(100, FALSE); _clear(); set_poisoned(10, FALSE);
    CHECK(p_ptr->poisoned == 100); _silent(); set_poisoned(0, TRUE);
    set_invuln(100, FALSE); _clear(); set_invuln(120, FALSE); _silent(); set_invuln(0, TRUE);
    /* An underlying song/kata still supplies resistance: no false expiry. */
    p_ptr->special_defense |= KATA_MUSOU;
    _clear(); set_oppose_acid(10, FALSE); set_oppose_acid(0, TRUE); _silent();
    p_ptr->special_defense &= ~KATA_MUSOU;
    p_ptr->is_dead = TRUE;
    _clear(); set_blind(10, FALSE); set_poisoned(10, FALSE); _silent();
    p_ptr->is_dead = FALSE;
    _state("prevented");

    /* Birth/programmatic inventory grants stay silent; a real pickup speaks. */
    pack_init();
    object_prep(&obj, lookup_kind(TV_SWORD, SV_DAGGER)); obj.number = 1;
    _clear(); pack_carry(&obj); _silent(); CHECK(!obj.number);
    object_prep(&obj, lookup_kind(TV_SWORD, SV_DAGGER)); obj.number = 1;
    _clear(); pack_get(&obj); _expect(SOUND_PICKUP, 1); CHECK(!obj.number);
    _state("pickup");
    /* Cancellation and completion go through the normal equipment prompts. */
    _clear(); _key(ESCAPE); equip_wield_ui(); _silent();
    slot = pack_find_obj(TV_SWORD, SV_DAGGER); CHECK(slot);
    _clear(); _key(slot_label(slot)); _keys[1] = 'a'; _keys[2] = 0;
    equip_wield_ui(); _expect(SOUND_WIELD, 1);
    slot = equip_find_obj(TV_SWORD, SV_DAGGER); CHECK(slot);
    _clear(); _key(ESCAPE); equip_takeoff_ui(); _silent();
    _clear(); _key(slot_label(slot)); equip_takeoff_ui(); _expect(SOUND_WIELD, 1);
    _state("equipment");

    /* Explicit quiver loading: a full quiver and a cancelled quantity are silent. */
    object_prep(&obj, lookup_kind(TV_QUIVER, 0)); obj.xtra4 = 2;
    equip_wield(&obj, equip_first_slot(&obj));
    quiver_init(); pack_init();
    object_prep(&obj, lookup_kind(TV_ARROW, 1)); obj.number = 3;
    pack_carry_aux(&obj);
    _clear(); _key('a'); _keys[1] = ESCAPE; _keys[2] = 0;
    equip_wield_ui(); _silent(); CHECK(quiver_count(NULL) == 0);
    _clear(); _key('a'); _keys[1] = '\r'; _keys[2] = 0;
    equip_wield_ui(); _expect(SOUND_QUIVER, 1); CHECK(quiver_count(NULL) == 2);
    _clear(); _key('a'); equip_wield_ui(); _silent();
    _state("quiver");

    /* Real commerce helpers: no cues for a refusal, cancellation or poverty. */
    shop = shop_alloc(SHOP_GENERAL);
    object_prep(&obj, lookup_kind(TV_FOOD, SV_FOOD_RATION)); obj.number = 1;
    p_ptr->au = 100000;
    _clear(); _key('n'); CHECK(!audio_test_trade(shop, &obj, TRUE)); _silent();
    p_ptr->au = 0;
    _clear(); _key('y'); CHECK(!audio_test_trade(shop, &obj, TRUE)); _silent();
    p_ptr->au = 100000; before = p_ptr->au;
    _clear(); _key('y'); CHECK(audio_test_trade(shop, &obj, TRUE)); _expect(SOUND_BUY, 1);
    CHECK(p_ptr->au < before); _state("buy");
    object_prep(&obj, lookup_kind(TV_FOOD, SV_FOOD_RATION)); obj.number = 1;
    _clear(); _key('n'); CHECK(!audio_test_trade(shop, &obj, FALSE)); _silent();
    before = p_ptr->au;
    _clear(); _key('y'); CHECK(audio_test_trade(shop, &obj, FALSE)); _expect(SOUND_SELL, 1);
    CHECK(p_ptr->au > before); _state("sell");
    no_selling = TRUE; before = p_ptr->au;
    object_prep(&obj, lookup_kind(TV_FOOD, SV_FOOD_RATION)); obj.number = 1;
    _clear(); _key('y'); CHECK(audio_test_trade(shop, &obj, FALSE));
    _expect(SOUND_DROP, 1); _expect(SOUND_SELL, 0); CHECK(p_ptr->au == before);
    no_selling = FALSE;
    object_prep(&obj, lookup_kind(TV_FOOD, SV_FOOD_RATION)); obj.number = 2;
    _clear(); _key('y'); audio_test_buyout(shop, &obj); _expect(SOUND_BUY, 1);
    _clear(); _key('y'); audio_test_buyout(shop, NULL); _silent(); /* empty */
    shop_free(shop); _state("buyout");

    /* Throws cancelled at targeting and character-sheet previews are silent. */
    for (i = -4; i <= 4; ++i) for (j = -8; j <= 8; ++j)
    { cave_set_feat(py+i, px+j, feat_floor); cave[py+i][px+j].info |= CAVE_GLOW | CAVE_MARK; }
    object_prep(&ammo, lookup_kind(TV_SWORD, SV_DAGGER)); ammo.number = 30;
    {
        py_throw_t c = {0}; doc_ptr doc = doc_alloc(80);
        c.obj = &ammo;
        _clear(); _key(ESCAPE); CHECK(!py_throw(&c)); _silent();
        c.type = THROW_DISPLAY;
        _clear(); py_throw_doc(&c, doc); _silent(); doc_free(doc);
    }
    object_prep(&bow, lookup_kind(TV_BOW, SV_LONG_BOW));
    for (i = 0; i < 80; ++i)
    {
        int idx = place_monster_one(0, py, px+3, 4, 0, PM_NO_KAGE), hp;
        mon_ptr mon;
        py_throw_t c = {0};
        CHECK(idx); if (!idx) break;
        mon = &m_list[idx]; mon->ml = (i % 4 < 2);
        mon->hp = mon->maxhp = 30000;
        p_ptr->skills.thb = p_ptr->skill_tht = i % 8 < 4 ? 1000 : -1000;
        p_ptr->shooter_info.base_shot = 100;
        object_prep(&ammo, lookup_kind(i%2 ? TV_SWORD : TV_ARROW, i%2 ? SV_DAGGER : 1));
        ammo.number = 50; ammo.dd = 2; ammo.ds = 6; ammo.to_d = 20;
        hp = mon->hp; _clear();
        if (i % 2)
        { c.obj=&ammo; c.dir=6; c.range=6; c.mult=100; CHECK(py_throw(&c)); _expect(SOUND_THROW, 1); }
        else
        { do_cmd_fire_aux2(&bow,&ammo,px,py,px+3,py); _expect(SOUND_SHOOT, 1); }
        _expect(SOUND_SHOOT_HIT, mon->hp < hp ? 1 : 0);
        printf("audio-projectile %d hp=%d ammo=%d rng=%08lx\n", i, mon->hp, ammo.number, (unsigned long)_rng());
        delete_monster_idx(idx);
    }
    _state("ranged");

    /* Successful disarming, repeated attempts and undiscovered chest traps. */
    {
        int idx = o_pop(); obj_ptr chest = &o_list[idx];
        object_prep(chest, lookup_kind(TV_CHEST, 1));
        chest->loc.where = INV_FLOOR; chest->loc.slot = idx;
        chest->loc.x = px+1; chest->loc.y = py;
        chest->next_o_idx = cave[py][px+1].o_idx; cave[py][px+1].o_idx = idx;
        for (i = 1; i < 64 && !chest_traps[i]; ++i) {}
        CHECK(i < 64); chest->pval = i;
        p_ptr->skills.dis = 20000;
        command_dir = 6; _clear(); _key('6'); do_cmd_disarm(); _silent();
        chest->ident |= IDENT_KNOWN;
        command_dir = 6; _clear(); _key('6'); do_cmd_disarm();
        _expect(SOUND_DISARM, 1); CHECK(chest->pval < 0);
        command_dir = 6; _clear(); _key('6'); do_cmd_disarm(); _silent();
        delete_object_idx(idx);
        _state("disarm");
    }

    /* Stairs reject an ordinary floor silently, then report a real departure. */
    autosave_l = FALSE;
    _clear(); do_cmd_go_up(); do_cmd_go_down(); _silent();
    dungeon_type = DUNGEON_SINK; dun_level = 5;
    cave_set_feat(py, px, feat_up_stair);
    _clear(); do_cmd_go_up(); _expect(SOUND_STAIRS_UP, 1); CHECK(p_ptr->leaving);
    p_ptr->leaving = FALSE; cave_set_feat(py, px, feat_down_stair);
    _clear(); do_cmd_go_down(); _expect(SOUND_STAIRS_DOWN, 1); CHECK(p_ptr->leaving);
    p_ptr->leaving = FALSE;
    _state("stairs");

    /* Actual movement, failed movement, building visits and menu input. */
    cave_set_feat(py, px, feat_floor);
    cave_set_feat(py, px+1, feat_floor);
    before = px; _clear(); move_player(6, FALSE, FALSE);
    CHECK(px == before+1); _expect(SOUND_WALK, 1); _state("walk");
    cave_set_feat(py, px+1, feat_granite); cave[py][px+1].info |= CAVE_MARK;
    before = px; _clear(); move_player(6, FALSE, FALSE);
    CHECK(px == before); _expect(SOUND_WALK, 0); _expect(SOUND_HITWALL, 1);
    _clear(); _key(ESCAPE); home_ui();
    _expect(SOUND_HOME_ENTER, 1); _expect(SOUND_STORE_LEAVE, 1);
    _clear(); _key(ESCAPE); museum_ui();
    _expect(SOUND_STORE1, 1); _expect(SOUND_STORE_LEAVE, 1);
    shop = shop_alloc(SHOP_GENERAL);
    _clear(); _key(ESCAPE); shop_ui(shop);
    _expect(SOUND_STORE1, 1); _expect(SOUND_STORE_LEAVE, 1);
    shop_free(shop); _state("visits");
    {
        doc_ptr menu = doc_alloc(60);
        u32b rng = _rng();
        doc_insert(menu, "Settings"); doc_add_hotspot(menu, 's');
        _clear(); _key('s'); CHECK(ui_menu_inkey(menu) == 's'); _expect(SOUND_UI_ACCEPT, 1);
        _clear(); _key('!'); CHECK(ui_menu_inkey(menu) == '!'); _silent();
        _clear(); _key(ESCAPE); CHECK(ui_menu_inkey(menu) == ESCAPE); _expect(SOUND_UI_BACK, 1);
        CHECK(_rng() == rng); doc_free(menu);
    }
    _state("menus");
    _clear(); do_cmd_bldg(); _expect(SOUND_STORE1, 0); _expect(SOUND_STORE_LEAVE, 0);
    for (i = 1; i < max_f_idx; ++i)
        if (have_flag(f_info[i].flags, FF_BLDG) && f_info[i].subtype == 1) break;
    CHECK(i < max_f_idx);
    if (i < max_f_idx)
    {
        cave_set_feat(py, px, i); _clear(); _key(ESCAPE); do_cmd_bldg();
        _expect(SOUND_STORE1, 1); _expect(SOUND_STORE_LEAVE, 1);
        cave_set_feat(py, px, feat_floor); _state("building");
    }
    {
        int idx = o_pop(); obj_ptr chest = &o_list[idx];
        cave_set_feat(py, px+1, feat_floor);
        object_prep(chest, lookup_kind(TV_CHEST, 1));
        chest->pval = 1; chest->ident |= IDENT_KNOWN;
        chest->loc.where = INV_FLOOR; chest->loc.slot = idx;
        chest->loc.x = px+1; chest->loc.y = py;
        chest->next_o_idx = cave[py][px+1].o_idx; cave[py][px+1].o_idx = idx;
        p_ptr->skills.dis = 20000;
        command_dir = 6; _clear(); _key('6'); do_cmd_open();
        _expect(SOUND_UNLOCK, 1); _expect(SOUND_CHEST_OPEN, 1); CHECK(chest->pval == 0);
        command_dir = 6; _clear(); _key('6'); do_cmd_open();
        _expect(SOUND_UNLOCK, 0); _expect(SOUND_CHEST_OPEN, 0);
        delete_object_idx(idx); _state("chest");
    }
    auto_more_state = old_more;
    Term->xtra_hook = _old_hook;
    printf("audio checks  %d\naudio failures  %d\n", _checks, _failures);
}
