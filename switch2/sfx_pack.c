/* SPDX-License-Identifier: ISC
 * PS4 system sound pack downloader + loader.
 *
 * Downloads the public PlayStation 4 System BIOS .wav rip (The Sounds
 * Resource, id 26815) into themes/ps4-orbis/sounds/, keeping only the UI
 * cues hbmenu uses. Sony retains copyright on the audio: the pack is
 * always user-fetched at runtime, never bundled.
 */
#include "sfx_pack.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <switch.h>
#include <physfs.h>

#include "switch2_sfx.h"
#include "../nx_main/http.h"

extern char *menuGetRootBasePath(void);

#define PACK_ID      "ps4-orbis"
#define DL_URL_DEFAULT "https://www.sounds-resource.com/download/26815/"
#define DL_URL_DEFAULT_TEXT DL_URL_DEFAULT /* exposed to switch2_ui.c */
#define DL_REFERER   "https://www.sounds-resource.com/playstation_4/systembios/sound/26815/"

/* Raw rip filename (often truncated inside the archive) -> clean target */
static const struct {
    const char *raw_prefix;
    const char *target;
} kRipMap[] = {
    { "snd_cursor1-Cursol_refine_C-",          "cursor_move.wav"  },
    { "snd_cursor_end-TTS_b_1222_-16dB",      "cursor_end.wav"   },
    { "snd_cz_cursor_in2-Decide_refine_D_",    "confirm.wav"      },
    { "snd_cz_cursor_in1-Cancel_refine_D_",    "cancel.wav"      },
    { "snd_msg_notification-se_msg_notificat", "notification.wav" },
    { "snd_boot_game_boot-se_boot_game_boo",  "launch.wav"       },
    { "snd_cz_cursor_in2-Decide_refine_D_",   "dock_enter.wav"   },
    { "snd_boot_game_quit-se_boot_game_qui", "dock_exit.wav"    },
    { "bgm.wav",                             "bgm.wav"           },
};

/* clean target -> SFX event (+ theme.json audio.sfx key) */
static const struct {
    const char *file; /* built-in alias, fallback when theme.json omits it */
    const char *key;
    S2SfxId id;
} kEventMap[] = {
    { "cursor_move.wav",  "cursor_move",  S2Sfx_Move },
    { "confirm.wav",      "confirm",      S2Sfx_Confirm },
    { "cancel.wav",       "cancel",       S2Sfx_Back },
    { "notification.wav", "notification", S2Sfx_Notification },
    { "launch.wav",       "launch",       S2Sfx_Launch },
    { "enter_menu.wav",   "enter_menu",   S2Sfx_EnterMenu },
    { "back_menu.wav",    "back_menu",    S2Sfx_BackMenu },
    { "dialog_open.wav",  "dialog_open",  S2Sfx_DialogOpen },
    { "startup.wav",      "startup",      S2Sfx_Startup },
    { "dock_enter.wav",   "dock_enter",   S2Sfx_DockEnter },
    { "dock_exit.wav",    "dock_exit",    S2Sfx_DockExit },
};

/* Focused JSON string-value scan: finds "key": "value". No parser dep. */
static int jsonGetString(const char *buf, const char *key, char *out, size_t out_sz) {
    char pat[80];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *k = strstr(buf, pat);
    if (!k)
        return 0;
    const char *q1 = strchr(k + strlen(pat), '"');
    if (!q1)
        return 0;
    const char *q2 = strchr(q1 + 1, '"');
    if (!q2)
        return 0;
    size_t len = (size_t)(q2 - q1 - 1);
    if (len >= out_sz)
        len = out_sz - 1;
    memcpy(out, q1 + 1, len);
    out[len] = 0;
    return out[0] != 0;
}

static char s_url[512] = DL_URL_DEFAULT;

static bool s_http_ready = false; /* network+TLS, initialized on the UI thread */

static volatile int s_state = SfxDl_Idle;
static volatile unsigned s_progress = 0;
static char s_status[128] = "";

static void packDir(char *out, size_t sz);

/* Reads "bgmVolume" from themes/<pack>/theme.json (default 0.5). Only one
 * float is needed, so a focused scan beats pulling in a JSON parser. */
static float packBgmVolume(void) {
    char path[512];
    char root[512];
    packDir(root, sizeof(root));
    snprintf(path, sizeof(path), "%s/theme.json", root);
    FILE *f = fopen(path, "rb");
    if (!f)
        return 0.5f;
    char buf[8192];
    const size_t got = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[got] = 0;
    const char *key = strstr(buf, "\"bgmVolume\"");
    if (!key)
        return 0.5f;
    const char *colon = strchr(key, ':');
    if (!colon)
        return 0.5f;
    const float vol = strtof(colon + 1, NULL);
    if (vol > 0.0f && vol <= 1.0f)
        return vol;
    return 0.5f;
}

void sfxPackSetUrl(const char *url) {
    if (url && url[0])
        snprintf(s_url, sizeof(s_url), "%.255s", url);
    else
        snprintf(s_url, sizeof(s_url), "%s", DL_URL_DEFAULT);
}

static void packDir(char *out, size_t sz) {
    snprintf(out, sz, "%s/config/nx-hbmenu/themes/" PACK_ID, menuGetRootBasePath());
}

static void soundsDir(char *out, size_t sz) {
    char dir[512];
    packDir(dir, sizeof(dir));
    snprintf(out, sz, "%s/sounds", dir);
}

/* --- WAV decoding (PCM16, mono/stereo, 44.1k/48k -> 48k stereo) --------- */

#define WAV_MAX_FRAMES 500000 /* ~10s (startup chime is 8s) */

static int16_t *wavLoad48kStereo(const char *path, uint32_t *frames_out) {
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    int16_t *out = NULL;
    unsigned char hdr[12];
    if (fread(hdr, 1, 12, f) != 12 || memcmp(hdr, "RIFF", 4) != 0 ||
        memcmp(hdr + 8, "WAVE", 4) != 0)
        goto fail;

    unsigned fmt_channels = 0;
    unsigned fmt_rate = 0;
    unsigned bits = 0;
    long data_pos = -1;
    unsigned data_size = 0;
    for (;;) {
        unsigned char ch[8];
        if (fread(ch, 1, 8, f) != 8)
            break;
        unsigned size = ch[4] | (ch[5] << 8) | (ch[6] << 16) | ((unsigned)ch[7] << 24);
        if (memcmp(ch, "fmt ", 4) == 0) {
            unsigned char fmt[16];
            const unsigned want = size < 16 ? size : 16;
            if (fread(fmt, 1, want, f) != want)
                break;
            if (size > want)
                fseek(f, (long)(size - want), SEEK_CUR);
            const unsigned format = fmt[0] | (fmt[1] << 8);
            fmt_channels = fmt[2] | (fmt[3] << 8);
            fmt_rate = fmt[4] | (fmt[5] << 8) | (fmt[6] << 16) | ((unsigned)fmt[7] << 24);
            bits = fmt[14] | (fmt[15] << 8);
            if (format != 1 || bits != 16 || (fmt_channels != 1 && fmt_channels != 2) ||
                (fmt_rate != 44100 && fmt_rate != 48000))
                goto fail;
        } else if (memcmp(ch, "data", 4) == 0) {
            data_pos = ftell(f);
            data_size = size;
            break;
        } else {
            fseek(f, (long)size, SEEK_CUR);
        }
        if (size & 1)
            fseek(f, 1, SEEK_CUR);
    }
    if (data_pos < 0 || !fmt_channels || data_size < 4)
        goto fail;

    fseek(f, data_pos, SEEK_SET);
    const unsigned frames_in = data_size / (2 * fmt_channels);
    if (frames_in == 0 || frames_in > WAV_MAX_FRAMES * 2)
        goto fail;

    int16_t *raw = malloc(frames_in * fmt_channels * sizeof(int16_t));
    if (!raw)
        goto fail;
    if (fread(raw, sizeof(int16_t), frames_in * fmt_channels, f) != frames_in * fmt_channels) {
        free(raw);
        goto fail;
    }

    const unsigned frames_out_n =
        fmt_rate == 48000 ? frames_in : (unsigned)((unsigned long long)frames_in * 48000 / 44100);
    out = malloc(frames_out_n * 2 * sizeof(int16_t));
    if (out) {
        for (unsigned i = 0; i < frames_out_n; i++) {
            unsigned src = fmt_rate == 48000
                ? i
                : (unsigned)((unsigned long long)i * 44100 / 48000);
            if (src >= frames_in)
                src = frames_in - 1;
            if (fmt_channels == 2) {
                out[i * 2 + 0] = raw[src * 2 + 0];
                out[i * 2 + 1] = raw[src * 2 + 1];
            } else {
                out[i * 2 + 0] = raw[src];
                out[i * 2 + 1] = raw[src];
            }
        }
        *frames_out = frames_out_n;
    }
    free(raw);
fail:
    fclose(f);
    return out;
}

/* --- Installed pack loading ---------------------------------------------- */

int sfxPackInstalled(void) {
    char path[512];
    char dir[512];
    soundsDir(dir, sizeof(dir));
    snprintf(path, sizeof(path), "%s/cursor_move.wav", dir);
    FILE *f = fopen(path, "rb");
    if (f) {
        fclose(f);
        return 1;
    }
    /* A theme.json alone also proves the pack folder is installed */
    packDir(dir, sizeof(dir));
    snprintf(path, sizeof(path), "%s/theme.json", dir);
    f = fopen(path, "rb");
    if (f) {
        fclose(f);
        return 1;
    }
    return 0;
}

void sfxPackLoadActive(void) {
    char dir[512], root[512];
    soundsDir(dir, sizeof(dir));
    packDir(root, sizeof(root));

    /* theme.json is authoritative: audio.sfx.<key>, audio.bgm,
     * audio.bgmVolume. Built-in aliases are only the fallback. */
    static char tj[16384];
    tj[0] = 0;
    char tj_path[512];
    snprintf(tj_path, sizeof(tj_path), "%s/theme.json", root);
    FILE *f = fopen(tj_path, "rb");
    if (f) {
        const size_t got = fread(tj, 1, sizeof(tj) - 1, f);
        fclose(f);
        tj[got] = 0;
    }

    for (size_t i = 0; i < sizeof(kEventMap) / sizeof(kEventMap[0]); i++) {
        char file[128];
        if (!jsonGetString(tj, kEventMap[i].key, file, sizeof(file)))
            snprintf(file, sizeof(file), "%s", kEventMap[i].file);
        char path[512];
        /* theme.json paths are pack-root relative ("sounds/x.wav");
         * built-in aliases are sounds-dir relative. Try both. */
        snprintf(path, sizeof(path), "%s/%s", root, file);
        FILE *probe = fopen(path, "rb");
        if (probe) {
            fclose(probe);
        } else {
            snprintf(path, sizeof(path), "%s/%s", dir, file);
        }
        uint32_t frames = 0;
        int16_t *pcm = wavLoad48kStereo(path, &frames);
        if (pcm)
            switch2SfxSetSample(kEventMap[i].id, pcm, frames);
    }

    /* Looping background music: streamed from SD by the mixer thread, no
     * SFX_MAX_FRAMES cap applies to it. */
    char bgm_file[128];
    if (!jsonGetString(tj, "bgm", bgm_file, sizeof(bgm_file)))
        snprintf(bgm_file, sizeof(bgm_file), "bgm.wav");
    char bgm_path[512];
    snprintf(bgm_path, sizeof(bgm_path), "%s/%s", root, bgm_file);
    FILE *probe = fopen(bgm_path, "rb");
    if (probe) {
        fclose(probe);
        switch2SfxSetBgm(bgm_path, packBgmVolume());
    } else {
        snprintf(bgm_path, sizeof(bgm_path), "%s/bgm.wav", dir);
        probe = fopen(bgm_path, "rb");
        if (probe) {
            fclose(probe);
            switch2SfxSetBgm(bgm_path, packBgmVolume());
        } else {
            switch2SfxSetBgm(NULL, 0.0f);
        }
    }
}

/* --- Download thread ------------------------------------------------------ */

static void mkdirOne(const char *path) {
    mkdir(path, 0777);
}

static void mkdirChain(const char *base, const char *rel) {
    char cur[512];
    snprintf(cur, sizeof(cur), "%s", base);
    mkdirOne(cur);
    char *seg = strdup(rel);
    char *save = NULL;
    for (char *tok = strtok_r(seg, "/", &save); tok; tok = strtok_r(NULL, "/", &save)) {
        size_t len = strlen(cur);
        snprintf(cur + len, sizeof(cur) - len, "/%s", tok);
        mkdirOne(cur);
    }
    free(seg);
}

static void dlProgress(unsigned long long received, unsigned long long total, void *user) {
    (void)user;
    if (total)
        s_progress = (unsigned)(received * 100 / total);
    else
        s_progress = (unsigned)((received / 1024) % 100); /* KB counter fallback */
}

/* Copy one zip entry (by mounted path) to a destination file, streaming in
 * 64KB chunks: no size cap, no whole-file allocation (bgm.wav is ~18MB). */
static void copyEntry(const char *src_mounted, const char *dst) {
    PHYSFS_File *pf = PHYSFS_openRead(src_mounted);
    if (!pf)
        return;
    const PHYSFS_sint64 size = PHYSFS_fileLength(pf);
    if (size <= 0) {
        PHYSFS_close(pf);
        return;
    }
    FILE *out = fopen(dst, "wb");
    if (!out) {
        PHYSFS_close(pf);
        return;
    }
    static unsigned char chunk[64 * 1024];
    PHYSFS_sint64 remaining = size;
    while (remaining > 0) {
        const PHYSFS_uint32 take =
            remaining > (PHYSFS_sint64)sizeof(chunk) ? (PHYSFS_uint32)sizeof(chunk)
                                                      : (PHYSFS_uint32)remaining;
        const PHYSFS_sint64 got = PHYSFS_readBytes(pf, chunk, take);
        if (got <= 0)
            break;
        fwrite(chunk, 1, (size_t)got, out);
        remaining -= got;
    }
    fclose(out); /* flush each file to SD immediately */
    PHYSFS_close(pf);
}

/* Recursively walk the mounted zip so any nesting layout works:
 * files at the root, under sounds/, under <packid>/, etc. */
static void walkZipTree(const char *dir, int depth, const char *sounds,
                        const char *pack_root, char *theme_json_done) {
    if (depth > 4)
        return;
    char **entries = PHYSFS_enumerateFiles(dir);
    if (!entries)
        return;
    for (char **e = entries; *e; e++) {
        const char *name = *e;
        char sub[512];
        snprintf(sub, sizeof(sub), "%s/%s", dir, name);
        if (PHYSFS_isDirectory(sub)) {
            walkZipTree(sub, depth + 1, sounds, pack_root, theme_json_done);
            continue;
        }
        for (size_t m = 0; m < sizeof(kRipMap) / sizeof(kRipMap[0]); m++) {
            if (strcasecmp(name, kRipMap[m].target) != 0 &&
                strncasecmp(name, kRipMap[m].raw_prefix, strlen(kRipMap[m].raw_prefix)) != 0)
                continue;
            char dst[512];
            snprintf(dst, sizeof(dst), "%s/%s", sounds, kRipMap[m].target);
            copyEntry(sub, dst); /* no break: one raw file may map to many targets */
        }
        if (!*theme_json_done && strcasecmp(name, "theme.json") == 0) {
            char dst[512];
            snprintf(dst, sizeof(dst), "%s/theme.json", pack_root);
            copyEntry(sub, dst);
            *theme_json_done = 1;
        }
    }
    PHYSFS_freeList(entries);
}

static void extractPack(const char *zip_path, const char *sounds, const char *pack_root) {
    if (!PHYSFS_mount(zip_path, "ps4pack", 1))
        return;
    char theme_json_done = 0;
    walkZipTree("ps4pack", 0, sounds, pack_root, &theme_json_done);
    PHYSFS_unmount("ps4pack");
}

int sfxPackZipLooksLikeTheme(const char *zip_path) {
    if (!PHYSFS_mount(zip_path, "ps4pack", 1))
        return 0;
    int is_theme = 0;
    for (size_t d = 0; d < 2 && !is_theme; d++) {
        char cfg[64];
        snprintf(cfg, sizeof(cfg), d ? "ps4pack/theme.cfg" : "ps4packtheme.cfg");
        PHYSFS_File *pf = PHYSFS_openRead(cfg);
        if (pf) {
            PHYSFS_close(pf);
            is_theme = 1;
        }
    }
    PHYSFS_unmount("ps4pack");
    return is_theme;
}

int sfxPackInstallZip(const char *zip_path, char *err, size_t err_size) {
    char base[512], sounds[512];
    packDir(base, sizeof(base));
    soundsDir(sounds, sizeof(sounds));
    mkdirChain(menuGetRootBasePath(), "config/nx-hbmenu/themes/" PACK_ID "/sounds");
    extractPack(zip_path, sounds, base);
    /* Remove stale cue files from older pack versions (e.g. dock_switch) */
    DIR *d = opendir(sounds);
    if (d) {
        struct dirent *de;
        while ((de = readdir(d))) {
            const size_t len = strlen(de->d_name);
            if (len < 5 || strcasecmp(de->d_name + len - 4, ".wav") != 0)
                continue;
            bool known = false;
            for (size_t m = 0; m < sizeof(kRipMap) / sizeof(kRipMap[0]); m++) {
                if (strcasecmp(de->d_name, kRipMap[m].target) == 0) {
                    known = true;
                    break;
                }
            }
            if (!known) {
                char stale[512];
                snprintf(stale, sizeof(stale), "%s/%s", sounds, de->d_name);
                remove(stale);
            }
        }
        closedir(d);
    }
    if (!sfxPackInstalled()) {
        snprintf(err, err_size, "No PS4 sound files found in this zip");
        return -1;
    }
    int count = 0;
    for (size_t i = 0; i < sizeof(kEventMap) / sizeof(kEventMap[0]); i++) {
        char path[512];
        snprintf(path, sizeof(path), "%s/%s", sounds, kEventMap[i].file);
        FILE *f = fopen(path, "rb");
        if (f) {
            fclose(f);
            count++;
        }
    }
    return count;
}

static void downloadThread(void *arg) {
    (void)arg;
    char err[256] = "";
    char base[512];
    char sounds[512];
    char zip[512];
    packDir(base, sizeof(base));
    soundsDir(sounds, sizeof(sounds));
    snprintf(zip, sizeof(zip), "%s/.tmp_ps4_rip.zip", base);

    (void)err;
    s_state = SfxDl_Running;
    s_progress = 0;

    char sha[65] = "";
    const int rc = http_download_progress(s_url[0] ? s_url : DL_URL_DEFAULT, zip, sha,
                                          err, sizeof(err), dlProgress, NULL);

    if (rc != 0) {
        remove(zip);
        snprintf(s_status, sizeof(s_status), "Download failed: %s",
                 err[0] ? err : http_result_text(rc));
        s_state = SfxDl_Error;
        return;
    }

    snprintf(s_status, sizeof(s_status), "Extracting sounds...");
    extractPack(zip, sounds, base);
    remove(zip);

    if (!sfxPackInstalled()) {
        snprintf(s_status, sizeof(s_status), "Archive did not contain the PS4 UI sounds");
        s_state = SfxDl_Error;
        return;
    }

    snprintf(s_status, sizeof(s_status), "PS4 sounds installed");
    s_state = SfxDl_Done;
}

static Thread s_thread;
static volatile bool s_thread_active = false;

/* Must run on the main/UI thread every frame: reaps the worker and applies
 * the pack. Touching the SFX engine from the worker would race with
 * playback (crash), so sample loading + the completion chime happen here. */
void sfxPackPoll(void) {
    if (!s_thread_active || s_state == SfxDl_Running)
        return;
    threadWaitForExit(&s_thread);
    threadClose(&s_thread);
    s_thread_active = false;
    if (s_http_ready) {
        http_global_exit(); /* no-op for sockets owned by the host env */
        s_http_ready = false;
    }
    if (s_state == SfxDl_Done) {
        sfxPackLoadActive();
        switch2SfxPlay(S2Sfx_Notification);
    }
}

void sfxPackStartDownload(void) {
    if (s_thread_active)
        return;
    /* sockets + SSL must come up on the UI thread (brewstore does the same);
     * doing this inside the worker crashes the process. */
    if (!s_http_ready) {
        char err[256] = "";
        http_set_referer(DL_REFERER);
        if (http_global_init(err, sizeof(err)) != 0) {
            snprintf(s_status, sizeof(s_status), "%s", err);
            s_state = SfxDl_Error;
            return;
        }
        s_http_ready = true;
    }
    /* TLS + resolver need a large stack: 256KB like brewstore's downloader. */
    Result rc = threadCreate(&s_thread, downloadThread, NULL, NULL, 0x40000, 0x2C, -2);
    if (R_FAILED(rc)) {
        snprintf(s_status, sizeof(s_status), "threadCreate failed");
        s_state = SfxDl_Error;
        return;
    }
    s_thread_active = true;
    threadStart(&s_thread);
}

SfxDlState sfxPackState(void)   { return (SfxDlState)s_state; }
unsigned sfxPackProgress(void)  { return s_progress; }
const char *sfxPackStatusText(void) { return s_status; }
