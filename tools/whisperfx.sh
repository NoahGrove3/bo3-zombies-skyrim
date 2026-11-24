#!/bin/bash
# (Noah, 2026-11-19: the altar's voice - "C with a little less echo/reverb") one raw line (ElevenLabs mp3/wav) -> the ethereal
# whisper the altars play: a low cut, an octave-down under-voice (0.32), a 0.94 pitch double, chorus, a short hall (IR) at
# 0.08 wet, a short echo (0.12 | 0.05), loudness -18 LUFS; 44.1 kHz mono 16-bit.
# (2026-11-24, Felldir's new voice: "Let's go with C" - about half of it) under-voice 0.16, double 0.22, chorus 0.22|0.16,
# hall 0.05 wet, echo 0.07 | 0.025; only the silence at the end trimmed (it cut lines at their first long pause).
#   tools/whisperfx.sh <in> <out.wav>
set -e
IR="$(dirname "$0")/data/whisper_hall_ir.wav"
ffmpeg -hide_banner -loglevel error -y -i "$1" -i "$IR" -filter_complex "\
[0:a]aformat=channel_layouts=mono,aresample=44100,highpass=f=110,apad=pad_dur=2.5,asplit=3[a][b][c];\
[b]asetrate=22050,aresample=44100,atempo=2.0,volume=0.16[low];\
[c]asetrate=41454,aresample=44100,atempo=1.0638,volume=0.22[dbl];\
[a][low][dbl]amix=inputs=3:normalize=0,chorus=0.7:0.9:50|60:0.22|0.16:0.25|0.4:2|1.3,asplit[dry][w];\
[1:a]aformat=channel_layouts=mono,aresample=44100[ir];\
[w][ir]afir=dry=10:wet=10[rev];\
[dry]volume=0.95[d2];[rev]volume=0.05[r2];\
[d2][r2]amix=inputs=2:normalize=0,aecho=1:1:180|360:0.07|0.025,loudnorm=I=-18:TP=-2,aresample=44100,areverse,silenceremove=start_periods=1:start_threshold=-60dB:start_silence=0.6,areverse[out]" \
 -map "[out]" -ac 1 -ar 44100 -c:a pcm_s16le "$2"
