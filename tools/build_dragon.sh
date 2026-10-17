#!/bin/bash
# The glider dragon's nifs, from Skyrim's dragon (tools/dragonbuild.py + tools/modelconv/dragonbuild) - into <out>/Data:
#   Meshes/NickNak/ParaGlider.nif (+ _b1.._b3: each bond's look), Meshes/CurseOfAlduin/fx/dragon_away*.nif (the fly-off)
#   Textures/CurseOfAlduin/Dragon: wind, harness, fire palettes
#   bash tools/build_dragon.sh <work dir with orig.nif> <dragon.txt> <skel.txt> <out>
set -e
W=$1; DT=$2; SK=$3; OUT=$4
T=/home/claude/tools
HERE=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$OUT/Data/Meshes/NickNak" "$OUT/Data/Meshes/CurseOfAlduin/fx" "$OUT/Data/Textures"
python3 "$HERE/dragonbuild.py" job "$DT" "$SK" "$OUT/job.txt"
python3 "$HERE/dragonbuild.py" poses "$DT" "$SK" "$HERE/../src/DragonPoses.h"
python3 -c "import sys; sys.path.insert(0, '$HERE'); import dragonharness as h; h.texture('$OUT/Data/Textures')"
python3 "$HERE/firepalette.py" "$OUT/Data/Textures"
$T/dragonbuild "$W/orig.nif" "$OUT/job.txt" "$OUT/base_h.nif" --hidden
$T/dragonbuild "$W/orig.nif" "$OUT/job.txt" "$OUT/base_w.nif" --world
python3 "$HERE/dragonfx.py" "$OUT/base_h.nif" "$OUT/fx_h.nif" "$OUT/Data/Textures" "$OUT/job.points.json"
python3 "$HERE/dragonfx.py" "$OUT/base_w.nif" "$OUT/fx_w.nif" "$OUT/Data/Textures" "$OUT/job.points.json"
M=$OUT/Data/Meshes
cp "$OUT/fx_h.nif" "$M/NickNak/ParaGlider.nif"
cp "$OUT/fx_w.nif" "$M/CurseOfAlduin/fx/dragon_away.nif"
for k in 1 2 3; do
  case $k in
    1) D='textures\actors\dragon\Dragon_Snow.dds'; C="0.55 0.85 1.0 2.0";;
    2) D='textures\CurseOfAlduin\Dragon\bond2_d.dds'; C="0.35 0.7 1.0 1.6";;
    3) D='textures\CurseOfAlduin\Dragon\bond3_d.dds'; C="1.0 0.28 0.06 1.6";;
  esac
  G="textures\\CurseOfAlduin\\Dragon\\bond${k}_g.dds"
  $T/dragonvariant "$OUT/fx_h.nif" "$M/NickNak/ParaGlider_b$k.nif" "$D" "$G" $C >/dev/null
  $T/dragonvariant "$OUT/fx_w.nif" "$M/CurseOfAlduin/fx/dragon_away_b$k.nif" "$D" "$G" $C >/dev/null
done
find "$OUT/Data" -type f | sort
