/* Exercise the actual SDL3 decoder, mixer and fallback with synthetic fixtures.
 * Copyright (c) 2026 John Horton. Source terms: LICENSE.
 * Include the implementation so callback state can be inspected while the
 * dummy device is paused; no test-only branches enter the shipped engine.
 */
#include "ui_sound.c"

#define CHECK(x) do { if (!(x)) { printf("FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)

int main(int argc, char **argv)
{
    int e, i, v, loaded = 0;
    u32b saved_state[RAND_DEG], saved_value;
    u16b saved_place;
    float mixed[16];
    snd_clip *clip, *first;
    bool full_pack = argc > 2;
    CHECK(argc > 1);
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    CHECK(_init_at(argv[1]));
    CHECK(SDL_PauseAudioStreamDevice(_stream));
    Rand_state_init(1234567);
    Rand_quick = FALSE;
    Rand_value = 7654321;
    saved_value = Rand_value;
    saved_place = Rand_place;
    memcpy(saved_state, Rand_state, sizeof(saved_state));
    ui_sound_options(TRUE, TRUE, 100);

    for (e = 1; e < SOUND_MAX; ++e)
    {
        if (e == SOUND_UNUSED) { CHECK(!_cue[e].path); continue; }
        CHECK(_cue[e].variants > 0 && _cue[e].variants <= MAX_VARIANTS);
        clip = _prepare(e, 0);
        CHECK(clip && clip->frames > 0);
        for (i = 0; i < clip->frames * MIX_CHANNELS; ++i)
            CHECK(isfinite(clip->pcm[i]) && fabs(clip->pcm[i]) <= 1.0f);
        for (v = 1; v <= _cue[e].variants; ++v)
        {
            clip = _prepare(e, v);
            if (full_pack) CHECK(clip && clip->frames > 0);
            if (clip) ++loaded;
        }
        CHECK(_cached <= CACHE_LIMIT);
    }

    if (!full_pack)
    {
        /* Stereo PCM conversion and rejection of corrupt/unsupported variants. */
        clip = _prepare(SOUND_HIT, 1);
        CHECK(clip && clip->frames == 480);
        CHECK(fabs(clip->pcm[0] - 0.25f) < 0.0001f);
        CHECK(fabs(clip->pcm[1] + 0.125f) < 0.0001f);
        CHECK(!_prepare(SOUND_HIT, 2));
        CHECK(!_prepare(SOUND_HIT, 3));
        CHECK(!_prepare(SOUND_HIT, 4));
        CHECK(_prepare(SOUND_HIT, 5));
        CHECK(_clips[SOUND_HIT][2].failed && _clips[SOUND_HIT][4].failed);

        ui_sound_play(SOUND_HIT);
        first = _playing[0].clip;
        CHECK(first == &_clips[SOUND_HIT][1]);
        ui_sound_play(SOUND_HIT); /* Same cue immediately: do not pile up. */
        CHECK(!_playing[1].clip && _next_variant[SOUND_HIT] == 1);
        _has_played[SOUND_HIT] = FALSE;
        ui_sound_play(SOUND_HIT); /* Skip damaged/missing files, use healthy #5. */
        CHECK(_playing[1].clip == &_clips[SOUND_HIT][5]);
        ui_sound_play(SOUND_MISS); /* Entire cue absent: original synth. */
        CHECK(_playing[2].clip == &_clips[SOUND_MISS][0]);
    }

    /* Two stereo voices mix concurrently; gain/volume have known arithmetic.
     * The second voice ends early, without delaying the first or leaving tails. */
    {
        float a[] = {0.25f, -0.5f, 0.5f, 0.25f};
        float b[] = {0.5f, 0.25f};
        snd_clip ca = {a, 2}, cb = {b, 1};
        memset(_playing, 0, sizeof(_playing));
        _volume = 50;
        _playing[0].clip = &ca; _playing[0].gain = 0.5f;
        _playing[1].clip = &cb; _playing[1].gain = 1.0f;
        _mix(mixed, 4);
        CHECK(fabs(mixed[0] - 0.3125f) < 0.0001f && mixed[1] == 0.0f);
        CHECK(fabs(mixed[2] - 0.125f) < 0.0001f && fabs(mixed[3] - 0.0625f) < 0.0001f);
        CHECK(mixed[4] == 0.0f && mixed[7] == 0.0f);
        CHECK(!_playing[0].clip && !_playing[1].clip);
        for (i = 0; i < MIX_VOICES; ++i) { _playing[i].clip = &ca; _playing[i].gain = 1; }
        _volume = 100;
        _mix(mixed, 4);
        CHECK(mixed[0] == 1.0f && mixed[1] == -1.0f);
    }

    /* Repeated cache eviction cannot free a sample still used by a voice. */
    ui_sound_options(TRUE, FALSE, 70);
    ui_sound_play(SOUND_DEATH);
    first = _playing[0].clip;
    CHECK(first && first->pcm);
    for (e = 1; e < SOUND_MAX; ++e)
        if (e != SOUND_UNUSED) CHECK(_prepare(e, 0));
    CHECK(_playing[0].clip == first && first->pcm);
    CHECK(_cached <= CACHE_LIMIT);
    /* Flood all voice slots: ordinary events must not evict death/warnings. */
    for (e = 1; e < SOUND_MAX; ++e)
        if (!_priority(e)) { _has_played[e] = FALSE; ui_sound_play(e); }
    CHECK(_playing[0].event == SOUND_DEATH && _playing[0].clip == first);
    ui_sound_options(FALSE, FALSE, 70);
    for (i = 0; i < MIX_VOICES; ++i) CHECK(!_playing[i].clip);
    ui_sound_play(SOUND_HIT);
    CHECK(!_playing[0].clip);
    ui_sound_options(TRUE, FALSE, 0);
    ui_sound_play(SOUND_HIT);
    CHECK(!_playing[0].clip);
    ui_sound_options(TRUE, FALSE, 70);
    ui_sound_play(SOUND_HIT);
    CHECK(_playing[0].clip == &_clips[SOUND_HIT][0]);
    ui_sound_options(TRUE, TRUE, 70);
    CHECK(!_playing[0].clip);
    ui_sound_play(0); ui_sound_play(-1); ui_sound_play(SOUND_MAX); ui_sound_play(SOUND_UNUSED);
    CHECK(!_playing[0].clip);

    /* A long held key sounds one bump, regardless of the rate limiter. A
     * fresh press immediately re-arms it; text repeats never do. */
    ui_sound_options(TRUE, FALSE, 70);
    memset(_playing, 0, sizeof(_playing));
    ui_sound_input(FALSE); ui_sound_play(SOUND_HITWALL);
    CHECK(_playing[0].event == SOUND_HITWALL && _playing[0].clip);
    for (i = 0; i < 20; ++i)
    {
        _has_played[SOUND_HITWALL] = FALSE;
        ui_sound_input(TRUE); ui_sound_play(SOUND_HITWALL);
        CHECK(!_playing[1].clip);
    }
    ui_sound_input(FALSE); ui_sound_play(SOUND_HITWALL);
    CHECK(_playing[1].event == SOUND_HITWALL && _playing[1].clip);
    memset(_playing, 0, sizeof(_playing));
    ui_sound_details(FALSE, FALSE);
    ui_sound_play(SOUND_WALK); ui_sound_play(SOUND_UI_ACCEPT);
    CHECK(!_playing[0].clip);
    ui_sound_details(TRUE, TRUE);
    _has_played[SOUND_WALK] = _has_played[SOUND_UI_ACCEPT] = FALSE;
    ui_sound_play(SOUND_WALK); ui_sound_play(SOUND_UI_ACCEPT);
    CHECK(_playing[0].event == SOUND_WALK && _playing[1].event == SOUND_UI_ACCEPT);
    ui_sound_details(FALSE, FALSE);
    CHECK(!_playing[0].clip && !_playing[1].clip);
    ui_sound_details(TRUE, TRUE);

    CHECK(SDL_ResumeAudioStreamDevice(_stream));
    for (i = 0; i < 300; ++i)
    {
        e = 1 + i % (SOUND_MAX - 1);
        _has_played[e] = FALSE;
        ui_sound_play(e);
        SDL_Delay(1);
    }
    CHECK(Rand_quick == FALSE && Rand_value == saved_value && Rand_place == saved_place);
    CHECK(memcmp(saved_state, Rand_state, sizeof(saved_state)) == 0);
    ui_sound_shutdown();
    CHECK(!_stream && !_cached);
    CHECK(_init_at(argv[1]));
    ui_sound_play(SOUND_HIT);
    ui_sound_shutdown();
    ui_sound_shutdown();
    printf("PASS: decoder, %d cue variants, mixer arithmetic, clipping, cache, priorities, mute, fallback, rotation, reopen and unchanged game RNG\n", loaded);
    return 0;
}
