"""(Noah, the heavy attacks redone: "Sunlight pillar + ring", "Lightning dome + chains", "Elemental shockwave line") the
models the class swords' Tier II heavy attacks stand up where they land - each drawn whole at scale 1 and grown, held and
faded by src/Gun.cpp Grow:

  sunpillar.nif     a column of white sunlight from high above down onto the ground, a bright core in a soft shaft,
                    sunlight shimmering up round its foot and motes of light lifting off it (the Summoner)
  ldome.nif         a dome of crackling blue lightning 100 units across: veins of light crawling over it, brightest at
                    its rim (the Destruction sword)
  spike_frost.nif   a cluster of ice spikes bursting up out of the ground, a cold glint along their edges
  spike_blood.nif   the same in dark crimson (the Spellsword's blood line)

The ground at z 0, up +Z.
    python3 tools/heavyfx.py  ->  assets/fx/Meshes/CurseOfAlduin/fx/..., Textures/CurseOfAlduin/fx/sunbeam.dds, dome_shock.dds, spike.dds"""
import math, pathlib, random, subprocess
import numpy as np
from PIL import Image
from scipy.ndimage import gaussian_filter

ROOT = pathlib.Path(__file__).resolve().parent.parent
MESH = ROOT / 'assets' / 'fx' / 'Meshes' / 'CurseOfAlduin' / 'fx'
TEX = ROOT / 'assets' / 'fx' / 'Textures' / 'CurseOfAlduin' / 'fx'
WORK = ROOT / 'build' / 'heavyfx'
FX = 'CurseOfAlduin\\fx\\'


def dds(a, name, alpha=None):
    WORK.mkdir(parents=True, exist_ok=True); TEX.mkdir(parents=True, exist_ok=True)
    png = WORK / (name + '.png')
    if alpha is None:
        Image.fromarray((np.clip(a, 0, 1) * 255).astype(np.uint8)).convert('RGB').save(png)
        comp = 'dxt1'
    else:
        rgb = np.dstack([np.clip(a, 0, 1)] * 3) if a.ndim == 2 else np.clip(a, 0, 1)
        Image.fromarray((np.dstack([rgb, np.clip(alpha, 0, 1)]) * 255).astype(np.uint8), 'RGBA').save(png)
        comp = 'dxt5'
    subprocess.run(['convert', str(png), '-define', f'dds:compression={comp}', '-define', 'dds:mipmaps=8', str(TEX / (name + '.dds'))], check=True)


def textures():
    # the sunbeam: soft across, light shafts running its length, fading out at its top and soft at its foot
    W, H = 128, 512
    u = np.linspace(-1, 1, W)[None, :]; v = np.linspace(0, 1, H)[:, None]   # v 0: the top .. 1: the ground
    rng = np.random.default_rng(5)
    shafts = gaussian_filter(rng.random((1, W)), (0, 2.5), mode='wrap')
    shafts = (shafts - shafts.min()) / (np.ptp(shafts) + 1e-6)
    across = np.exp(-(u / 0.5) ** 2) * (0.6 + 0.4 * shafts) + 0.6 * np.exp(-(u / 0.12) ** 2)
    along = np.clip(v / 0.45, 0, 1) ** 1.4 * np.clip((1 - v) / 0.04, 0, 1)
    dds(across * along, 'sunbeam')
    # the dome's lightning: 16 frames (4x4) of veins - the zero lines of a smooth noise - crawling and flickering
    n, cols = 256, 4
    at = np.zeros((n * cols, n * cols))
    yy = np.linspace(0, 1, n)[:, None]
    for f in range(16):
        r = np.random.default_rng(100 + f)
        a = gaussian_filter(r.random((n, n)), 18, mode='wrap'); a = (a - a.mean()) / (a.std() + 1e-6)
        b = gaussian_filter(r.random((n, n)), 7, mode='wrap'); b = (b - b.mean()) / (b.std() + 1e-6)
        veins = np.exp(-(a / 0.06) ** 2) * 0.95 + np.exp(-(b / 0.05) ** 2) * 0.3 * (np.abs(a) < 0.35)
        glow = gaussian_filter(veins, 3, mode='wrap') * 1.6
        img = np.clip(veins + glow + 0.06, 0, 1) * np.clip(yy / 0.04, 0, 1)   # (v 0 at the dome's top)
        cy, cx = divmod(f, cols)
        at[cy * n:(cy + 1) * n, cx * n:(cx + 1) * n] = img
    dds(at, 'dome_shock')
    # the spikes: pale along their length, bright down their edges, almost clear at the tip
    W, H = 64, 256
    u = np.linspace(0, 1, W)[None, :]; v = np.linspace(0, 1, H)[:, None]   # v 0: the tip .. 1: the foot
    facet = 0.55 + 0.45 * np.abs(np.sin(u * math.pi * 6)) ** 4
    lum = np.clip(0.45 + 0.55 * facet * (0.6 + 0.4 * v), 0, 1)
    alpha = np.clip(0.55 + 0.4 * v, 0, 1) * np.clip(v / 0.05, 0, 1) * np.ones_like(u)
    dds(lum * np.ones_like(v), 'spike', alpha)


def card(L, name, tex, col, sc, w, h0, h1, at, flip=None, fps=60, phase=0.0, bb=2):
    L.append(f'shape {name} tex={tex} color={col},1 scale={sc} blend=add billboard={bb} ' + (f'flip={flip[0]}x{flip[1]} fps={fps} phase={phase:.2f} ' if flip else '') + f'at={at[0]:.1f},{at[1]:.1f},{at[2]:.1f}')
    cu, cv = (1 / flip[0], 1 / flip[1]) if flip else (1, 1)
    for X, Y, uu, vv in ((-w, h1, 0, 0), (w, h1, cu, 0), (w, h0, cu, cv), (-w, h0, 0, cv)):
        L.append(f'v {X:.2f} {Y:.2f} 0 {uu:.5f} {vv:.5f} 1 1 1 1')
    L += ['t 0 1 2', 't 0 2 3']


def disc(L, name, tex, col, sc, r, z=3):
    L.append(f'shape {name} tex={tex} color={col},1 scale={sc} blend=add single=0')
    for X, Y, uu, vv in ((-r, r, 0, 0), (r, r, 1, 0), (r, -r, 1, 1), (-r, -r, 0, 1)):
        L.append(f'v {X:.1f} {Y:.1f} {z} {uu} {vv} 1 1 1 1'); L.append('n 0 0 1')
    L += ['t 0 1 2', 't 0 2 3']


def write(L, name):
    WORK.mkdir(parents=True, exist_ok=True)
    jf = WORK / (name + '.job'); jf.write_text('\n'.join([f'out {MESH / name}'] + L) + '\n')
    r = subprocess.run(['/home/claude/tools/fxwrite', str(jf)], capture_output=True, text=True)
    print(name, r.stdout.strip()[-100:], r.stderr.strip()[-300:])
    if r.returncode: raise SystemExit('fxwrite failed')


def sunpillar():
    random.seed(11)
    L = []
    card(L, 'shaft', FX + 'sunbeam.dds', '1,0.93,0.72', 1.4, 135, -10, 1700, (0, 0, 0))
    card(L, 'core', FX + 'sunbeam.dds', '1,1,0.95', 2.6, 48, -10, 1700, (0, 0, 0))
    for i in range(7):   # sunlight shimmering up round its foot
        a = 2 * math.pi * i / 7
        card(L, f'g{i}', FX + 'ground_sun.dds', '1,0.9,0.6', 1.6, 75, -6, 170, (55 * math.cos(a), 55 * math.sin(a), 0), (16, 8), 60, random.uniform(0, 2.1))
    for i in range(3):
        card(L, f'm{i}', FX + 'motes_rise.dds', '1,0.92,0.6', 1.4, 90, 0, 420, (0, 0, 20), (16, 16), 60, i * 0.7)
    disc(L, 'pool', FX + 'glow_disc.dds', '1,0.88,0.55', 1.6, 260)
    write(L, 'sunpillar.nif')


def ldome():
    L = ['shape dome tex=' + FX + 'dome_shock.dds color=0.45,0.7,1,1 scale=2.4 blend=add rim=1 single=0 flip=4x4 fps=20 phase=0']
    segs, rows, R = 40, 12, 50.0
    for k in range(segs + 1):
        a = 2 * math.pi * k / segs
        for j in range(rows + 1):
            el = (math.pi / 2) * (1 - j / rows)   # j 0: the top .. rows: the ground
            x, y, z = R * math.cos(el) * math.cos(a), R * math.cos(el) * math.sin(a), R * math.sin(el)
            L.append(f'v {x:.2f} {y:.2f} {z:.2f} {k / segs * 0.25:.4f} {j / rows * 0.25:.4f} 1 1 1 1')
            L.append(f'n {x / R:.3f} {y / R:.3f} {z / R:.3f}')
    for k in range(segs):
        for j in range(rows):
            a0 = k * (rows + 1) + j; b0 = a0 + rows + 1
            L += [f't {a0} {b0} {b0 + 1}', f't {a0} {b0 + 1} {a0 + 1}']
    disc(L, 'floor', FX + 'glow_disc.dds', '0.4,0.65,1', 1.5, 70)
    write(L, 'ldome.nif')


def cone(L, name, tex, col, sc, blend, extra, base, h, r, tilt, yaw, grow=1.0):
    L.append(f'shape {name} tex={tex} color={col},1 scale={sc} blend={blend} single=0 {extra}')
    segs = 6
    ct, st, cy, sy = math.cos(tilt), math.sin(tilt), math.cos(yaw), math.sin(yaw)
    def tf(x, y, z):   # tilted about the spike's yaw axis, out from the cluster's middle
        x2, z2 = x * ct + z * st, -x * st + z * ct
        return base[0] + x2 * cy - y * sy, base[1] + x2 * sy + y * cy, base[2] + z2
    tip = tf(0, 0, h * grow)
    for k in range(segs + 1):
        a = 2 * math.pi * k / segs
        x, y = r * grow * math.cos(a), r * grow * math.sin(a)
        p0 = tf(x, y, -10)
        nx, ny = math.cos(a), math.sin(a)
        L.append(f'v {p0[0]:.2f} {p0[1]:.2f} {p0[2]:.2f} {k / segs:.4f} 1 1 1 1 1'); L.append(f'n {nx:.3f} {ny:.3f} 0.2')
        L.append(f'v {tip[0]:.2f} {tip[1]:.2f} {tip[2]:.2f} {k / segs:.4f} 0 1 1 1 1'); L.append(f'n {nx:.3f} {ny:.3f} 0.2')
    for k in range(segs):
        a = 2 * k
        L += [f't {a} {a + 2} {a + 1}', f't {a + 2} {a + 3} {a + 1}']


def spikes(key, col, glow):
    random.seed(31)
    L = []
    for i in range(7):
        a = 2 * math.pi * i / 7 + random.uniform(-0.3, 0.3)
        d = 0 if i == 0 else random.uniform(18, 55)
        base = (d * math.cos(a), d * math.sin(a), 0)
        h = random.uniform(150, 210) if i == 0 else random.uniform(70, 150)
        r = random.uniform(16, 24) if i == 0 else random.uniform(9, 17)
        tilt = 0 if i == 0 else random.uniform(0.2, 0.55)
        cone(L, f's{i}', FX + 'spike.dds', col, 1.0, 'alpha', '', base, h, r, tilt, a)
        cone(L, f'r{i}', FX + 'spike.dds', glow, 1.6, 'add', 'rim=1', base, h, r, tilt, a, 1.06)
    disc(L, 'frost', FX + 'glow_disc.dds', glow, 1.0, 110)
    write(L, f'spike_{key}.nif')


if __name__ == '__main__':
    textures()
    sunpillar()
    ldome()
    spikes('frost', '0.62,0.85,1', '0.5,0.8,1')
    spikes('blood', '0.55,0.04,0.06', '0.85,0.08,0.1')
