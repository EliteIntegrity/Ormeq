/* Public synthetic-fixture tests: no proprietary artwork needed.
 * Copyright (c) 2026 John Horton. Angband licence: LICENSE. */
#include "ui_tiles.c"
#include "ui_square.h"
#define CHECK(x) do { if (!(x)) { printf("FAIL line %d: %s\n",__LINE__,#x); return 1; } } while (0)
static void file(cptr dir, cptr name, cptr body)
{
    char path[1200]; FILE *fp;
    SDL_snprintf(path,sizeof(path),"%s/%s",dir,name);
    fp=fopen(path,"w"); if (!fp) exit(2);
    fputs(body,fp); fclose(fp);
}
int main(int argc, char **argv)
{
    SDL_Surface *target, *fixture, *pixels;
    SDL_Renderer *r;
    SDL_Texture *tex;
    SDL_FRect src, cell={0,0,64,64};
    char path[1200]; int size, i;
    u32b saved[RAND_DEG], value; u16b place;
    CHECK(argc > 1);
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER,"dummy");
    CHECK(SDL_Init(SDL_INIT_VIDEO));
    target=SDL_CreateSurface(256,256,SDL_PIXELFORMAT_RGBA32);
    r=SDL_CreateSoftwareRenderer(target); CHECK(r);
    Rand_state_init(98765); Rand_quick=FALSE;
    memcpy(saved,Rand_state,sizeof(saved)); value=Rand_value; place=Rand_place;
    for (size=32;size<=64;size+=32)
    {
        fixture=SDL_CreateSurface(size,size,SDL_PIXELFORMAT_RGBA32); CHECK(fixture);
        SDL_FillSurfaceRect(fixture,NULL,SDL_MapSurfaceRGBA(fixture,20,200,30,255));
        SDL_snprintf(path,sizeof(path),"%s/sprite-%d.png",argv[1],size);
        CHECK(IMG_SavePNG(fixture,path)); SDL_DestroySurface(fixture);
        SDL_snprintf(path,sizeof(path),"pack-%d.txt",size);
        { char body[256]; SDL_snprintf(body,sizeof(body),"monster sprite-%d.png monster.txt\nobject sprite-%d.png object.txt\nterrain sprite-%d.png terrain.txt\n",size,size,size); file(argv[1],path,body); }
    }
    file(argv[1],"monster.txt","shared-name 0 0\n");
    file(argv[1],"object.txt","shared-name 0 0\n");
    file(argv[1],"terrain.txt","granite-wall 0 0\n");
    ui_tiles_init(r,argv[1]);
    for (i=0;i<40;++i)
    {
        ui_tiles_select(2,i&1 ? 32:64);
        CHECK(ui_tiles_count()==3);
        CHECK(ui_tiles_find("monster","Shared NAME",&tex,&src));
        CHECK(src.w==(i&1 ? 32:64));
        CHECK(ui_tiles_find("object","shared-name",NULL,NULL));
        CHECK(!ui_tiles_find("monster","unpainted",NULL,NULL));
        CHECK(!ui_tiles_find("terrain","shared-name",NULL,NULL));
        SDL_SetRenderDrawColor(r,0,0,0,255); SDL_RenderClear(r);
        SDL_RenderTexture(r,tex,&src,&cell);
        pixels=SDL_RenderReadPixels(r,NULL); CHECK(pixels);
        { Uint8 red,green,blue,alpha; CHECK(SDL_ReadSurfacePixel(pixels,10,10,&red,&green,&blue,&alpha)); CHECK(red==20 && green==200 && blue==30 && alpha==255); }
        SDL_DestroySurface(pixels);
        ui_tiles_select(1,64); CHECK(ui_tiles_count()==2);
        CHECK(!ui_tiles_find("terrain","granite-wall",NULL,NULL));
        ui_tiles_reset(); CHECK(ui_tiles_count()==2);
        ui_tiles_select(0,64); CHECK(!ui_tiles_ready() && sheet_count==0);
    }
    /* Actual edge pixels: blend inside each of four sides, leave the hazard
     * centre and every pixel outside the cell untouched, even at low zoom.
     * Distinct atlas colours also catch sampling across a sprite boundary. */
    {
        SDL_Rect half={32,0,32,32};
        cptr edges[4]={NULL,NULL,NULL,NULL};
        int side,x,y,border;
        fixture=SDL_CreateSurface(64,32,SDL_PIXELFORMAT_RGBA32); CHECK(fixture);
        SDL_FillSurfaceRect(fixture,NULL,SDL_MapSurfaceRGBA(fixture,20,200,30,255));
        SDL_FillSurfaceRect(fixture,&half,SDL_MapSurfaceRGBA(fixture,220,40,50,255));
        SDL_snprintf(path,sizeof(path),"%s/edges.png",argv[1]);
        CHECK(IMG_SavePNG(fixture,path)); SDL_DestroySurface(fixture);
        file(argv[1],"pack-32.txt","terrain edges.png edges.txt\n");
        file(argv[1],"edges.txt","surface 0 0\nshore 1 0\nrock-bank 1 0\n");
        ui_tiles_select(2,32);
        for(size=4;size<=64;size+=4) for(side=0;side<4;++side) for(border=0;border<2;++border)
        {
            int touched=0;
            cell=(SDL_FRect){16,16,(float)size,(float)size};
            edges[side]=border ? "rock-bank" : "shore";
            SDL_SetRenderDrawColor(r,0,0,0,255); SDL_RenderClear(r);
            CHECK(ui_tiles_draw_terrain("surface",cell,255,768u | side,TRUE,edges,0));
            pixels=SDL_RenderReadPixels(r,NULL); CHECK(pixels);
            for(y=0;y<size+32;++y) for(x=0;x<size+32;++x)
            {
                Uint8 red,green,blue,alpha;
                CHECK(SDL_ReadSurfacePixel(pixels,x,y,&red,&green,&blue,&alpha));
                if(x<16 || y<16 || x>=16+size || y>=16+size) CHECK(red==0 && green==0 && blue==0);
                else if(x>=16+size/4 && x<16+size*3/4 && y>=16+size/4 && y<16+size*3/4)
                    CHECK(red==20 && green==200 && blue==30);
                if(red>20) ++touched;
            }
            if(size>=8) CHECK(touched>0);
            else CHECK(!touched);
            SDL_DestroySurface(pixels); edges[side]=NULL;
        }
        /* Missing edge art keeps the base; missing base requests glyphs. */
        edges[0]="missing";
        CHECK(ui_tiles_draw_terrain("surface",cell,63,1,TRUE,edges,0));
        CHECK(!ui_tiles_draw_terrain("missing",cell,255,1,TRUE,edges,15));
        /* Fractional zooms: independently flipped neighbours still fill one
         * continuous rectangle without dark cracks or atlas-cell bleed. */
        for(size=0;size<4;++size)
        {
            float step=13.25f+size*6.5f;
            SDL_SetRenderDrawColor(r,0,0,0,255); SDL_RenderClear(r);
            for(y=0;y<3;++y) for(x=0;x<3;++x)
            {
                cell=(SDL_FRect){16.3f+x*step,16.3f+y*step,step,step};
                CHECK(ui_tiles_draw_terrain("surface",cell,255,768u | (x+y)%4,TRUE,NULL,0));
            }
            pixels=SDL_RenderReadPixels(r,NULL); CHECK(pixels);
            for(y=16;y<(int)SDL_roundf(16.3f+3*step);++y)
                for(x=16;x<(int)SDL_roundf(16.3f+3*step);++x)
                {
                    Uint8 red,green,blue,alpha;
                    CHECK(SDL_ReadSurfacePixel(pixels,x,y,&red,&green,&blue,&alpha));
                    CHECK(red==20 && green==200 && blue==30);
                }
            SDL_DestroySurface(pixels);
        }
        ui_tiles_select(0,64);
    }
    /* Connected chasms discard only the rim towards a known matching cell.
     * Curtains keep their openings, including vertical/non-square rendering.
     * Synthetic alpha and atlas colours catch stretching into other sprites. */
    {
        SDL_Rect box;
        int mask,x,y,kind;
        fixture=SDL_CreateSurface(128,32,SDL_PIXELFORMAT_RGBA32); CHECK(fixture);
        SDL_FillSurfaceRect(fixture,NULL,SDL_MapSurfaceRGBA(fixture,220,40,50,255));
        box=(SDL_Rect){8,8,16,16};
        SDL_FillSurfaceRect(fixture,&box,SDL_MapSurfaceRGBA(fixture,20,200,30,255));
        box=(SDL_Rect){36,0,24,32};
        SDL_FillSurfaceRect(fixture,&box,SDL_MapSurfaceRGBA(fixture,20,200,30,255));
        box=(SDL_Rect){68,0,24,32};
        SDL_FillSurfaceRect(fixture,&box,SDL_MapSurfaceRGBA(fixture,20,200,30,255));
        box=(SDL_Rect){74,8,12,24};
        SDL_FillSurfaceRect(fixture,&box,SDL_MapSurfaceRGBA(fixture,0,0,0,0));
        box=(SDL_Rect){96,0,32,32};
        SDL_FillSurfaceRect(fixture,&box,SDL_MapSurfaceRGBA(fixture,255,255,255,255));
        SDL_snprintf(path,sizeof(path),"%s/connected.png",argv[1]);
        CHECK(IMG_SavePNG(fixture,path)); SDL_DestroySurface(fixture);
        file(argv[1],"pack-32.txt","terrain connected.png connected.txt\n");
        file(argv[1],"connected.txt","dark-pit 0 0\nicy-crevasse 0 0\ncurtain 1 0\nopen-curtain 2 0\nopen-floor 3 0\ndirt 3 0\npatch-of-grass 3 0\n");
        ui_tiles_select(2,32);
        for(size=4;size<=64;size+=4) for(mask=0;mask<16;++mask) for(kind=0;kind<4;++kind)
        {
            static cptr names[]={"dark pit","icy crevasse","curtain","open curtain"};
            int w=size,h=kind>=2 ? size*3/4 : size;
            cell=(SDL_FRect){16.25f,16.25f,(float)w,(float)h};
            SDL_SetRenderDrawColor(r,0,0,0,255); SDL_RenderClear(r);
            CHECK(ui_tiles_draw_terrain(names[kind],cell,255,768,FALSE,NULL,mask));
            pixels=SDL_RenderReadPixels(r,NULL); CHECK(pixels);
            for(y=0;y<h+32;++y) for(x=0;x<w+32;++x)
            {
                Uint8 red,green,blue,alpha;
                CHECK(SDL_ReadSurfacePixel(pixels,x,y,&red,&green,&blue,&alpha));
                if(x<16 || y<16 || x>=16+w || y>=16+h) CHECK(!red && !green && !blue);
                if(x==16+w/2 && y==16+h/2)
                {
                    if(kind==3) { if(size>=16) CHECK(!red && !green && !blue); }
                    else CHECK(red==20 && green==200 && blue==30);
                }
                if(kind<2 && size>=16)
                {
                    int side=-1;
                    if(x==16+w/2 && y==17) side=0;
                    if(x==14+w && y==16+h/2) side=1;
                    if(x==16+w/2 && y==14+h) side=2;
                    if(x==17 && y==16+h/2) side=3;
                    if(side>=0) CHECK(red==((mask & (1<<side)) ? 20:220));
                }
            }
            SDL_DestroySurface(pixels);
        }
        for(kind=0;kind<3;++kind)
        {
            static cptr ground[]={"open floor","dirt","patch of grass"};
            static const int expected[]={216,127,191};
            Uint8 red,green,blue,alpha;
            cell=(SDL_FRect){0,0,32,32};
            CHECK(ui_tiles_draw_terrain(ground[kind],cell,255,768,TRUE,NULL,0));
            pixels=SDL_RenderReadPixels(r,NULL); CHECK(pixels);
            CHECK(SDL_ReadSurfacePixel(pixels,16,16,&red,&green,&blue,&alpha));
            CHECK(red==expected[kind] && green==red && blue==red);
            SDL_DestroySurface(pixels);
        }
        ui_tiles_select(0,64);
    }
    /* Bounds, duplicates, traversal, oversized keys and damaged files. */
    file(argv[1],"monster.txt","bad 1 0\n"); ui_tiles_select(2,64); CHECK(!ui_tiles_ready());
    file(argv[1],"monster.txt","same 0 0\nsame 0 0\n"); ui_tiles_reset(); CHECK(!ui_tiles_ready());
    file(argv[1],"pack-64.txt","monster ../sprite-64.png monster.txt\n"); ui_tiles_reset(); CHECK(!ui_tiles_ready());
    file(argv[1],"pack-64.txt","monster missing.png monster.txt\nobject sprite-64.png object.txt\n");
    ui_tiles_reset(); CHECK(ui_tiles_count()==1);
    file(argv[1],"missing.png","not a PNG"); ui_tiles_reset(); CHECK(ui_tiles_count()==1);
    ui_tiles_select(0,32);
    if (argc > 2)
    {
        ui_tiles_init(r,argv[2]);
        for(size=32;size<=64;size+=32)
        {
            ui_tiles_select(2,size); CHECK(ui_tiles_count()>580);
            CHECK(ui_tiles_find("monster","Giant white mouse",&tex,&src));
            CHECK(src.w==size);
            CHECK(!ui_tiles_find("monster","unpainted fictional test creature",NULL,NULL));
            {
                static cptr terrain[]={"door","open door","broken door","up staircase",
                    "down staircase","deep water","shallow water","deep lava","shallow lava","pile of rubble",
                    "snow","ice slick","slush","snow wall","ice wall","slippery slope","glacier",
                    "icy crevasse","dark pit","swamp","poisonous acid","toxic waste",
                    "glass floor","glass wall","permanent glass wall","glass door","open glass door",
                    "broken glass door","curtain","open curtain","web","snow-covered tree",
                    "open floor","dirt","patch of grass","shop-sign","Building",
                    "Quest Entrance","entrance to dungeon","town exit","Town","gate of arena",
                    "shaft up","shaft down","trap door","pit","dart trap","gas trap",
                    "discolored spot","evil rune","strange rune","explosive rune","compact rune",
                    "glyph of warding","mirror","Sigil startpoint","section of the Sigil",
                    "section of the Sigil (discharged)","Sigil exit","corrupted section of the Sigil",
                    "flower","bear trap","alarm","icicle trap",NULL};
                static cptr objects[]={"Gold","Food","Wooden Torch","Brass Lantern","Spellbook",
                    "Arrows","Bolts","Shots","Hard Armor","Chest","Digger","Spikes","Flask",
                    "Crown","Shield","Boots","Potion","Mushroom","Sword","Helm",
                    "Corpse","Bones","Statue","Magical Figurine","Junk","Dragon Scale Mail",
                    "Empty Bottle",NULL};
                SDL_FRect first, second;
                for(i=0;terrain[i];++i) CHECK(ui_tiles_find("terrain",terrain[i],NULL,NULL));
                for(i=0;objects[i];++i) CHECK(ui_tiles_find("object",objects[i],NULL,NULL));
                CHECK(ui_tiles_find("object","Potion",NULL,&first));
                CHECK(ui_tiles_find("object","unidentified Potion",NULL,&second));
                CHECK(first.x==second.x && first.y==second.y);
                for(i=20;objects[i];++i)
                {
                    char unknown[128];
                    SDL_snprintf(unknown,sizeof(unknown),"unidentified %s",objects[i]);
                    CHECK(ui_tiles_find("object",objects[i],NULL,&first));
                    CHECK(ui_tiles_find("object",unknown,NULL,&second));
                    CHECK(first.x==second.x && first.y==second.y);
                }
                CHECK(ui_tiles_find("terrain","deep water",NULL,&first));
                CHECK(ui_tiles_find("terrain","shallow water",NULL,&second));
                CHECK(first.x!=second.x || first.y!=second.y);
                CHECK(!ui_tiles_find("terrain","secret door",NULL,NULL));
                CHECK(!ui_tiles_find("terrain","locked door",NULL,NULL));
                CHECK(!ui_tiles_find("terrain","invisible trap",NULL,NULL));
                CHECK(!ui_tiles_find("terrain","locked glass door",NULL,NULL));
                CHECK(!ui_tiles_find("terrain","jammed glass door",NULL,NULL));
                CHECK(!ui_tiles_find("object","Photograph",NULL,NULL));
                CHECK(!ui_tiles_find("object","Body Part",NULL,NULL));
                CHECK(ui_tiles_find("terrain","shaft up",NULL,&first));
                CHECK(ui_tiles_find("terrain","shaft down",NULL,&second));
                CHECK(first.x!=second.x || first.y!=second.y);
                CHECK(ui_tiles_find("terrain","section of the Sigil",NULL,&first));
                CHECK(ui_tiles_find("terrain","section of the Sigil (discharged)",NULL,&second));
                CHECK(first.x!=second.x || first.y!=second.y);
                CHECK(!ui_tiles_find("terrain","poison pit",NULL,NULL));
                CHECK(!ui_tiles_find("terrain","poison gas trap",NULL,NULL));
            }
        }
    }
    for(size=4;size<=192;++size)
    {
        SDL_FRect a,b,dot; cell=(SDL_FRect){13,17,(float)size,(float)size};
        a=ui_square_rect(8,13,12,16,cell,FALSE);
        b=ui_square_rect(8,13,12,16,cell,TRUE);
        dot=ui_square_rect(2,2,12,16,cell,FALSE);
        CHECK(a.x>=cell.x && a.y>=cell.y && a.x+a.w<=cell.x+cell.w && a.y+a.h<=cell.y+cell.h);
        CHECK(b.w>=a.w && b.h==a.h && b.x+b.w<=cell.x+cell.w);
        CHECK(dot.w<=a.w && dot.h<=a.h);
    }
    CHECK(!memcmp(saved,Rand_state,sizeof(saved)) && value==Rand_value && place==Rand_place && !Rand_quick);
    ui_tiles_shutdown(); CHECK(!count && !sheet_count);
    SDL_DestroyRenderer(r); SDL_DestroySurface(target); SDL_Quit();
    puts("PASS: sprite/edge pixels, domains, fallback, corruption, 32/64 switching, unload/reset, square bounds and unchanged RNG");
    return 0;
}
