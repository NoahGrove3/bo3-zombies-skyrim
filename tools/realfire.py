"""(Noah: "use the flame effect mod we downloaded that makes it look realistic for all fire related effects in the curse of
alduin") our fire drawn with Skyrim's own fire flipbooks - the ones Ultimate HD Fire Effects (Nexus 20654) replaces with filmed
fire - instead of our hand-drawn ones. We only name the game's own texture paths: with the mod installed they're its realistic
fire, without it Skyrim's vanilla fire (never ours to ship). Skyrim's fire flipbooks are greyscale; each is coloured here by a
greyscale-to-palette ramp of ours (grad_fire / grad_violet / grad_gold, drawn by ramps() below), the same way Skyrim colours
its own fire (and the way src/Movement.cpp TintBreath colours the dragon's breath).

    fix(lines) -> lines     rewrites every fxwrite `shape` on one of our fire flipbooks (and the uvs of its vertices) onto
                            the game's: the fire puffs (alch_fire, the altar bows' arrow fire) onto fxfireanim04loop (8x4),
                            the tall flame sheets (pillar_flame, pillar_flame_violet) onto fxfirecolumnanimloop (8x8)
    python3 tools/realfire.py   ->  assets/fx/Textures/CurseOfAlduin/fx/grad_<fire|violet|gold>.dds"""
import pathlib, re
import numpy as np
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parent.parent
TEX = ROOT / 'assets' / 'fx' / 'Textures' / 'CurseOfAlduin' / 'fx'
PAL = 'CurseOfAlduin\\fx\\grad_{}.dds'

# ours -> the game's: (its texture, its grid, our grid, the speed it plays at)
SWAP = {
    'alch_fire.dds':            ('effects\\fxfireanim04loop.dds', (8, 4), (16, 8), 30),
    'emberstring_arrowfx.dds':  ('effects\\fxfireanim04loop.dds', (8, 4), (8, 8), 30),
    'ground_fire.dds':          ('effects\\fxfirecolumnanimloop.dds', (8, 8), (16, 8), 30),
    'pillar_flame.dds':         ('effects\\fxfirecolumnanimloop.dds', (8, 8), (16, 8), 30),
    'pillar_flame_violet.dds':  ('effects\\fxfirecolumnanimloop.dds', (8, 8), (16, 8), 30),
}

# (position along the ramp - the flipbook's brightness - and colour) dark at the flame's smoky edge to its white-hot heart;
# every row the same, so it doesn't matter which row the game reads
RAMPS = {
    'fire':   [(0.0, (0, 0, 0)), (0.18, (60, 8, 0)), (0.4, (190, 50, 6)), (0.62, (250, 130, 25)), (0.82, (255, 205, 110)), (1.0, (255, 245, 220))],
    'violet': [(0.0, (0, 0, 0)), (0.18, (30, 0, 60)), (0.4, (100, 20, 200)), (0.62, (155, 70, 255)), (0.82, (205, 150, 255)), (1.0, (245, 230, 255))],
    'gold':   [(0.0, (0, 0, 0)), (0.18, (60, 30, 0)), (0.4, (200, 120, 10)), (0.62, (250, 190, 40)), (0.82, (255, 225, 120)), (1.0, (255, 250, 225))],
    # (the effects map, src/Gun.cpp FxMapLoad: Skyrim's own effects coloured for each sword and element)
    'blue':   [(0.0, (0, 0, 0)), (0.18, (0, 14, 60)), (0.4, (20, 70, 210)), (0.62, (70, 140, 255)), (0.82, (160, 205, 255)), (1.0, (235, 245, 255))],
    'white':  [(0.0, (0, 0, 0)), (0.18, (40, 38, 30)), (0.4, (150, 145, 125)), (0.62, (215, 210, 190)), (0.82, (245, 242, 230)), (1.0, (255, 255, 255))],
    'red':    [(0.0, (0, 0, 0)), (0.18, (55, 0, 2)), (0.4, (180, 12, 14)), (0.62, (240, 50, 40)), (0.82, (255, 140, 120)), (1.0, (255, 230, 220))],
    'frost':  [(0.0, (0, 0, 0)), (0.18, (10, 30, 50)), (0.4, (70, 150, 200)), (0.62, (140, 210, 245)), (0.82, (205, 240, 255)), (1.0, (245, 252, 255))],
    'shock':  [(0.0, (0, 0, 0)), (0.18, (10, 20, 70)), (0.4, (60, 110, 240)), (0.62, (130, 180, 255)), (0.82, (200, 225, 255)), (1.0, (250, 252, 255))],
    'sun':    [(0.0, (0, 0, 0)), (0.18, (60, 40, 5)), (0.4, (220, 160, 50)), (0.62, (255, 215, 110)), (0.82, (255, 240, 190)), (1.0, (255, 255, 245))],
    'blood':  [(0.0, (0, 0, 0)), (0.18, (35, 0, 0)), (0.4, (120, 4, 8)), (0.62, (185, 15, 20)), (0.82, (230, 70, 60)), (1.0, (255, 190, 170))],
    'poison': [(0.0, (0, 0, 0)), (0.18, (10, 40, 0)), (0.4, (60, 150, 20)), (0.62, (120, 210, 50)), (0.82, (190, 240, 130)), (1.0, (240, 255, 220))],
}


def ramps():
    TEX.mkdir(parents=True, exist_ok=True)
    x = (np.arange(256) + 0.5) / 256
    for name, stops in RAMPS.items():
        row = np.stack([np.interp(x, [s[0] for s in stops], [s[1][c] for s in stops]) for c in range(3)], -1)
        img = np.zeros((4, 256, 4), np.uint8)
        img[..., :3] = row[None].astype(np.uint8)
        img[..., 3] = 255
        Image.fromarray(img, 'RGBA').save(TEX / f'grad_{name}.dds')   # (uncompressed: a ramp must not band)
        print(TEX / f'grad_{name}.dds')


def _palette(src, col):
    if 'violet' in src: return 'violet'
    r, g, b = col
    if b > 0.6 * r and b > g: return 'violet'
    if g > 0.68 * r and b < 0.55 * r: return 'gold'
    return 'fire'


def fix(lines, palette=None):
    """palette: 'fire' | 'violet' | 'gold' for every fire card in these lines (else guessed from each card's colour)"""
    out, scale = [], None
    for ln in lines:
        if ln.startswith('shape '):
            scale = None
            m = re.search(r'tex=(\S+)', ln)
            key = m.group(1).split('\\')[-1].lower() if m else ''
            if key in SWAP:
                tex, grid, ours, fps = SWAP[key]
                cm = re.search(r'color=([\d.]+),([\d.]+),([\d.]+)', ln)
                col = tuple(float(c) for c in cm.groups()) if cm else (1, 1, 1)
                pal = palette or _palette(key, col)
                ln = ln.replace(m.group(0), f'tex={tex} palette={PAL.format(pal)}')
                if cm:   # (the ramp gives the colour: what tint there was is kept only as brightness)
                    lum = max(col)
                    ln = ln.replace(cm.group(0), f'color={lum:.2f},{lum:.2f},{lum:.2f}')
                ln = re.sub(r'flip=\d+x\d+', f'flip={grid[0]}x{grid[1]}', ln)
                ln = re.sub(r'fps=[\d.]+', f'fps={fps}', ln)
                scale = (ours[0] / grid[0], ours[1] / grid[1])
        elif ln.startswith('v ') and scale:
            p = ln.split()
            p[4] = f'{float(p[4]) * scale[0]:.5f}'
            p[5] = f'{float(p[5]) * scale[1]:.5f}'
            ln = ' '.join(p)
        out.append(ln)
    return out


if __name__ == '__main__':
    ramps()
