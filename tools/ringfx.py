"""(Noah: the heavy attacks redone - "Sunlight pillar + ring", "Big single impacts ... a shockwave ring across the ground") a
shockwave ring lying on the ground: a bright band of light with a soft glow inside it, 100 units across at scale 1 -
src/Gun.cpp Ring grows it outward and fades it. One model per colour.

    python3 tools/ringfx.py  ->  assets/fx/Meshes/CurseOfAlduin/fx/ring_<colour>.nif, Textures/CurseOfAlduin/fx/ring_band.dds"""
import math, pathlib, subprocess
import numpy as np
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parent.parent
MESH = ROOT / 'assets' / 'fx' / 'Meshes' / 'CurseOfAlduin' / 'fx'
TEX = ROOT / 'assets' / 'fx' / 'Textures' / 'CurseOfAlduin' / 'fx'
WORK = ROOT / 'build' / 'ringfx'
FX = 'CurseOfAlduin\\fx\\'
COL = {'white': '1,0.95,0.8', 'blue': '0.35,0.62,1', 'gold': '1,0.72,0.2', 'fire': '1,0.42,0.08', 'purple': '0.62,0.25,1',
       'frost': '0.55,0.85,1', 'shock': '0.6,0.5,1', 'blood': '0.75,0.05,0.06', 'sun': '1,0.85,0.35'}


def texture():
    W, H = 512, 128
    v = np.linspace(0, 1, H)[:, None]   # 0: the outer edge .. 1: the centre
    rng = np.random.default_rng(3)
    grain = np.clip(0.75 + 0.25 * np.repeat(rng.random((1, W)), H, 0), 0, 1)
    band = np.exp(-((v - 0.06) / 0.035) ** 2) + 0.35 * np.exp(-((v - 0.12) / 0.09) ** 2)
    fill = 0.18 * np.clip(v * 1.6, 0, 1) * (1 - v) * 2.2
    a = np.clip((band * grain + fill), 0, 1)
    WORK.mkdir(parents=True, exist_ok=True); TEX.mkdir(parents=True, exist_ok=True)
    png = WORK / 'ring_band.png'
    Image.fromarray((a * 255).astype(np.uint8)).convert('RGB').save(png)
    subprocess.run(['convert', str(png), '-define', 'dds:compression=dxt1', '-define', 'dds:mipmaps=8', str(TEX / 'ring_band.dds')], check=True)


def build(key, col):
    L = [f'out {MESH / f"ring_{key}.nif"}', f'shape ring tex={FX}ring_band.dds color={col},1 scale=2.2 blend=add single=0']
    segs, rows, R = 64, 6, 50.0
    for k in range(segs + 1):
        a = 2 * math.pi * k / segs
        for j in range(rows + 1):
            g = j / rows
            r = R * (1 - g)
            L.append(f'v {r * math.cos(a):.2f} {r * math.sin(a):.2f} 2 {k / segs * 4:.4f} {g:.4f} 1 1 1 1')
            L.append('n 0 0 1')
    for k in range(segs):
        for j in range(rows):
            a0 = k * (rows + 1) + j
            b0 = a0 + rows + 1
            L += [f't {a0} {b0} {b0 + 1}', f't {a0} {b0 + 1} {a0 + 1}']
    jf = WORK / f'ring_{key}.job'; jf.write_text('\n'.join(L) + '\n')
    r = subprocess.run(['/home/claude/tools/fxwrite', str(jf)], capture_output=True, text=True)
    print(key, r.stdout.strip()[-100:], r.stderr.strip()[-200:])
    if r.returncode: raise SystemExit('fxwrite failed')


if __name__ == '__main__':
    texture()
    for k, c in COL.items(): build(k, c)
