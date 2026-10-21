/* Optional sprite packs. Copyright (c) 2026 John Horton.
 * Angband licence in LICENSE. No game RNG or saved state. */
#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include "scottish.h"
#include "ui_tiles.h"
#define MAX_TILES 4096
#define MAX_SHEETS 16
#define KEY_LEN 160
typedef struct { char key[KEY_LEN]; SDL_Texture *tex; SDL_FRect src; } tile_t;
static tile_t entries[MAX_TILES];
static SDL_Texture *sheets[MAX_SHEETS];
static int count, sheet_count, selected_mode = -1, selected_detail;
static SDL_Renderer *renderer;
static char directory[1024];
static void clear_pack(void)
{
    int i;
    for (i = 0; i < sheet_count; ++i) SDL_DestroyTexture(sheets[i]);
    sheet_count = count = 0;
}
static bool safe_file(cptr file)
{
    size_t i, n = strlen(file);
    if (!n || n > 100 || file[0] == '.' || strstr(file, "..")) return FALSE;
    for (i = 0; i < n; ++i)
        if (!((file[i] >= 'a' && file[i] <= 'z') ||
              (file[i] >= '0' && file[i] <= '9') ||
              file[i] == '-' || file[i] == '_' || file[i] == '.')) return FALSE;
    return TRUE;
}
static bool key_of(cptr domain, cptr name, char *out)
{
    size_t n;
    bool gap = FALSE;
    if (!domain || !name || (strcmp(domain,"monster") && strcmp(domain,"object") && strcmp(domain,"terrain"))) return FALSE;
    n = strlen(domain);
    memcpy(out, domain, n);
    out[n++] = ':';
    while (*name)
    {
        unsigned char c = (unsigned char)*name++;
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
        {
            if (n + 2 >= KEY_LEN) return FALSE;
            if (gap && out[n-1] != ':') out[n++] = '-';
            out[n++] = c;
            gap = FALSE;
        }
        else gap = TRUE;
    }
    out[n] = 0;
    return out[n-1] != ':';
}
static int compare_tiles(const void *a, const void *b)
{
    return strcmp(((const tile_t *)a)->key, ((const tile_t *)b)->key);
}
static bool read_sheet(cptr domain, cptr image, cptr index, int detail)
{
    char path[1200], line[256], name[160], extra;
    int cols, rows, x, y;
    FILE *fp;
    SDL_Surface *surface;
    SDL_Texture *texture;
    if (!safe_file(image) || !safe_file(index) || sheet_count >= MAX_SHEETS) return FALSE;
    SDL_snprintf(path, sizeof(path), "%s/%s", directory, image);
    surface = IMG_Load(path);
    /* Missing/corrupt artwork leaves only this sheet's identities absent. */
    if (!surface) return TRUE;
    if (surface->w <= 0 || surface->h <= 0 || surface->w % detail || surface->h % detail ||
        surface->w > 4096 || surface->h > 4096)
    { SDL_DestroySurface(surface); return FALSE; }
    cols = surface->w / detail; rows = surface->h / detail;
    texture = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_DestroySurface(surface);
    if (!texture) return TRUE;
    sheets[sheet_count++] = texture;
    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    SDL_snprintf(path, sizeof(path), "%s/%s", directory, index);
    fp = fopen(path, "r");
    if (!fp) return TRUE;
    while (fgets(line, sizeof(line), fp))
    {
        tile_t *t;
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
        if (sscanf(line, "%159s %d %d %c", name, &x, &y, &extra) != 3 ||
            x < 0 || y < 0 || x >= cols || y >= rows || count >= MAX_TILES)
        { fclose(fp); return FALSE; }
        t = &entries[count];
        if (!key_of(domain, name, t->key)) { fclose(fp); return FALSE; }
        t->tex = texture;
        t->src = (SDL_FRect){(float)(x*detail),(float)(y*detail),(float)detail,(float)detail};
        ++count;
    }
    fclose(fp);
    return TRUE;
}
void ui_tiles_select(int mode, int detail)
{
    char path[1200], line[384], domain[16], image[110], index[110], extra;
    FILE *fp;
    int i;
    if (detail != 32) detail = 64;
    if (selected_mode == mode && selected_detail == detail) return;
    clear_pack();
    selected_mode = mode; selected_detail = detail;
    if (mode <= 0 || !renderer) return;
    SDL_snprintf(path, sizeof(path), "%s/pack-%d.txt", directory, detail);
    fp = fopen(path, "r");
    if (!fp) return; /* Cache absence too: no file access while drawing. */
    while (fgets(line, sizeof(line), fp))
    {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
        if (sscanf(line, "%15s %109s %109s %c", domain, image, index, &extra) != 3 ||
            (strcmp(domain,"monster") && strcmp(domain,"object") && strcmp(domain,"terrain"))) goto invalid;
        if (mode == 1 && !strcmp(domain,"terrain")) continue;
        if (!read_sheet(domain, image, index, detail)) goto invalid;
    }
    fclose(fp);
    qsort(entries, count, sizeof(entries[0]), compare_tiles);
    for (i = 1; i < count; ++i)
        if (!strcmp(entries[i-1].key, entries[i].key)) { clear_pack(); break; }
    return;
invalid:
    fclose(fp);
    clear_pack();
}
bool ui_tiles_init(SDL_Renderer *r, cptr dir)
{
    ui_tiles_shutdown();
    renderer = r;
    SDL_strlcpy(directory, dir, sizeof(directory));
    return TRUE;
}
void ui_tiles_reset(void)
{
    int mode = selected_mode, detail = selected_detail;
    selected_mode = -1;
    ui_tiles_select(mode, detail);
}
void ui_tiles_shutdown(void) { clear_pack(); renderer = NULL; selected_mode = -1; }
bool ui_tiles_ready(void) { return count > 0; }
int ui_tiles_count(void) { return count; }
void ui_tiles_report_missing(void) { printf("tiles: %d optional entries loaded\n", count); }
bool ui_tiles_find(cptr domain, cptr name, SDL_Texture **tex, SDL_FRect *src)
{
    tile_t key, *found;
    if (!count || !key_of(domain, name, key.key)) return FALSE;
    found = bsearch(&key, entries, count, sizeof(entries[0]), compare_tiles);
    if (!found) return FALSE;
    if (tex) *tex = found->tex;
    if (src) *src = found->src;
    return TRUE;
}

/* Narrow, irregular feathered bands stay entirely inside their own cell.
 * Only a known neighbour can request one. The centre retains the original
 * material, so a shore never makes deep water or a chasm look walkable. */
static void terrain_edge(SDL_Texture *tex, SDL_FRect src, SDL_FRect dst,
                         int side, int dim, u32b seed, bool shore)
{
    SDL_Vertex vertices[15];
    int indices[48], i, j;
    float tw, th;
    SDL_GetTextureSize(tex, &tw, &th);
    for (i = 0; i < 5; ++i)
    {
        float along = i / 4.0f;
        float depth = 0.10f + ((seed >> (i * 3)) & 7) * 0.012f;
        for (j = 0; j < 3; ++j)
        {
            float x = along, y = j == 2 ? depth : j * depth * 0.3f;
            SDL_Vertex *v = &vertices[i * 3 + j];
            if (side == 1) { x = 1.0f - y; y = along; }
            else if (side == 2) { x = 1.0f - along; y = 1.0f - y; }
            else if (side == 3) { x = y; y = 1.0f - along; }
            v->position = (SDL_FPoint){dst.x + x * dst.w, dst.y + y * dst.h};
            v->tex_coord = (SDL_FPoint){(src.x + x * src.w) / tw, (src.y + y * src.h) / th};
            v->color = (SDL_FColor){dim / 255.0f, dim / 255.0f, dim / 255.0f,
                j == 2 || (!j && shore) ? 0.0f : 0.92f};
        }
        if (i < 4)
        {
            for (j = 0; j < 2; ++j)
            {
                int k = i * 12 + j * 6, at = i * 3 + j;
                indices[k] = at; indices[k+1] = at+3; indices[k+2] = at+1;
                indices[k+3] = at+1; indices[k+4] = at+3; indices[k+5] = at+4;
            }
        }
    }
    SDL_SetTextureColorMod(tex,255,255,255);
    SDL_RenderGeometry(renderer,tex,vertices,15,indices,48);
}

static void terrain_surface(SDL_Texture *tex, SDL_FRect src, SDL_FRect dst,
                            int dim, unsigned flip, unsigned shore)
{
    static const float axis[] = {0.0f,0.18f,0.82f,1.0f};
    SDL_Vertex v[16];
    int indices[54], x, y;
    float tw, th;
    SDL_GetTextureSize(tex,&tw,&th);
    for (y = 0; y < 4; ++y) for (x = 0; x < 4; ++x)
    {
        int at = y * 4 + x;
        bool edge = (!y && (shore & 1)) || (x == 3 && (shore & 2)) ||
                    (y == 3 && (shore & 4)) || (!x && (shore & 8));
        float u = flip & 1 ? 1.0f - axis[x] : axis[x];
        float w = flip & 2 ? 1.0f - axis[y] : axis[y];
        v[at].position = (SDL_FPoint){dst.x+axis[x]*dst.w,dst.y+axis[y]*dst.h};
        v[at].tex_coord = (SDL_FPoint){(src.x+u*src.w)/tw,(src.y+w*src.h)/th};
        v[at].color = (SDL_FColor){dim/255.0f,dim/255.0f,dim/255.0f,edge ? 0.0f : 1.0f};
        if (x < 3 && y < 3)
        {
            int k = (y * 3 + x) * 6;
            indices[k]=at; indices[k+1]=at+1; indices[k+2]=at+4;
            indices[k+3]=at+4; indices[k+4]=at+1; indices[k+5]=at+5;
        }
    }
    SDL_SetTextureColorMod(tex,255,255,255);
    SDL_RenderGeometry(renderer,tex,v,16,indices,54);
}

/* Reuse the original picture, trimming end caps only towards known matching
 * neighbours. UV rotation keeps a vertical curtain inside its own cell even
 * at non-square pixel sizes; an open curtain keeps its transparent opening. */
static void terrain_connected(SDL_Texture *tex, SDL_FRect src, SDL_FRect dst,
                              int dim, unsigned links, bool curtain)
{
    SDL_Vertex v[4];
    static const int indices[] = {0,1,2,0,2,3};
    static const float px[] = {0,1,1,0}, py[] = {0,0,1,1};
    float tw,th,left=0,right=1,top=0,bottom=1;
    bool vertical = curtain && !(links & 10) && (links & 5);
    int i;
    if (curtain)
    {
        left = (links & (vertical ? 1 : 8)) ? 0.24f : 0;
        right = (links & (vertical ? 4 : 2)) ? 0.76f : 1;
    }
    else
    {
        if (links & 8) left=0.36f;
        if (links & 2) right=0.64f;
        if (links & 1) top=0.36f;
        if (links & 4) bottom=0.64f;
    }
    SDL_GetTextureSize(tex,&tw,&th);
    for (i=0;i<4;++i)
    {
        float u=vertical ? py[i] : px[i], w=vertical ? 1-px[i] : py[i];
        v[i].position=(SDL_FPoint){dst.x+px[i]*dst.w,dst.y+py[i]*dst.h};
        v[i].tex_coord=(SDL_FPoint){(src.x+(left+u*(right-left))*src.w)/tw,
                                   (src.y+(top+w*(bottom-top))*src.h)/th};
        v[i].color=(SDL_FColor){dim/255.0f,dim/255.0f,dim/255.0f,1};
    }
    SDL_SetTextureColorMod(tex,255,255,255);
    SDL_RenderGeometry(renderer,tex,v,4,indices,6);
}

bool ui_tiles_draw_terrain(cptr name, SDL_FRect dst, int dim, u32b seed,
                           bool vary, cptr edges[4], unsigned links)
{
    SDL_Texture *tex;
    SDL_FRect src;
    int i;
    unsigned shore = 0, flip = vary ? seed & 3 : 0;
    if (!ui_tiles_find("terrain",name,&tex,&src)) return FALSE;
    /* Adjacent cells must share one physical pixel boundary. In particular,
     * software-rendered flips can leave a seam at fractional map zooms. */
    dst.w = SDL_roundf(dst.x + dst.w) - SDL_roundf(dst.x);
    dst.h = SDL_roundf(dst.y + dst.h) - SDL_roundf(dst.y);
    dst.x = SDL_roundf(dst.x); dst.y = SDL_roundf(dst.y);
    if (dst.w <= 0 || dst.h <= 0) return FALSE;
    if (vary) dim = dim * (97 + (seed >> 8) % 4) / 100;
    /* Quiet ground is deliberately subordinate to occupants and hazards. */
    if (!strcmp(name,"open floor")) dim = dim * 85 / 100;
    else if (!strcmp(name,"dirt")) dim = dim / 2;
    else if (!strcmp(name,"patch of grass")) dim = dim * 75 / 100;
    if (dst.w >= 6 && dst.h >= 6 && edges)
        for (i = 0; i < 4; ++i)
            if (edges[i] && !strcmp(edges[i],"rock-bank") &&
                ui_tiles_find("terrain",edges[i],NULL,NULL)) shore |= 1u << i;
    if (links && (!strcmp(name,"dark pit") || !strcmp(name,"icy crevasse") ||
        !strcmp(name,"curtain") || !strcmp(name,"open curtain")))
        terrain_connected(tex,src,dst,dim,links,
            !strcmp(name,"curtain") || !strcmp(name,"open curtain"));
    else if (shore) terrain_surface(tex,src,dst,dim,flip,shore);
    else
    {
        SDL_SetTextureColorMod(tex,(Uint8)dim,(Uint8)dim,(Uint8)dim);
        SDL_RenderTextureRotated(renderer,tex,&src,&dst,0,NULL,(SDL_FlipMode)flip);
    }
    if (dst.w >= 6 && dst.h >= 6 && edges)
        for (i = 0; i < 4; ++i)
            if (edges[i] && ui_tiles_find("terrain",edges[i],&tex,&src))
                terrain_edge(tex,src,dst,i,dim,seed ^ (u32b)i * 2654435761u,BOOL(shore & (1u << i)));
    return TRUE;
}
