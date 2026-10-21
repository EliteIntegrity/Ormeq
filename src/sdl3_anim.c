/*
 * File: sdl3_anim.c
 *
 * The two things in the SDL3 frontend that move on their own clock.
 *
 * Everything else here draws because the game asked it to -- Term says the
 * screen changed and the frontend redraws it. These two do not. The weather
 * falls whether or not the player has pressed a key, and the home screen
 * drifts while nothing at all is happening, so both run on a millisecond
 * timer of their own and both have to answer the same question: how long may
 * the frontend sleep before I want the screen again?
 *
 * That question is the whole reason they share a file. sdl3_weather_due_ms()
 * and sdl3_home_due_ms() are the same idea twice, the event loop takes the
 * smaller of the two, and getting one of them wrong shows up as the other one
 * stuttering. They are easier to keep honest side by side.
 *
 * Lifted out of main-sdl3.c. See sdl3_local.h for what the port shares.
 *
 * Copyright (c) 2026 John Horton.
 *
 * Added since the fork and offered on the same terms as the rest of the
 * tree -- the Angband licence in LICENSE.
 */

#include "sdl3_local.h"

/*
 * Rain and snow, above ground.
 *
 * Modelled on the one in the Angband fork next door, which had already solved
 * the parts that matter, and there was no sense inventing worse versions:
 *
 *   - Every drop is derived, not stored. A particle's position is a hash of
 *     its index plus the frame number, so a hundred of them cost a hundred
 *     multiplies and no memory, and nothing has to be born or reaped.
 *   - Each one is aimed at a *grid*, and the last tenth of its fall is the
 *     splash landing on that grid. Rain that stops at the bottom of the
 *     viewport reads as weather on the glass; rain that lands on the floor
 *     reads as weather in the world.
 *   - Intensity drifts on its own timer, so it comes and goes rather than
 *     falling at one rate for ever.
 *   - Lightning is a double flash, because a single even one reads as the
 *     screen glitching rather than as lightning.
 *
 * Twenty ticks a second, not sixty. This is the only thing here that redraws
 * while the game sits waiting for a key, and it is being played on a laptop.
 */
#define WEATHER_TICK_MS      50
#define WEATHER_FLASH_TICKS  4

#define WEATHER_LIGHT   1
#define WEATHER_STEADY  2
#define WEATHER_HEAVY   3

static struct
{
    int    kind;           /* UI_WEATHER_*                       */
    int    intensity;      /* WEATHER_LIGHT .. WEATHER_HEAVY     */
    u32b   seed;
    u32b   frame;
    u32b   next_intensity;
    u32b   next_lightning;
    int    flash;
    Uint64 ticked;         /* when the last tick was, in ms      */
} sdl3_sky;

static u32b sdl3_weather_hash(u32b v)
{
    v ^= v >> 16;
    v *= 0x7feb352dU;
    v ^= v >> 15;
    v *= 0x846ca68bU;
    v ^= v >> 16;
    return v;
}

static u32b sdl3_weather_interval(u32b salt, u32b least, u32b span)
{
    return least + sdl3_weather_hash(sdl3_sky.seed ^ sdl3_sky.frame ^ salt) % span;
}

/* What should be falling right now, given the setting and the world. */
static int sdl3_weather_kind(void)
{
    int mode = display_weather_get();

    if (mode == DISPLAY_WEATHER_OFF) return UI_WEATHER_NONE;
    if (ui_screen_has_layer(UI_LAYER_MENU)) return UI_WEATHER_NONE;
    if (sdl3_panel_active) return UI_WEATHER_NONE;
    if (!sdl3_map_showing()) return UI_WEATHER_NONE;

    /* Even a forced Rain stops underground. A setting is a preference about
     * weather, not an instruction to invent a sky where the game says there
     * is none. */
    if (ui_weather_kind() == UI_WEATHER_NONE) return UI_WEATHER_NONE;

    if (mode == DISPLAY_WEATHER_RAIN) return UI_WEATHER_RAIN;
    if (mode == DISPLAY_WEATHER_SNOW) return UI_WEATHER_SNOW;

    return ui_weather_kind();
}

/* One tick. TRUE if anything moved and the screen wants presenting. */
static Uint64 sdl3_home_ticked = 0;
#define SDL3_HOME_TICK_MS 40

bool sdl3_home_advance(void)
{
    Uint64 now = SDL_GetTicks();

    if (!sdl3_home_active()) return FALSE;
    if (now - sdl3_home_ticked < SDL3_HOME_TICK_MS) return FALSE;

    sdl3_home_ticked = now;
    return TRUE;
}

bool sdl3_weather_advance(void)
{
    Uint64 now  = SDL_GetTicks();
    int    kind = sdl3_weather_kind();

    if (kind == UI_WEATHER_NONE)
    {
        sdl3_sky.kind  = UI_WEATHER_NONE;
        sdl3_sky.flash = 0;
        return FALSE;
    }

    if (kind != sdl3_sky.kind)
    {
        /* A change of weather starts its own run rather than inheriting the
         * last one's timers, so walking out of the snow does not bring the
         * rain in mid-downpour. */
        sdl3_sky.kind      = kind;
        sdl3_sky.intensity = WEATHER_LIGHT;
        sdl3_sky.flash     = 0;

        if (!sdl3_sky.seed) sdl3_sky.seed = 0x9e3779b9U;

        sdl3_sky.next_intensity = sdl3_sky.frame
                                + sdl3_weather_interval(0x6122d34fU, 240, 500);
        sdl3_sky.next_lightning = sdl3_sky.frame
                                + sdl3_weather_interval(0xc33905ebU, 900, 1800);
    }

    if (now - sdl3_sky.ticked < WEATHER_TICK_MS) return FALSE;
    sdl3_sky.ticked = now;

    sdl3_sky.frame++;
    if (sdl3_sky.flash) sdl3_sky.flash--;

    if (sdl3_sky.frame >= sdl3_sky.next_intensity)
    {
        int want = WEATHER_LIGHT
                 + (int)(sdl3_weather_hash(sdl3_sky.seed ^ sdl3_sky.frame
                                           ^ 0x7d61a85bU) % 3U);

        /* Never the same twice: an interval that expires and changes nothing
         * is an interval you cannot see. */
        if (want == sdl3_sky.intensity)
            want = (want == WEATHER_HEAVY) ? WEATHER_LIGHT : want + 1;

        sdl3_sky.intensity = want;
        sdl3_sky.next_intensity = sdl3_sky.frame
                                + sdl3_weather_interval(0x214ec753U, 240, 500);
    }

    if (sdl3_sky.kind == UI_WEATHER_RAIN
     && sdl3_sky.frame >= sdl3_sky.next_lightning)
    {
        sdl3_sky.flash = WEATHER_FLASH_TICKS;
        sdl3_sky.next_lightning = sdl3_sky.frame
                                + sdl3_weather_interval(0xda244417U, 900, 1800);
    }

    return TRUE;
}

/*
 * How long until the weather needs drawing again, in milliseconds. -1 if there
 * is no weather and nothing is coming.
 *
 * This exists because of a bug that hid the whole feature. The idle loop drew
 * a frame whenever something asked it to and otherwise blocked in
 * SDL_WaitEvent -- which is right for sprites, because a live sprite keeps
 * saying yes, but wrong for weather, which says yes once every fifty
 * milliseconds and no in between. So the first tick happened, the second was
 * due fifty milliseconds later, and nothing was ever going to wake us up for
 * it. The rain has been falling one frame at a time since it was written.
 */
int sdl3_weather_due_ms(void)
{
    Uint64 now, since;

    if (sdl3_weather_kind() == UI_WEATHER_NONE) return -1;

    now   = SDL_GetTicks();
    since = now - sdl3_sky.ticked;

    if (since >= WEATHER_TICK_MS) return 0;

    return (int)(WEATHER_TICK_MS - since);
}

/* The home screen animates too, and wants waking for the same reason. */
int sdl3_home_due_ms(void)
{
    Uint64 now, since;

    if (!sdl3_home_active()) return -1;

    now   = SDL_GetTicks();
    since = now - sdl3_home_ticked;

    if (since >= SDL3_HOME_TICK_MS) return 0;

    return (int)(SDL3_HOME_TICK_MS - since);
}

static int sdl3_weather_count(void)
{
    static const int rain[4] = { 0, 45, 80, 130 };
    static const int snow[4] = { 0, 30, 55,  90 };
    int i = sdl3_sky.intensity;

    if (i < 1) i = 1;
    if (i > 3) i = 3;

    return (sdl3_sky.kind == UI_WEATHER_SNOW) ? snow[i] : rain[i];
}

void sdl3_draw_weather(void)
{
    SDL_Rect  clip;
    SDL_FRect area;
    rect_t    map = ui_map_rect();
    rect_t    msg = msg_line_rect();
    int       count, i, top;

    if (sdl3_sky.kind == UI_WEATHER_NONE) return;
    if (sdl3_weather_kind() == UI_WEATHER_NONE) return;

    /* The sky is as wide as the window, because the level is. It used to stop
     * at the game's map rectangle, which was the edge of the world when the
     * chrome stood beside the map; now the chrome floats on it and rain that
     * stopped in a hard line down the side of the character panel would say
     * so. It still keeps clear of the message line, which is an overlay that
     * grows downwards and is meant to be read. */
    if (sdl3_map_overlay()) map = rect(0, 0, sdl3.cols, sdl3.rows);

    /* Keep clear of the message line, which is an overlay inside the map
     * region and grows downwards -- the same rule the zoomed map follows. */
    top = map.y;
    if (msg.cy > 0 && msg.y + msg.cy > top) top = msg.y + msg.cy;
    if (top >= map.y + map.cy) return;

    area.x = (float)(map.x * sdl3.cell_w);
    area.y = (float)(top * sdl3.cell_h);
    area.w = (float)(map.cx * sdl3.cell_w);
    area.h = (float)((map.y + map.cy - top) * sdl3.cell_h);

    clip.x = (int)area.x;
    clip.y = (int)area.y;
    clip.w = (int)area.w;
    clip.h = (int)area.h;

    SDL_SetRenderClipRect(sdl3.renderer, &clip);
    SDL_SetRenderDrawBlendMode(sdl3.renderer, SDL_BLENDMODE_BLEND);

    count = sdl3_weather_count();

    for (i = 0; i < count; i++)
    {
        u32b  seed = sdl3_weather_hash(sdl3_sky.seed ^ ((u32b)i * 0x9e3779b9U));
        int   cols = map.cx;
        int   rows = (map.y + map.cy) - top;
        int   gx   = cols ? (int)((seed >> 3)  % (u32b)cols) : 0;
        int   gy   = rows ? (int)((seed >> 13) % (u32b)rows) : 0;

        /* The grid this one is aimed at, and where on it it lands. */
        float land_x = area.x + (float)(gx * sdl3.cell_w)
                     + (float)sdl3.cell_w * (0.25f + (float)(seed & 255U) / 510.0f);
        float land_y = area.y + (float)(gy * sdl3.cell_h)
                     + (float)sdl3.cell_h * 0.72f;

        u32b  speed = (sdl3_sky.kind == UI_WEATHER_RAIN)
                    ? (37U + (seed >> 24) % 17U)
                    : (11U + (seed >> 24) % 9U);
        u32b  fall  = (sdl3_sky.kind == UI_WEATHER_RAIN) ? 900U : 950U;
        u32b  phase = ((seed >> 8) + sdl3_sky.frame * speed) % 1000U;

        if (phase < fall)
        {
            float progress = (float)phase / (float)fall;
            float y = area.y - 8.0f + progress * (land_y - area.y + 8.0f);

            if (sdl3_sky.kind == UI_WEATHER_RAIN)
            {
                /* Slanted, and the slant is proportional to how far there is
                 * still to fall, so the drops converge on where they land
                 * rather than running in parallel like wallpaper. */
                float length = 4.0f + 1.5f * sdl3_sky.intensity;
                float x = land_x - (land_y - y) * 0.08f;

                SDL_SetRenderDrawColor(sdl3.renderer, 146, 190, 222,
                                       (Uint8)(85 + sdl3_sky.intensity * 20));
                SDL_RenderLine(sdl3.renderer, x, y, x - length * 0.45f, y + length);
            }
            else
            {
                int   step  = (int)((sdl3_sky.frame + (u32b)i * 13U) % 24U);
                float drift = (float)(step - 12) * 0.22f;
                float x = land_x + drift;
                float r = (sdl3_sky.intensity == WEATHER_HEAVY) ? 1.5f : 1.0f;

                SDL_SetRenderDrawColor(sdl3.renderer, 226, 235, 242, 175);
                SDL_RenderLine(sdl3.renderer, x - r, y, x + r, y);
                SDL_RenderLine(sdl3.renderer, x, y - r, x, y + r);
            }
        }
        else
        {
            float settle = (float)(phase - fall) / (float)(1000U - fall);

            if (sdl3_sky.kind == UI_WEATHER_RAIN)
            {
                float w = (float)sdl3.cell_w * (0.18f + settle * 0.16f);

                if (w < 2.0f) w = 2.0f;

                SDL_SetRenderDrawColor(sdl3.renderer, 164, 205, 232,
                                       (Uint8)(150.0f * (1.0f - settle)));
                SDL_RenderLine(sdl3.renderer, land_x - w, land_y - 1.0f,
                                              land_x - w * 0.35f, land_y);
                SDL_RenderLine(sdl3.renderer, land_x + w * 0.35f, land_y,
                                              land_x + w, land_y - 1.0f);
                SDL_RenderLine(sdl3.renderer, land_x - w * 0.45f, land_y + 1.0f,
                                              land_x + w * 0.45f, land_y + 1.0f);
            }
            else
            {
                SDL_SetRenderDrawColor(sdl3.renderer, 232, 239, 244,
                                       (Uint8)(120.0f * (1.0f - settle)));
                SDL_RenderLine(sdl3.renderer, land_x - 1.0f, land_y,
                                              land_x + 1.0f, land_y);
            }
        }
    }

    if (sdl3_sky.kind == UI_WEATHER_RAIN && sdl3_sky.flash)
    {
        /* Two bright ticks with a dim one between. */
        static const Uint8 alpha[WEATHER_FLASH_TICKS + 1] = { 0, 14, 48, 22, 72 };
        int f = sdl3_sky.flash;

        if (f > WEATHER_FLASH_TICKS) f = WEATHER_FLASH_TICKS;

        SDL_SetRenderDrawColor(sdl3.renderer, 214, 228, 255, alpha[f]);
        SDL_RenderFillRect(sdl3.renderer, &area);
    }

    SDL_SetRenderDrawBlendMode(sdl3.renderer, SDL_BLENDMODE_NONE);
    SDL_SetRenderClipRect(sdl3.renderer, NULL);
}


/*
 * The home screen.
 *
 * Two things, one behind the other.
 *
 * In front: the game's name, as large as the typeface will bake it, with the
 * final letter never quite arriving. That is not a graphical flourish -- it is
 * the setting's one rule, which is that you may say the name as far as the end
 * and the end is the address. The title flinches, in the same place and for the
 * same reason everybody in the game flinches, and the player will have been
 * saying the whole thing aloud every time they launched it.
 *
 * Behind: creatures out of the collection, surfacing very dark, drifting, and
 * gone again. They are the plates the monster recall already uses, drawn at a
 * few per cent of their strength, so the screen behind the name is the wreckage
 * of places somebody said it in. Nothing is ever the same twice and nothing is
 * ever quite legible, which is the point: this is not a splash illustration,
 * it is something moving in a dark room.
 */

#define SDL3_HOME_DRIFT   3      /* how many can be in the air at once     */

/*
 * Where they surface, and how big.
 *
 * Fixed, deliberately. Scattering them at random left holes -- three small
 * shapes adrift in a lot of black, which is not a dark room, it is an empty
 * one. So the frame is composed once and only the occupants change: something
 * very large down the right that runs off the top and the bottom of the screen,
 * one low and centre, one smaller in the corner. A shape cropped by the edge of
 * the frame reads as a thing that is too big to fit. The same shape floating
 * clear of every edge reads as a sticker.
 *
 * The big one is faintest. It has the most cells, so at equal strength it would
 * carry the most ink and stop being a backdrop.
 */
static const struct { float cx, cy, side; Uint8 alpha; }
sdl3_home_slot[SDL3_HOME_DRIFT] =
{
    { 0.72f, 0.44f, 1.30f, 30 },   /* the wall */
    { 0.54f, 0.88f, 0.58f, 36 },   /* low, off the bottom edge */
    { 0.94f, 0.78f, 0.44f, 42 },   /* off the right edge */
};

/*
 * Everything we have a picture of, minus the uniques.
 *
 * It used to be seventeen, hand-picked while the renaming was still going on
 * -- a home screen is the last place to advertise a name being removed. That
 * is finished, and there are eighty-four plates now, so the list is simply
 * all of them. Uniques are left out on purpose: the thing at the bottom of
 * the dungeon should not drift across the title screen before anybody has
 * met it.
 */
static cptr sdl3_home_art[] =
{
    "2-headed hydra", "4-headed hydra", "5-headed hydra",
    "7-headed hydra", "9-headed hydra", "Abyss worm mass",
    "Aether hound", "Aether vortex", "Agent of black market",
    "Agent of Vane", "Aimless looking merchant", "Air elemental",
    "Air hound", "Air spirit", "Thornhide",
    "Ancient black dragon", "Ancient blue dragon", "Ancient gold dragon",
    "Ancient green dragon", "Ancient multi-hued dragon", "Ancient red dragon",
    "Ancient roc", "Ancient white dragon", "Angel",
    "Arcframe", "Archlich", "Archpriest",
    "Ash basin bat", "Baby black dragon", "Baby blue dragon",
    "Baby green dragon", "Baby multi-hued dragon", "Baby red dragon",
    "Baby white dragon", "Balance drake", "Bandit",
    "Banshee", "Basilisk", "Battle scarred veteran",
    "Berserker", "Black harpy", "Black knight",
    "Black mamba", "Black naga", "Black ogre",
    "Black ooze", "Black wraith", "Blackreach marshal",
    "Bladewheel demon", "Blinkcat", "Bloodshot eye",
    "Bloodshot icky thing", "Blubbering icky thing", "Blubbering idiot",
    "Blue dragon bat", "Blue yeek", "Bog mass",
    "Bone golem", "Bronze golem", "Brown mold",
    "Brown yeek", "Bush ranger", "Catoblepas",
    "Caustic bellows demon", "Caustic icky thing", "Cave devil bat",
    "Cave lizard", "Cave ogre", "Cave orc",
    "Cave spider", "Cave troll", "Chaos drake",
    "Chaos hound", "Chest mimic", "Chimera",
    "Chronoseam hound", "Cinder spirit", "Cinderfold demon",
    "Cindermaw marshal", "Clay golem", "Clear hound",
    "Clear icky thing", "Clear mushroom patch", "Clear worm mass",
    "Cloud giant", "Coilward sentinel", "Cold hound",
    "Cold vortex", "Colossus", "Copperhead snake",
    "Corridor jelly", "Corvane of Many Colours", "Creeping adamantite coins",
    "Creeping copper coins", "Creeping gold coins", "Creeping silver coins",
    "Creeping starmetal coins", "Crow", "Crystal drake",
    "Cyclops", "Day-troll", "Death drake",
    "Death mold", "Death watch beetle", "Deathcoil colony",
    "Demonic quylthulg", "Demonologist", "Dimensional shambler",
    "Disenchanter eye", "Disenchanter mold", "Disenchanter worm mass",
    "Doombat", "Draconic quylthulg", "Dread",
    "Drudge", "Druid", "Dusk elf druid",
    "Dusk elf lord", "Dusk elf mage", "Dusk elf priest",
    "Dusk elf warlock", "Dusk elf warrior", "Dusk elf",
    "Eagle", "Earth elemental", "Earth hound",
    "Earth spirit", "Elder vampire", "Elderbough",
    "Eldrak", "Elf lord", "Elvar Bluecloak",
    "Emperor wight", "Enchantress", "Energy hound",
    "Energy vortex", "Ethereal dragon", "Ethereal drake",
    "Fallen angel", "Farmer Bulley", "Fearmaster",
    "Fieldcap patch", "Filthy street urchin", "Fire elemental",
    "Fire giant", "Fire hound", "Fire spirit",
    "Fire vortex", "Flaming crow", "Floating eye",
    "Floating orb", "Floor snare", "Forest troll",
    "Frost giant", "Frost spirit", "Frosty jelly",
    "Fruit bat", "Frumious bandersnatch", "Furnace baron",
    "Furnace chimera", "Gazer", "Ghast",
    "Ghost", "Ghoul", "Giant army ant",
    "Giant black ant", "Giant black dragon fly", "Giant blue ant",
    "Giant bronze dragon fly", "Giant brown bat", "Giant clear centipede",
    "Giant fire ant", "Giant flea", "Giant fruit fly",
    "Giant gold dragon fly", "Giant green dragon fly", "Giant green frog",
    "Giant grey ant", "Giant grey rat", "Giant grey scorpion",
    "Giant mutant ant", "Giant pink ant", "Giant pink frog",
    "Giant pink scorpion", "Giant red tick", "Giant salamander",
    "Giant spider", "Giant tarantula", "Giant white ant",
    "Giant white centipede", "Giant white dragon fly", "Giant white louse",
    "Giant white mouse", "Giant white rat", "Giant white tick",
    "Giant yellow scorpion", "Gorgon", "Grand master mystic",
    "Grape jelly", "Grave eye idol", "Grave hand idol",
    "Grave imp", "Grave skull idol", "Grave wight",
    "Gravebone dragon", "Gravebound knight", "Gravefield tyrant",
    "Gravewall breaker", "Gravity hound", "Great Bile Wyrm",
    "Great crystal drake", "Great Gazer", "Great Hell Wyrm",
    "Great Ice Wyrm", "Great Orc", "Great Storm Wyrm",
    "Great wolf", "Great Wyrm of Balance", "Great Wyrm of Chaos",
    "Great Wyrm of Law", "Great Wyrm of Many Colours", "Greater basilisk",
    "Greater demonic quylthulg", "Greater draconic quylthulg", "Greater Emberlord",
    "Greater mummy", "Greater rotting quylthulg", "Greater titan",
    "Green glutton ghost", "Green jelly", "Green mold",
    "Green naga", "Green worm mass", "Grey icky thing",
    "Grey mold", "Grey wraith", "Griffon",
    "Grizzly bear", "Hairy mold", "Hardened warrior",
    "Hateflame", "High priest", "Hill giant",
    "Hill orc", "Hippogriff", "Hobo",
    "Homonculus", "Hummerhorn", "Hushmoss",
    "Ice elemental", "Ice troll", "Illusionist",
    "Imp", "Impact hound", "Indigo scavenger",
    "Inertia hound", "Iron golem", "Jabberwock",
    "Killer brown beetle", "Killer crimson beetle", "Killer iridescent beetle",
    "Killer red beetle", "Killer slicer beetle", "Killer stag beetle",
    "Killer white beetle", "Kilnmaw demon", "King cobra",
    "Knight archer", "Kobold", "Large grey snake",
    "Large kobold", "Large white snake", "Lashspine demon",
    "Law drake", "Lemure", "Lesser Emberlord",
    "Lesser gazer", "Lesser titan", "Lich",
    "Light hound", "Lost soul", "Lurker",
    "Mad bear", "Mage", "Magma elemental",
    "Mangy looking leper", "Manticore", "Marrowframe demon",
    "Master face thief", "Master lich", "Master mystic",
    "Master quylthulg", "Master rogue", "Master thief",
    "Master vampire", "Master yeek", "Mature black dragon",
    "Mature blue dragon", "Mature gold dragon", "Mature green dragon",
    "Mature multi-hued dragon", "Mature red dragon", "Mature white dragon",
    "Mean looking mercenary", "Metallic blue centipede", "Metallic green centipede",
    "Metallic red centipede", "Minotaur", "Moaning spirit",
    "Mother Hydra", "Mound wight", "Multi-hued hound",
    "Mummified human", "Mummified orc", "Mummified troll",
    "Mystic", "Nether hound", "Nether wraith",
    "Nexus hound", "Nexus quylthulg", "Nexus spider",
    "Nexus vortex", "Night lizard", "Night mare",
    "Nightblade", "Nightroot", "Nightspinner",
    "Nightwood spider", "Ninja", "Novice archer",
    "Novice mage", "Novice paladin", "Novice priest",
    "Novice ranger", "Novice rogue", "Novice warrior",
    "Rotling", "Ochre solvent", "Offal crawler",
    "Ogre brute", "Ogre mage", "Ogre shaman",
    "Ogre", "Old sorcerer", "Ooze elemental",
    "Orc captain", "Orc shaman", "Orckin raider",
    "Paladin", "Pallcoil", "Pallwalker",
    "Pallwing", "Panther", "Pink jelly",
    "Pink naga", "Pitiful looking beggar", "Planetar",
    "Plasma hound", "Plasma vortex", "Plated vein grazer",
    "Poltergeist", "Potion mimic", "Priest",
    "Pseudo dragon", "Purple mushroom patch", "Pyrebristle demon",
    "Quylthulg", "Radiation eye", "Ranger",
    "Rat shaman", "Rat shifter", "Rattle beetle",
    "Rattlesnake", "Raving lunatic", "Red dragon bat",
    "Red mold", "Red worm mass", "Riftgaze dragon",
    "Rimecarapace demon", "Ring mimic", "Rock lizard",
    "Rock mole", "Rotting quylthulg", "Sabre-tooth tiger",
    "Salamander", "Sasquatch", "Scrawny cat",
    "Scroll mimic", "Scruffy little dog", "Scruffy looking warrener",
    "Seam spider", "Seamworm mass", "Shade",
    "Shadow drake", "Shadow fairy", "Shadow hound",
    "Shard vortex", "Shimmering vortex", "Shrieker mushroom patch",
    "Sidhe ranger", "Silver jelly", "Skeleton human",
    "Skeleton kobold", "Skeleton orc", "Skeleton troll",
    "Skip hound", "Sky dragon", "Slime Mold",
    "Slimy jelly", "Slimy ooze", "Slush pile",
    "Small kobold", "Smoke elemental", "Snow huntress",
    "Soldier ant", "Sorcerer", "Soul",
    "Spectre", "Spirit troll", "Spite imp",
    "Spotted jelly", "Spotted mushroom patch", "Starmetal golem",
    "Stitchwork golem", "Stone giant", "Stone golem",
    "Stone troll", "Stonebody sentinel", "Storm giant",
    "Swordsman", "Taipan", "Tengu",
    "That @$^%& Black Bat", "The Archer Huntsman", "The Bear Cult King",
    "The Emperor Lich", "The Gazer-King", "The Long Silence",
    "The Louse King", "The Midwinter Patron", "The Navigator",
    "The Old Willow", "The Phoenix", "The Queen Ant",
    "The Stonefist", "The Wallbreaker", "Thoughteater",
    "Threshold idol", "Tiger", "Time hound",
    "Time vortex", "Troll king", "Troll priest",
    "Tunnel brute", "Twin troll skeleton", "Twin troll",
    "Undead gazer", "Unmoored lich", "Vampire bat",
    "Vampire lord", "Vampire", "Veil naga",
    "Vein grazer", "Venom auger", "Venom spider",
    "Venomwork drake", "Vibration hound", "War bear",
    "War mammoth", "War troll", "Water elemental",
    "Water hound", "Water spirit", "Water vortex",
    "Weir", "Werewolf", "White harpy",
    "White icky thing", "White wolf", "White worm mass",
    "White wraith", "Wild cat", "Winged Horror",
    "Witch", "Wolf Chieftain", "Wolf",
    "Wood spider", "Woodsman", "Woodward",
    "Worldcore golem", "Wyvern", "Yellow jelly",
    "Yellow mold", "Yellow mushroom patch", "Yellow worm mass",
    "Yeti", "Young black dragon", "Young blue dragon",
    "Young gold dragon", "Young green dragon", "Young multi-hued dragon",
    "Young red dragon", "Young silver dragon", "Young white dragon",
    "Zombified human", "Zombified kobold", "Zombified orc"
};

typedef struct
{
    int    which;
    float  x, y, vx, vy;
    float  side;
    Uint64 born;
    int    life;
    bool   live;
} sdl3_drift_t;

static sdl3_drift_t sdl3_home_drift[SDL3_HOME_DRIFT];

bool sdl3_home_active(void)
{
    /* And only while the menu itself is up. A page opened from it -- the
     * lore, the settings list -- gets a solid ground and the reader's whole
     * attention, rather than creatures moving about behind the text. */
    /* The start screen's own menu, and not the lore page or the settings
     * opened from it -- those are pages in their own right and want a solid
     * ground and the reader's whole attention. See ui_doc_menu_rect(). */
    return BOOL(ui_home_layout && sdl3_panel_active);
}

/* One fragment, at an arbitrary size and strength. */
void sdl3_draw_art_box(const ui_art_t *art, float px, float py,
                              float w, float h, Uint8 alpha)
{
    const sdl3_atlas_entry *atlas;
    float cw, ch, ox, oy;
    int   row, col;

    if (!art || art->cols <= 0 || art->rows <= 0 || alpha == 0) return;
    if (w <= 0.0f || h <= 0.0f) return;

    /* Square sub-tiles, fitted to whichever axis runs out first, and centred
     * in what is left. Square because the picture's cells are square: a plate
     * that is thirty across and forty down is a shape, and drawing it on
     * one-by-two character cells would make it a different one. */
    cw = w / (float)art->cols;
    ch = h / (float)art->rows;
    if (ch < cw) cw = ch; else ch = cw;

    ox = px + (w - cw * (float)art->cols) / 2.0f;
    oy = py + (h - ch * (float)art->rows) / 2.0f;

    atlas = sdl3_atlas_for(ch);
    SDL_SetTextureAlphaMod(atlas ? atlas->tex : sdl3.atlas, alpha);

    for (row = 0; row < art->rows; row++)
    {
        for (col = 0; col < art->cols; col++)
        {
            const ui_art_cell *c = &art->cells[row * art->cols + col];
            int                slot;

            if (!c->ink) continue;

            slot = sdl3_atlas_slot_of(c->cp);
            if (slot < 0) continue;

            SDL_SetTextureColorMod(atlas ? atlas->tex : sdl3.atlas,
                                   c->r, c->g, c->b);
            sdl3_draw_slot_scaled(atlas, slot, ox + col * cw, oy + row * ch,
                                  cw, ch, ATLAS_MAP);
        }
    }

    SDL_SetTextureColorMod(atlas ? atlas->tex : sdl3.atlas, 255, 255, 255);
    SDL_SetTextureAlphaMod(atlas ? atlas->tex : sdl3.atlas, SDL_ALPHA_OPAQUE);
}


/* A square box. The look card wants one, and said so twice before this was
 * a separate function. */
void sdl3_draw_art_rect(const ui_art_t *art, float px, float py,
                        float side, Uint8 alpha)
{
    sdl3_draw_art_box(art, px, py, side, side, alpha);
}

static void sdl3_home_spawn(sdl3_drift_t *d, Uint64 now, int i)
{
    int   n    = (int)(sizeof(sdl3_home_art) / sizeof(sdl3_home_art[0]));
    u32b  seed = sdl3_weather_hash((u32b)now + (u32b)i * 0x9e3779b9U);

    d->which = (int)(seed % (u32b)n);

    /* Its place is the slot's; only enough drift to stop two visits looking
     * like the same photograph. */
    d->side  = (float)sdl3.grid_h * sdl3_home_slot[i].side
             * (0.94f + (float)((seed >> 8) & 31) / 260.0f);
    d->x     = (float)sdl3.grid_w * sdl3_home_slot[i].cx - d->side * 0.5f
             + (float)sdl3.grid_w * ((float)((seed >> 14) & 63) - 31.5f) / 900.0f;
    d->y     = (float)sdl3.grid_h * sdl3_home_slot[i].cy - d->side * 0.5f
             + (float)sdl3.grid_h * ((float)((seed >> 24) & 63) - 31.5f) / 900.0f;

    /* Slow enough that you are never sure it moved. */
    d->vx   = ((float)((seed >> 5) & 31) - 15.5f) * 0.0009f;
    d->vy   = ((float)((seed >> 19) & 31) - 15.5f) * 0.0006f;
    d->born = now;
    d->life = 11000 + (int)((seed >> 3) % 7000U);
    d->live = TRUE;
}

/*
 * The fragments, then the name over them.
 *
 * Drawn before the terminal goes down, so the menu -- which paints its own
 * panel -- sits cleanly on top and stays readable. See sdl3_ground_colour(),
 * which punches the rest of the screen through to this.
 */
void sdl3_draw_home(void)
{
    Uint64 now = SDL_GetTicks();
    int    i;

    if (!sdl3_home_active()) return;

    /**** behind: the collection ****/
    for (i = 0; i < SDL3_HOME_DRIFT; i++)
    {
        sdl3_drift_t *d = &sdl3_home_drift[i];
        float         t, fade;
        const ui_art_t *art;

        if (!d->live)
        {
            /* Stagger the first three so they do not arrive together. */
            sdl3_home_spawn(d, now + (Uint64)i * 3700U, i);
            d->born = now - (Uint64)(i * d->life / SDL3_HOME_DRIFT);
        }

        t = (float)(int)(now - d->born) / (float)d->life;
        if (t >= 1.0f) { sdl3_home_spawn(d, now, i); t = 0.0f; }

        /* In, hold, out. A fragment is never at full strength for long. */
        fade = (t < 0.25f) ? t / 0.25f
             : (t > 0.72f) ? (1.0f - t) / 0.28f
             : 1.0f;
        if (fade < 0.0f) fade = 0.0f;

        art = ui_art_get_px(UI_ART_MONSTER, sdl3_home_art[d->which],
                            (int)d->side, (int)d->side);
        if (!art) continue;

        sdl3_draw_art_rect(art,
                           d->x + d->vx * (float)(int)(now - d->born),
                           d->y + d->vy * (float)(int)(now - d->born),
                           d->side,
                           (Uint8)((float)sdl3_home_slot[i].alpha * fade));
    }

    /**** in front: the name ****/
    {
        static const char *title = "ORMEQ";
        const sdl3_atlas_entry *atlas;
        float ch, cw, total, x, y;
        int   k, n = 5;

        /* As tall as the typeface will go without the ladder giving up, and
         * sized off the window so it holds at any interface size. */
        ch = (float)sdl3.grid_h * 0.215f;
        cw = ch * (float)sdl3.cell_w / (float)sdl3.cell_h;

        total = cw * (float)n;

        /*
         * Hard against the menu's own left margin, directly above it. Title and
         * menu are then one column of type down one side of the frame, which is
         * a layout; a word centred over a box in the middle is two things that
         * happen to be on the same screen.
         *
         * Two columns in, because the rows are indented by that much and the
         * eye lines up letters, not panels.
         */
        x = sdl3_panel_active
          ? (float)((sdl3_panel_x + 2) * sdl3.cell_w)
          : ((float)sdl3.grid_w - total) / 2.0f;

        y = sdl3_panel_active
          ? (float)(sdl3_panel_y * sdl3.cell_h) - ch * 1.22f
          : (float)sdl3.grid_h * 0.22f;
        if (y < 0.0f) y = 0.0f;

        atlas = sdl3_atlas_for(ch);

        for (k = 0; k < n; k++)
        {
            SDL_Texture  *tex = atlas ? atlas->tex : sdl3.atlas;
            theme_color_t c   = theme_ui(THEME_UI_TITLE);
            Uint8         a   = SDL_ALPHA_OPAQUE;

            /*
             * The last letter never quite arrives.
             *
             * Not a blink -- a blink reads as a fault. It breathes somewhere
             * between a tenth and a third of the others, so it is legible
             * enough to know what the word is and never solid enough to have
             * been said.
             */
            if (k == n - 1)
            {
                float w = 0.20f + 0.13f * (float)sin((double)now * 0.0011)
                                + 0.06f * (float)sin((double)now * 0.0037);

                a = (Uint8)(255.0f * (w < 0.05f ? 0.05f : w));
            }

            SDL_SetTextureColorMod(tex, c.r, c.g, c.b);
            SDL_SetTextureAlphaMod(tex, a);
            sdl3_draw_glyph_scaled(atlas, x + cw * (float)k, y, cw, ch,
                                   title[k], ATLAS_TEXT);
            SDL_SetTextureAlphaMod(tex, SDL_ALPHA_OPAQUE);
            SDL_SetTextureColorMod(tex, 255, 255, 255);
        }

        /*
         * The subtitle, small and quiet, tucked under the title's left edge.
         *
         * "say it again" is not a tagline. It is the instruction the player
         * has already been following: they have said the whole name every
         * time they launched the game, and the Q above has never once
         * finished. Lower case and dim, because a dare delivered loudly is
         * not a dare.
         */
        {
            static const char *sub = "say it again";
            const sdl3_atlas_entry *sa;
            float sh = ch * 0.135f;
            float sw = sh * (float)sdl3.cell_w / (float)sdl3.cell_h;
            theme_color_t c = theme_ui(THEME_UI_TITLE);
            SDL_Texture *tex;
            int j;

            sa  = sdl3_atlas_for(sh);
            tex = sa ? sa->tex : sdl3.atlas;

            SDL_SetTextureColorMod(tex, c.r, c.g, c.b);
            SDL_SetTextureAlphaMod(tex, 120);
            for (j = 0; sub[j]; j++)
            {
                sdl3_draw_glyph_scaled(sa, x + sw * (float)j,
                                       y + ch * 1.02f, sw, sh,
                                       sub[j], ATLAS_TEXT);
            }
            SDL_SetTextureAlphaMod(tex, SDL_ALPHA_OPAQUE);
            SDL_SetTextureColorMod(tex, 255, 255, 255);
        }
    }
}
