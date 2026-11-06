#!/usr/bin/env python3
"""(Noah: the Bloodbloom Flask made the Voidglass Flask - "Recolour to blue-white") the blood flask's liquid, glow and fizz
textures recoloured from red to the Arcane element's blue-white, in place (the models keep their paths).

    python3 tools/voidglass.py
"""
import pathlib, sys
import numpy as np
from PIL import Image
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from assets import write_dds_mips

ROOT = pathlib.Path(__file__).resolve().parent.parent
T = ROOT / 'assets' / 'fx' / 'Textures' / 'CurseOfAlduin'
FILES = [(T / 'alchemist' / 'liquid_blood_d.dds', 'DXT1'), (T / 'alchemist' / 'liquid_blood_g.dds', 'DXT1'), (T / 'fx' / 'alch_blood.dds', 'DXT5')]
DEEP, WHITE = np.array([30, 60, 200], np.float32), np.array([235, 245, 255], np.float32)
for p, fmt in FILES:
    im = Image.open(p).convert('RGBA')
    a = np.asarray(im, np.float32)
    lum = np.clip(a[..., :3].max(-1) / 255.0, 0, 1)            # (how bright the red was: its value, not its hue)
    t = lum ** 1.4
    rgb = DEEP[None, None] * (1 - t[..., None]) + WHITE[None, None] * t[..., None]
    rgb *= np.clip(lum * 1.25, 0, 1)[..., None]                   # (dark stays dark)
    out = np.dstack([np.clip(rgb, 0, 255), a[..., 3]]).astype(np.uint8)
    write_dds_mips(Image.fromarray(out, 'RGBA'), p, fmt)
    print('voidglass:', p.name)
