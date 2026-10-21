/* Optional authored effects with Ormeq's original synthesised fallback.
 * SDL3 presentation only; no gameplay state or random numbers are consumed.
 * Copyright (c) 2026 John Horton. Source terms: LICENSE.
 */
#ifndef INCLUDED_UI_SOUND_H
#define INCLUDED_UI_SOUND_H
extern bool ui_sound_init(void);
extern void ui_sound_shutdown(void);
extern void ui_sound_options(bool enabled, bool authored, int volume);
extern void ui_sound_play(int event);
extern void ui_sound_details(bool footsteps, bool menus);
/* Fresh physical presses re-arm wall feedback; auto-repeat does not. */
extern void ui_sound_input(bool repeat);
/* -S auditions the installed pack; -S0 auditions the original synth. */
extern void ui_sound_audition(void);
#endif
