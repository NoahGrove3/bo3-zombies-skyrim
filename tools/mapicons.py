"""The world map's own marker icons, for the compass (src/Hud.cpp DrawCompass: Noah - the compass's places should look
like the map's). Skyrim's map.swf (interface\\map.swf, Skyrim - Interface.bsa) is exported with JPEXS
(ffdec -format sprite:png -zoom 3 -export sprite <dir> map.swf); this packs each marker type's first frame, in the order
of RE::MARKER_TYPE, into an 8x8 atlas of 64 px cells: Interface/CurseOfAlduin/mapicons.png.
    python tools/mapicons.py <exported sprite dir> <out png>"""
import sys, glob, os
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
    s = (C - 6) / max(im.size)
    im = im.resize((max(1, round(im.width * s)), max(1, round(im.height * s))), Image.LANCZOS)
    atlas.paste(im, ((i % 8) * C + (C - im.width) // 2, (i // 8) * C + (C - im.height) // 2), im)
    found += 1
atlas.save(out)
print(out, found, 'icons')
