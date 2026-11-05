"""(Noah, the Shadowflame Sword's heavy attack: "Fire cyclone" - a spinning tornado of violet fire travels 10 m forward,
pulling enemies in and burning them, then bursts) the cyclone: three nested funnels of violet flame widening as they rise,
their flame-streaks wound round them and streaming up (a tiling streak texture drawn here), real violet fire licking up
round its foot (tools/alchfx.py's pillar_flame_violet flipbook, 60 fps, on upright camera-facing cards), embers lifting
off it and its light on the ground. Spun, carried and grown by src/Gun.cpp FireCyclone.

The ground at z 0, up +Z.
    python3 tools/firecyclone.py  ->  assets/fx/Meshes/CurseOfAlduin/fx/firecyclone.nif, Textures/.../cyclone_fire.dds"""
import math, pathlib, random, subprocess
import realfire  # (Noah: Skyrim's own fire flipbooks - realistic with Ultimate HD Fire Effects)
import numpy as np
from PIL import Image
from scipy.ndimage import gaussian_filter

ROOT = pathlib.Path(__file__).resolve().parent.parent
MESH = ROOT / 'assets' / 'fx' / 'Meshes' / 'CurseOfAlduin' / 'fx'
TEX = ROOT / 'assets' / 'fx' / 'Textures' / 'CurseOfAlduin' / 'fx'
WORK = ROOT / 'build' / 'firecyclone'
FX = 'CurseOfAlduin\\fx\\'


def texture():
    """tongues of flame streaking along v (the way they flow), ragged and uneven, tiling both ways"""
    W, H = 256, 512
    rng = np.random.default_rng(13)
    n1 = gaussian_filter(rng.random((H, W)), (24, 3), mode='wrap')
    n2 = gaussian_filter(rng.random((H, W)), (10, 1.5), mode='wrap')
    norm = lambda a: (a - a.min()) / (np.ptp(a) + 1e-6)
    f = norm(n1) * 0.7 + norm(n2) * 0.45
    a = np.clip((f - 0.42) * 2.4, 0, 1) ** 1.3
    WORK.mkdir(parents=True, exist_ok=True); TEX.mkdir(parents=True, exist_ok=True)
    png = WORK / 'cyclone_fire.png'
    Image.fromarray((a * 255).astype(np.uint8)).convert('RGB').save(png)
    subprocess.run(['convert', str(png), '-define', 'dds:compression=dxt1', '-define', 'dds:mipmaps=8', str(TEX / 'cyclone_fire.dds')], check=True)


def funnel(L, name, r0, r1, h0, h1, twist, alpha, vscroll, uscale, col, sc):
    L.append(f'shape {name} tex={FX}cyclone_fire.dds color={col},1 scale={sc} blend=add falloff=1 single=0 vscroll={vscroll}')
    na, nh = 40, 16
    for j in range(nh + 1):
        t = j / nh
        h = h0 + (h1 - h0) * t
        r = r0 + (r1 - r0) * t ** 1.3
        fade = min(1.0, t / 0.12) * min(1.0, (1 - t) / 0.3)
        dr = (r1 - r0) * 1.3 * max(t, 1e-3) ** 0.3 / (h1 - h0)
        for i in range(na + 1):
            a = 2 * math.pi * i / na
            n = np.array([math.cos(a), math.sin(a), -dr]); n /= np.linalg.norm(n)
            L.append(f'v {r * math.cos(a):.2f} {r * math.sin(a):.2f} {h:.2f} {uscale * i / na:.4f} {t * 1.4 + twist * i / na:.4f} 1 1 1 {alpha * fade:.3f}')
            L.append(f'n {n[0]:.4f} {n[1]:.4f} {n[2]:.4f}')
    for j in range(nh):
        for i in range(na):
            a = j * (na + 1) + i
            b, c, d = a + 1, a + na + 1, a + na + 2
            L += [f't {a} {c} {b}', f't {b} {c} {d}']


def card(L, name, tex, col, sc, w, h0, h1, at, flip, fps, phase):
    L.append(f'shape {name} tex={tex} color={col},1 scale={sc} blend=add billboard=2 flip={flip[0]}x{flip[1]} fps={fps} phase={phase:.2f} at={at[0]:.1f},{at[1]:.1f},{at[2]:.1f}')
    cu, cv = 1 / flip[0], 1 / flip[1]
    for X, Y, u, v in ((-w, h1, 0, 0), (w, h1, cu, 0), (w, h0, cu, cv), (-w, h0, 0, cv)):
        L.append(f'v {X:.2f} {Y:.2f} 0 {u:.5f} {v:.5f} 1 1 1 1')
    L += ['t 0 1 2', 't 0 2 3']


def disc(L, name, tex, col, sc, r):
    L.append(f'shape {name} tex={tex} color={col},1 scale={sc} blend=add single=0')
    for X, Y, u, v in ((-r, r, 0, 0), (r, r, 1, 0), (r, -r, 1, 1), (-r, -r, 0, 1)):
        L.append(f'v {X:.1f} {Y:.1f} 3 {u} {v} 1 1 1 1'); L.append('n 0 0 1')
    L += ['t 0 1 2', 't 0 2 3']


def build():
    random.seed(77)
    L = [f'out {MESH / "firecyclone.nif"}']
    funnel(L, 'c1', 22, 70, -10, 380, 1.1, 0.9, 0.5, 3, '0.95,0.7,1', 2.2)     # (the hot heart: pale violet, quick)
    funnel(L, 'c2', 36, 110, -10, 450, 1.5, 0.7, 0.75, 4, '0.62,0.25,1', 2.0)
    funnel(L, 'c3', 55, 160, -5, 520, 2.0, 0.45, 1.1, 5, '0.42,0.1,0.85', 1.8)  # (the outer veil: deep violet, slow)
    for i in range(9):   # violet fire licking up round its foot
        a = 2 * math.pi * i / 9 + random.uniform(-0.2, 0.2)
        r = random.uniform(35, 75)
        card(L, f'f{i}', FX + 'pillar_flame_violet.dds', random.choice(('1,1,1', '0.92,0.85,1', '0.85,0.75,1')), 0.9,
             random.uniform(45, 60), -8, random.uniform(150, 230), (r * math.cos(a), r * math.sin(a), 0), (16, 8), random.uniform(54, 64), random.uniform(0, 2.1))
    for i in range(3):   # embers thrown up off it
        card(L, f'e{i}', FX + 'motes_rise.dds', '0.62,0.3,1', 1.5, 110, 0, 560, (0, 0, 20), (16, 16), random.uniform(54, 66), i * 0.7)
    disc(L, 'glow', FX + 'glow_disc.dds', '0.5,0.15,0.95', 1.3, 230)
    jf = WORK / 'firecyclone.job'; WORK.mkdir(parents=True, exist_ok=True); jf.write_text('\n'.join(realfire.fix(L)) + '\n')
    r = subprocess.run(['/home/claude/tools/fxwrite', str(jf)], capture_output=True, text=True)
    print(r.stdout.strip()[-120:], r.stderr.strip()[-300:])
    if r.returncode: raise SystemExit('fxwrite failed')


if __name__ == '__main__':
    texture()
    build()
