/* Knowledge and RNG checks for presentation queries. Headless tests only.
 * Copyright (c) 2026 John Horton; Angband licence in LICENSE. */
#include "scottish.h"
#define VERIFY(x) do { if (!(x)) { printf("map-check FAIL %d: %s\n",__LINE__,#x); ++failures; } } while (0)
static void _doc_ink_counts(doc_ptr doc, int counts[256])
{
    int x,y;
    memset(counts,0,256*sizeof(int));
    for(y=0;y<=doc_cursor(doc).y;++y)
    {
        doc_char_ptr cells=doc_char(doc,doc_pos_create(0,y));
        for(x=0;x<doc_width(doc);++x)
            if(cells[x].c && cells[x].c!=' ') ++counts[(unsigned char)cells[x].c];
    }
}
void test_map_probe(void)
{
    int x=1,y=1,i,mouse=0,other=0,failures=0,k;
    cave_type *c=&cave[y][x];
    u32b state[RAND_DEG],value;
    u16b place;
    bool quick;
    byte a; char ch,specific[128],generic[128];
    cptr name;
    /* Recycled documents must not inherit a previous menu's held width. */
    for(i=0;i<32;++i)
    {
        doc_ptr doc=doc_alloc(64);
        VERIFY(doc_content_width(doc)==0);
        doc_insert(doc,"Short");
        VERIFY(doc_content_width(doc)==5);
        doc_hold_width(doc,64);
        VERIFY(doc_content_width(doc)==64);
        doc_free(doc);
    }
    for(i=1;i<max_r_idx;++i)
    {
        if(r_info[i].name && streq(r_name+r_info[i].name,"Giant white mouse")) mouse=i;
        if(r_info[i].name && (r_info[i].flags1 & RF1_ATTR_MULTI)) other=i;
    }
    VERIFY(mouse && other);
    /* Compact cards rearrange the same knowledge-gated text as wide recall.
     * Compare every printed character (ignoring layout/order), before and
     * after full lore, to catch dropped/duplicated fields or leaked numbers. */
    {
        int races[4]={mouse,other,0,0},j,known;
        bool old_spoiler=spoiler_hack,old_easy=easy_lore,old_wizard=p_ptr->wizard;
        for(i=1;i<max_r_idx;++i)
        {
            if(r_info[i].name && streq(r_name+r_info[i].name,"Grey mold")) races[2]=i;
            if(!races[3] && r_info[i].spells && r_info[i].spells->freq) races[3]=i;
        }
        easy_lore=FALSE; p_ptr->wizard=FALSE;
        memcpy(state,Rand_state,sizeof(state)); value=Rand_value; place=Rand_place; quick=Rand_quick;
        for(j=0;j<4;++j) for(known=0;known<2;++known)
        {
            int narrow[256],wide[256];
            doc_ptr a=doc_alloc(48),b=doc_alloc(72);
            monster_race before=r_info[races[j]];
            VERIFY(races[j]); spoiler_hack=known;
            mon_display_doc_card(&r_info[races[j]],a);
            mon_display_doc_card(&r_info[races[j]],b);
            _doc_ink_counts(a,narrow); _doc_ink_counts(b,wide);
            VERIFY(!memcmp(narrow,wide,sizeof(wide)));
            VERIFY(!memcmp(&before,&r_info[races[j]],sizeof(before)));
            doc_free(a); doc_free(b);
        }
        spoiler_hack=old_spoiler; easy_lore=old_easy; p_ptr->wizard=old_wizard;
        VERIFY(!memcmp(state,Rand_state,sizeof(state)) && value==Rand_value && place==Rand_place && quick==Rand_quick);
    }
    memset(c,0,sizeof(*c)); c->feat=f_tag_to_index("GRANITE"); c->info=CAVE_MARK;
    VERIFY(ui_map_wall(x,y));
    c->feat=f_tag_to_index("MAGMA_HIDDEN");
    name=ui_map_terrain(x,y,NULL,NULL,NULL);
    VERIFY(name && streq(name,"magma vein"));
    c->info=0; VERIFY(!ui_map_terrain(x,y,NULL,NULL,NULL));
    c->info=CAVE_MARK;
    memset(&m_list[1],0,sizeof(m_list[1]));
    c->m_idx=1; m_list[1].r_idx=other; m_list[1].ap_r_idx=mouse;
    VERIFY(!ui_map_monster(x,y) && !ui_map_monster_present(x,y));
    m_list[1].ml=TRUE;
    VERIFY(streq(ui_map_monster(x,y),"Giant white mouse"));
    VERIFY(ui_map_occupied(x,y) && !ui_map_wall(x,y));
    m_list[1].mflag2 |= MFLAG2_FUZZY;
    VERIFY(!ui_map_monster(x,y) && ui_map_monster_present(x,y));
    m_list[1].mflag2=0; m_list[1].ap_r_idx=other;
    memcpy(state,Rand_state,sizeof(state)); value=Rand_value; place=Rand_place; quick=Rand_quick;
    for(i=0;i<100;++i)
    {
        ui_map_cell(x,y,&a,&ch); ui_map_terrain(x,y,NULL,NULL,NULL); ui_map_wall(x,y);
    }
    p_ptr->image=10;
    for(i=0;i<100;++i) { ui_map_cell(x,y,&a,&ch); VERIFY(!ui_map_terrain(x,y,NULL,NULL,NULL)); VERIFY(!ui_map_monster(x,y)); }
    p_ptr->image=0;
    VERIFY(!memcmp(state,Rand_state,sizeof(state)) && value==Rand_value && place==Rand_place && quick==Rand_quick);
    c->m_idx=0; c->o_idx=1;
    k=lookup_kind(TV_POTION,SV_POTION_HEALING);
    object_prep(&o_list[1],k); o_list[1].marked=OM_FOUND; k_info[k].aware=FALSE;
    VERIFY(ui_map_object_art(x,y,specific,generic,sizeof(specific),NULL,NULL));
    VERIFY(!specific[0] && streq(generic,"unidentified Potion"));
    k_info[k].aware=TRUE;
    VERIFY(ui_map_object_art(x,y,specific,generic,sizeof(specific),NULL,NULL) && specific[0]);
    o_list[1].marked=0;
    VERIFY(!ui_map_object_art(x,y,specific,generic,sizeof(specific),NULL,NULL));

    /* Sprite selection must not reveal door locks, secret doors, trap types,
     * or the effects of an unidentified object. */
    {
        static const char *terrain[][2] = {
            {"CLOSED_DOOR","door"}, {"LOCKED_DOOR_1","door"},
            {"JAMMED_DOOR_7","door"}, {"SECRET_DOOR","granite wall"},
            {"OPEN_DOOR","open door"}, {"BROKEN_DOOR","broken door"},
            {"UP_STAIR","up staircase"}, {"DOWN_STAIR","down staircase"},
            {"DEEP_WATER","deep water"}, {"SHALLOW_WATER","shallow water"},
            {"DEEP_LAVA","deep lava"}, {"SHALLOW_LAVA","shallow lava"},
            {"RUBBLE","pile of rubble"}, {"INVIS","open floor"},
            {"CLOSED_GLASS_DOOR","glass door"}, {"LOCKED_GLASS_DOOR_1","glass door"},
            {"JAMMED_GLASS_DOOR_7","glass door"}, {"OPEN_GLASS_DOOR","open glass door"},
            {"BROKEN_GLASS_DOOR","broken glass door"}, {"GLASS_WALL","glass wall"},
            {"PERMANENT_GLASS_WALL","permanent glass wall"}, {"GLASS_FLOOR","glass floor"},
            {"OPEN_CURTAIN","open curtain"}, {"CLOSED_CURTAIN","curtain"}, {"WEB","web"},
            {"SNOW_FLOOR","snow"}, {"ICE_FLOOR","ice slick"}, {"SLUSH","slush"},
            {"SNOW_WALL","snow wall"}, {"ICE_WALL","ice wall"}, {"SNOW_TREE","snow-covered tree"},
            {"GLACIER","slippery slope"}, {"GLACIER_STEEP","glacier"},
            {"DARK_PIT","dark pit"}, {"CREVASSE","icy crevasse"}, {"SWAMP","swamp"},
            {"DEEP_WASTE","toxic waste"}, {"SHALLOW_WASTE","poisonous acid"}, {NULL,NULL}
        };
        static const int potions[] = {SV_POTION_HEALING,SV_POTION_SPEED,SV_POTION_POISON};
        object_type before;
        c->o_idx=0;
        for(i=0;terrain[i][0];++i)
        {
            c->feat=f_tag_to_index(terrain[i][0]); c->info=CAVE_MARK;
            name=ui_map_terrain(x,y,NULL,NULL,NULL);
            VERIFY(name && streq(name,terrain[i][1]));
            c->info=0;
            VERIFY(!ui_map_terrain(x,y,NULL,NULL,NULL));
        }
        VERIFY(!ui_map_terrain_ground(NULL));
        VERIFY(!ui_map_plain_floor(NULL));
        VERIFY(ui_map_plain_floor("open floor") && ui_map_plain_floor("dirt") && ui_map_plain_floor("patch of grass"));
        VERIFY(!ui_map_plain_floor("snow") && !ui_map_plain_floor("ice slick"));
        VERIFY(!ui_map_plain_floor("glass floor") && !ui_map_plain_floor("shallow lava"));
        VERIFY(!ui_map_plain_floor("poisonous acid") && !ui_map_plain_floor("door"));
        VERIFY(!ui_map_terrain_ground("deep water"));
        VERIFY(!ui_map_terrain_ground("dark pit"));
        VERIFY(!ui_map_terrain_ground("poison pit"));
        VERIFY(streq(ui_map_terrain_ground("curtain"),"open floor"));
        VERIFY(streq(ui_map_terrain_ground("open glass door"),"open floor"));
        VERIFY(streq(ui_map_terrain_ground("tree"),"patch of grass"));
        VERIFY(streq(ui_map_terrain_ground("flower"),"patch of grass"));
        VERIFY(streq(ui_map_terrain_ground("snow-covered tree"),"snow"));
        c->feat=f_tag_to_index("FLOOR"); c->info=CAVE_MARK; c->o_idx=1;
        for(i=0;i<3;++i)
        {
            k=lookup_kind(TV_POTION,potions[i]);
            object_prep(&o_list[1],k); o_list[1].marked=OM_FOUND;
            k_info[k].aware=FALSE; before=o_list[1];
            VERIFY(ui_map_object_art(x,y,specific,generic,sizeof(specific),NULL,NULL));
            VERIFY(!specific[0] && streq(generic,"unidentified Potion"));
            VERIFY(!memcmp(&before,&o_list[1],sizeof(before)) && !k_info[k].aware);
            k_info[k].aware=TRUE;
            VERIFY(ui_map_object_art(x,y,specific,generic,sizeof(specific),NULL,NULL));
            VERIFY(specific[0] && streq(generic,"Potion"));
        }
        k=lookup_kind(TV_FOOD,SV_FOOD_POISON);
        object_prep(&o_list[1],k); o_list[1].marked=OM_FOUND;
        k_info[k].aware=TRUE;
        VERIFY(ui_map_object_art(x,y,specific,generic,sizeof(specific),NULL,NULL));
        VERIFY(streq(generic,"Mushroom"));
        k_info[k].aware=FALSE;
        VERIFY(ui_map_object_art(x,y,specific,generic,sizeof(specific),NULL,NULL));
        VERIFY(!specific[0] && streq(generic,"unidentified mushroom"));
        k=lookup_kind(TV_FOOD,SV_FOOD_RATION);
        object_prep(&o_list[1],k); o_list[1].marked=OM_FOUND; k_info[k].aware=TRUE;
        VERIFY(ui_map_object_art(x,y,specific,generic,sizeof(specific),NULL,NULL));
        VERIFY(streq(generic,"Food"));
        p_ptr->image=1;
        VERIFY(!ui_map_object_art(x,y,specific,generic,sizeof(specific),NULL,NULL));
        p_ptr->image=0; o_list[1].marked=0;
    }

    /* Marker pictures, labels and shop emblems see only the apparent name.
     * Real hidden traps mimic the floor, even when that floor is remembered. */
    {
        static cptr tags[] = {"GENERAL_STORE","ARMOURY","WEAPON_SMITHS","TEMPLE",
            "ALCHEMY_SHOP","MAGIC_SHOP","BLACK_MARKET","HOME","BOOKSTORE",
            "MUSEUM","JEWELER","SHROOMERY","DRAGONSKIN","QUEST_ENTER",
            "TOWN_EXIT","TOWN","ENTRANCE","ARENA_GATE","SHAFT_UP","SHAFT_DOWN",
            "TRAP_TRAPDOOR","TRAP_PIT","TRAP_SPIKED_PIT","TRAP_POISON_PIT",
            "TRAP_TY_CURSE","TRAP_TELEPORT","TRAP_FIRE","TRAP_ACID","TRAP_SLOW",
            "TRAP_LOSE_STR","TRAP_LOSE_DEX","TRAP_LOSE_CON","TRAP_BLIND",
            "TRAP_CONFUSE","TRAP_POISON","TRAP_SLEEP","TRAP_TRAPS","TRAP_ALARM",
            "TRAP_BEAR","TRAP_ICICLE","GLYPH","EXPLOSIVE_RUNE","MIRROR",
            "SIGIL_START","SIGIL_1","SIGIL_2","SIGIL_3","SIGIL_4","SIGIL_END",
            "SIGIL_OLD","SIGIL_EXIT","SIGIL_CORRUPTED",NULL};
        int j;
        memset(c,0,sizeof(*c));
        memcpy(state,Rand_state,sizeof(state)); value=Rand_value; place=Rand_place; quick=Rand_quick;
        VERIFY(!ui_map_terrain_label(NULL) && !ui_map_terrain_emblem(NULL));
        VERIFY(!ui_map_terrain_label("open floor") && !ui_map_terrain_emblem("open floor"));
        VERIFY(!ui_map_terrain_label("unknown grid (not detected)"));
        VERIFY(ui_map_terrain_label("Building") && !ui_map_terrain_emblem("Building"));
        for(j=0;tags[j];++j)
        {
            cave_type before;
            feature_type feature;
            byte attr,lit,cell_attr; char glyph,cell_glyph;
            cptr key;
            c->feat=f_tag_to_index(tags[j]); c->info=CAVE_MARK;
            VERIFY(c->feat); before=*c; feature=f_info[c->feat];
            name=ui_map_terrain(x,y,&attr,&glyph,&lit);
            VERIFY(name && ui_map_terrain_label(name));
            VERIFY(ui_map_terrain_ground(name));
            key=ui_map_terrain_tile(x,y,name); VERIFY(key);
            VERIFY(ui_map_cell(x,y,&cell_attr,&cell_glyph));
            VERIFY(attr==cell_attr && glyph==cell_glyph);
            if(j<13) VERIFY(streq(key,"shop-sign") && ui_map_terrain_emblem(name));
            else VERIFY(!ui_map_terrain_emblem(name));
            if(j>=21 && j<=23) VERIFY(streq(key,"pit"));
            if(j>=26 && j<=27) VERIFY(streq(key,"discolored spot"));
            if(j>=28 && j<=31) VERIFY(streq(key,"dart trap"));
            if(j>=32 && j<=35) VERIFY(streq(key,"gas trap"));
            VERIFY(!memcmp(c,&before,sizeof(before)));
            VERIFY(!memcmp(&f_info[c->feat],&feature,sizeof(feature)));
            c->info=0;
            name=ui_map_terrain(x,y,NULL,NULL,NULL);
            VERIFY(!name && !ui_map_terrain_label(name) && !ui_map_terrain_emblem(name));
            c->info=CAVE_MARK; p_ptr->image=1;
            VERIFY(!ui_map_terrain(x,y,NULL,NULL,NULL));
            VERIFY(!ui_map_terrain_tile(x,y,key));
            p_ptr->image=0;
            /* A covered trap is indistinguishable from known ordinary floor. */
            c->mimic=f_tag_to_index("FLOOR");
            name=ui_map_terrain(x,y,NULL,NULL,NULL);
            VERIFY(name && streq(ui_map_terrain_tile(x,y,name),"open floor"));
            VERIFY(!ui_map_terrain_label(name) && !ui_map_terrain_emblem(name));
            c->mimic=0;
        }
        VERIFY(!memcmp(state,Rand_state,sizeof(state)) && value==Rand_value && place==Rand_place && quick==Rand_quick);
    }

    /* Utility icons use visible families, not a corpse/statue's creature
     * identity. Bones stay distinct; photos and body parts use glyphs. */
    {
        static const struct { int tv,sv; cptr family; } items[] = {
            {TV_CORPSE,SV_CORPSE,"Corpse"}, {TV_CORPSE,SV_SKELETON,"Bones"},
            {TV_SKELETON,3,"Bones"}, {TV_STATUE,SV_STONE_STATUE,"Statue"},
            {TV_FIGURINE,0,"Magical Figurine"}, {TV_DRAG_ARMOR,SV_DRAGON_BLACK,"Dragon Scale Mail"},
            {TV_BOTTLE,1,"Empty Bottle"}, {TV_JUNK,3,"Junk"},
            {TV_STATUE,SV_PHOTO,"Photograph"}
        };
        int j;
        c->o_idx=1;
        for(i=0;i<sizeof(items)/sizeof(items[0]);++i)
        {
            object_type before;
            k=lookup_kind(items[i].tv,items[i].sv); VERIFY(k);
            object_prep(&o_list[1],k); o_list[1].marked=OM_FOUND;
            for(j=0;j<2;++j)
            {
                k_info[k].aware=j;
                o_list[1].pval=j ? other : mouse; before=o_list[1];
                VERIFY(streq(ui_map_object(x,y),items[i].family));
                VERIFY(ui_map_object_art(x,y,specific,generic,sizeof(specific),NULL,NULL));
                VERIFY(streq(generic,j ? items[i].family : format("unidentified %s",items[i].family)));
                VERIFY(!memcmp(&before,&o_list[1],sizeof(before)));
            }
            o_list[1].marked=0; VERIFY(!ui_map_object(x,y));
            o_list[1].marked=OM_FOUND; p_ptr->image=1; VERIFY(!ui_map_object(x,y)); p_ptr->image=0;
        }
        o_list[1].tval=TV_CORPSE; o_list[1].sval=SV_BODY_HEAD;
        VERIFY(streq(ui_map_object(x,y),"Body Part"));
        c->o_idx=0;
    }

    /* Edge requests depend only on apparent, discovered neighbours. Hidden
     * floor, wall, trap and liquid must be indistinguishable to this query. */
    {
        static const int dx[]={0,1,0,-1},dy[]={-1,0,1,0};
        static cptr hidden[]={"FLOOR","SECRET_DOOR","INVIS","SHALLOW_WATER","DEEP_LAVA"};
        cptr edges[4];
        int side,j;
        cave_type before[4];
        for(side=0;side<4;++side)
        {
            cave_type *n=&cave[12+dy[side]][12+dx[side]];
            memset(n,0,sizeof(*n)); n->feat=f_tag_to_index("SHALLOW_WATER"); n->info=CAVE_MARK;
        }
        VERIFY(ui_map_terrain_edges(12,12,"deep water",edges));
        for(side=0;side<4;++side) VERIFY(edges[side] && streq(edges[side],"shallow-water"));
        for(side=0;side<4;++side)
        {
            cave_type *n=&cave[12+dy[side]][12+dx[side]];
            n->info=0;
            for(j=0;j<5;++j)
            {
                n->feat=f_tag_to_index(hidden[j]);
                ui_map_terrain_edges(12,12,"deep water",edges);
                VERIFY(!edges[side]);
            }
            n->info=CAVE_MARK;
            n->feat=f_tag_to_index("SECRET_DOOR");
            ui_map_terrain_edges(12,12,"deep water",edges);
            VERIFY(edges[side] && streq(edges[side],"rock-bank"));
            n->feat=f_tag_to_index("INVIS");
            ui_map_terrain_edges(12,12,"deep water",edges);
            VERIFY(edges[side] && streq(edges[side],"rock-bank"));
            n->feat=f_tag_to_index("GRASS");
            ui_map_terrain_edges(12,12,"deep water",edges);
            VERIFY(edges[side] && streq(edges[side],"rock-bank"));
            n->info=0;
            ui_map_terrain_edges(12,12,"deep water",edges); VERIFY(!edges[side]);
            n->info=CAVE_MARK;
            n->feat=f_tag_to_index("DEEP_WATER");
            ui_map_terrain_edges(12,12,"deep water",edges); VERIFY(!edges[side]);
            n->feat=f_tag_to_index("SHALLOW_LAVA");
            ui_map_terrain_edges(12,12,"deep lava",edges);
            VERIFY(edges[side] && streq(edges[side],"shallow-lava"));
            n->feat=f_tag_to_index("SHALLOW_WASTE");
            ui_map_terrain_edges(12,12,"toxic waste",edges);
            VERIFY(edges[side] && streq(edges[side],"poisonous-acid"));
            n->feat=f_tag_to_index("SNOW_FLOOR");
            ui_map_terrain_edges(12,12,"icy crevasse",edges);
            VERIFY(edges[side] && streq(edges[side],"snow"));
            n->feat=f_tag_to_index("ICE_FLOOR");
            ui_map_terrain_edges(12,12,"icy crevasse",edges);
            VERIFY(edges[side] && streq(edges[side],"ice-slick"));
            before[side]=*n;
        }
        for(i=0;i<100;++i) ui_map_terrain_edges(12,12,"icy crevasse",edges);
        for(side=0;side<4;++side) VERIFY(!memcmp(&before[side],&cave[12+dy[side]][12+dx[side]],sizeof(cave_type)));
        VERIFY(!ui_map_terrain_edges(12,12,NULL,edges));
        for(side=0;side<4;++side) VERIFY(!edges[side]);
        VERIFY(!ui_map_terrain_edges(-1,-1,"deep water",edges));
        ui_map_terrain_edges(0,0,"deep water",edges); VERIFY(!edges[0] && !edges[3]);
        p_ptr->image=1;
        VERIFY(!ui_map_terrain_edges(12,12,"icy crevasse",edges));
        for(side=0;side<4;++side) VERIFY(!edges[side]);
        p_ptr->image=0; p_ptr->wild_mode=TRUE;
        VERIFY(ui_map_terrain_edges(12,12,"icy crevasse",edges));
        for(side=0;side<4;++side) VERIFY(!edges[side]);
        p_ptr->wild_mode=FALSE;
        VERIFY(!memcmp(state,Rand_state,sizeof(state)) && value==Rand_value && place==Rand_place && quick==Rand_quick);
    }

    /* Connected art never reads through the knowledge boundary. All sixteen
     * cardinal patterns, both curtain states, two chasm materials, and traps. */
    {
        static const int dx[]={0,1,0,-1},dy[]={-1,0,1,0};
        static cptr tags[]={"CLOSED_CURTAIN","OPEN_CURTAIN","DARK_PIT","CREVASSE"};
        static cptr names[]={"curtain","open curtain","dark pit","icy crevasse"};
        int family,mask,side;
        memcpy(state,Rand_state,sizeof(state)); value=Rand_value; place=Rand_place; quick=Rand_quick;
        for(family=0;family<4;++family) for(mask=0;mask<16;++mask)
        {
            cave_type before[4];
            for(side=0;side<4;++side)
            {
                cave_type *n=&cave[12+dy[side]][12+dx[side]];
                memset(n,0,sizeof(*n));
                n->feat=f_tag_to_index(tags[family<2 ? (side&1) : family]);
                n->info=mask & (1<<side) ? CAVE_MARK : 0;
                before[side]=*n;
            }
            VERIFY(ui_map_terrain_links(12,12,names[family])==mask);
            for(side=0;side<4;++side)
            {
                cave_type *n=&cave[12+dy[side]][12+dx[side]];
                VERIFY(!memcmp(n,&before[side],sizeof(*n)));
                if (!(mask & (1<<side))) n->feat=f_tag_to_index("INVIS");
            }
            VERIFY(ui_map_terrain_links(12,12,names[family])==mask);
        }
        VERIFY(!ui_map_terrain_links(12,12,NULL));
        VERIFY(!ui_map_terrain_links(-1,-1,"dark pit"));
        VERIFY(!ui_map_terrain_links(12,12,"open floor"));
        VERIFY(!ui_map_terrain_links(12,12,"dark pit")); /* neighbours are crevasses */
        VERIFY(!(ui_map_terrain_links(0,0,"icy crevasse") & 9));
        p_ptr->image=1; VERIFY(!ui_map_terrain_links(12,12,"icy crevasse")); p_ptr->image=0;
        p_ptr->wild_mode=TRUE; VERIFY(!ui_map_terrain_links(12,12,"icy crevasse")); p_ptr->wild_mode=FALSE;
        VERIFY(!memcmp(state,Rand_state,sizeof(state)) && value==Rand_value && place==Rand_place && quick==Rand_quick);
    }

    /* Village plans: separate buildings vary, each facade stays together.
     * Exercise cold-cache loading as well as every wilderness scroll offset.
     * This disposable fixture exits without saving or resuming gameplay. */
    {
        static const int buildings[][4] = {
            {63,20,96,29}, {91,20,105,24}, {130,22,100,29}, {70,24,73,30}
        };
        cptr materials[4], trees[4];
        u32b seeds[4];
        int b, j, wx, wy, dx, dy, tx, ty, variant;
        dun_level=0; p_ptr->wild_mode=FALSE; no_wilderness=FALSE; coffee_break=0;
        p_ptr->wilderness_x=1; p_ptr->wilderness_y=1;
        p_ptr->wilderness_dx=0; p_ptr->wilderness_dy=0;
        wilderness[1][1].town=TOWN_OUTPOST;
        memcpy(state,Rand_state,sizeof(state)); value=Rand_value; place=Rand_place; quick=Rand_quick;
        for (b=0;b<4;++b)
        {
            materials[b]=ui_map_terrain_tile(buildings[b][0],buildings[b][1],"permanent wall");
            trees[b]=ui_map_terrain_tile(buildings[b][0],buildings[b][1],"tree");
            seeds[b]=ui_map_terrain_seed(buildings[b][0],buildings[b][1]);
            VERIFY(streq(materials[b],ui_map_terrain_tile(buildings[b][2],buildings[b][3],"permanent wall")));
            for(j=0;j<b;++j) VERIFY(!streq(materials[b],materials[j]));
        }
        for (wx=0;wx<=2;++wx) for (wy=0;wy<=2;++wy)
        for (dx=-1;dx<=1;++dx) for (dy=-1;dy<=1;++dy)
        {
            p_ptr->wilderness_x=wx; p_ptr->wilderness_y=wy;
            p_ptr->wilderness_dx=dx; p_ptr->wilderness_dy=dy;
            for (b=0;b<4;++b)
            {
                tx=buildings[b][0]+(1-wx)*MAX_WID-dx*WILD_SCROLL_CX;
                ty=buildings[b][1]+(1-wy)*MAX_HGT-dy*WILD_SCROLL_CY;
                if (in_bounds2(ty,tx))
                {
                    VERIFY(streq(materials[b],ui_map_terrain_tile(tx,ty,"permanent wall")));
                    VERIFY(streq(trees[b],ui_map_terrain_tile(tx,ty,"tree")));
                    VERIFY(seeds[b]==ui_map_terrain_seed(tx,ty));
                }
            }
        }
        p_ptr->wilderness_x=1; p_ptr->wilderness_y=1;
        p_ptr->wilderness_dx=0; p_ptr->wilderness_dy=0;
        /* All town plans, plus both compact Outpost variants, can load and
         * expose each finish. Zul has mountain towers rather than masonry. */
        for (variant=1;variant<=9;++variant)
        {
            int found=0;
            wilderness[1][1].town=variant<=7 ? variant:TOWN_OUTPOST;
            no_wilderness=variant==8;
            coffee_break=variant==9 ? SPEED_INSTA_COFFEE:0;
            for(ty=0;ty<MAX_HGT;++ty) for(tx=0;tx<MAX_WID;++tx)
            {
                name=ui_map_terrain_tile(tx,ty,"permanent wall");
                for(b=0;b<4;++b) if(streq(name,materials[b])) found|=1<<b;
            }
            if(variant!=TOWN_ZUL) VERIFY(found==15);
        }
        no_wilderness=FALSE; coffee_break=0;
        wilderness[1][1].town=TOWN_OUTPOST;
        /* Unseen terrain, doors and quest entrances never become masonry;
         * dungeon and world-map features retain their ordinary tile key. */
        VERIFY(!ui_map_terrain_tile(63,20,NULL));
        VERIFY(streq(ui_map_terrain_tile(63,20,"quest entrance"),"quest entrance"));
        VERIFY(streq(ui_map_terrain_tile(63,20,"closed door"),"closed door"));
        c->o_idx=0; c->info=0;
        VERIFY(!ui_map_terrain_tile(x,y,ui_map_terrain(x,y,NULL,NULL,NULL)));
        c->feat=f_tag_to_index("GRANITE"); c->info=CAVE_MARK;
        VERIFY(streq(materials[0],ui_map_terrain_tile(63,20,"permanent wall")));
        dun_level=1;
        VERIFY(streq(ui_map_terrain_tile(63,20,"permanent wall"),"permanent wall"));
        dun_level=0; p_ptr->wild_mode=TRUE;
        VERIFY(streq(ui_map_terrain_tile(63,20,"permanent wall"),"permanent wall"));
        p_ptr->wild_mode=FALSE; p_ptr->image=10;
        VERIFY(!ui_map_terrain_tile(63,20,"permanent wall"));
        p_ptr->image=0;
        VERIFY(!memcmp(state,Rand_state,sizeof(state)) && value==Rand_value && place==Rand_place && quick==Rand_quick);
    }
    printf("map-check failures %d\n",failures);
}
