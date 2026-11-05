"""(Noah: "For the sword models I want to change them to use the swords from Light Greatswords ... Alduin's Bane and Dragon
Slayer 2 hand and the sword for each class 1hand but using the same swords from the mod"; "for the weapon effects around the
swords in hand use the effects mod we downloaded for that" - Visual Animated Enchants and Vibrant Weapons, both, layered)

The swords' models and the Vibrant Weapons art that burns along them, made from the two downloads - other authors' work, so
built into build/lgswords/Data (the game and the Party Pack carry it), never into the repo:

  - the class swords: Light Greatswords' Silver (the Summoner's Radiant Blade), Stahlrim (Destruction's Carian Blade),
    Daedric (the Alchemist's Shadowflame Sword) and Nordic (the Spellsword's Spellbound Blade), at 85% - a long sword in
    one hand, worn at the hip
  - Alduin's Bane: its Dragonbone greatsword; the Dragon Slayer: its Akaviri - two-handed, full size, on the back; each with
    a still copy for the Reliquary's display (no havok body) and a greyed-out one (until it's yours)
  - each sword look's Vibrant Weapons art (their greatsword art, at the blade's size), turned to that look's colour: fire
    drawn through our own flame ramps (Textures/CurseOfAlduin/fx/pal_*.dds, ours - made here with --palettes)

    python3 tools/lgswords.py <Light Greatswords zip> <Vibrant Weapons Lite.bsa> [--palettes]"""
import io, pathlib, subprocess, sys, tempfile, zipfile
import numpy as np
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT = ROOT / 'build' / 'lgswords' / 'Data' / 'Meshes' / 'CurseOfAlduin'
PALS = ROOT / 'assets' / 'fx' / 'Textures' / 'CurseOfAlduin' / 'fx'
TOOLS = pathlib.Path('/home/claude/tools')
ONE = 0.85   # (Noah: "85% is good")
SWORDS = {   # ours: (their model, scale, sheath)
    'radiant': ('silver', ONE, 'WeaponSword'), 'carian': ('stahlrim', ONE, 'WeaponSword'),
    'shadowflame': ('daedric', ONE, 'WeaponSword'), 'spellbound': ('nordic', ONE, 'WeaponSword'),
    'bane': ('dragonbone', 1.0, 'WeaponBack'), 'slayer': ('akaviri', 1.0, 'WeaponBack'),
}
STILL = ('bane', 'slayer')
# each look (tools/esp_ids.py BLADE_VIZ): Vibrant's art, its colour (r g b), our flame ramp ('-': theirs), its size
PAL = {  # ramps, dark to hot (greyscale 0 -> 1)
    'purple':  [(0, 0, 0), (40, 0, 90), (120, 30, 230), (190, 110, 255), (245, 220, 255)],
    'white':   [(0, 0, 0), (90, 80, 60), (210, 205, 190), (250, 248, 235), (255, 255, 255)],
    'gold':    [(0, 0, 0), (110, 40, 0), (240, 150, 30), (255, 215, 110), (255, 250, 225)],
    'crimson': [(0, 0, 0), (60, 0, 0), (170, 10, 15), (230, 60, 50), (255, 190, 170)],
    'red':     [(0, 0, 0), (90, 5, 0), (210, 40, 15), (255, 110, 50), (255, 220, 180)],
    'blue':    [(0, 0, 0), (10, 30, 110), (60, 130, 255), (150, 200, 255), (235, 245, 255)],
}
LOOKS = {
    'alch':     ('firegreatsword', (0.62, 0.25, 1.0), 'purple', ONE),
    'carian':   ('shockgreatsword', (0.35, 0.62, 1.0), 'blue', ONE),
    'radiant':  ('firegreatsword', (1.0, 0.97, 0.88), 'white', ONE),
    'ss_none':  ('firegreatsword', (1.0, 0.2, 0.1), 'red', ONE),
    'ss_fire':  ('firegreatsword', None, '-', ONE),
    'ss_frost': ('frostgreatsword', None, '-', ONE),
    'ss_shock': ('shockgreatsword', None, '-', ONE),
    'ss_sun':   ('firegreatsword', (1.0, 0.82, 0.35), 'gold', ONE),
    'ss_blood': ('firegreatsword', (0.7, 0.03, 0.05), 'crimson', ONE),
    'slayer':   ('firegreatsword', (1.0, 0.72, 0.25), 'gold', 1.0),
}


def palettes():
    PALS.mkdir(parents=True, exist_ok=True)
    for k, keys in PAL.items():
        x = np.linspace(0, 1, 256)
        xs = np.linspace(0, 1, len(keys))
        row = np.stack([np.interp(x, xs, [c[i] for c in keys]) for i in range(3)], 1)
        img = np.repeat(row[None], 4, 0).astype(np.uint8)
        png = pathlib.Path(tempfile.mkdtemp()) / f'pal_{k}.png'
        Image.fromarray(img, 'RGB').save(png)
        subprocess.run(['convert', str(png), '-define', 'dds:compression=none', '-define', 'dds:mipmaps=0', str(PALS / f'pal_{k}.dds')], check=True)
        print('palette', k)


def run(*a):
    r = subprocess.run([str(x) for x in a], capture_output=True, text=True)
    print(r.stdout.strip() or r.stderr.strip())
    if r.returncode: raise SystemExit(f'failed: {a}')


def main():
    if '--palettes' in sys.argv: palettes()
    zp, bsa = sys.argv[1], sys.argv[2]
    work = pathlib.Path(tempfile.mkdtemp())
    (OUT / 'swords').mkdir(parents=True, exist_ok=True); (OUT / 'vw').mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(zp) as z:
        for ours, (theirs, sc, prn) in SWORDS.items():
            src = work / f'{theirs}.nif'
            src.write_bytes(z.read(f'meshes/Light Greatswords/{theirs}/LightGreatsword.nif'))
            run(TOOLS / 'swordprep', src, OUT / 'swords' / f'{ours}.nif', sc, prn)
            if ours in STILL:
                run(TOOLS / 'swordprep', src, OUT / 'swords' / f'{ours}_static.nif', sc, '-', 'static')
                run(TOOLS / 'ghostnif', OUT / 'swords' / f'{ours}_static.nif', OUT / 'swords' / f'ghost_{ours}.nif', 'CurseOfAlduin\\wonder\\ghost_d.dds')
    run('python3', '-I', TOOLS / 'bsax.py', bsa, work / 'vw', 'greatsword')
    art = work / 'vw' / 'meshes' / 'vibrant weapons' / 'magic'
    for look, (src, col, pal, sc) in LOOKS.items():
        r, g, b = col or (1.0, 1.0, 1.0)
        palpath = '-' if pal == '-' else f'textures\\CurseOfAlduin\\fx\\pal_{pal}.dds'
        if col is None:   # (their own colours: only sized)
            run(TOOLS / 'vibtint', art / f'{src}.nif', OUT / 'vw' / f'{look}.nif', -1, -1, -1, sc, '-')
        else:
            run(TOOLS / 'vibtint', art / f'{src}.nif', OUT / 'vw' / f'{look}.nif', r, g, b, sc, palpath)


if __name__ == '__main__':
    main()
