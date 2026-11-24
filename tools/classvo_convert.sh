#!/bin/bash
# (Noah, 2026-11-23) the ElevenLabs class lines (<id>_<male|female>.mp3) to the game's wavs: a rumble cut (70 Hz), the silence trimmed
# at both ends, loudness to -16 LUFS, mono 44.1 kHz 16-bit (Classes.cpp times its subtitle by the bytes: 88,200 a second).
#   classvo_convert.sh <mp3 folder> <out folder> [<second out folder>]   (a file already made is skipped)
src="$1"; out="$2"; out2="$3"
mkdir -p "$out"; [ -n "$out2" ] && mkdir -p "$out2"
n=0
for f in "$src"/*_male.mp3 "$src"/*_female.mp3; do
  [ -e "$f" ] || continue
  b=$(basename "$f" .mp3); w="$out/$b.wav"
  if [ ! -s "$w" ] || [ "$f" -nt "$w" ]; then
    ffmpeg -v error -y -i "$f" -af "highpass=f=70,silenceremove=start_periods=1:start_threshold=-50dB:start_silence=0.05,areverse,silenceremove=start_periods=1:start_threshold=-50dB:start_silence=0.12,areverse,loudnorm=I=-16:TP=-1.5:LRA=11" -ac 1 -ar 44100 -sample_fmt s16 "$w.tmp.wav" && mv "$w.tmp.wav" "$w"
    n=$((n+1))
  fi
  [ -n "$out2" ] && { [ -s "$out2/$b.wav" ] && cmp -s "$w" "$out2/$b.wav" || cp "$w" "$out2/$b.wav"; }
done
echo "converted $n"
