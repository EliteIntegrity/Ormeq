/*
 * File: ui_sound.c
 *
 * See ui_sound.h. This file is the oscillator and the recipe book.
 *
 * Copyright (c) 2026 John Horton.
 *
 * Added since the fork and offered on the same terms as the rest of the
 * tree -- the Angband licence in LICENSE.
 */

/* SDL's headers must come before angband.h -- see the note in main-sdl3.c. */
#include <SDL3/SDL.h>

#include "scottish.h"
#include "ui_sound.h"

#include <math.h>

/* Keep the original synth recipes at their original sample rate. Both the
 * fallback and authored WAVs are converted to 48 kHz stereo for mixing. */
#define SND_RATE 22050
#define SND_MAX_FRAMES (SND_RATE * 600 / 1000)
#define MIX_RATE 48000
#define MIX_CHANNELS 2
#define MIX_FRAMES 1024
#define MIX_VOICES 12
#define CACHE_LIMIT 32
#define MAX_VARIANTS 6



enum
{
    W_SINE = 0,
    W_TRI,
    W_SQUARE,
    W_NOISE
};

/*
 * One sound.
 *
 * `f0` and `f1` are the pitch at the start and at the end, so a sweep costs
 * nothing to describe. `attack` is the percentage of the length spent getting
 * loud -- 0 for a percussive hit, higher for something that swells. `noise` is
 * how much of the result is hiss rather than tone, which is what separates a
 * bell from a splash. Everything is deliberately in plain units so a recipe can
 * be adjusted by reading it.
 */
typedef struct
{
    byte  wave;
    short f0, f1;    /* Hz */
    short ms;
    byte  attack;    /* per cent of the length */
    byte  noise;     /* per cent */
    byte  gain;      /* per cent */
} snd_voice;

#define V(w, f0, f1, ms, atk, noi, g) { W_##w, f0, f1, ms, atk, noi, g }

/*
 * The recipe book.
 *
 * Grouped by what a thing *is* rather than by the order the table happens to
 * be in, because that is how they were designed and how they will be adjusted:
 * every impact should sound like the same kind of world, and the way to keep
 * them that way is to write them next to each other.
 */
static const snd_voice _voice[SOUND_MAX] =
{
    /* 0 is not a sound. */
    V(SINE,     0,    0,    0,   0,   0,   0),

    /**** Blows landing ****/
    V(TRI,    190,   90,   90,   0,  45,  70),  /* HIT      */
    V(NOISE,  900,  380,   80,  10, 100,  32),  /* MISS     */
    V(NOISE,  700, 1500,  180,  20, 100,  30),  /* FLEE     */

    /**** Things being carried, dropped, opened ****/
    V(TRI,    520,  300,   70,   0,  30,  45),  /* DROP     */

    /**** A death ****/
    V(TRI,    240,  110,  200,   0,  35,  70),  /* KILL     */
    V(SINE,   520,  780,  420,  18,   0,  62),  /* LEVEL    */
    V(SINE,   150,   70,  650,   6,  20,  85),  /* DEATH    */
    V(SINE,   660,  990,  260,  20,   0,  50),  /* STUDY    */

    /**** Going somewhere you were not ****/
    V(SINE,   300, 1400,  240,  12,  18,  48),  /* TELEPORT */

    V(NOISE, 1400,  600,   90,   6, 100,  36),  /* SHOOT    */
    V(SINE,   140,  320,  180,  25,  15,  50),  /* QUAFF    */
    V(SQUARE, 900,  260,  140,   0,  20,  42),  /* ZAP      */
    /* A quiet footfall; independently switchable in Sound settings. */
    V(NOISE,  260,  150,   24,  15, 100,   3),  /* WALK     */
    V(SINE,  1200,  340,  240,  10,  22,  44),  /* TPOTHER  */
    V(TRI,    150,   70,   72,   0,  55,  12),  /* HITWALL  */
    V(SINE,   180,  260,  200,  25,  35,  48),  /* EAT      */

    /**** Shop doors ****/
    V(SINE,   440,  660,  120,  14,  10,  40),  /* STORE1   */
    V(SINE,   392,  588,  120,  14,  10,  40),  /* STORE2   */
    V(SINE,   349,  523,  120,  14,  10,  40),  /* STORE3   */
    V(SINE,   330,  494,  120,  14,  10,  40),  /* STORE4   */

    V(NOISE,  600,  300,  150,  15, 100,  40),  /* DIG      */
    V(SINE,   700,  520,   70,   0,  25,  36),  /* OPENDOOR */
    V(SINE,   520,  360,   80,   0,  30,  38),  /* SHUTDOOR */
    V(SINE,   220, 1500,  420,  10,  15,  55),  /* TPLEVEL  */
    V(NOISE, 1100,  700,  180,  25, 100,  28),  /* SCROLL   */
    V(SINE,   784, 1046,  150,  10,   0,  44),  /* BUY      */
    V(SINE,  1046,  784,  150,  10,   0,  44),  /* SELL     */
    V(SQUARE, 330,  330,  180,   8,   0,  40),  /* WARN     */

    /**** Things going off ****/
    V(NOISE,  260,   80,  340,   0, 100,  80),  /* ROCKET   */
    V(TRI,    200,   90,  240,   0,  40,  72),  /* N_KILL   */
    V(SINE,   130,   60,  620,   5,  25,  88),  /* U_KILL   */
    V(SINE,   587,  880,  520,  16,   0,  66),  /* QUEST    */

    /**** Mending ****/
    V(SINE,   440,  660,  300,  30,   0,  46),  /* HEAL     */
    V(SINE,   523,  784,  420,  30,   0,  56),  /* X_HEAL   */

    /**** Teeth and claws ****/
    V(NOISE,  380,  150,  110,   0, 100,  46),  /* BITE     */
    V(NOISE,  900,  400,   90,   0, 100,  40),  /* CLAW     */

    /**** Magic ****/
    V(SQUARE, 660,  220,  200,  10,  15,  40),  /* M_SPELL  */
    V(NOISE,  180,   90,  420,  25, 100,  52),  /* SUMMON   */
    V(NOISE,  300,  110,  460,  15, 100,  66),  /* BREATH   */
    V(NOISE,  200,   70,  320,   0, 100,  72),  /* BALL     */
    V(SINE,   392,  588,  300,  30,   0,  44),  /* M_HEAL   */
    V(SQUARE, 740,  260,  220,   8,  18,  44),  /* ATK_SPELL*/
    V(NOISE,  130,   70,  520,  30, 100,  56),  /* EVIL     */

    /**** Being touched by something unpleasant ****/
    V(NOISE,  700,  300,  130,  12, 100,  38),  /* TOUCH    */
    V(SINE,  1400,  800,  100,   0,  45,  40),  /* STING    */
    V(TRI,    110,   50,  240,   0,  50,  84),  /* CRUSH    */
    V(NOISE,  400,  160,  260,  20, 100,  44),  /* SLIME    */
    V(SINE,   520,  180,  520,  20,  30,  56),  /* WAIL     */

    V(SINE,   523, 1046,  700,  14,   0,  76),  /* WINNER   */

    /**** The elements ****/
    V(NOISE,  500,  180,  300,  10, 100,  60),  /* FIRE     */
    V(NOISE, 1600,  900,  280,  10, 100,  46),  /* ACID     */
    V(SQUARE,2200,  600,  160,   0,  70,  46),  /* ELEC     */
    V(SINE,  1800, 1100,  300,   6,  35,  44),  /* COLD     */

    /**** The interface talking back ****/
    V(SQUARE, 160,  160,   70,   0,   0,  30),  /* ILLEGAL  */
    V(SQUARE, 220,  150,  120,   0,  10,  34),  /* FAIL     */
    V(NOISE,  300,  200,  120,  15, 100,  34),  /* WAKEUP   */
    V(SINE,   440, 1320,  380,  22,   0,  50),  /* INVULN   */
    V(SINE,   300,   90,  260,   0,  30,  60),  /* FALL     */
    V(SINE,   340,  180,  160,   0,  35,  52),  /* PAIN     */
    V(NOISE, 1900,  700,  200,   0, 100,  50),  /* DESTITEM */
    V(SINE,   220,  150,  340,  25,  25,  46),  /* MOAN     */
    V(SINE,   880,  880,   45,   0,   0,  26),  /* SHOW     */
    V(SINE,     0,    0,    0,   0,   0,   0),  /* UNUSED   */
    V(NOISE,  180,   60,  420,   0, 100,  88),  /* EXPLODE  */
    V(SINE,  2600, 1500,  180,   0,  55,  48),  /* GLASS    */

    /**** Ranged contact, interactions and player status transitions ****/
    V(NOISE, 900, 380, 80, 10, 100, 32), /* THROW */
    V(TRI, 280, 110, 85, 0, 55, 65), /* SHOOT_HIT */
    V(TRI, 300, 520, 70, 0, 30, 40), /* PICKUP */
    V(TRI, 400, 600, 100, 10, 25, 40), /* WIELD */
    V(NOISE, 600, 900, 80, 10, 80, 35), /* QUIVER */
    V(TRI, 220, 440, 160, 10, 30, 45), /* STAIRS_UP */
    V(TRI, 440, 220, 160, 10, 30, 45), /* STAIRS_DOWN */
    V(TRI, 700, 400, 100, 0, 35, 40), /* DISARM */
    V(SINE, 440, 660, 250, 25, 0, 42), /* RECOVER */
    V(SINE, 550, 440, 120, 15, 0, 32), /* BUFF_END */
    V(NOISE, 600, 120, 200, 20, 70, 40), /* BLIND */
    V(TRI, 370, 620, 200, 20, 35, 42), /* CONFUSED */
    V(NOISE, 250, 160, 220, 20, 75, 42), /* POISONED */
    V(SINE, 480, 200, 220, 10, 30, 42), /* AFRAID */
    V(TRI, 300, 120, 200, 0, 25, 45), /* PARALYZED */
    V(SINE, 240, 700, 300, 30, 30, 40), /* DRUGGED */
    V(SINE, 330, 660, 180, 15, 10, 40), /* SPEED */
    V(SINE, 660, 220, 220, 15, 10, 40), /* SLOW */
    V(TRI, 220, 440, 240, 20, 15, 42), /* SHIELD */
    V(SINE, 523, 784, 260, 25, 0, 42), /* BLESSED */
    V(TRI, 392, 588, 200, 15, 10, 42), /* HERO */
    V(TRI, 180, 360, 240, 15, 25, 45), /* BERSERK */
    V(SINE, 330, 495, 260, 20, 15, 42), /* PROTECTION */
    V(SINE, 660, 990, 200, 20, 10, 38), /* SEE_INVIS */
    V(SINE, 220, 330, 220, 20, 10, 38), /* INFRARED */
    V(NOISE, 900, 300, 180, 15, 55, 38), /* RES_ACID */
    V(SQUARE, 1100, 550, 150, 15, 35, 35), /* RES_ELEC */
    V(NOISE, 400, 200, 200, 20, 65, 38), /* RES_FIRE */
    V(SINE, 1400, 900, 200, 20, 25, 38), /* RES_COLD */
    V(TRI, 250, 500, 200, 20, 25, 38), /* RES_POIS */
    V(TRI, 150, 80, 120, 0, 45, 55), /* STUN */
    V(NOISE, 1100, 500, 100, 0, 80, 45), /* CUT */
    V(TRI, 440, 550, 35, 10, 20, 7), /* UI_NAVIGATE */
    V(TRI, 460, 535, 35, 10, 20, 7), /* UI_ACCEPT */
    V(TRI, 480, 520, 35, 10, 20, 7), /* UI_BACK */
    V(TRI, 500, 505, 35, 10, 20, 7), /* UI_OPEN */
    V(TRI, 520, 490, 35, 10, 20, 7), /* UI_CLOSE */
    V(TRI, 540, 475, 35, 10, 20, 7), /* UI_CHANGE */
    V(TRI, 560, 460, 35, 10, 20, 7), /* UI_TAB */
    V(TRI, 580, 445, 100, 10, 20, 25), /* HOME_ENTER */
    V(TRI, 600, 430, 100, 10, 20, 25), /* STORE_LEAVE */
    V(TRI, 620, 415, 100, 10, 20, 25), /* CHEST_OPEN */
    V(TRI, 640, 400, 100, 10, 20, 25), /* UNLOCK */
};


typedef struct { const char *name, *path; int variants, gain, interval; } snd_cue;
static const snd_cue _cue[SOUND_MAX] = {
#define SFX(id, path, count, gain, interval) [id] = {#id + 6, path, count, gain, interval},
#include "ui_sound_map.h"
#undef SFX
};
typedef struct { float *pcm; int frames; Uint64 used; bool failed; } snd_clip;
typedef struct { snd_clip *clip; int cursor, event; Uint64 started; float gain; } snd_playing;
static snd_clip _clips[SOUND_MAX][MAX_VARIANTS + 1]; /* 0 = original synth */
static snd_playing _playing[MIX_VOICES];
static unsigned int _next_variant[SOUND_MAX];
static Uint64 _last_play[SOUND_MAX], _serial;
static bool _has_played[SOUND_MAX];
static int _cached;
static SDL_AudioStream *_stream;
static bool _owns_audio;
static bool _enabled = TRUE, _authored = TRUE;
static int _volume = 70;
static bool _footsteps = TRUE, _menus = TRUE, _wall_latched;
static char _pack_root[1024];
static u32b _seed = 0x1234567u; /* presentation noise, never game RNG */

/* Cheap white noise. Nothing here needs a better generator than this. */
static float _noise(void)
{
    _seed = _seed * 1103515245u + 12345u;

    return (float)((int)((_seed >> 9) & 0xFFFF) - 32768) / 32768.0f;
}

static float _osc(int wave, float phase)
{
    switch (wave)
    {
    case W_SINE:   return (float)sin(phase * 2.0 * 3.14159265358979);
    case W_TRI:    return 4.0f * (float)fabs(phase - 0.5) - 1.0f;
    default:       return (phase < 0.5f) ? 0.6f : -0.6f;   /* W_SQUARE */
    }
}


static bool _synth(snd_clip *clip, int event)
{
    const snd_voice *v = &_voice[event];
    float buf[SND_MAX_FRAMES], phase = 0.0f, hold = 0.0f, hold_phase = 1.0f;
    int frames = MIN(v->ms * SND_RATE / 1000, SND_MAX_FRAMES), i, bytes = 0;
    Uint8 *converted = NULL;
    SDL_AudioSpec src = {SDL_AUDIO_F32, 1, SND_RATE};
    SDL_AudioSpec dst = {SDL_AUDIO_F32, MIX_CHANNELS, MIX_RATE};
    if (!frames || !v->gain) return FALSE;

    for (i = 0; i < frames; i++)
    {
        float t   = (float)i / (float)frames;   /* 0 -> 1 over the sound */
        float hz  = (float)v->f0 + ((float)v->f1 - (float)v->f0) * t;
        float env;
        float s;

        /*
         * Attack then decay. The decay is exponential rather than linear
         * because a linear fade sounds like somebody turning a knob, where
         * everything that actually makes a noise dies away fastest at the
         * start.
         */
        if (v->attack && t < (float)v->attack / 100.0f)
            env = t / ((float)v->attack / 100.0f);
        else
        {
            float d = (v->attack)
                    ? (t - (float)v->attack / 100.0f)
                      / (1.0f - (float)v->attack / 100.0f)
                    : t;

            env = (float)exp(-4.0 * (double)d);
        }

        phase += hz / (float)SND_RATE;
        if (phase >= 1.0f) phase -= 1.0f;

        /*
         * Noise is sampled and held at the voice's own frequency, not taken
         * fresh every sample.
         *
         * White noise has no pitch, so the first version ignored f0 and f1 for
         * every noise voice entirely -- an arrow loosed, a pick striking rock,
         * an explosion and a splash of acid were all the same burst of hiss
         * with different lengths on it. Holding each random value for a whole
         * cycle gives noise a pitch you can hear, so the sweep finally does
         * something and those four stop being the same sound.
         */
        hold_phase += hz / (float)SND_RATE;
        if (hold_phase >= 1.0f)
        {
            hold_phase -= 1.0f;
            hold = _noise();
        }

        s = (v->wave == W_NOISE) ? hold : _osc(v->wave, phase);

        /* Mixed in rather than switched to, so a blow landing can be mostly a
         * thump with grit on it rather than either a tone or a hiss. */
        if (v->noise && v->wave != W_NOISE)
        {
            float n = (float)v->noise / 100.0f;

            s = s * (1.0f - n) + hold * n;
        }

        buf[i] = s * env * ((float)v->gain / 100.0f) * 0.5f;
    }

    if (!SDL_ConvertAudioSamples(&src, (Uint8 *)buf, frames * sizeof(float),
                                &dst, &converted, &bytes)) return FALSE;
    clip->pcm = (float *)converted;
    clip->frames = bytes / (sizeof(float) * MIX_CHANNELS);
    return clip->frames > 0;
}

/* Called on the main thread, outside the audio lock. Only immutable sample
 * data are published to the callback, when a voice is assigned under lock. */
static bool _wav(snd_clip *clip, int event, int variant)
{
    char path[1200];
    SDL_PathInfo info;
    SDL_AudioSpec src, dst = {SDL_AUDIO_F32, MIX_CHANNELS, MIX_RATE};
    Uint8 *raw = NULL, *converted = NULL;
    Uint32 length = 0;
    int bytes = 0;
    SDL_snprintf(path, sizeof(path), "%s/%s_%02d.wav", _pack_root, _cue[event].path, variant);
    /* Effects only: bounded files/duration, not long ambience or music. */
    if (!SDL_GetPathInfo(path, &info) || info.type != SDL_PATHTYPE_FILE ||
        info.size > 4 * 1024 * 1024 || !SDL_LoadWAV(path, &src, &raw, &length)) return FALSE;
    if (src.freq != MIX_RATE || src.format != SDL_AUDIO_S16LE ||
        (src.channels != 1 && src.channels != 2) || !length ||
        length % (src.channels * 2) || length > (Uint32)(MIX_RATE * src.channels * 2 * 3))
    {
        SDL_free(raw);
        return FALSE;
    }
    if (!SDL_ConvertAudioSamples(&src, raw, (int)length, &dst, &converted, &bytes))
    {
        SDL_free(raw);
        return FALSE;
    }
    SDL_free(raw);
    if (bytes <= 0 || bytes % (sizeof(float) * MIX_CHANNELS))
    {
        SDL_free(converted);
        return FALSE;
    }
    clip->pcm = (float *)converted;
    clip->frames = bytes / (sizeof(float) * MIX_CHANNELS);
    return TRUE;
}

static bool _room_in_cache(void)
{
    snd_clip *oldest = NULL;
    float *pcm;
    int e, v, i;
    if (_cached < CACHE_LIMIT) return TRUE;
    if (!SDL_LockAudioStream(_stream)) return FALSE;
    for (e = 1; e < SOUND_MAX; ++e)
        for (v = 0; v <= MAX_VARIANTS; ++v)
        {
            snd_clip *clip = &_clips[e][v];
            bool active = FALSE;
            if (!clip->pcm) continue;
            for (i = 0; i < MIX_VOICES; ++i)
                if (_playing[i].clip == clip) active = TRUE;
            if (!active && (!oldest || clip->used < oldest->used)) oldest = clip;
        }
    if (!oldest) { SDL_UnlockAudioStream(_stream); return FALSE; }
    pcm = oldest->pcm;
    oldest->pcm = NULL;
    oldest->frames = 0;
    --_cached;
    SDL_UnlockAudioStream(_stream);
    SDL_free(pcm);
    return TRUE;
}

static snd_clip *_prepare(int event, int variant)
{
    snd_clip *clip = &_clips[event][variant];
    if (clip->failed) return NULL;
    if (!clip->pcm)
    {
        if (!_room_in_cache()) return NULL;
        if (!(variant ? _wav(clip, event, variant) : _synth(clip, event)))
        {
            clip->failed = TRUE; /* Don't retry bad/missing files every blow. */
            return NULL;
        }
        ++_cached;
    }
    clip->used = ++_serial;
    return clip;
}

/* No I/O, allocation, randomness, or game-state access in the callback. */
static void _mix(float *out, int frames)
{
    int i, f, ch;
    SDL_memset(out, 0, frames * MIX_CHANNELS * sizeof(float));
    for (i = 0; i < MIX_VOICES; ++i)
    {
        snd_playing *v = &_playing[i];
        if (!v->clip) continue;
        for (f = 0; f < frames; ++f)
        {
            if (v->cursor >= v->clip->frames) { v->clip = NULL; break; }
            for (ch = 0; ch < MIX_CHANNELS; ++ch)
                out[f * MIX_CHANNELS + ch] += v->clip->pcm[v->cursor * MIX_CHANNELS + ch]
                    * v->gain * _volume / 100.0f;
            ++v->cursor;
        }
    }
    for (i = 0; i < frames * MIX_CHANNELS; ++i)
        out[i] = MAX(-1.0f, MIN(1.0f, out[i]));
}

static void SDLCALL _fill(void *unused, SDL_AudioStream *stream, int additional, int total)
{
    float buf[MIX_FRAMES * MIX_CHANNELS];
    const int frame_bytes = MIX_CHANNELS * sizeof(float);
    (void)unused;
    (void)total;
    while (additional > 0)
    {
        int frames = MIN(MIX_FRAMES, (additional + frame_bytes - 1) / frame_bytes);
        _mix(buf, frames);
        if (!SDL_PutAudioStreamData(stream, buf, frames * frame_bytes)) return;
        additional -= frames * frame_bytes;
    }
}

void ui_sound_options(bool enabled, bool authored, int volume)
{
    volume = MAX(0, MIN(100, volume));
    if (_enabled == enabled && _authored == authored && _volume == volume) return;
    if (_stream && !SDL_LockAudioStream(_stream)) return;
    if (!enabled || !volume || _authored != authored)
    {
        SDL_memset(_playing, 0, sizeof(_playing));
        SDL_memset(_has_played, 0, sizeof(_has_played));
        if (_stream) SDL_ClearAudioStream(_stream);
    }
    _enabled = enabled;
    _authored = authored;
    _volume = volume;
    if (_stream) SDL_UnlockAudioStream(_stream);
}

void ui_sound_details(bool footsteps, bool menus)
{
    int i;
    if (_footsteps == footsteps && _menus == menus) return;
    if (_stream && !SDL_LockAudioStream(_stream)) return;
    _footsteps = footsteps; _menus = menus;
    for (i = 0; i < MIX_VOICES; ++i)
        if ((!footsteps && _playing[i].event == SOUND_WALK) ||
            (!menus && _playing[i].event >= SOUND_UI_NAVIGATE && _playing[i].event <= SOUND_UI_TAB))
            _playing[i].clip = NULL;
    if (_stream) SDL_UnlockAudioStream(_stream);
}

void ui_sound_input(bool repeat)
{
    if (repeat) return;
    _wall_latched = FALSE;
    _has_played[SOUND_HITWALL] = FALSE;
}

static int _priority(int event)
{
    if (event == SOUND_DEATH || event == SOUND_WARN) return 2;
    if (event == SOUND_LEVEL || event == SOUND_U_KILL || event == SOUND_WINNER || event == SOUND_QUEST) return 1;
    return 0;
}

void ui_sound_play(int event)
{
    snd_clip *clip = NULL;
    snd_playing *voice = NULL;
    Uint64 now;
    int i;
    if (!_stream || !_enabled || !_volume || event <= 0 || event >= SOUND_MAX || !_cue[event].path) return;
    if (event == SOUND_WALK && !_footsteps) return;
    if (event >= SOUND_UI_NAVIGATE && event <= SOUND_UI_TAB && !_menus) return;
    if (event == SOUND_HITWALL)
    {
        if (_wall_latched) return;
        _wall_latched = TRUE;
    }
    now = SDL_GetTicks();
    if (_has_played[event] && now - _last_play[event] < (Uint64)_cue[event].interval) return;
    if (_authored)
    {
        unsigned int first = _next_variant[event]++ % _cue[event].variants;
        /* A partial pack still uses any healthy variants of this cue. */
        for (i = 0; i < _cue[event].variants && !clip; ++i)
            clip = _prepare(event, 1 + (first + i) % _cue[event].variants);
    }
    if (!clip) clip = _prepare(event, 0);
    if (!clip || !SDL_LockAudioStream(_stream)) return;
    for (i = 0; i < MIX_VOICES; ++i)
    {
        snd_playing *v = &_playing[i];
        if (!v->clip) { voice = v; break; }
        if (_priority(v->event) <= _priority(event) && (!voice || v->started < voice->started)) voice = v;
    }
    if (voice)
    {
        voice->clip = clip;
        voice->cursor = 0;
        voice->event = event;
        voice->started = ++_serial;
        voice->gain = _authored && clip != &_clips[event][0] ? _cue[event].gain / 100.0f : 1.0f;
        _last_play[event] = now;
        _has_played[event] = TRUE;
    }
    SDL_UnlockAudioStream(_stream);
}

static bool _init_at(const char *directory)
{
    SDL_AudioSpec spec = {SDL_AUDIO_F32, MIX_CHANNELS, MIX_RATE};
    if (_stream) return TRUE;
    SDL_strlcpy(_pack_root, directory, sizeof(_pack_root));
    if (!SDL_WasInit(SDL_INIT_AUDIO))
    {
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) return FALSE;
        _owns_audio = TRUE;
    }
    _stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, _fill, NULL);
    if (!_stream || !SDL_ResumeAudioStreamDevice(_stream))
    {
        SDL_Log("Ormeq: audio unavailable: %s", SDL_GetError());
        ui_sound_shutdown();
        return FALSE;
    }
    return TRUE;
}

bool ui_sound_init(void)
{
    char path[1024];
    const char *base = SDL_GetBasePath();
    SDL_snprintf(path, sizeof(path), "%slib/xtra/sound/pack", base ? base : "./");
    return _init_at(path);
}

void ui_sound_shutdown(void)
{
    int e, v;
    /* Destruction waits for the callback before its sample storage is freed. */
    if (_stream) SDL_DestroyAudioStream(_stream);
    _stream = NULL;
    SDL_memset(_playing, 0, sizeof(_playing));
    for (e = 0; e < SOUND_MAX; ++e)
        for (v = 0; v <= MAX_VARIANTS; ++v) SDL_free(_clips[e][v].pcm);
    SDL_memset(_clips, 0, sizeof(_clips));
    SDL_memset(_next_variant, 0, sizeof(_next_variant));
    SDL_memset(_has_played, 0, sizeof(_has_played));
    _cached = 0;
    _serial = 0;
    _wall_latched = FALSE;
    if (_owns_audio) SDL_QuitSubSystem(SDL_INIT_AUDIO);
    _owns_audio = FALSE;
}

void ui_sound_audition(void)
{
    int e;
    if (!ui_sound_init()) { printf("No audio device.\n"); return; }
    for (e = 1; e < SOUND_MAX; ++e)
    {
        snd_clip *clip = NULL;
        int i;
        if (!_cue[e].path) continue;
        if (_authored)
            for (i = 1; i <= _cue[e].variants && !clip; ++i) clip = _prepare(e, i);
        printf("%2d %-10s %s\n", e, _cue[e].name, clip ? _cue[e].path : "synth fallback");
        fflush(stdout);
        ui_sound_play(e);
        SDL_Delay((clip ? clip->frames * 1000 / MIX_RATE : _voice[e].ms) + 250);
    }
    ui_sound_shutdown();
}
