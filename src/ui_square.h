/* Square-map ink placement. Copyright (c) 2026 John Horton.
 * Angband licence in LICENSE. Inputs and output are physical pixels. */
#ifndef INCLUDED_UI_SQUARE_H
#define INCLUDED_UI_SQUARE_H
#include <SDL3/SDL.h>
static SDL_FRect ui_square_rect(float iw, float ih, float maxw, float maxh,
                               SDL_FRect cell, bool wall)
{
    SDL_FRect result;
    float width = SDL_max(1.0f, cell.w - 2.0f);
    float height = SDL_max(1.0f, cell.h - 2.0f);
    float scale = SDL_min(width / SDL_max(1.0f,maxw), height / SDL_max(1.0f,maxh));
    result.w = SDL_min(width, SDL_max(1.0f, SDL_roundf(iw * scale * (wall ? 1.2f : 1.0f))));
    result.h = SDL_min(height, SDL_max(1.0f, SDL_roundf(ih * scale)));
    result.x = SDL_roundf(cell.x + (cell.w - result.w) * 0.5f);
    result.y = SDL_roundf(cell.y + (cell.h - result.h) * 0.5f);
    return result;
}
#endif
