/* SPDX-License-Identifier: ISC
 * Lightweight procedural Switch 2 tactile UI SFX engine (non-blocking audout).
 */
#include <math.h>
#include <malloc.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifdef __SWITCH__
#include <switch.h>
#endif

#include "switch2_sfx.h"

#define SFX_SAMPLE_RATE 48000
#define SFX_CHANNELS    2
#define SFX_SLOTS       6
#define SFX_MAX_FRAMES  720000 /* 15s cap (theme pack wavs) */


#ifdef __SWITCH__
static bool s_sfx_ready = false;
static bool s_sfx_enabled = true;


static int16_t *s_samples[S2Sfx_Count];
static uint32_t s_frame_counts[S2Sfx_Count];

static void synthMicroTick(int16_t *out, uint32_t frames, float f0, float f1,
                           float decay_ms, float amp);
static void synthSoftDoubleTick(int16_t *out, uint32_t frames, float f0, float f1,
                                uint32_t gap, float amp);

/* (Re)generate the built-in tactile synth sounds, freeing any loaded
 * theme-pack samples. Used at init and when the pack is toggled off. */
static void synthDefaults(void) {
    for (int i = 0; i < S2Sfx_Count; i++) {
        free(s_samples[i]);
        s_samples[i] = NULL;
        s_frame_counts[i] = 0;
    }
    s_frame_counts[S2Sfx_Move]    = 768;  /* 16ms soft tactile tick */
    s_frame_counts[S2Sfx_Confirm] = 1536; /* 32ms gentle double pop */
    s_frame_counts[S2Sfx_Back]    = 960;  /* 20ms soft hollow tock */
    s_frame_counts[S2Sfx_Star]    = 1728; /* 36ms delicate sparkle tap */
    s_frame_counts[S2Sfx_Notification] = 1536;
    s_frame_counts[S2Sfx_Launch] = 1728;

    for (int i = 0; i < S2Sfx_Count; i++) {
        s_samples[i] = malloc(s_frame_counts[i] * SFX_CHANNELS * sizeof(int16_t));
    }
    /* S2Sfx_Pill is intentionally unassigned: no synth fallback, silent
     * unless a theme pack provides a cue for it. */
    free(s_samples[S2Sfx_Pill]);
    s_samples[S2Sfx_Pill] = NULL;
    s_frame_counts[S2Sfx_Pill] = 0;

    if (s_samples[S2Sfx_Move])
        synthMicroTick(s_samples[S2Sfx_Move], s_frame_counts[S2Sfx_Move], 420.0f, 640.0f, 4.2f, 0.085f);
    if (s_samples[S2Sfx_Confirm])
        synthSoftDoubleTick(s_samples[S2Sfx_Confirm], s_frame_counts[S2Sfx_Confirm], 540.0f, 760.0f, 680, 0.095f);
    if (s_samples[S2Sfx_Back])
        synthMicroTick(s_samples[S2Sfx_Back], s_frame_counts[S2Sfx_Back], 460.0f, 290.0f, 5.5f, 0.085f);
    if (s_samples[S2Sfx_Star])
        synthSoftDoubleTick(s_samples[S2Sfx_Star], s_frame_counts[S2Sfx_Star], 740.0f, 1080.0f, 760, 0.090f);
    if (s_samples[S2Sfx_Notification])
        synthSoftDoubleTick(s_samples[S2Sfx_Notification], s_frame_counts[S2Sfx_Notification],
                            620.0f, 930.0f, 820, 0.10f);
    if (s_samples[S2Sfx_Launch])
        synthSoftDoubleTick(s_samples[S2Sfx_Launch], s_frame_counts[S2Sfx_Launch],
                            520.0f, 1240.0f, 900, 0.10f);
}


static AudioOutBuffer s_slots[SFX_SLOTS];
static bool s_slot_busy[SFX_SLOTS];
static S2SfxId s_slot_ids[SFX_SLOTS]; /* event occupying each slot */
/* Playback buffers are allocated per play (theme sounds can be seconds long)
 * and freed when audout releases the slot. */

/* Ultra-short, soft Switch 2-style percussive micro-tick (low volume, warm woody/bubble feel) */
static void synthMicroTick(int16_t *out, uint32_t frames, float f0, float f1,
                           float decay_ms, float amp) {
    const float sr = (float)SFX_SAMPLE_RATE;
    const float att = 0.0008f * sr; /* 0.8ms soft transient */
    const float dec = decay_ms * 0.001f * sr;
    float phase = 0.0f;
    float phase_sub = 0.0f;

    for (uint32_t i = 0; i < frames; i++) {
        float t = (float)i / (float)(frames > 1 ? frames - 1 : 1);
        /* Fast pitch drop/rise curve for tactile "pop/tock" instead of electronic beep */
        float ease = 1.0f - (1.0f - t) * (1.0f - t);
        float freq = f0 + (f1 - f0) * ease;
        phase += 6.2831853f * freq / sr;
        phase_sub += 6.2831853f * (freq * 0.5f) / sr;

        float env = ((float)i < att) ? ((float)i / att) : expf(-((float)i - att) / (dec > 1.0f ? dec : 1.0f));
        if (i + 48 > frames)
            env *= (float)(frames - i) / 48.0f;

        float s = (0.78f * sinf(phase) + 0.22f * sinf(phase_sub)) * env * amp;
        if (s > 0.9f) s = 0.9f;
        if (s < -0.9f) s = -0.9f;
        int16_t v = (int16_t)(s * 32767.0f);
        out[i * 2 + 0] = v;
        out[i * 2 + 1] = v;
    }
}

static void synthSoftDoubleTick(int16_t *out, uint32_t frames, float f_a, float f_b,
                                uint32_t split_frame, float amp) {
    if (split_frame > frames) split_frame = frames / 2;
    synthMicroTick(out, split_frame, f_a, f_a * 1.06f, 5.5f, amp * 0.8f);
    synthMicroTick(out + split_frame * 2, frames - split_frame, f_b, f_b * 1.04f, 8.5f, amp);
}

/* --- Software mixer -------------------------------------------------------- */
/* audout pre-ingests several appended buffers into the DSP lookahead, so
 * queueing whole cue files builds a backlog that keeps ticking after the
 * D-pad is released. Instead, a small mixer thread streams 10ms chunks:
 * voices are cursors into s_samples, so releasing input stops the sound
 * within one chunk, and retriggering resets the cursor in place. */

#define MIX_FRAMES 960 /* 20ms at 48kHz stereo: mixer headroom */
#define MIX_REGION 0x1000 /* audout wants 0x1000-aligned buffer sizes */

typedef struct {
    S2SfxId id;
    uint32_t pos; /* frame index into s_samples[id] */
    float gain;   /* 1.0 normal, lower when tails overlap */
    bool active;
} SfxVoice;

static SfxVoice s_voices[SFX_SLOTS];
static Mutex s_voice_lock;

/* --- Software mixer buffers / thread --------------------------------------- */
static Thread s_mix_thread;
static volatile bool s_mix_running = false;
static AudioOutBuffer s_mix_bufs[2];
static int16_t *s_mix_mem[2];

/* --- Looping BGM: preloaded into RAM (no SD I/O in the mixer thread) -----
 * A ~6min track at 24kHz mono is ~18.5MB -- fine for hbmenu's heap, and it
 * removes every seek/read stall that stuttered the real-time mixer. */
static uint64_t s_bgm_frames;      /* total frames in the file (per-channel) */
static uint32_t s_bgm_rate = SFX_SAMPLE_RATE; /* file sample rate (may differ) */
static uint32_t s_bgm_ch = 2;      /* file channel count (mono allowed) */
static int16_t *s_bgm_samples;     /* s_bgm_frames * s_bgm_ch PCM16 */
static uint64_t s_bgm_phase;       /* 16.16 fixed-point source frame position */
static float s_bgm_volume = 0.5f;
static bool s_bgm_enabled = false;

static bool bgmHeaderParse(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f)
        return false;
    unsigned char hdr[12];
    bool ok = false;
    uint32_t rate = 0;
    unsigned ch = 0;
    uint64_t data_offset = 0;
    if (fread(hdr, 1, 12, f) == 12 && memcmp(hdr, "RIFF", 4) == 0 &&
        memcmp(hdr + 8, "WAVE", 4) == 0)
        ok = true;
    while (ok) {
        unsigned char chunk[8];
        if (fread(chunk, 1, 8, f) != 8)
            break;
        uint32_t size = chunk[4] | (chunk[5] << 8) | (chunk[6] << 16) | ((uint32_t)chunk[7] << 24);
        if (memcmp(chunk, "fmt ", 4) == 0) {
            unsigned char fmt[16];
            const uint32_t want = size < 16 ? size : 16;
            if (fread(fmt, 1, want, f) != want) { ok = false; break; }
            if (size > want) fseek(f, (long)(size - want), SEEK_CUR);
            const unsigned format = fmt[0] | (fmt[1] << 8);
            ch = fmt[2] | (fmt[3] << 8);
            rate = fmt[4] | (fmt[5] << 8) | (fmt[6] << 16) | ((uint32_t)fmt[7] << 24);
            if (format != 1 || (ch != 1 && ch != 2) || rate < 22050 ||
                rate > SFX_SAMPLE_RATE) { ok = false; break; }
            s_bgm_rate = rate;
            s_bgm_ch = ch;
        } else if (memcmp(chunk, "data", 4) == 0) {
            data_offset = (uint64_t)ftell(f);
            s_bgm_frames = size / (2 * ch);
            break;
        } else {
            fseek(f, (long)size, SEEK_CUR);
        }
        if (size & 1)
            fseek(f, 1, SEEK_CUR);
    }
    if (ok && s_bgm_frames > 0 && data_offset) {
        if (fseek(f, (long)data_offset, SEEK_SET) == 0) {
            size_t total;
            const uint64_t bytes = s_bgm_frames * s_bgm_ch * 2;
            int16_t *buf = malloc(bytes);
            if (buf) {
                total = fread(buf, 1, (size_t)bytes, f);
                if (total == (size_t)bytes) {
                    free(s_bgm_samples);
                    s_bgm_samples = buf;
                    ok = true;
                } else {
                    free(buf);
                    ok = false;
                }
            } else {
                ok = false; /* out of memory: BGM simply stays off */
            }
        } else {
            ok = false;
        }
    } else {
        ok = false;
    }
    fclose(f);
    return ok;
}

static void sfxMixChunk(int16_t *out) {
    memset(out, 0, MIX_FRAMES * SFX_CHANNELS * sizeof(int16_t));
    mutexLock(&s_voice_lock);

    /* Looping BGM first, underneath every SFX voice. Fixed-point phase
     * accumulation handles any file rate; interpolation wraps across the
     * seam for a clean loop. */
    if (s_bgm_enabled && s_bgm_samples && s_bgm_frames) {
        const uint32_t step = (uint32_t)((uint64_t)s_bgm_rate << 16) / SFX_SAMPLE_RATE;
        const uint64_t phase_wrap = s_bgm_frames << 16;
        for (uint32_t f = 0; f < MIX_FRAMES; f++) {
            const uint32_t i0 = (uint32_t)(s_bgm_phase >> 16);
            const uint32_t i1 = i0 + 1 >= s_bgm_frames ? 0 : i0 + 1; /* seam wrap */
            const float frac = (float)(s_bgm_phase & 0xffff) / 65536.0f;
            const int16_t *a = &s_bgm_samples[(size_t)i0 * s_bgm_ch];
            const int16_t *b = &s_bgm_samples[(size_t)i1 * s_bgm_ch];
            int32_t l = (int32_t)(a[0] + (b[0] - a[0]) * frac);
            int32_t r = s_bgm_ch == 2
                ? (int32_t)(a[1] + (b[1] - a[1]) * frac)
                : l; /* mono: duplicate to both channels */
            out[f * 2 + 0] += (int16_t)(l * s_bgm_volume);
            out[f * 2 + 1] += (int16_t)(r * s_bgm_volume);
            s_bgm_phase += step;
            if (s_bgm_phase >= phase_wrap)
                s_bgm_phase -= phase_wrap;
        }
    }
    for (int v = 0; v < SFX_SLOTS; v++) {
        if (!s_voices[v].active)
            continue;
        const S2SfxId id = s_voices[v].id;
        const int16_t *src = s_samples[id];
        const uint32_t frames = s_frame_counts[id];
        if (!src || !frames) {
            s_voices[v].active = false;
            continue;
        }
        const uint32_t p = s_voices[v].pos;
        for (uint32_t f = 0; f < MIX_FRAMES && p + f < frames; f++) {
            /* 3ms (144-frame) fade-in guards against click transients */
            float g = s_voices[v].gain;
            if (p + f < 144)
                g *= (float)(p + f + 1) / 144.0f;
            int32_t l = out[f * 2 + 0] + (int32_t)(src[(p + f) * 2 + 0] * g);
            int32_t r = out[f * 2 + 1] + (int32_t)(src[(p + f) * 2 + 1] * g);
            if (l > 32767) l = 32767; else if (l < -32768) l = -32768;
            if (r > 32767) r = 32767; else if (r < -32768) r = -32768;
            out[f * 2 + 0] = (int16_t)l;
            out[f * 2 + 1] = (int16_t)r;
        }
        s_voices[v].pos += MIX_FRAMES;
        if (s_voices[v].pos >= frames)
            s_voices[v].active = false;
    }
    mutexUnlock(&s_voice_lock);
}

static void sfxMixThread(void *arg) {
    (void)arg;
    for (int i = 0; i < 2; i++) {
        sfxMixChunk(s_mix_mem[i]);
        s_mix_bufs[i].next = NULL;
        s_mix_bufs[i].buffer = s_mix_mem[i];
        s_mix_bufs[i].buffer_size = MIX_REGION;
        s_mix_bufs[i].data_size = MIX_FRAMES * SFX_CHANNELS * sizeof(int16_t);
        s_mix_bufs[i].data_offset = 0;
        audoutAppendAudioOutBuffer(&s_mix_bufs[i]);
    }
    while (s_mix_running) {
        AudioOutBuffer *released = NULL;
        u32 released_count = 0;
        if (R_FAILED(audoutWaitPlayFinish(&released, &released_count, UINT64_MAX)))
            break;
        if (!s_mix_running)
            break;
        /* Refill the buffer audout actually released -- assuming a fixed
         * alternation desyncs the queue and stutters the stream. count can
         * be 2 when both buffers finished while we were blocked: refill
         * both, released one first to preserve FIFO order. */
        int first = -1;
        for (int i = 0; i < 2; i++) {
            if (released == &s_mix_bufs[i]) {
                first = i;
                break;
            }
        }
        if (first < 0)
            continue;
        sfxMixChunk(s_mix_mem[first]);
        s_mix_bufs[first].data_size = MIX_FRAMES * SFX_CHANNELS * sizeof(int16_t);
        audoutAppendAudioOutBuffer(&s_mix_bufs[first]);
        if (released_count == 2) {
            const int second = first ^ 1;
            sfxMixChunk(s_mix_mem[second]);
            s_mix_bufs[second].data_size = MIX_FRAMES * SFX_CHANNELS * sizeof(int16_t);
            audoutAppendAudioOutBuffer(&s_mix_bufs[second]);
        }
    }
}

void switch2SfxInit(void) {
    if (s_sfx_ready)
        return;

    if (R_FAILED(audoutInitialize()))
        return;
    if (R_FAILED(audoutStartAudioOut())) {
        audoutExit();
        return;
    }

    mutexInit(&s_voice_lock);
    for (int i = 0; i < SFX_SLOTS; i++) {
        s_voices[i].id = S2Sfx_Count;
        s_voices[i].pos = 0;
        s_voices[i].gain = 1.0f;
        s_voices[i].active = false;
    }
    for (int i = 0; i < 2; i++) {
        s_mix_mem[i] = memalign(0x1000, MIX_REGION);
        if (!s_mix_mem[i]) {
            audoutExit();
            return;
        }
        memset(s_mix_mem[i], 0, MIX_REGION);
    }

    synthDefaults();
    s_sfx_ready = true;
    s_mix_running = true;
    if (R_FAILED(threadCreate(&s_mix_thread, sfxMixThread, NULL, NULL, 0x4000, 0x2C, -2))) {
        s_mix_running = false;
        s_sfx_ready = false;
        audoutExit();
        return;
    }
    if (R_FAILED(threadStart(&s_mix_thread))) {
        s_mix_running = false;
        s_sfx_ready = false;
        threadClose(&s_mix_thread);
        audoutExit();
        return;
    }
}

void switch2SfxExit(void) {
    if (!s_sfx_ready)
        return;

    s_mix_running = false;
    audoutFlushAudioOutBuffers(NULL); /* unblock the mixer's wait */
    threadWaitForExit(&s_mix_thread);
    threadClose(&s_mix_thread);
    s_bgm_enabled = false;
    free(s_bgm_samples);
    s_bgm_samples = NULL;
    for (int i = 0; i < 2; i++) {
        free(s_mix_mem[i]);
        s_mix_mem[i] = NULL;
    }
    for (int i = 0; i < S2Sfx_Count; i++) {
        free(s_samples[i]);
        s_samples[i] = NULL;
    }
    audoutExit();
    s_sfx_ready = false;
}

void switch2SfxSetEnabled(bool enabled) {
    s_sfx_enabled = enabled;
}

void switch2SfxResetSynth(void) {
    if (!s_sfx_ready)
        return;
    mutexLock(&s_voice_lock);
    for (int i = 0; i < SFX_SLOTS; i++)
        s_voices[i].active = false;
    synthDefaults();
    mutexUnlock(&s_voice_lock);
}

void switch2SfxSetSample(S2SfxId id, int16_t *samples, uint32_t frames) {
    if (!s_sfx_ready || (int)id < 0 || id >= S2Sfx_Count || !samples ||
        frames == 0 || frames > SFX_MAX_FRAMES)
        return;
    mutexLock(&s_voice_lock);
    for (int i = 0; i < SFX_SLOTS; i++) {
        if (s_voices[i].active && s_voices[i].id == id)
            s_voices[i].active = false;
    }
    free(s_samples[id]);
    s_samples[id] = samples;
    s_frame_counts[id] = frames;
    mutexUnlock(&s_voice_lock);
}

void switch2SfxSetBgm(const char *wav_path, float volume) {
    if (!s_sfx_ready)
        return;
    mutexLock(&s_voice_lock);
    s_bgm_enabled = false;
    s_bgm_frames = 0;
    if (wav_path && bgmHeaderParse(wav_path)) {
        /* bgmHeaderParse preloaded the whole payload into s_bgm_samples. */
        s_bgm_phase = 0;
        s_bgm_volume = (volume > 0.0f && volume <= 1.0f) ? volume : 0.5f;
        s_bgm_enabled = true;
    } else {
        free(s_bgm_samples);
        s_bgm_samples = NULL;
    }
    mutexUnlock(&s_voice_lock);
}

void switch2SfxSetBgmEnabled(bool enabled) {
    if (!s_sfx_ready)
        return;
    mutexLock(&s_voice_lock);
    if (!s_bgm_samples)
        enabled = false;
    s_bgm_enabled = enabled;
    mutexUnlock(&s_voice_lock);
}

int switch2SfxBgmActive(void) {
    return (s_bgm_samples && s_bgm_enabled && s_bgm_frames) ? 1 : 0;
}

void switch2SfxPlay(S2SfxId id) {
    if (!s_sfx_ready || !s_sfx_enabled || id < 0 || id >= S2Sfx_Count ||
        !s_samples[id])
        return;

    if (id == S2Sfx_Move || id == S2Sfx_Pill) {
        /* 70ms debounce, under the 83ms input repeat rate */
        static u64 s_last_nav_tick = 0;
        const u64 now = armGetSystemTick();
        if (now - s_last_nav_tick < armNsToTicks(70000000ULL))
            return;
        s_last_nav_tick = now;
    }

    mutexLock(&s_voice_lock);
    if (id == S2Sfx_Move || id == S2Sfx_Pill) {
        /* Polyphonic nav overlap (PS4 ripple): let the previous tick's decay
         * ring out underneath the new one, up to 3 concurrent nav voices.
         * Overlapped voices duck to 0.85 gain; past the cap, steal the
         * oldest (largest pos). */
        int nav_count = 0, free_slot = -1, oldest = -1;
        uint32_t oldest_pos = 0;
        for (int v = 0; v < SFX_SLOTS; v++) {
            if (!s_voices[v].active) {
                if (free_slot < 0)
                    free_slot = v;
                continue;
            }
            if (s_voices[v].id == S2Sfx_Move || s_voices[v].id == S2Sfx_Pill) {
                nav_count++;
                if (s_voices[v].pos >= oldest_pos) {
                    oldest_pos = s_voices[v].pos;
                    oldest = v;
                }
            }
        }
        const bool overlapped = nav_count > 0;
        int slot = free_slot;
        if (nav_count >= 3 || slot < 0)
            slot = oldest; /* steal the furthest-progressed nav voice */
        if (slot >= 0) {
            s_voices[slot].id = id;
            s_voices[slot].pos = 0;
            s_voices[slot].gain = overlapped ? 0.85f : 1.0f;
            s_voices[slot].active = true;
        }
    } else {
        for (int v = 0; v < SFX_SLOTS; v++) {
            if (!s_voices[v].active) {
                s_voices[v].id = id;
                s_voices[v].pos = 0;
                s_voices[v].gain = 1.0f;
                s_voices[v].active = true;
                break;
            }
        }
    }
    mutexUnlock(&s_voice_lock);
}


#else
/* Non-Switch builds: stubs (the engine is Switch audout-only). */
void switch2SfxInit(void) {}
void switch2SfxExit(void) {}
void switch2SfxSetEnabled(bool enabled) { (void)enabled; }
void switch2SfxResetSynth(void) {}
void switch2SfxSetSample(S2SfxId id, int16_t *samples, uint32_t frames) { (void)id; (void)samples; (void)frames; }
void switch2SfxSetBgm(const char *wav_path, float volume) {
    if (!s_sfx_ready)
        return;
    mutexLock(&s_voice_lock);
    s_bgm_enabled = false;
    s_bgm_frames = 0;
    if (wav_path && bgmHeaderParse(wav_path)) {
        /* bgmHeaderParse preloaded the whole payload into s_bgm_samples. */
        s_bgm_phase = 0;
        s_bgm_volume = (volume > 0.0f && volume <= 1.0f) ? volume : 0.5f;
        s_bgm_enabled = true;
    } else {
        free(s_bgm_samples);
        s_bgm_samples = NULL;
    }
    mutexUnlock(&s_voice_lock);
}

void switch2SfxSetBgmEnabled(bool enabled) {
    if (!s_sfx_ready)
        return;
    mutexLock(&s_voice_lock);
    if (!s_bgm_samples)
        enabled = false;
    s_bgm_enabled = enabled;
    mutexUnlock(&s_voice_lock);
}

void switch2SfxPlay(S2SfxId id) { (void)id; }
#endif
