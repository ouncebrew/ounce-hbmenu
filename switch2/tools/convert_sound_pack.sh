#!/usr/bin/env bash
# Convert a raw PS4 sound rip into hbmenu theme-pack wavs.
#
# Pipeline per cue (firmware rips pad cues with leading dead air that adds
# input lag; the 10ms software mixer handles interruption, so decay tails
# are kept intact):
#   1. trim leading silence only (silenceremove, -60dB)
#   2. 3ms fade-in on the leading edge (no attack click)
#   3. NO tail trim: full natural decay kept
#
# Usage: convert_sound_pack.sh <rip_dir_with_mp3s> <output_wav_dir>
set -euo pipefail

IN="${1:?usage: $0 <in_dir> <out_dir>}"
OUT="${2:?usage: $0 <in_dir> <out_dir>}"
mkdir -p "$OUT"

# raw rip filename -> clean pack filename [leading-silence threshold, fade-in ms]
MAP=(
  "40. Src7 Cursol Refine C.mp3|cursor_move.wav"
  "42. Src9 Tts B 1222 16db.mp3|cursor_end.wav"
  "47. Src11 Decide Refine D.mp3|confirm.wav"
  "03. Src10 Cancel Refine D.mp3|cancel.wav"
  "09. Src16 Se Enter Menu.mp3|enter_menu.wav"
  "13. Src2 Se Back Menu.mp3|back_menu.wav"
  "33. Src38 Se Std Option1.mp3|options.wav"
  "07. Src14 Se Dlg Show Posi.mp3|dialog_open.wav"
  "17. Src23 Se Msg Notificat.mp3|notification.wav"
  "67. Src3 Se Boot Game Boo.mp3|launch.wav"
  "43. Ps4 Startup.mp3|startup.wav"
  "47. Src11 Decide Refine D.mp3|dock_enter.wav"
  "35. Src4 Se Boot Game Qui.mp3|dock_exit.wav"
)

AF_DEFAULT='silenceremove=start_periods=1:start_threshold=-60dB,'
AF_DEFAULT+='afade=t=in:st=0:d=0.003'

for pair in "${MAP[@]}"; do
  src="$(cut -d'|' -f1 <<<"$pair")"
  dst="$(cut -d'|' -f2 <<<"$pair")"
  thr="$(cut -d'|' -f3 <<<"$pair")"; thr="${thr:--60dB}"
  fad="$(cut -d'|' -f4 <<<"$pair")"; fad="${fad:-3}"
  AF="silenceremove=start_periods=1:start_threshold=$thr,"
  AF+="afade=t=in:st=0:d=0.00${fad}"
  if [[ -f "$IN/$src" ]]; then
    ffmpeg -y -loglevel error -fflags +bitexact -flags:v +bitexact -flags:a +bitexact -i "$IN/$src" \
      -ar 48000 -ac 2 -c:a pcm_s16le -af "$AF" "$OUT/$dst"
    echo "  $dst (${thr}, ${fad}ms fades)"
  else
    echo "  SKIP (missing): $src" >&2
  fi
done
