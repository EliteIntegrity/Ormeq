/* Real SDL music/effects streams with generated PCM, no authored assets.
 * Copyright (c) 2026 John Horton. Source terms: LICENSE. */
#include "ui_sound.c"
#include "ui_music.c"

#define CHECK(x) do { if (!(x)) { printf("FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)

static int _test_track(const char *path)
{
    float out[MUSIC_BLOCK * 2], peak = 0.0f;
    int remaining, frames, i, frame, channel;
    CHECK(_music_load(path)); CHECK(SDL_PauseAudioStreamDevice(_music_stream));
    CHECK(strcmp(ui_music_status(), "Ready") == 0);
    ui_music_options(TRUE, 100); ui_music_title(TRUE);
    _music_gain = _music_target = 1.0f; _music_ramp = 0;
    remaining = _music_frames;
    while (remaining)
    {
        frames = MIN(MUSIC_BLOCK, remaining);
        _music_mix(out, frames);
        for (i = 0; i < frames * 2; ++i)
        {
            CHECK(out[i] >= -1.0f && out[i] <= 1.0f); /* Also rejects NaN. */
            if (fabs(out[i]) > peak) peak = (float)fabs(out[i]);
        }
        remaining -= frames;
    }
    CHECK(peak > 0.01f && _music_cursor == 0);
    /* Verify real delivered samples, away from the loop's edge envelope. */
    frame = _music_frames / 2; _music_cursor = frame;
    _music_mix(out, 1);
    for (channel = 0; channel < 2; ++channel)
    {
        const Uint8 *p = _music_pcm + (frame * _music_channels +
            (_music_channels == 1 ? 0 : channel)) * 2;
        int sample = p[0] | (p[1] << 8);
        if (sample >= 32768) sample -= 65536;
        CHECK(out[channel] == sample / 32768.0f);
    }
    printf("PASS: complete installed title track, %d frames at %d Hz; peak %.6f, loop and PCM verified\n",
        _music_frames, _music_rate, peak);
    ui_music_shutdown();
    return 0;
}

int main(int argc, char **argv)
{
    char path[1200];
    float out[1024];
    int i, cursor;
    snd_clip *effect;
    u32b saved_state[RAND_DEG], saved_value;
    u16b saved_place;
    const char *bad[] = {"missing.wav", "corrupt.wav", "empty.wav", "truncated.wav", "rate.wav", "large.wav", "wide.wav"};
    CHECK(argc == 2 || (argc == 3 && strcmp(argv[1], "--track") == 0));
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    Rand_state_init(1234567); Rand_quick = FALSE; Rand_value = 7654321;
    saved_value = Rand_value; saved_place = Rand_place;
    memcpy(saved_state, Rand_state, sizeof(saved_state));
    if (argc == 3)
    {
        CHECK(_test_track(argv[2]) == 0);
        CHECK(Rand_quick == FALSE && Rand_value == saved_value && Rand_place == saved_place);
        CHECK(memcmp(saved_state, Rand_state, sizeof(saved_state)) == 0);
        return 0;
    }
    for (i = 0; i < (int)(sizeof(bad)/sizeof(bad[0])); ++i)
    {
        SDL_snprintf(path, sizeof(path), "%s/%s", argv[1], bad[i]);
        CHECK(!_music_load(path)); CHECK(!_music_stream && !_music_pcm);
        ui_music_options(FALSE, 0); ui_music_title(TRUE); ui_music_title(FALSE);
        ui_music_shutdown();
    }
    /* Separate streams: either mute must leave the other channel untouched. */
    CHECK(_init_at(argv[1])); CHECK(SDL_PauseAudioStreamDevice(_stream));
    ui_sound_options(TRUE, FALSE, 70); ui_sound_play(SOUND_HIT);
    effect = _playing[0].clip; CHECK(effect);
    SDL_snprintf(path, sizeof(path), "%s/stereo.wav", argv[1]);
    SDL_SetHint(SDL_HINT_WAVE_TRUNCATION, "dropframe");
    CHECK(_music_load(path)); CHECK(SDL_PauseAudioStreamDevice(_music_stream));
    CHECK(strcmp(SDL_GetHint(SDL_HINT_WAVE_TRUNCATION), "dropframe") == 0);
    CHECK(_music_stream != _stream && _music_rate == 48000 && _music_frames == 48000);
    CHECK(strcmp(ui_music_status(), "Ready") == 0);
    ui_music_options(TRUE, 50); ui_music_title(TRUE);
    _music_cursor = 1000; _music_gain = _music_target = 0.5f; _music_ramp = 0;
    _music_mix(out, 1);
    CHECK(fabs(out[0] - 0.125f) < 0.00001f && fabs(out[1] + 0.0625f) < 0.00001f);
    _music_cursor = _music_frames - 2;
    _music_mix(out, 4);
    CHECK(_music_cursor == 2 && out[0] > 0 && out[2] == 0 && out[4] == 0 && out[6] > 0);
    cursor = _music_cursor;
    ui_music_options(FALSE, 50); _music_mix(out, 512);
    CHECK(_music_cursor == cursor && _playing[0].clip == effect);
    for (i = 0; i < 1024; ++i) CHECK(out[i] == 0.0f);
    ui_music_options(TRUE, 50); ui_sound_options(FALSE, FALSE, 70);
    CHECK(_music_target == 0.5f && !_playing[0].clip);
    _music_mix(out, 512); CHECK(_music_cursor != cursor && _music_gain > 0);
    cursor = _music_cursor;
    ui_music_focus(FALSE); _music_mix(out, 512); CHECK(_music_cursor == cursor);
    for (i = 0; i < 1024; ++i) CHECK(out[i] == 0.0f);
    ui_music_focus(TRUE); _music_mix(out, 512); CHECK(_music_cursor != cursor);
    ui_music_options(TRUE, 0); cursor = _music_cursor;
    _music_mix(out, 512); CHECK(_music_cursor == cursor && _music_gain == 0.0f);
    ui_music_options(TRUE, 1000); CHECK(_music_volume == 100);
    ui_music_options(TRUE, -1); CHECK(_music_volume == 0);
    ui_music_options(TRUE, 50);
    /* Fade in/out uses sample time, not turn count; stopping resets the loop. */
    ui_music_title(FALSE);
    for (i = 0; i < 24; ++i) _music_mix(out, 512);
    CHECK(!_music_ramp && _music_gain == 0.0f && _music_cursor == 0);
    _music_mix(out, 512); CHECK(_music_cursor == 0);
    ui_music_title(TRUE);
    _music_mix(out, 512); CHECK(_music_gain > 0 && _music_gain < 0.5f);
    for (i = 0; i < 66; ++i) _music_mix(out, 512);
    CHECK(!_music_ramp && _music_gain == 0.5f);
    cursor = _music_cursor; ui_music_title(TRUE);
    CHECK(_music_cursor == cursor); /* Repeated title notifications don't restart. */
    CHECK(SDL_ResumeAudioStreamDevice(_stream));
    CHECK(SDL_ResumeAudioStreamDevice(_music_stream));
    for (i = 0; i < 200; ++i)
    {
        ui_music_options(i % 3 != 0, i % 101);
        ui_sound_options(TRUE, FALSE, 70); ui_sound_play(SOUND_HIT);
        ui_music_focus(i % 5 != 0); ui_music_title(i % 7 != 0);
        SDL_Delay(1);
    }
    ui_music_shutdown();
    CHECK(!_music_stream && !_music_pcm && _stream);
    /* Standard 44.1 kHz mono master: duplicate channels; SDL resamples output. */
    SDL_snprintf(path, sizeof(path), "%s/mono.wav", argv[1]);
    CHECK(_music_load(path)); CHECK(SDL_PauseAudioStreamDevice(_music_stream));
    CHECK(_music_rate == 44100 && _music_frames == 44100 && _music_channels == 1);
    ui_music_focus(TRUE); ui_music_options(TRUE, 50); ui_music_title(TRUE);
    _music_gain = _music_target = 0.5f; _music_ramp = 0; _music_cursor = 1000;
    _music_mix(out, 1);
    CHECK(out[0] == out[1] && fabs(out[0] - 0.125f) < 0.00001f);
    ui_music_shutdown(); ui_music_shutdown(); ui_sound_shutdown();
    CHECK(Rand_quick == FALSE && Rand_value == saved_value && Rand_place == saved_place);
    CHECK(memcmp(saved_state, Rand_state, sizeof(saved_state)) == 0);
    printf("PASS: title WAV validation, looping, fades, independent channels, mute/focus, reopen, callback stress and unchanged game RNG\n");
    return 0;
}
