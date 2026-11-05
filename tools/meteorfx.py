"""(Noah: Alduin's Bane - "heavy attacks send a medium size meteor in 5m in front of the player"; the Dragon Slayer - "heavy attack
should rain golden fire meteors on any enemies within 30 meters") a falling meteor: a white-hot heart in a churning ball of
flame (tools/alchfx.py's fire flipbook, 60 fps, on camera-facing cards) and a long tail of fire streaming behind it. Flown
by src/Gun.cpp Lob - its +Y along its flight, so the tail runs back along -Y.

    python3 tools/meteorfx.py  ->  assets/fx/Meshes/CurseOfAlduin/fx/meteor_<fire|gold>.nif, Textures/.../meteor_tail.dds"""
import math, pathlib, random, subprocess
import realfire  # (Noah: Skyrim's own fire flipbooks - realistic with Ultimate HD Fire Effects)
import numpy as np
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parent.parent
MESH = ROOT / 'assets' / 'fx' / 'Meshes' / 'CurseOfAlduin' / 'fx'
TEX = ROOT / 'assets' / 'fx' / 'Textures' / 'CurseOfAlduin' / 'fx'
WORK = ROOT / 'build' / 'meteorfx'
FX = 'CurseOfAlduin\\fx\\'
COL = {   # heart, flame tints, tail
    'fire': ('1,0.9,0.7', ('1,0.55,0.15', '1,0.42,0.08', '1,0.65,0.25'), '1,0.4,0.08'),
    'gold': ('1,0.97,0.8', ('1,0.82,0.3', '1,0.72,0.18', '1,0.9,0.45'), '1,0.72,0.2'),
}


def tail_texture():
    """streaks of fire along the tail (u: 0 at the meteor .. 1 at the end), soft across it (v)"""
    W, H = 512, 128
    u = np.linspace(0, 1, W)[None, :]; v = np.linspace(-1, 1, H)[:, None]
    rng = np.random.default_rng(7)
    streak = np.zeros((H, W))
    for _ in range(140):
        y = rng.normal(0, 0.35); w = rng.uniform(0.02, 0.07); ln = rng.uniform(0.3, 1.0); b = rng.uniform(0.3, 1.0)
        streak += b * np.exp(-((v - y) / w) ** 2) * np.clip(1 - u / ln, 0, 1) ** 1.5
    body = np.exp(-(v / (0.25 + 0.5 * u)) ** 2) * (1 - u) ** 1.8
    a = np.clip(body * 0.8 + streak * 0.35 * (1 - u), 0, 1)
    WORK.mkdir(parents=True, exist_ok=True); TEX.mkdir(parents=True, exist_ok=True)
    png = WORK / 'meteor_tail.png'
    Image.fromarray((a * 255).astype(np.uint8)).convert('RGB').save(png)
    subprocess.run(['convert', str(png), '-define', 'dds:compression=dxt1', '-define', 'dds:mipmaps=8', str(TEX / 'meteor_tail.dds')], check=True)


def card(L, name, tex, col, sc, w, at, phase, flip=(16, 8), fps=60, bb=1):
    L.append(f'shape {name} tex={tex} color={col},1 scale={sc} blend=add billboard={bb} ' + (f'flip={flip[0]}x{flip[1]} fps={fps} phase={phase:.2f} ' if flip else '') + f'at={at[0]:.1f},{at[1]:.1f},{at[2]:.1f}')
    cu, cv = (1 / flip[0], 1 / flip[1]) if flip else (1, 1)
    for X, Y, u, v in ((-w, w, 0, 0), (w, w, cu, 0), (w, -w, cu, cv), (-w, -w, 0, cv)):
        L.append(f'v {X:.2f} {Y:.2f} 0 {u:.5f} {v:.5f} 1 1 1 1')
    L += ['t 0 1 2', 't 0 2 3']


def tail(L, name, col, sc, length, width, ang):
    L.append(f'shape {name} tex={FX}meteor_tail.dds color={col},1 scale={sc} blend=add single=0 falloff=1')
    c, s = math.cos(ang), math.sin(ang)
    segs = 8
    for k in range(segs + 1):
        f = k / segs
        y = -length * f
        w = width * (1 - 0.6 * f)
        for j, side in enumerate((-1, 1)):
            x, z = side * w * c, side * w * s
            L.append(f'v {x:.2f} {y:.2f} {z:.2f} {f:.3f} {j:.0f} 1 1 1 1')
            L.append(f'n {-s:.3f} 0 {c:.3f}')
    for k in range(segs):
        a = 2 * k
        L += [f't {a} {a + 2} {a + 3}', f't {a} {a + 3} {a + 1}']


def build(key):
    heart, flames, tcol = COL[key]
    random.seed(7 + len(key))
    L = [f'out {MESH / f"meteor_{key}.nif"}']
    card(L, 'heart', FX + 'glow_disc.dds', heart, 2.2, 34, (0, 0, 0), 0, flip=None)
    card(L, 'halo', FX + 'glow_disc.dds', flames[0], 0.9, 90, (0, -10, 0), 0, flip=None)
    for i in range(7):   # the ball of fire round it
        a = 2 * math.pi * i / 7
        card(L, f'f{i}', FX + 'alch_fire.dds', random.choice(flames), 1.5, random.uniform(38, 52),
             (12 * math.cos(a), -10 - 18 * random.random(), 12 * math.sin(a)), random.uniform(0, 2.1))
    for i, ang in enumerate((0.0, math.pi / 3, 2 * math.pi / 3)):   # the tail, three planes about its line
        tail(L, f't{i}', tcol, 1.6, 420.0, 44.0, ang)
    for i in range(4):   # sparks thrown off it
        card(L, f's{i}', FX + 'sparks_burst.dds', flames[0], 1.1, 60, (0, -60 - 70 * i, 0), i * 0.5, flip=(16, 8), fps=30)
    jf = WORK / f'meteor_{key}.job'; jf.write_text('\n'.join(realfire.fix(L)) + '\n')
    r = subprocess.run(['/home/claude/tools/fxwrite', str(jf)], capture_output=True, text=True)
    print(key, r.stdout.strip()[-120:], r.stderr.strip()[-300:])
    if r.returncode: raise SystemExit('fxwrite failed')


if __name__ == '__main__':
    tail_texture()
    for k in COL: build(k)
