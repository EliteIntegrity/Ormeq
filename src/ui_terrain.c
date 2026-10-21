/*
 * File: ui_terrain.c
 *
 * See ui_terrain.h for what this is for and the two rules it has to keep.
 *
 * Copyright (c) 2026 John Horton.
 *
 * Added since the fork and offered on the same terms as the rest of the
 * tree -- the Angband licence in LICENSE.
 */

#include "scottish.h"

/*
 * A cheap, well-mixed integer hash (Murmur-style finalisers).
 *
 * It has to be a hash rather than the game's random number generator: the
 * answer for a grid must be the same every time it is asked, in any order,
 * without storing anything.
 */
static u32b _mix(u32b v)
{
    v ^= v >> 16;
    v *= 0x7feb352dU;
    v ^= v >> 15;
    v *= 0x846ca68bU;
    v ^= v >> 16;
    return v;
}

static u32b _hash(u32b key, int x, int y, int material)
{
    u32b v = key
           ^ ((u32b)x * 0x9e3779b1U)
           ^ ((u32b)y * 0x85ebca77U)
           ^ ((u32b)material * 0xc2b2ae3dU);

    return _mix(v);
}


/*
 * One value standing for "which level is this".
 *
 * Two dungeons at the same depth should not wear the same pattern, and neither
 * should the same depth on a later visit. This is not stored in the savefile:
 * a level regenerated is a different level, and looking different is right.
 */
static u32b _level_key(void)
{
    u32b v = (u32b)dun_level * 0x9e3779b1U
           ^ (u32b)dungeon_type * 0x85ebca77U
           ^ (u32b)cur_wid * 0xc2b2ae3dU
           ^ (u32b)cur_hgt * 0x27d4eb2fU;

    /* On the surface the depth is always zero, so lean on where in the world
     * you are standing instead. */
    if (!dun_level)
    {
        v ^= (u32b)p_ptr->wilderness_x * 0x165667b1U;
        v ^= (u32b)p_ptr->wilderness_y * 0x9e3779b9U;
    }

    return _mix(v);
}


/*
 * What is this grid made of?
 *
 * By flag rather than by feature, so that the many features this variant has
 * that plain Angband does not are classified correctly without being listed --
 * and so that anything unrecognised falls through to NONE and is left alone.
 *
 * The order of the tests is the safety rule. Everything that carries meaning
 * is rejected first, and only then is the remainder sorted into materials.
 */
static int _material(int feat)
{
    feature_type *f;

    if (feat <= 0 || feat >= max_f_idx) return UI_TERRAIN_NONE;

    f = &f_info[feat];

    /*
     * Anything a player reads for information keeps its exact appearance.
     * A decorated door is a door you might not recognise as a door.
     */
    if (have_flag(f->flags, FF_DOOR))         return UI_TERRAIN_NONE;
    if (have_flag(f->flags, FF_STAIRS))       return UI_TERRAIN_NONE;
    if (have_flag(f->flags, FF_LESS))         return UI_TERRAIN_NONE;
    if (have_flag(f->flags, FF_MORE))         return UI_TERRAIN_NONE;
    if (have_flag(f->flags, FF_STORE))        return UI_TERRAIN_NONE;
    if (have_flag(f->flags, FF_TRAP))         return UI_TERRAIN_NONE;
    if (have_flag(f->flags, FF_HIT_TRAP))     return UI_TERRAIN_NONE;
    if (have_flag(f->flags, FF_GLYPH))        return UI_TERRAIN_NONE;
    if (have_flag(f->flags, FF_MAY_HAVE_GOLD))return UI_TERRAIN_NONE;
    if (have_flag(f->flags, FF_HAS_GOLD))     return UI_TERRAIN_NONE;
    if (have_flag(f->flags, FF_HAS_ITEM))     return UI_TERRAIN_NONE;

    /* Lava and water before floor: both are floors you can be standing in. */
    if (have_flag(f->flags, FF_LAVA))         return UI_TERRAIN_LAVA;
    if (have_flag(f->flags, FF_WATER))        return UI_TERRAIN_WATER;
    if (have_flag(f->flags, FF_SNOW))         return UI_TERRAIN_SNOW;
    if (have_flag(f->flags, FF_SLUSH))        return UI_TERRAIN_SNOW;

    if (have_flag(f->flags, FF_WALL))
    {
        if (have_flag(f->flags, FF_PERMANENT)) return UI_TERRAIN_PERMANENT;

        /* Rubble is diggable and worth noticing, so it is not plain rock. */
        if (!have_flag(f->flags, FF_HURT_ROCK)) return UI_TERRAIN_NONE;

        return UI_TERRAIN_GRANITE;
    }

    if (have_flag(f->flags, FF_FLOOR))
        return dun_level ? UI_TERRAIN_FLOOR : UI_TERRAIN_GROUND;

    return UI_TERRAIN_NONE;
}


/*
 * How coarsely each material clusters, and how often a grid inside a cluster
 * is picked out for a mark.
 *
 * Coarse blocks are the whole trick. Hashing each cell on its own gives
 * television static; hashing a block of them gives a patch, and a run of
 * patches reads as wear on a floor or a band in the rock. The numbers differ
 * per material because a floor wears in broad areas while a vein of quartz
 * runs in narrow seams.
 */
static void _cluster_size(int material, int *w, int *h)
{
    switch (material)
    {
    case UI_TERRAIN_GROUND:    *w = 6; *h = 4; break;
    case UI_TERRAIN_FLOOR:     *w = 4; *h = 3; break;
    case UI_TERRAIN_GRANITE:   *w = 3; *h = 2; break;
    case UI_TERRAIN_PERMANENT: *w = 4; *h = 3; break;
    case UI_TERRAIN_WATER:     *w = 5; *h = 2; break;
    case UI_TERRAIN_LAVA:      *w = 5; *h = 2; break;
    case UI_TERRAIN_SNOW:      *w = 6; *h = 4; break;
    default:                   *w = 4; *h = 3; break;
    }
}

/*
 * Is this grid picked out for wear?
 *
 * The tone is the only thing left, so it has to carry the whole effect.
 *
 * There were drawn pixel marks alongside it -- a speck of grit, a blade of
 * grass, a band in the rock -- and at the sizes this game is played at they
 * read as specks stuck to the characters rather than as texture, so they are
 * gone. What is left is some characters drawn a shade darker than their
 * neighbours, in patches, which is what wear on a floor actually looks like.
 *
 * Pitched by measurement, twice over: render all three levels and compare
 * crops at four times magnification before touching any of these numbers.
 */
static bool _chosen(int material, int style, u32b cluster, u32b detail)
{
    bool bold = BOOL(style == UI_TERRAIN_BOLD);

    switch (material)
    {
    case UI_TERRAIN_FLOOR:
        return bold ? BOOL(cluster % 4 < 3 && detail % 9 != 0)
                    : BOOL(cluster % 5 < 3 && detail % 7 != 0);

    case UI_TERRAIN_GROUND:
        return bold ? BOOL(cluster % 6 < 5 && detail % 9 != 0)
                    : BOOL(cluster % 5 < 4 && detail % 7 != 0);

    case UI_TERRAIN_GRANITE:
        return bold ? BOOL(cluster % 5 < 4 && detail % 7 != 0)
                    : BOOL(cluster % 3 < 2 && detail % 5 != 0);

    case UI_TERRAIN_PERMANENT:
        return bold ? BOOL(cluster % 3 < 2 && detail % 7 != 0)
                    : BOOL(cluster % 4 < 2 && detail % 5 != 0);

    case UI_TERRAIN_WATER:
    case UI_TERRAIN_LAVA:
        return bold ? BOOL(cluster % 5 < 4 && detail % 9 != 0)
                    : BOOL(cluster % 4 < 3 && detail % 7 != 0);

    default:
        return bold ? BOOL(cluster % 4 < 3 && detail % 7 != 0)
                    : BOOL(cluster % 5 < 3 && detail % 5 != 0);
    }
}



bool ui_terrain_at(int cave_x, int cave_y, int style, ui_terrain_t *out){
    cave_type *c_ptr;
    u32b       key, cluster, detail;
    int        material, cw, ch;
    bool       glowing;

    if (!out) return FALSE;
    if (style <= UI_TERRAIN_PLAIN || style >= UI_TERRAIN_STYLES) return FALSE;

    /* Before a level exists there is nothing to describe -- and cave[][] is
     * not safe to read. Birth and the start screen both come through here. */
    if (!character_generated) return FALSE;
    if (!in_bounds2(cave_y, cave_x)) return FALSE;

    c_ptr = &cave[cave_y][cave_x];

    /*
     * Anything the player knows about, whether they are looking at it now or
     * remembering it.
     *
     * Restricting this to line of sight was the first attempt, on the reasoning
     * that remembered ground is drawn duller precisely to say "you are not
     * looking at this", and decorating it would blur that. In practice almost
     * everything on screen is remembered, so the variation appeared as a smear
     * around the player and the rest of the map stayed as photocopied as
     * before -- which was the whole thing it was meant to fix.
     *
     * The distinction is safe anyway, because everything done here stays inside
     * the colour the game already chose: a remembered floor is drawn dim, so
     * its decoration comes out dim, and a lit one bright. The boundary keeps
     * itself, and lit is reported below so a frontend can lean on it further.
     */
    if (!(c_ptr->info & CAVE_MARK) && !player_has_los_bold(cave_y, cave_x))
        return FALSE;

    glowing = BOOL(c_ptr->info & (CAVE_GLOW | CAVE_LITE));

    material = _material(get_feat_mimic(c_ptr));
    if (material == UI_TERRAIN_NONE) return FALSE;

    key = _level_key();

    _cluster_size(material, &cw, &ch);

    cluster = _hash(key, cave_x / cw, cave_y / ch, material);
    detail  = _hash(key ^ 0xa511e9b3U, cave_x, cave_y, material);

    out->material = (byte)material;
    out->lit      = BOOL(glowing && (c_ptr->info & CAVE_VIEW)
                      && player_has_los_bold(cave_y, cave_x));
    out->torch    = BOOL(c_ptr->info & CAVE_LITE);
    out->seed     = (u16b)(detail ^ (detail >> 16));
    out->detail   = _chosen(material, style, cluster, detail);

    /*
     * Tone stays inside the colour the game already chose, rather than moving
     * to the next one along. Reassigning the colour would erase the boundary
     * between what is lit and what is only remembered, which is the single
     * most important thing the map colours say.
     */
    /*
     * Tone whether the grid is lit or only remembered.
     *
     * Restricting it to lit grids was over-caution: in a town almost everything
     * is remembered, so the one signal that actually reads as wear -- some
     * floor characters drawn a shade darker than their neighbours -- was
     * switched off nearly everywhere, and what was left was the background
     * wash on its own, which reads as haze rather than as ground. The tone
     * stays inside the colour the game chose, so it cannot blur the boundary
     * between what is lit and what is only remembered.
     */
    if (!out->detail)
        out->tone = 0;
    else if (material == UI_TERRAIN_LAVA)
        out->tone = (style == UI_TERRAIN_BOLD) ?  46 :  32;
    else
        out->tone = (style == UI_TERRAIN_BOLD) ? -42 : -30;

    return TRUE;
}


cptr ui_terrain_style_name(int style)
{
    switch (style)
    {
    case UI_TERRAIN_SUBTLE: return "Subtle";
    case UI_TERRAIN_BOLD:   return "Bold";
    default:                return "Plain";
    }
}


/*
 * The colour of the light outside. See ui_terrain.h for why brightness is not
 * part of it.
 *
 * The clock is the game's own: TOWN_DAWN ticks carry it from one dawn to the
 * next, and is_daytime() calls the first half of that day. So this is
 * reporting something the game already believes rather than inventing a cycle
 * of its own -- which matters, because the town lighting changes on the same
 * schedule and the two would otherwise drift apart.
 */
bool ui_daylight(ui_daylight_t *out)
{
    s32b len = TURNS_PER_TICK * TOWN_DAWN;
    int  phase;   /* thousandths through the day, zero at dawn */

    if (!out) return FALSE;
    if (!display_daylight_get()) return FALSE;
    if (!character_generated) return FALSE;

    /* No sky underground, and no sky in a quest or arena level either -- all
     * of which the depth already tells us. */
    if (dun_level) return FALSE;

    if (len <= 0) return FALSE;

    phase = (int)(((game_turn % len) * 1000) / len);

    /*
     * Dawn and dusk are short and gold; the night is long and blue; the middle
     * of the day is left exactly as the game drew it. The ramps matter more
     * than the numbers: an abrupt change of light at a tick boundary reads as
     * a glitch, and a slow one reads as the sun moving.
     */
    if (phase < 60)                      /* sunrise */
    {
        out->r = 0xFF; out->g = 0xB4; out->b = 0x5A;
        out->strength = 30 - (phase * 30) / 60;
        out->name = "dawn";
    }
    else if (phase < 440)                /* the day */
    {
        out->r = 0xFF; out->g = 0xFF; out->b = 0xFF;
        out->strength = 0;
        out->name = "day";
    }
    else if (phase < 500)                /* sunset */
    {
        out->r = 0xFF; out->g = 0x9E; out->b = 0x4A;
        out->strength = ((phase - 440) * 30) / 60;
        out->name = "dusk";
    }
    else                                 /* night */
    {
        int into = phase - 500;          /* 0 .. 499 */

        out->r = 0x5A; out->g = 0x7E; out->b = 0xC8;

        /* Ease in over the first fiftieth and out over the last, so the two
         * ends meet the gold of dusk and dawn without a step. */
        if (into < 40)            out->strength = (into * 24) / 40;
        else if (into > 460)      out->strength = ((500 - into) * 24) / 40;
        else                      out->strength = 24;

        out->name = "night";
    }

    return BOOL(out->strength > 0);
}


/*
 * What the sky here would be doing. See ui_terrain.h.
 *
 * The look around the player is deliberately small and cheap: this is asked
 * once per redraw, and a wrong answer costs a few flakes rather than rain,
 * not a wrong reading of the map.
 */
int ui_weather_kind(void)
{
    int snowy = 0, looked = 0;
    int dx, dy;

    if (!character_generated) return UI_WEATHER_NONE;

    /* No sky below the surface, and none in an arena or a quest either --
     * all of which the depth already tells us. */
    if (dun_level) return UI_WEATHER_NONE;

    for (dy = -4; dy <= 4; dy++)
    {
        for (dx = -4; dx <= 4; dx++)
        {
            int x = px + dx, y = py + dy;
            int material;

            if (!in_bounds2(y, x)) continue;

            material = _material(get_feat_mimic(&cave[y][x]));
            if (material == UI_TERRAIN_NONE) continue;

            looked++;
            if (material == UI_TERRAIN_SNOW) snowy++;
        }
    }

    /* A third of what is around you being snow is enough to call it snow:
     * ground under snow is patchy, and waiting for all of it would mean rain
     * falling on a snowfield. */
    if (looked && snowy * 3 >= looked) return UI_WEATHER_SNOW;

    return UI_WEATHER_RAIN;
}
