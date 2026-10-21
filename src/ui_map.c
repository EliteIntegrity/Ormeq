/*
 * File: ui_map.c
 *
 * The map layer. See ui_map.h for what it is and why it pulls rather than
 * pushes.
 *
 * Copyright (c) 2026 John Horton.
 *
 * Added since the fork and offered on the same terms as the rest of the
 * tree -- the Angband licence in LICENSE.
 */

#include "scottish.h"

/* map_info can roll cosmetic colours and hallucinations. Frontend reads must
 * leave every gameplay RNG field intact, including during hallucination. */
static void display_map_info(int y, int x, byte *a, char *c, byte *ta, char *tc)
{
    u32b state[RAND_DEG], value = Rand_value;
    u16b place = Rand_place;
    bool quick = Rand_quick;
    memcpy(state,Rand_state,sizeof(state));
    Rand_quick = TRUE;
    Rand_value = (u32b)x*73856093u ^ (u32b)y*19349663u ^ (u32b)game_turn;
    map_info(y,x,a,c,ta,tc);
    memcpy(Rand_state,state,sizeof(state));
    Rand_value = value; Rand_place = place; Rand_quick = quick;
}

float ui_map_safe_axis(float origin, int focus, float scale, float low, float high)
{
    float available, margin, first, last, at;
    if(scale<=0 || high<=low) return origin;
    available=MAX(0,high-low-scale);
    margin=MIN(3*scale,available*.25f);
    first=low+margin; last=high-margin-scale;
    if(first>last) first=last=(low+high-scale)*.5f;
    at=origin+focus*scale;
    if(at<first) return origin+first-at;
    if(at>last) return origin+last-at;
    return origin;
}

byte ui_player_health_attr(byte normal)
{
    if (!character_generated || p_ptr->is_dead || p_ptr->mhp <= 0) return normal;
    if (p_ptr->chp <= p_ptr->mhp / 10) return TERM_L_RED;
    if (p_ptr->chp <= p_ptr->mhp / 4) return TERM_ORANGE;
    if (p_ptr->chp <= p_ptr->mhp / 2) return TERM_YELLOW;
    return normal;
}

ui_player_warning_t ui_player_warning(void)
{
    if (!character_generated || p_ptr->is_dead || p_ptr->mhp <= 0)
        return UI_WARNING_NONE;
    if (p_ptr->chp <= p_ptr->mhp / 4) return UI_WARNING_HEALTH;
    if (p_ptr->paralyzed) return UI_WARNING_IMPAIRED;
    if (p_ptr->poisoned) return UI_WARNING_POISON;
    if (p_ptr->cut) return UI_WARNING_BLEEDING;
    if (p_ptr->blind || p_ptr->confused || p_ptr->stun || p_ptr->afraid)
        return UI_WARNING_IMPAIRED;
    if (p_ptr->food < PY_FOOD_FAINT) return UI_WARNING_HUNGER;
    return UI_WARNING_NONE;
}

void ui_map_size(int *width, int *height)
{
    if (width)  *width  = cur_wid;
    if (height) *height = cur_hgt;
}

/*
 * Whether the level on screen is the world map rather than a place in it.
 *
 * The frontend asks because the two want different things of the zoom. A
 * dungeon should not zoom out past the point where empty space comes into
 * view -- there is nothing out there to look at. The world map should, because
 * seeing the whole of it at once is the entire reason for opening it.
 */
bool ui_map_is_world(void)
{
    return p_ptr->wild_mode;
}

void ui_map_player(int *x, int *y)
{
    if (x) *x = px;
    if (y) *y = py;
}

/* Use standard lit terrain colours on the world map, regardless of player
 * position or time. Preserve entity colours already returned by map_info().
 * Dungeon and surface levels retain their ordinary lighting rules. */
static bool _draw_as_map(void)
{
    return BOOL(p_ptr->wild_mode);
}

static byte _lit_attr(int x, int y, byte fallback)
{
    s16b feat = get_feat_mimic(&cave[y][x]);

    if (feat <= 0 || feat >= max_f_idx) return fallback;
    if (!f_info[feat].name) return fallback;

    return f_info[feat].x_attr[F_LIT_STANDARD];
}

bool ui_map_cell(int x, int y, byte *attr, char *ch)
{
    byte a, ta;
    char c, tc;

    if (!in_bounds2(y, x)) return FALSE;

    /* Undiscovered ground stays undiscovered. A zoomed-out view is a view of
     * what the player has explored, not a map of the level. */
    if (!(cave[y][x].info & CAVE_MARK))
    {
        /* ...except where they can currently see, which is how the grids
         * around the player show up before they have been walked over. */
        if (!player_has_los_bold(y, x)) return FALSE;
    }

    display_map_info(y, x, &a, &c, &ta, &tc);

    /* Terrain only: a == ta and c == tc is how map_info says there is nothing
     * standing on this grid. See _draw_as_map(). */
    if (_draw_as_map() && a == ta && c == tc)
        a = _lit_attr(x, y, a);

    if (attr) *attr = a;
    if (ch)   *ch   = c;

    return TRUE;
}


bool ui_map_seen(int x, int y)
{
    if (!in_bounds2(y, x)) return FALSE;
    return BOOL(player_can_see_bold(y, x));
}


cptr ui_map_terrain(int x, int y, byte *attr, char *ch, byte *lit)
{
    byte a, ta;
    char c, tc;
    s16b feat;

    if (!in_bounds2(y, x) || p_ptr->image) return NULL;

    if (!(cave[y][x].info & CAVE_MARK) && !player_has_los_bold(y, x))
        return NULL;

    /* map_info hands back the terrain underneath as well as the top of the
     * pile, which is exactly the pair a tile renderer wants: it is the same
     * distinction Term_pict has always drawn between cp and tcp. */
    display_map_info(y, x, &a, &c, &ta, &tc);

    /* The tile layer takes its dimming from ta against lit, so handing back
     * the lit colour here is what makes the world map's tiles come out whole.
     * See _draw_as_map(). */
    if (_draw_as_map()) ta = _lit_attr(x, y, ta);

    if (tc == ' ') return NULL;
    if (attr) *attr = ta;
    if (ch)   *ch   = tc;

    feat = get_feat_mimic(&cave[y][x]);
    if (feat <= 0 || feat >= max_f_idx) return NULL;
    if (!f_info[feat].name) return NULL;

    /* What this same ground looks like with the light on. A letter carries
     * the lighting in its colour and needs no help; a picture is drawn in
     * its own colours and would look the same in the dark, which is a map
     * that has stopped telling you what you can currently see. */
    if (lit) *lit = f_info[feat].x_attr[F_LIT_STANDARD];

    return f_name + f_info[feat].name;
}

u32b ui_map_terrain_seed(int x, int y)
{
    u32b seed;
    if (py_on_surface() && !p_ptr->wild_mode)
    {
        x += p_ptr->wilderness_x * MAX_WID + p_ptr->wilderness_dx * WILD_SCROLL_CX;
        y += p_ptr->wilderness_y * MAX_HGT + p_ptr->wilderness_dy * WILD_SCROLL_CY;
    }
    /* Unsigned arithmetic is intentional. No game RNG or terrain reads. */
    seed = (u32b)x * 73856093u ^ (u32b)y * 19349663u;
    seed ^= seed >> 16;
    seed *= 0x7feb352du;
    seed ^= seed >> 15;
    return seed;
}

enum {
    SURFACE_NONE, WATER_SHALLOW, WATER_DEEP, LAVA_SHALLOW, LAVA_DEEP,
    WASTE_SHALLOW, WASTE_DEEP, SURFACE_SNOW, SURFACE_ICE, SURFACE_SLUSH,
    SURFACE_SWAMP, SURFACE_PIT, SURFACE_CREVASSE,
    SURFACE_FLOOR, SURFACE_DIRT, SURFACE_GRASS
};

static int _surface(cptr name)
{
    static cptr names[] = {"shallow water", "deep water", "shallow lava", "deep lava",
        "poisonous acid", "toxic waste", "snow", "ice slick", "slush", "swamp",
        "dark pit", "icy crevasse", "open floor", "dirt", "patch of grass", NULL};
    int i;
    if (name) for (i = 0; names[i]; ++i) if (streq(name, names[i])) return i + 1;
    return 0;
}

static bool _shore(cptr name)
{
    /* An explicit list, rather than treating unknown/special features as land.
     * In particular, a trap, doorway or unseen neighbour adds no edge. */
    static cptr names[] = {"open floor", "dirt", "patch of grass", "flower", "tree", "brake",
        "granite wall", "permanent wall", "magma vein", "quartz vein",
        "magma vein with treasure", "quartz vein with treasure", "mountain chain",
        "snow wall", "ice wall", "snow-covered tree", "glacier", NULL};
    int i;
    if (name) for (i = 0; names[i]; ++i) if (streq(name, names[i])) return TRUE;
    return FALSE;
}

bool ui_map_terrain_edges(int x, int y, cptr known_name, cptr edges[4])
{
    static const int dx[] = {0,1,0,-1}, dy[] = {-1,0,1,0};
    static cptr shallow[] = {"shallow-water", "shallow-lava", "poisonous-acid"};
    int i, kind = _surface(known_name);
    for (i = 0; i < 4; ++i) edges[i] = NULL;
    if (!kind || !in_bounds2(y,x) || p_ptr->image) return FALSE;
    /* World-map cells stand for entire regions, not adjacent floor squares. */
    if (p_ptr->wild_mode) return TRUE;
    if (kind >= SURFACE_FLOOR) return TRUE;
    for (i = 0; i < 4; ++i)
    {
        cptr other = ui_map_terrain(x + dx[i], y + dy[i], NULL, NULL, NULL);
        int next = _surface(other);
        if (kind <= WASTE_DEEP && !(kind & 1) && next == kind - 1)
            edges[i] = shallow[kind / 2 - 1];
        else if ((kind == SURFACE_ICE || kind == SURFACE_SLUSH || kind == SURFACE_CREVASSE) && next == SURFACE_SNOW)
            edges[i] = "snow";
        else if (kind == SURFACE_CREVASSE && next == SURFACE_ICE)
            edges[i] = "ice-slick";
        else if (_shore(other))
            edges[i] = kind == SURFACE_CREVASSE ? "ice-slick" : "rock-bank";
    }
    return TRUE;
}

unsigned ui_map_terrain_links(int x, int y, cptr known_name)
{
    static const int dx[] = {0,1,0,-1}, dy[] = {-1,0,1,0};
    unsigned links = 0;
    int i;
    bool curtain;
    if (!known_name || !in_bounds2(y,x) || p_ptr->image || p_ptr->wild_mode) return 0;
    curtain = streq(known_name,"curtain") || streq(known_name,"open curtain");
    if (!curtain && !streq(known_name,"dark pit") && !streq(known_name,"icy crevasse")) return 0;
    for (i = 0; i < 4; ++i)
    {
        cptr other = ui_map_terrain(x+dx[i],y+dy[i],NULL,NULL,NULL);
        if (other && (streq(other,known_name) ||
            (curtain && (streq(other,"curtain") || streq(other,"open curtain")))))
            links |= 1u << i;
    }
    return links;
}

/* These are public appearances, not feature IDs. In particular, poisoned
 * pits and different dart/gas effects must share their apparent family. */
cptr ui_map_terrain_emblem(cptr known_name)
{
    static const struct { cptr name, emblem; } shops[] = {
        {"General Store","Food"}, {"Armoury","Shield"},
        {"Weapon Smiths","Sword"}, {"Temple","Amulet"},
        {"Alchemy Shop","Flask"}, {"Magic Shop","Wand"},
        {"Black Market","Gold"}, {"Home","Brass Lantern"},
        {"Bookstore","Spellbook"}, {"Museum","Statue"},
        {"Jeweler","Ring"}, {"Mushroom Store","Mushroom"},
        {"Dragonskin Emporium","Dragon Scale Mail"}, {NULL,NULL}
    };
    int i;
    if (!known_name) return NULL;
    for (i=0;shops[i].name;++i)
        if (streq(known_name,shops[i].name)) return shops[i].emblem;
    return NULL;
}

bool ui_map_terrain_label(cptr known_name)
{
    static cptr markers[] = {"Building", "Quest Entrance", "entrance to dungeon",
        "town exit", "Town", "gate of arena", "up staircase", "down staircase",
        "shaft up", "shaft down", "trap door", "pit", "dart trap", "gas trap",
        "discolored spot", "evil rune", "strange rune", "explosive rune",
        "compact rune", "glyph of warding", "mirror", "Sigil startpoint",
        "section of the Sigil", "section of the Sigil (discharged)",
        "Sigil exit", "corrupted section of the Sigil", "bear trap", "alarm",
        "icicle trap", NULL};
    int i;
    if (!known_name) return FALSE;
    if (ui_map_terrain_emblem(known_name)) return TRUE;
    for (i=0;markers[i];++i) if (streq(known_name,markers[i])) return TRUE;
    return FALSE;
}

cptr ui_map_terrain_ground(cptr known_name)
{
    static cptr props[] = {"door", "open door", "broken door", "glass door",
        "open glass door", "broken glass door", "curtain", "open curtain",
        "up staircase", "down staircase", "pile of rubble", "web", NULL};
    int i;
    if (!known_name) return NULL;
    if (streq(known_name,"snow-covered tree")) return "snow";
    if (streq(known_name,"tree") || streq(known_name,"brake") || streq(known_name,"flower"))
        return "patch of grass";
    if (ui_map_terrain_label(known_name)) return "open floor";
    for (i=0;props[i];++i) if (streq(known_name,props[i])) return "open floor";
    return NULL;
}

bool ui_map_plain_floor(cptr known_name)
{
    return known_name && (streq(known_name,"open floor") ||
        streq(known_name,"dirt") || streq(known_name,"patch of grass"));
}

/* Cosmetic building finishes come from the static town plans, not from live
 * cave neighbours. This keeps a partially visible/scrolled building uniform
 * without reading undiscovered terrain or depending on quest progress.
 * These cached bytes are presentation only: never saved or used by gameplay. */
static byte _village_styles[10][MAX_HGT][MAX_WID];
static bool _village_loaded[10];

static void _load_village_styles(int plan)
{
    static cptr files[] = {NULL, "t_outp.txt", "t_telmo.txt", "t_mori.txt",
        "t_angwil.txt", "t_ana.txt", "t_thalos.txt", "t_zul.txt",
        "t_lite.txt", "t_ulite.txt"};
    byte walls[MAX_HGT][MAX_WID] = {{0}}, letters[256] = {0};
    char rows[MAX_HGT][MAX_WID] = {{0}}, buf[1024], path[1024];
    u16b queue[MAX_HGT * MAX_WID];
    FILE *fp;
    int x, y = 0, h, component = 0;

    _village_loaded[plan] = TRUE;
    path_build(path, sizeof(path), SCOTTISH_DIR_EDIT, files[plan]);
    fp = my_fopen(path, "r");
    if (!fp) return;
    letters[(byte)'#'] = 1; /* t_pref.txt's permanent masonry */
    while (!my_fgets(fp, buf, sizeof(buf)))
    {
        /* Include every possible permanent quest doorway, regardless of the
         * condition around its declaration. Opening one must not restyle the
         * building. Only actual known permanent walls use the result. */
        if (!strncmp(buf, "L:", 2) && strlen(buf) >= 13 &&
            buf[3] == ':' && !strncmp(buf + 4, "PERMANENT", 9) &&
            (!buf[13] || buf[13] == '('))
            letters[(byte)buf[2]] = 1;
        if (strncmp(buf, "M:", 2)) continue;
        if (y >= MAX_HGT || strlen(buf + 2) > MAX_WID)
        {
            my_fclose(fp);
            return; /* A changed/unsupported plan keeps the normal wall art. */
        }
        memcpy(rows[y++], buf + 2, strlen(buf + 2));
    }
    my_fclose(fp);
    h = y;
    for (y = 0; y < h; ++y)
        for (x = 0; x < MAX_WID; ++x)
            walls[y][x] = letters[(byte)rows[y][x]];

    /* Connected masonry, with diagonal decorative corners kept together.
     * No random rolls, live cave reads, or terrain modifications. */
    for (y = 0; y < h; ++y)
    {
        for (x = 0; x < MAX_WID; ++x)
        {
            int head = 0, tail = 0;
            byte style;
            if (!walls[y][x]) continue;
            style = 1 + (component++ + plan) % 4;
            walls[y][x] = 0;
            queue[tail++] = y * MAX_WID + x;
            while (head < tail)
            {
                int at = queue[head++], cx = at % MAX_WID, cy = at / MAX_WID;
                int dx, dy;
                _village_styles[plan][cy][cx] = style;
                for (dy = -1; dy <= 1; ++dy)
                {
                    for (dx = -1; dx <= 1; ++dx)
                    {
                        int nx = cx + dx, ny = cy + dy;
                        if (nx < 0 || nx >= MAX_WID || ny < 0 || ny >= h || !walls[ny][nx]) continue;
                        walls[ny][nx] = 0;
                        queue[tail++] = ny * MAX_WID + nx;
                    }
                }
            }
        }
    }
}

cptr ui_map_terrain_tile(int x, int y, cptr known_name)
{
    static cptr finishes[] = {"permanent-wall", "village-masonry",
        "granite-wall", "permanent-wall", "slate-wall"};
    int wx, wy, plan;

    if (!known_name || !in_bounds2(y, x) || p_ptr->image) return NULL;
    if (ui_map_terrain_emblem(known_name)) return "shop-sign";
    if (!py_on_surface() || p_ptr->wild_mode)
    {
        if (streq(known_name, "tree") &&
            ((u32b)x * 73856093u ^ (u32b)y * 19349663u) % 3 == 0)
            return "pine-tree";
        return known_name;
    }

    /* Undo the moving wilderness viewport to recover stable town-local
     * coordinates, including when a neighbouring wilderness area is visible. */
    wx = p_ptr->wilderness_x; wy = p_ptr->wilderness_y;
    x += p_ptr->wilderness_dx * WILD_SCROLL_CX;
    y += p_ptr->wilderness_dy * WILD_SCROLL_CY;
    if (x < 0) { x += MAX_WID; --wx; }
    if (x >= MAX_WID) { x -= MAX_WID; ++wx; }
    if (y < 0) { y += MAX_HGT; --wy; }
    if (y >= MAX_HGT) { y -= MAX_HGT; ++wy; }

    if (streq(known_name, "tree"))
        return ((u32b)(wx * MAX_WID + x) * 73856093u ^
            (u32b)(wy * MAX_HGT + y) * 19349663u) % 3 == 0 ? "pine-tree" : known_name;
    if (streq(known_name, "granite wall")) return "rock-bank";
    if (!streq(known_name, "permanent wall")) return known_name;
    if (!wilderness || wx < 0 || wx >= max_wild_x || wy < 0 || wy >= max_wild_y)
        return known_name;
    plan = wilderness[wy][wx].town;
    if (plan < 1 || plan > 7) return known_name;
    if (plan == TOWN_OUTPOST)
    {
        if (coffee_break == SPEED_INSTA_COFFEE) plan = 9;
        else if (no_wilderness) plan = 8;
    }
    if (!_village_loaded[plan]) _load_village_styles(plan);
    return finishes[_village_styles[plan][y][x]];
}

/*
 * The kinds of thing that can be on a floor.
 *
 * A short table rather than a switch, because it is a list and reads as one.
 * Anything not here has no tile and falls back to its letter, which is what
 * every unpainted thing in the game does.
 */
static struct { int tval; cptr kind; } _obj_kinds[] =
{
    { TV_SWORD,       "Sword"          },
    { TV_POLEARM,     "Polearm"        },
    { TV_HAFTED,      "Hafted Weapon"  },
    { TV_DIGGING,     "Digger"         },
    { TV_BOW,         "Bow"            },
    { TV_ARROW,       "Arrows"         },
    { TV_BOLT,        "Bolts"          },
    { TV_SHOT,        "Shots"          },
    { TV_SHIELD,      "Shield"         },
    { TV_HELM,        "Helm"           },
    { TV_CROWN,       "Crown"          },
    { TV_GLOVES,      "Gloves"         },
    { TV_BOOTS,       "Boots"          },
    { TV_CLOAK,       "Cloak"          },
    { TV_SOFT_ARMOR,  "Soft Armor"     },
    { TV_HARD_ARMOR,  "Hard Armor"     },
    { TV_DRAG_ARMOR,  "Dragon Scale Mail" },
    { TV_RING,        "Ring"           },
    { TV_AMULET,      "Amulet"         },
    { TV_LITE,        "Lite"           },
    { TV_POTION,      "Potion"         },
    { TV_FLASK,       "Flask"          },
    { TV_SCROLL,      "Scroll"         },
    { TV_PARCHMENT,   "Parchment"      },
    { TV_WAND,        "Wand"           },
    { TV_STAFF,       "Staff"          },
    { TV_ROD,         "Rod"            },
    { TV_FOOD,        "Food"           },
    { TV_CHEST,       "Chest"          },
    { TV_CORPSE,      "Corpse"         },
    { TV_STATUE,      "Statue"         },
    { TV_FIGURINE,    "Magical Figurine" },
    { TV_SKELETON,    "Bones"          },
    { TV_JUNK,        "Junk"           },
    { TV_BOTTLE,      "Empty Bottle"   },
    { TV_SPIKE,       "Spikes"         },
    { TV_GOLD,        "Gold"           },
    { 0, NULL }
};

/*
 * Drop the counting word k_info writes into a name.
 *
 * "& Set~ of Leather Gloves" is one pair of gloves however it is spelled, and
 * a painter files the picture under leather-gloves. Tried only after the full
 * name has missed, so an object actually called "Set of ..." still wins.
 */
static void _plain_name(char *dest, int len, cptr src)
{
    int i = 0;

    while (*src == ' ' || *src == '&' || *src == '[' || *src == '(') src++;

    for (; *src && i < len - 1; src++)
        if (*src != '~' && *src != ']' && *src != ')') dest[i++] = *src;

    dest[i] = '\0';
}


/*
 * Drop the counting word k_info writes into a name.
 *
 * "Set of Leather Gloves" is one pair of gloves however it is spelled, and a
 * painter files the picture under leather-gloves. Returns the name unchanged
 * when it does not begin with one, so an object genuinely called "Piece of
 * ..." still asks for itself first.
 */
static cptr _without_count(cptr name)
{
    static const char *lead[] = { "Set of ", "Pair of ", "Piece of ", NULL };
    int i;

    for (i = 0; lead[i]; i++)
    {
        int n = strlen(lead[i]);
        if (!strncmp(name, lead[i], n)) return name + n;
    }
    return name;
}


bool ui_map_object_art(int x, int y, char *specific, char *generic, int len,
                       byte *attr, char *ch)
{
    cave_type   *c_ptr;
    object_type *o_ptr;
    cptr         kind;

    if (specific) specific[0] = '\0';
    if (generic)  generic[0]  = '\0';

    if (!in_bounds2(y, x)) return FALSE;
    if (p_ptr->image) return FALSE;

    c_ptr = &cave[y][x];
    if (!c_ptr->o_idx) return FALSE;

    o_ptr = &o_list[c_ptr->o_idx];
    if (!(o_ptr->marked & OM_FOUND)) return FALSE;

    kind = ui_map_object(x, y);
    if (!kind) return FALSE;

    /* The same colour and letter the map draws it in, flavour and all. */
    if (attr) *attr = object_attr(o_ptr);
    if (ch)   *ch   = object_char(o_ptr);

    if (object_is_aware(o_ptr))
    {
        object_kind *k_ptr = &k_info[o_ptr->k_idx];
        char         base[128];

        /* k_name holds the name as k_info.txt writes it -- "& Set~ of Leather
         * Gloves" -- because the markers are parsed out at the point of use
         * and not at the point of storage. Take them out before asking for a
         * picture, or the gloves ask for set-of-leather-gloves and quietly
         * settle for being drawn as generic Gloves. */
        _plain_name(base, sizeof(base), k_name + k_ptr->name);

        if (specific) strnfmt(specific, len, "%s", _without_count(base));
        if (generic)  strnfmt(generic, len, "%s", kind);
    }
    else if (generic)
    {
        /* An unknown mushroom is not "unidentified food". Mushrooms are a
         * flavoured family of their own -- everything below SV_FOOD_MIN_FOOD,
         * learned one bite at a time exactly as potions are -- and the player
         * looking at one can see it is a mushroom and nothing more.
         *
         * The generic key also stays a mushroom after identification; neither
         * state should borrow the ration picture. */
        if (o_ptr->tval == TV_FOOD && o_ptr->sval < SV_FOOD_MIN_FOOD)
            strnfmt(generic, len, "unidentified mushroom");
        else
            strnfmt(generic, len, "unidentified %s", kind);
    }

    return TRUE;
}


cptr ui_map_object(int x, int y)
{
    cave_type   *c_ptr;
    object_type *o_ptr;
    int          i;

    if (!in_bounds2(y, x)) return NULL;
    if (p_ptr->image) return NULL;

    c_ptr = &cave[y][x];
    if (!c_ptr->o_idx) return NULL;

    o_ptr = &o_list[c_ptr->o_idx];
    if (!(o_ptr->marked & OM_FOUND)) return NULL;

    /* A mushroom's visible family does not reveal which effect it has. */
    if (o_ptr->tval == TV_FOOD && o_ptr->sval < SV_FOOD_MIN_FOOD)
        return "Mushroom";

    /* Visible object families, never the creature in pval. Body parts and
     * photographs must not acquire a misleading whole-corpse/statue icon. */
    if (o_ptr->tval == TV_CORPSE)
    {
        if (o_ptr->sval == SV_SKELETON) return "Bones";
        if (o_ptr->sval != SV_CORPSE) return "Body Part";
    }
    if (o_ptr->tval == TV_STATUE && o_ptr->sval == SV_PHOTO) return "Photograph";

    /* A spellbook is a spellbook whichever discipline it teaches -- there
     * are seventeen kinds and one glyph, and one tile is the honest answer. */
    if (TV_BOOK_BEGIN <= o_ptr->tval && o_ptr->tval <= TV_BOOK_END)
        return "Spellbook";

    for (i = 0; _obj_kinds[i].kind; i++)
        if (_obj_kinds[i].tval == o_ptr->tval) return _obj_kinds[i].kind;

    return NULL;
}

bool ui_map_look(int x, int y, ui_look_t *out)
{
    cave_type    *c_ptr;
    monster_type *m_ptr;
    monster_race *r_ptr;

    if (!out) return FALSE;

    out->name = NULL;
    out->health = NULL;
    out->attr = TERM_WHITE;
    out->ch = ' ';
    out->kills = 0;

    /* "?" is the default for every figure, so anything the gates below do not
     * fill stays honestly unknown rather than accidentally zero. */
    strcpy(out->level, "?");
    strcpy(out->speed, "?");
    strcpy(out->ac, "?");
    strcpy(out->hp, "?");
    strcpy(out->attacks, "?");
    strcpy(out->exp, "?");

    if (!in_bounds2(y, x)) return FALSE;
    if (p_ptr->image) return FALSE;

    c_ptr = &cave[y][x];
    if (!c_ptr->m_idx) return FALSE;

    m_ptr = &m_list[c_ptr->m_idx];
    if (!m_ptr->ml) return FALSE;

    /* The apparent race throughout, so a mimic keeps its disguise. */
    r_ptr = &r_info[m_ptr->ap_r_idx];
    if (!r_ptr->name) return FALSE;

    out->name = r_name + r_ptr->name;

    /* The same words the monster list uses, from the same function, because
     * two places describing one wound differently is worse than either. */
    out->health = mon_health_desc(m_ptr);

    /* What it looks like as a letter. A frontend with no picture of this
     * creature still has this, and it is the same pair the map draws. */
    out->attr = r_ptr->x_attr;
    out->ch   = (char)r_ptr->x_char;

    out->kills = (int)r_ptr->r_pkills;

    /*
     * The recall page's figures. mon_info.c decides what is known; this only
     * decides how it reads.
     */
    if (r_ptr->level == 0)
        strcpy(out->level, "Town");
    else if (mon_lore_knows_level(r_ptr))
    {
        if (r_ptr->max_level != 999)
            strnfmt(out->level, sizeof(out->level), "%d to %d",
                    r_ptr->level, r_ptr->max_level);
        else
            strnfmt(out->level, sizeof(out->level), "%d", r_ptr->level);
    }

    /* Speed is never hidden -- a thing that outruns you shows you that by
     * outrunning you, so the recall page has never pretended otherwise. */
    if (effective_speed)
        strnfmt(out->speed, sizeof(out->speed), "%d.%dx",
                SPEED_TO_ENERGY(r_ptr->speed) / 10,
                SPEED_TO_ENERGY(r_ptr->speed) % 10);
    else
        strnfmt(out->speed, sizeof(out->speed), "%+d", r_ptr->speed - 110);

    if (mon_lore_knows_armor_hp(r_ptr))
    {
        strnfmt(out->ac, sizeof(out->ac), "%d", r_ptr->ac);

        if ((r_ptr->flags1 & RF1_FORCE_MAXHP) || r_ptr->hside == 1)
            strnfmt(out->hp, sizeof(out->hp), "%d",
                    r_ptr->hdice * r_ptr->hside);
        else
            strnfmt(out->hp, sizeof(out->hp), "%dd%d",
                    r_ptr->hdice, r_ptr->hside);
    }

    if (r_ptr->flags1 & RF1_NEVER_BLOW)
        strcpy(out->attacks, "None");
    else
    {
        int ct = mon_lore_known_attacks(r_ptr);
        if (ct > 0) strnfmt(out->attacks, sizeof(out->attacks), "%d", ct);
    }

    if (mon_lore_knows_level(r_ptr))
    {
        int  plev = p_ptr->max_plv;
        int  xp = r_ptr->mexp * r_ptr->level / (plev + 2);
        char buf[16];

        if (r_ptr->r_akills > (coffee_break ? 49 : 99))
        {
            xp *= 2;
            xp /= divide_exp_by(r_ptr->r_akills);
        }
        big_num_display(xp, buf);
        strnfmt(out->exp, sizeof(out->exp), "%s", buf);
    }

    return TRUE;
}


cptr ui_map_monster(int x, int y)
{
    cave_type    *c_ptr;
    monster_type *m_ptr;
    monster_race *r_ptr;

    if (!in_bounds2(y, x)) return NULL;

    /* The game is already showing something other than the truth. */
    if (p_ptr->image) return NULL;

    c_ptr = &cave[y][x];
    if (!c_ptr->m_idx) return NULL;

    m_ptr = &m_list[c_ptr->m_idx];
    if (!m_ptr->ml) return NULL;   /* not visible to the player */

    /* The apparent race, which is what the player believes is there. */
    r_ptr = &r_info[m_ptr->ap_r_idx];
    if (!r_ptr->name) return NULL;
    if ((m_ptr->mflag2 & MFLAG2_FUZZY) ||
        (r_ptr->flags1 & (RF1_CHAR_CLEAR | RF1_ATTR_CLEAR | RF1_SHAPECHANGER))) return NULL;

    return r_name + r_ptr->name;
}

/* An occupant remains an occupant even if its glyph matches the ground. */
bool ui_map_occupied(int x, int y)
{
    cave_type *c;
    if (!in_bounds2(y,x)) return FALSE;
    if (player_bold(y,x)) return TRUE;
    c = &cave[y][x];
    return (c->m_idx && m_list[c->m_idx].ml) ||
        (c->o_idx && (o_list[c->o_idx].marked & OM_FOUND));
}
bool ui_map_wall(int x, int y)
{
    int feat;
    if (p_ptr->image || !ui_map_terrain(x,y,NULL,NULL,NULL) || ui_map_occupied(x,y)) return FALSE;
    feat = get_feat_mimic(&cave[y][x]);
    return have_flag(f_info[feat].flags, FF_WALL) &&
        !have_flag(f_info[feat].flags, FF_GLASS);
}

bool ui_map_monster_present(int x, int y)
{
    return in_bounds2(y,x) && cave[y][x].m_idx && m_list[cave[y][x].m_idx].ml;
}
