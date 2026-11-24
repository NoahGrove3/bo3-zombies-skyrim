#!/bin/bash
# (Noah, 2026-11-24: "remake the voice's effect - it's making the voice quality sound poor instead of clear with an echo effect")
# Felldir's line kept clear: no pitch-shifted under-voice, no chorus (they smeared it) - his own voice, a low cut, a touch of
# presence, a gentle level, then a stone echo (two soft repeats, 260 and 520 ms) and a little of the hall behind it.
# Loudness -17 LUFS; 44.1 kHz mono 16-bit; only the silence at the end trimmed (the echo's tail kept).
#   tools/altarfx.sh <in> <out.wav>
set -e
IR="$(dirname "$0")/data/whisper_hall_ir.wav"
ffmpeg -hide_banner -loglevel error -y -i "$1" -i "$IR" -filter_complex "\
[0:a]aformat=channel_layouts=mono,aresample=44100,highpass=f=75,equalizer=f=3200:t=q:w=1.0:g=2,\
acompressor=threshold=-22dB:ratio=2.2:attack=8:release=140:makeup=1.5,apad=pad_dur=1.4,\
aecho=in_gain=1.0:out_gain=0.92:delays=260|520:decays=0.20|0.08,asplit[dry][w];\
[1:a]aformat=channel_layouts=mono,aresample=44100[ir];\
[w]lowpass=f=6000[wl];[wl][ir]afir=dry=10:wet=10[rev];\
[dry]volume=0.93[d2];[rev]volume=0.07[r2];\
[d2][r2]amix=inputs=2:normalize=0,loudnorm=I=-17:TP=-1.5:LRA=9,aresample=44100,areverse,silenceremove=start_periods=1:start_threshold=-58dB:start_silence=0.5,areverse[out]" \
 -map "[out]" -ac 1 -ar 44100 -c:a pcm_s16le "$2"
