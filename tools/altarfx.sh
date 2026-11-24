#!/bin/bash
# (Noah, 2026-11-24: "remake the voice's effect - it's making the voice quality sound poor instead of clear with an echo effect")
# Felldir's line kept clear: no pitch-shifted under-voice, no chorus (they smeared it) - his own voice, a low cut, a touch of
# presence, a gentle level, then a stone echo (two soft repeats, 260 and 520 ms) and a little of the hall behind it.
# Loudness -17 LUFS; 44.1 kHz mono 16-bit; only the silence at the end trimmed (the echo's tail kept).
# (Noah: "make the echo fade smoother instead of it cutting off") four repeats dying away (0.20, 0.11, 0.055, 0.025), the tail
# trimmed only below -72 dB and its last 0.9 s faded out gently.
#   tools/altarfx.sh <in> <out.wav>
# (Noah, 2026-11-24: "reduce the length of the echo") the echo's repeats, the hall's share and the tail's room and fade can be
# set from outside: ECHO_DELAYS, ECHO_DECAYS, HALL (0..1), PAD (s), FADE (s)
set -e
# (Noah: "2 Short") two soft repeats, 180 and 360 ms, a little less of the hall, a shorter tail
ECHO_DELAYS="${ECHO_DELAYS:-180|360}"; ECHO_DECAYS="${ECHO_DECAYS:-0.13|0.05}"
HALL="${HALL:-0.05}"; PAD="${PAD:-1.2}"; FADE="${FADE:-0.5}"
DRY=$(python3 -c "print(round(1-$HALL,3))")
IR="$(dirname "$0")/data/whisper_hall_ir.wav"
ffmpeg -hide_banner -loglevel error -y -i "$1" -i "$IR" -filter_complex "\
[0:a]aformat=channel_layouts=mono,aresample=44100,highpass=f=75,equalizer=f=3200:t=q:w=1.0:g=2,\
acompressor=threshold=-22dB:ratio=2.2:attack=8:release=140:makeup=1.5,apad=pad_dur=$PAD,\
aecho=in_gain=1.0:out_gain=0.92:delays=$ECHO_DELAYS:decays=$ECHO_DECAYS,asplit[dry][w];\
[1:a]aformat=channel_layouts=mono,aresample=44100[ir];\
[w]lowpass=f=6000[wl];[wl][ir]afir=dry=10:wet=10[rev];\
[dry]volume=$DRY[d2];[rev]volume=$HALL[r2];\
[d2][r2]amix=inputs=2:normalize=0,loudnorm=I=-17:TP=-1.5:LRA=9,aresample=44100,areverse,silenceremove=start_periods=1:start_threshold=-72dB:start_silence=0.2,afade=t=in:d=$FADE:curve=qsin,areverse[out]" \
 -map "[out]" -ac 1 -ar 44100 -c:a pcm_s16le "$2"
