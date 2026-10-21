/* Ormeq's interface to the licensed bitmap reader in readdib.c.
 * Copyright (c) 2026 John Horton. Distributed under the Angband licence
 * reproduced in LICENSE.
 */
#ifndef ORMEQ_WIN_BITMAP_H
#define ORMEQ_WIN_BITMAP_H

#include <windows.h>

typedef struct win_bitmap_s
{
    HGLOBAL storage;
    HBITMAP bitmap;
    HPALETTE palette;
} win_bitmap_t;

BOOL ReadDIB(HWND window, LPSTR filename, win_bitmap_t *image);

#endif
