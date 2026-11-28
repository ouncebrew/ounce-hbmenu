/* SPDX-License-Identifier: ISC
 * PS4 system sound pack: downloads the public PS4 BIOS sound rip from
 * The Sounds Resource, extracts the UI cues into the theme pack folder
 * and feeds them to the SFX engine. Falls back to built-in synth sounds.
 */
#ifndef HBMENU_SFX_PACK_H
#define HBMENU_SFX_PACK_H

#include <stddef.h>

typedef enum {
    SfxDl_Idle = 0,
    SfxDl_Running,
    SfxDl_Done,
    SfxDl_Error,
} SfxDlState;

/* true when themes/ps4-orbis/sounds/cursor_move.wav exists on the SD card */
int sfxPackInstalled(void);

/* Override the built-in pack URL (copied; NULL/empty resets to default). */
void sfxPackSetUrl(const char *url);

/* Kick off the download on a background thread (no-op if already running). */
void sfxPackStartDownload(void);

/* true when the zip contains a theme.cfg (i.e. it is a mountable theme,
 * not a sound pack). */
int sfxPackZipLooksLikeTheme(const char *zip_path);

/* Extract a local theme-pack zip (sound cues + theme.json) into
 * themes/ps4-orbis/. Returns the number of extracted cue files, or -1
 * with err filled when the zip holds nothing usable. The zip is left in
 * place; the caller removes it on success. */
int sfxPackInstallZip(const char *zip_path, char *err, size_t err_size);

SfxDlState sfxPackState(void);
unsigned sfxPackProgress(void);       /* 0..100, bytes-based when no length */
const char *sfxPackStatusText(void);  /* short user-facing status */

/* Load installed pack wav files into the SFX engine (safe if absent). */
void sfxPackLoadActive(void);

/* Call once per frame on the UI thread: reaps the download worker and,
 * on completion, hot-loads the pack + plays the notification chime. */
void sfxPackPoll(void);

#endif
