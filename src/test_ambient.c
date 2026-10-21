/* Test the shipped ambience mixer with a paused dummy device. LICENSE. */
#include "ui_sound.c"
#include "ui_ambient.c"
#define CHECK(x) do { if (!(x)) { printf("FAIL %d: %s\n", __LINE__, #x); return 1; } } while (0)

int main(int argc, char **argv)
{
    int i, j, n, loaded = 0;
    bool full = argc > 2;
    float out[AMB_BLOCK*2];
    u32b saved[RAND_DEG], value;
    u16b place;
    ambient_track *t;
    Uint8 *same;
    CHECK(argc > 1);
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    CHECK(_init_at(argv[1])); /* Effects own the SDL subsystem. */
    CHECK(SDL_PauseAudioStreamDevice(_stream));
    CHECK(_amb_init_at(argv[1]));
    CHECK(!_amb_owns_audio);
    CHECK(SDL_PauseAudioStreamDevice(_amb_stream));
    Rand_state_init(56789); Rand_quick = FALSE; Rand_value = 98765;
    memcpy(saved, Rand_state, sizeof(saved)); value = Rand_value; place = Rand_place;
    CHECK(ui_ambient_choose(FALSE, TRUE, 90, FALSE) == AMBIENT_NONE);
    CHECK(ui_ambient_choose(TRUE, FALSE, 90, TRUE) == AMBIENT_DAY);
    CHECK(ui_ambient_choose(TRUE, FALSE, 90, FALSE) == AMBIENT_NIGHT);
    for (i = 0; i <= 100; ++i)
        CHECK(ui_ambient_choose(TRUE, TRUE, i, TRUE) ==
            (i<=20 ? AMBIENT_SHALLOW : i<=40 ? AMBIENT_UPPER : i<=60 ? AMBIENT_MIDDLE : i<=80 ? AMBIENT_LOWER : AMBIENT_DEEP));
    for (i = 0; i < AMBIENT_COUNT; ++i)
        for (j = 1; j <= _amb_cues[i].variants; ++j)
        {
            ambient_track probe = {0};
            bool ok = _amb_load(&probe, i, j);
            if (full) CHECK(ok && probe.frames == AMB_RATE*60);
            else CHECK(ok == (i == AMBIENT_DAY && j == 1));
            if (ok) { ++loaded; SDL_free(probe.pcm); }
        }
    CHECK(SDL_GetHint(SDL_HINT_WAVE_TRUNCATION) == NULL);
    ui_ambient_options(TRUE, TRUE, 100);
    ui_ambient_update(AMBIENT_DAY, FALSE);
    t = &_amb_tracks[_amb_current]; same = t->pcm;
    CHECK(t->authored && same && t->gain == 0 && t->ramp == AMB_FADE);
    ui_ambient_update(AMBIENT_DAY, FALSE);
    CHECK(_amb_tracks[_amb_current].pcm == same && _amb_next[AMBIENT_DAY] == 1);
    _amb_mix(out, 1);
    if (!full)
    { CHECK(fabs(out[0] - 0.25f/AMB_FADE) < 0.0000001f); CHECK(out[1] < 0); }
    for (n = 0; n < AMB_FADE; n += AMB_BLOCK) _amb_mix(out, AMB_BLOCK);
    CHECK(t->gain == 1 && t->ramp == 0);
    t->cursor = t->frames - 1;
    _amb_mix(out, 2); CHECK(t->cursor == AMB_SEAM+1); /* blended loop */
    ui_ambient_update(AMBIENT_DAY, TRUE);
    for (n = 0; n < AMB_RATE; n += AMB_BLOCK) _amb_mix(out, AMB_BLOCK);
    CHECK(_amb_gain == 0.25f && t->gain == 1);
    ui_ambient_update(AMBIENT_DEEP, TRUE);
    CHECK(_amb_tracks[_amb_current].authored == full);
    CHECK(t->target == 0 && t->ramp == AMB_FADE);
    for (n = 0; n < AMB_FADE; n += AMB_BLOCK) _amb_mix(out, AMB_BLOCK);
    CHECK(t->gain == 0 && _amb_tracks[_amb_current].gain == 1);
    ui_ambient_focus(FALSE);
    i = _amb_tracks[_amb_current].cursor;
    _amb_mix(out, AMB_BLOCK);
    for (j = 0; j < AMB_BLOCK*2; ++j) CHECK(out[j] == 0);
    CHECK(_amb_tracks[_amb_current].cursor == i);
    ui_ambient_focus(TRUE); CHECK(_amb_gain == 0);
    ui_ambient_options(FALSE, TRUE, 100);
    ui_ambient_update(AMBIENT_DAY, FALSE);
    CHECK(!_amb_tracks[0].pcm && !_amb_tracks[1].pcm);
    ui_ambient_options(TRUE, FALSE, 100);
    ui_ambient_update(AMBIENT_DAY, FALSE);
    CHECK(!_amb_tracks[_amb_current].authored && _amb_tracks[_amb_current].pcm);
    ui_sound_options(FALSE, FALSE, 0);
    CHECK(_amb_tracks[_amb_current].pcm); /* independent effect mute */
    for (i = 0; i < 100; ++i)
    {
        ui_ambient_update(i % AMBIENT_COUNT, i % 2);
        _amb_mix(out, AMB_BLOCK);
        for (j = 0; j < AMB_BLOCK*2; ++j) CHECK(isfinite(out[j]) && fabs(out[j]) <= 1);
    }
    ui_ambient_update(AMBIENT_NONE, FALSE);
    for (n = 0; n < AMB_FADE+AMB_BLOCK; n += AMB_BLOCK) _amb_mix(out, AMB_BLOCK);
    for (j = 0; j < AMB_BLOCK*2; ++j) CHECK(out[j] == 0);
    CHECK(SDL_ResumeAudioStreamDevice(_amb_stream));
    for (i = 0; i < 30; ++i)
    { ui_ambient_update(i % AMBIENT_COUNT, FALSE); SDL_Delay(1); }
    CHECK(!Rand_quick && value == Rand_value && place == Rand_place);
    CHECK(memcmp(saved, Rand_state, sizeof(saved)) == 0);
    ui_ambient_shutdown(); CHECK(SDL_WasInit(SDL_INIT_AUDIO));
    CHECK(!_amb_stream && !_amb_tracks[0].pcm && !_amb_tracks[1].pcm);
    ui_sound_shutdown(); CHECK(!SDL_WasInit(SDL_INIT_AUDIO));
    CHECK(_amb_init_at(argv[1])); CHECK(_amb_owns_audio);
    ui_ambient_shutdown(); ui_ambient_shutdown();
    printf("PASS: %d ambience files; contexts, PCM, loop blend, fades, ducking, focus, mute, synth, rapid transitions, ownership and unchanged RNG\n", loaded);
    return 0;
}
