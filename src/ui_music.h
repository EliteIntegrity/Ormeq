/* Optional title music; SDL3 presentation only. Copyright (c) 2026 John Horton.
 * Source terms: LICENSE. Music files are distributed separately. */
#ifndef INCLUDED_UI_MUSIC_H
#define INCLUDED_UI_MUSIC_H
extern void ui_music_init(void);
extern void ui_music_shutdown(void);
extern void ui_music_options(bool enabled, int volume);
extern void ui_music_title(bool active);
extern void ui_music_focus(bool focused);
extern cptr ui_music_status(void);
#endif
