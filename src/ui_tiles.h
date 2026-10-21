/* Optional map artwork. Copyright (c) 2026 John Horton.
 * Angband licence in LICENSE; artwork has separate terms. */
#ifndef INCLUDED_UI_TILES_H
#define INCLUDED_UI_TILES_H
#include <SDL3/SDL.h>
/* Camera step, independent of artwork resolution. */
#define UI_TILE_PX 32
extern bool ui_tiles_init(SDL_Renderer *renderer, cptr dir);
extern void ui_tiles_select(int mode, int detail);
extern void ui_tiles_reset(void);
extern void ui_tiles_shutdown(void);
extern bool ui_tiles_ready(void);
extern int ui_tiles_count(void);
extern bool ui_tiles_find(cptr domain, cptr name, SDL_Texture **tex, SDL_FRect *src);
extern bool ui_tiles_draw_terrain(cptr name, SDL_FRect dst, int dim, u32b seed,
                                  bool vary, cptr edges[4], unsigned links);
extern void ui_tiles_report_missing(void);
#endif
