/* SPDX-License-Identifier: ISC
 * Lightweight procedural Switch 2 tactile UI SFX engine (non-blocking audout).
 */
#ifndef HBMENU_SWITCH2_SFX_H
#include <stdint.h>
#define HBMENU_SWITCH2_SFX_H

#include <stdbool.h>

typedef enum {
    S2Sfx_Move = 0,     /* Soft tactile tick when moving between apps */
    S2Sfx_Pill = 1,     /* Crisp light tap on pill bar / settings list */
    S2Sfx_Confirm = 2,  /* Warm two-tone confirm chime on A / toggle */
    S2Sfx_Back = 3,     /* Soft downward mellow blip on B */
    S2Sfx_Star = 4,     /* Bright sparkle chime on Star / Unstar */
    S2Sfx_Notification = 5, /* System chime on download complete / toast */
    S2Sfx_Launch = 6,   /* Game/app launch takeoff sound */
    S2Sfx_EnterMenu = 7,   /* Opening Settings / Library */
    S2Sfx_BackMenu = 8,    /* Exiting a sub-menu back to home */
    S2Sfx_DockSwitch = 9,  /* Game row <-> top function bar switch */
    S2Sfx_DialogOpen = 10, /* Modal / netloader dialogue opening */
    S2Sfx_Startup = 11,    /* Boot splash chime */
    S2Sfx_DockEnter = 12,  /* Moving up into the top function bar */
    S2Sfx_DockExit = 13,   /* Moving down from the bar back to the game row */
    S2Sfx_Count = 14,
} S2SfxId;

void switch2SfxInit(void);
void switch2SfxExit(void);
void switch2SfxSetEnabled(bool enabled);
void switch2SfxPlay(S2SfxId id);
/* Drop any loaded theme-pack samples and restore the built-in synth sounds. */
void switch2SfxResetSynth(void);

/* Streaming looped background music (48kHz stereo PCM16 wav). The file is
 * read from SD in ~1s chunks by the mixer thread, so multi-minute tracks
 * cost only a small cache instead of a huge preload. NULL path stops it. */
void switch2SfxSetBgm(const char *wav_path, float volume);
void switch2SfxSetBgmEnabled(bool enabled);
/* 1 when the bgm stream is open and enabled (for the settings status line). */
int switch2SfxBgmActive(void);
/* Replace an event's sound with an externally decoded 48kHz stereo PCM16
 * buffer (frames * 2 channels). Takes ownership of the allocation. */
void switch2SfxSetSample(S2SfxId id, int16_t *samples, uint32_t frames);

#endif
