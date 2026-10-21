/* Optional background soundscapes. Copyright (c) 2026 John Horton. LICENSE. */
#ifndef INCLUDED_UI_AMBIENT_H
#define INCLUDED_UI_AMBIENT_H
enum { AMBIENT_NONE = -1, AMBIENT_DAY, AMBIENT_NIGHT, AMBIENT_SHALLOW,
    AMBIENT_UPPER, AMBIENT_MIDDLE, AMBIENT_LOWER, AMBIENT_DEEP, AMBIENT_COUNT };
extern void ui_ambient_init(void);
extern void ui_ambient_shutdown(void);
extern void ui_ambient_options(bool enabled, bool authored, int volume);
extern void ui_ambient_focus(bool focused);
/* Inputs are public location/clock information, never unseen creatures. */
extern int ui_ambient_choose(bool playing, bool underground, int depth, bool day);
extern void ui_ambient_update(int context, bool quiet);
#endif
