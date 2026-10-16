"""The world map's own marker icons, for the compass (src/Hud.cpp DrawCompass: Noah - the compass's places should look
like the map's). Skyrim's map.swf (interface\\map.swf, Skyrim - Interface.bsa) is exported with JPEXS
(ffdec -format sprite:png -zoom 3 -export sprite <dir> map.swf); this packs each marker type's first frame, in the order
of RE::MARKER_TYPE, into an 8x8 atlas of 64 px cells: Interface/CurseOfAlduin/mapicons.png.
    python tools/mapicons.py <exported sprite dir> <out png>"""
import sys, glob, os
import numpy as np
from PIL import Image

TYPES = ('None City Town Settlement Cave Camp Fort NordicRuin DwemerRuin Shipwreck Grove Landmark DragonLair Farm WoodMill Mine '
         'ImperialCamp StormcloakCamp Doomstone WheatMill Smelter Stable ImperialTower Clearing Pass Altar Rock Lighthouse '
         'OrcStronghold GiantCamp Shack NordicTower NordicDwelling Docks Shrine RiftenCastle RiftenCapitol WindhelmCastle '
         'WindhelmCapitol WhiterunCastle WhiterunCapitol SolitudeCastle SolitudeCapitol MarkarthCastle MarkarthCapitol '
         'WinterholdCastle WinterholdCapitol MorthalCastle MorthalCapitol FalkreathCastle FalkreathCapitol DawnstarCastle '
         'DawnstarCapitol DLC02MiraakTemple DLC02RavenRock DLC02StandingStones DLC02TelvanniTower DLC02ToSkyrim DLC02ToSolstheim '
         'DLC02CastleKarstaag').split()
ALIAS = {'DwemerRuin': 'Dwemer', 'DragonLair': 'Dragonlair'}
src, out = sys.argv[1], sys.argv[2]
C = 64
atlas = Image.new('RGBA', (8 * C, 8 * C), (0, 0, 0, 0))
found = 0
for i, t in enumerate(TYPES):
    name = ALIAS.get(t, t) + 'Marker'
    dirs = [d for d in glob.glob(os.path.join(src, 'DefineSprite_*_' + name)) if os.path.isdir(d)]
    if not dirs:
        print('missing', t)
        continue
    im = Image.open(os.path.join(dirs[0], '1.png')).convert('RGBA')
    bb = im.getchannel('A').point(lambda a: 255 if a > 8 else 0).getbbox()
    if not bb:
        continue
    im = im.crop(bb)
    # (the map draws them faded until hovered: full strength here - alpha to 255, the grey brightened to near white,
    # keeping the dark detail inside them; resized premultiplied so the edges stay clean)
    px = np.asarray(im).astype(np.float32) / 255
    alpha = np.clip(px[..., 3] / max(1e-3, px[..., 3].max()), 0, 1)
    lum = px[..., :3] @ np.array([0.299, 0.587, 0.114], np.float32)
    ref = np.percentile(lum[alpha > 0.5], 90) if (alpha > 0.5).any() else 0.45
    rgb = np.clip(lum / max(0.05, ref) * 0.93, 0, 1)[..., None] * np.array([1.0, 0.97, 0.92], np.float32)
    s = (C - 6) / max(im.size)
    size = (max(1, round(im.width * s)), max(1, round(im.height * s)))
    pre = Image.fromarray((np.concatenate([rgb * alpha[..., None], alpha[..., None]], -1) * 255).astype(np.uint8), 'RGBA').resize(size, Image.LANCZOS)
    q = np.asarray(pre).astype(np.float32) / 255
    a2 = q[..., 3:4]
    im = Image.fromarray((np.concatenate([np.clip(q[..., :3] / np.maximum(a2, 1e-3), 0, 1), a2], -1) * 255).astype(np.uint8), 'RGBA')
    atlas.paste(im, ((i % 8) * C + (C - im.width) // 2, (i // 8) * C + (C - im.height) // 2), im)
    found += 1
atlas.save(out)
print(out, found, 'icons')
