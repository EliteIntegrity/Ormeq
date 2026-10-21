/* Background ambience on an independent SDL stream, using bounded PCM16
 * buffers. No gameplay state or RNG is accessed here. The callback performs
 * no loading, allocation or freeing. Copyright (c) 2026 John Horton. LICENSE. */
#include <SDL3/SDL.h>
#include "scottish.h"
#include "ui_ambient.h"
#include <math.h>

#define AMB_RATE 48000
#define AMB_BLOCK 512
#define AMB_FADE AMB_RATE
#define AMB_SEAM (AMB_RATE / 10)
typedef struct { const char *path; int variants; } ambient_cue;
static const ambient_cue _amb_cues[AMBIENT_COUNT] = {
#define AMBIENT(id, path, variants) [id] = {path, variants},
#include "ui_ambient_map.h"
#undef AMBIENT
};
typedef struct {
    Uint8 *pcm;
    int frames, cursor, ramp;
    float gain, target;
    bool authored;
} ambient_track;
static ambient_track _amb_tracks[2];
static SDL_AudioStream *_amb_stream;
static bool _amb_enabled = TRUE, _amb_authored = TRUE, _amb_focused = TRUE;
static bool _amb_owns_audio, _amb_quiet;
static int _amb_volume = 25, _amb_context = AMBIENT_NONE, _amb_current;
static unsigned _amb_next[AMBIENT_COUNT];
static float _amb_gain = 1.0f, _amb_target = 1.0f;
static int _amb_ramp;
static char _amb_root[1024];

int ui_ambient_choose(bool playing, bool underground, int depth, bool day)
{
    if (!playing) return AMBIENT_NONE;
    if (!underground) return day ? AMBIENT_DAY : AMBIENT_NIGHT;
    if (depth <= 20) return AMBIENT_SHALLOW;
    if (depth <= 40) return AMBIENT_UPPER;
    if (depth <= 60) return AMBIENT_MIDDLE;
    if (depth <= 80) return AMBIENT_LOWER;
    return AMBIENT_DEEP;
}

static bool _amb_load(ambient_track *track, int context, int variant)
{
    char path[1200];
    SDL_PathInfo info;
    SDL_AudioSpec spec;
    Uint8 *pcm = NULL;
    Uint32 bytes = 0;
    const char *hint;
    char *previous;
    bool loaded;
    SDL_snprintf(path, sizeof(path), "%s/%s_%02d.wav", _amb_root, _amb_cues[context].path, variant);
    if (!SDL_GetPathInfo(path, &info) || info.type != SDL_PATHTYPE_FILE ||
        info.size > 16 * 1024 * 1024) return FALSE;
    hint = SDL_GetHint(SDL_HINT_WAVE_TRUNCATION);
    previous = hint ? SDL_strdup(hint) : NULL;
    if (hint && !previous) return FALSE;
    SDL_SetHintWithPriority(SDL_HINT_WAVE_TRUNCATION, "verystrict", SDL_HINT_OVERRIDE);
    loaded = SDL_LoadWAV(path, &spec, &pcm, &bytes);
    if (previous) SDL_SetHintWithPriority(SDL_HINT_WAVE_TRUNCATION, previous, SDL_HINT_OVERRIDE);
    else SDL_ResetHint(SDL_HINT_WAVE_TRUNCATION);
    SDL_free(previous);
    if (!loaded) return FALSE;
    if (spec.freq != AMB_RATE || spec.format != SDL_AUDIO_S16LE || spec.channels != 2 ||
        bytes < AMB_RATE * 4 || bytes % 4 || bytes > AMB_RATE * 4 * 65)
    { SDL_free(pcm); return FALSE; }
    track->pcm = pcm; track->frames = bytes / 4; track->authored = TRUE;
    return TRUE;
}

/* A restrained, filtered air/drone bed for source builds. Generated once on
 * the main thread with a private seed. The loop seam is blended by the mixer. */
static bool _amb_synth(ambient_track *track, int context)
{
    unsigned seed = 0x65abc123u + context;
    float air = 0;
    int f, ch;
    track->frames = AMB_RATE * 4;
    track->pcm = SDL_malloc(track->frames * 4);
    if (!track->pcm) return FALSE;
    for (f = 0; f < track->frames; ++f)
    {
        double t = (double)f / AMB_RATE;
        seed = seed * 1664525u + 1013904223u;
        air += (((int)(seed >> 16) - 32768) / 32768.0f - air) * 0.012f;
        for (ch = 0; ch < 2; ++ch)
        {
            int s = (int)(32767 * (air * 0.07 +
                sin(t * 6.283185307179586 * (65 + context * 7) + ch * 0.3) * 0.008));
            Uint16 sample = (Uint16)(Sint16)s;
            track->pcm[f*4 + ch*2] = (Uint8)sample;
            track->pcm[f*4 + ch*2 + 1] = (Uint8)(sample >> 8);
        }
    }
    return TRUE;
}

static float _amb_sample(const ambient_track *track, int frame, int ch)
{
    const Uint8 *p = track->pcm + frame*4 + ch*2;
    int s = p[0] | (p[1] << 8);
    if (s >= 32768) s -= 65536;
    return s / 32768.0f;
}

static void _amb_mix(float *out, int frames)
{
    int f, i, ch;
    SDL_memset(out, 0, frames * 2 * sizeof(float));
    if (!_amb_enabled || !_amb_focused || !_amb_volume) return;
    for (f = 0; f < frames; ++f)
    {
        if (_amb_ramp)
        {
            _amb_gain += (_amb_target - _amb_gain) / _amb_ramp;
            if (!--_amb_ramp) _amb_gain = _amb_target;
        }
        for (i = 0; i < 2; ++i)
        {
            ambient_track *t = &_amb_tracks[i];
            int tail;
            if (!t->pcm) continue;
            if (t->ramp)
            {
                t->gain += (t->target - t->gain) / t->ramp;
                if (!--t->ramp) t->gain = t->target;
            }
            if (t->gain == 0 && !t->ramp) continue;
            tail = t->cursor - (t->frames - AMB_SEAM);
            for (ch = 0; ch < 2; ++ch)
            {
                float s = _amb_sample(t, t->cursor, ch);
                if (tail >= 0)
                {
                    float weight = tail / (float)AMB_SEAM;
                    s = s * (1 - weight) + _amb_sample(t, tail, ch) * weight;
                }
                out[f*2 + ch] += s * t->gain * _amb_gain * _amb_volume / 100.0f;
            }
            if (++t->cursor == t->frames) t->cursor = AMB_SEAM;
        }
        for (ch = 0; ch < 2; ++ch) out[f*2 + ch] = MAX(-1, MIN(1, out[f*2 + ch]));
    }
}

static void SDLCALL _amb_fill(void *unused, SDL_AudioStream *stream, int additional, int total)
{
    float out[AMB_BLOCK * 2];
    (void)unused; (void)total;
    while (additional > 0)
    {
        int frames = MIN(AMB_BLOCK, (additional + 7) / 8);
        _amb_mix(out, frames);
        if (!SDL_PutAudioStreamData(stream, out, frames * 8)) return;
        additional -= frames * 8;
    }
}

static bool _amb_init_at(const char *root)
{
    SDL_AudioSpec spec = {SDL_AUDIO_F32, 2, AMB_RATE};
    if (_amb_stream) return TRUE;
    SDL_strlcpy(_amb_root, root, sizeof(_amb_root));
    if (!SDL_WasInit(SDL_INIT_AUDIO))
    {
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) return FALSE;
        _amb_owns_audio = TRUE;
    }
    _amb_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, _amb_fill, NULL);
    if (!_amb_stream || !SDL_ResumeAudioStreamDevice(_amb_stream))
    { ui_ambient_shutdown(); return FALSE; }
    return TRUE;
}

void ui_ambient_init(void)
{
    char path[1200];
    const char *base = SDL_GetBasePath();
    SDL_snprintf(path, sizeof(path), "%slib/xtra/sound/pack", base ? base : "./");
    _amb_init_at(path);
}

void ui_ambient_update(int context, bool quiet)
{
    ambient_track next = {0};
    Uint8 *old_pcm;
    int i, first;
    if (!_amb_stream || !_amb_enabled || !_amb_volume || !_amb_focused) return;
    if (context < AMBIENT_NONE || context >= AMBIENT_COUNT) context = AMBIENT_NONE;
    if (quiet != _amb_quiet)
    {
        if (!SDL_LockAudioStream(_amb_stream)) return;
        _amb_quiet = quiet; _amb_target = quiet ? 0.25f : 1.0f;
        _amb_ramp = AMB_RATE / 3;
        SDL_UnlockAudioStream(_amb_stream);
    }
    if (context == _amb_context) return;
    if (context != AMBIENT_NONE)
    {
        if (_amb_authored)
        {
            first = _amb_next[context]++ % _amb_cues[context].variants;
            for (i = 0; i < _amb_cues[context].variants && !next.pcm; ++i)
                _amb_load(&next, context, 1 + (first+i) % _amb_cues[context].variants);
        }
        if (!next.pcm) _amb_synth(&next, context);
    }
    if (!SDL_LockAudioStream(_amb_stream)) { SDL_free(next.pcm); return; }
    _amb_context = context;
    /* Retain the louder old track during rapid changes. Storage stays bounded
     * to two published tracks plus the newly loaded track on the main thread. */
    i = _amb_tracks[0].gain <= _amb_tracks[1].gain ? 0 : 1;
    old_pcm = _amb_tracks[i].pcm;
    _amb_tracks[1-i].target = 0; _amb_tracks[1-i].ramp = AMB_FADE;
    next.target = next.pcm ? 1 : 0; next.ramp = AMB_FADE;
    _amb_tracks[i] = next; _amb_current = i;
    SDL_UnlockAudioStream(_amb_stream);
    SDL_free(old_pcm);
}

void ui_ambient_options(bool enabled, bool authored, int volume)
{
    int i;
    volume = MAX(0, MIN(100, volume));
    if (_amb_enabled == enabled && _amb_authored == authored && _amb_volume == volume) return;
    if (_amb_stream && !SDL_LockAudioStream(_amb_stream)) return;
    if (!enabled || !volume || authored != _amb_authored)
    {
        for (i = 0; i < 2; ++i)
        { SDL_free(_amb_tracks[i].pcm); SDL_memset(&_amb_tracks[i], 0, sizeof(ambient_track)); }
        _amb_context = AMBIENT_NONE;
        if (_amb_stream) SDL_ClearAudioStream(_amb_stream);
    }
    _amb_enabled = enabled; _amb_authored = authored; _amb_volume = volume;
    if (_amb_stream) SDL_UnlockAudioStream(_amb_stream);
}

void ui_ambient_focus(bool focused)
{
    if (_amb_focused == focused) return;
    if (_amb_stream && !SDL_LockAudioStream(_amb_stream)) return;
    _amb_focused = focused;
    _amb_gain = 0; _amb_ramp = AMB_FADE;
    if (!focused && _amb_stream) SDL_ClearAudioStream(_amb_stream);
    if (_amb_stream) SDL_UnlockAudioStream(_amb_stream);
}

void ui_ambient_shutdown(void)
{
    int i;
    if (_amb_stream) SDL_DestroyAudioStream(_amb_stream);
    _amb_stream = NULL;
    for (i = 0; i < 2; ++i) SDL_free(_amb_tracks[i].pcm);
    SDL_memset(_amb_tracks, 0, sizeof(_amb_tracks));
    SDL_memset(_amb_next, 0, sizeof(_amb_next));
    _amb_context = AMBIENT_NONE;
    if (_amb_owns_audio) SDL_QuitSubSystem(SDL_INIT_AUDIO);
    _amb_owns_audio = FALSE;
}
