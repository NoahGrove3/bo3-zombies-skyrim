"""(Noah, 2026-11-20: "The endless flame braziers need to be removed - make it the Pillar of Time ... give that stone pillar blue
runic design and symbols like the echo altar") High Hrothgar's centre pillar (HHLightPedestal02: a tapering column, 416 high,
~96 across its foot to ~52 near its top) ringed by three bands of blue runes hovering just off its stone - the Echo Altar's own
rune strokes (tools/echoaltar.py RUNES) - and a circle of runes on the floor round its foot; all of light (additive), turned
slowly in src/Echo.cpp. Its origin is the pillar's foot.

    python3 tools/pillarrunes.py  ->  assets/fx/Meshes/CurseOfAlduin/reliquary/pillar_runes.nif,
                                      Textures/CurseOfAlduin/reliquary/pillar_runes.dds"""
import math, pathlib, subprocess, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFilter
sys.path.insert(0, str(pathlib.Path(__file__).parent))
from echoaltar import RUNES

ROOT = pathlib.Path(__file__).resolve().parent.parent
MESH = ROOT / 'assets' / 'fx' / 'Meshes' / 'CurseOfAlduin' / 'reliquary'
TEX = ROOT / 'assets' / 'fx' / 'Textures' / 'CurseOfAlduin' / 'reliquary'
WORK = ROOT / 'build' / 'pillarrunes'
T = 'CurseOfAlduin\\reliquary\\pillar_runes.dds'


def texture():
    """a strip of runes between two thin rules: 2048 x 256, eight runes across, glowing blue on black (added onto the scene)"""
    W, H = 2048, 256
    m = Image.new('L', (W, H), 0)
    d = ImageDraw.Draw(m)
    rng = np.random.default_rng(11)
    d.line([(0, 18), (W, 18)], fill=255, width=5)
    d.line([(0, H - 18), (W, H - 18)], fill=255, width=5)
    d.line([(0, 34), (W, 34)], fill=150, width=2)
    d.line([(0, H - 34), (W, H - 34)], fill=150, width=2)
    n = 16
    cw = W / n
    for i in range(n):
        x0, y0, s = i * cw + cw * 0.22, 58, cw * 0.56
        hgt = H - 116
        for (a, b, c, e) in RUNES[int(rng.integers(len(RUNES)))]:
            d.line([(x0 + a * s, y0 + b * hgt), (x0 + c * s, y0 + e * hgt)], fill=255, width=9)
        d.ellipse([i * cw - 5, H / 2 - 5, i * cw + 5, H / 2 + 5], fill=200)   # (a dot between each)
    a = np.asarray(m, np.float32) / 255
    glow = np.asarray(m.filter(ImageFilter.GaussianBlur(9)), np.float32) / 255
    v = np.clip(np.maximum(a, glow * 0.9), 0, 1)[..., None]
    rgb = np.clip(v * np.array([70, 165, 255]) + (a ** 2)[..., None] * np.array([150, 200, 255]), 0, 255).astype(np.uint8)
    WORK.mkdir(parents=True, exist_ok=True); TEX.mkdir(parents=True, exist_ok=True)
    png = WORK / 'pillar_runes.png'
    Image.fromarray(rgb).save(png)
    subprocess.run(['convert', str(png), '-define', 'dds:compression=dxt1', '-define', 'dds:mipmaps=9', str(TEX / 'pillar_runes.dds')], check=True)


def band(L, name, z0, z1, r0, r1, reps, segs=72):
    """a ring of runes standing round the pillar from z0 (radius r0) to z1 (radius r1)"""
    L.append(f'shape {name} tex={T} color=0.45,0.7,1,1 scale=2.0 pulse=10,0.45,2.4 blend=add single=0')
    for k in range(segs + 1):
        a = 2 * math.pi * k / segs
        for j, (z, r) in enumerate(((z0, r0), (z1, r1))):
            L.append(f'v {r * math.cos(a):.2f} {r * math.sin(a):.2f} {z:.1f} {k / segs * reps:.4f} {1 - j:.4f} 1 1 1 1')
            L.append(f'n {math.cos(a):.3f} {math.sin(a):.3f} 0')
    for k in range(segs):
        a0, b0 = 2 * k, 2 * k + 2
        L += [f't {a0} {b0} {b0 + 1}', f't {a0} {b0 + 1} {a0 + 1}']


def floor(L, r0, r1, reps, segs=96):
    """the circle of runes on the floor round the pillar's foot"""
    L.append(f'shape floor tex={T} color=0.45,0.7,1,1 scale=1.6 pulse=10,0.35,1.9 blend=add single=0')
    for k in range(segs + 1):
        a = 2 * math.pi * k / segs
        for j, r in enumerate((r1, r0)):
            L.append(f'v {r * math.cos(a):.2f} {r * math.sin(a):.2f} 1.5 {k / segs * reps:.4f} {j:.4f} 1 1 1 1')
            L.append('n 0 0 1')
    for k in range(segs):
        a0, b0 = 2 * k, 2 * k + 2
        L += [f't {a0} {b0} {b0 + 1}', f't {a0} {b0 + 1} {a0 + 1}']


def main():
    texture()
    MESH.mkdir(parents=True, exist_ok=True)
    L = [f'out {MESH / "pillar_runes.nif"}']
    band(L, 'low', 58, 92, 101, 96, 3)       # (the column: r ~96 at z 40, ~89 at 90, ~79 at 140, ~71 at 190, ~60 at 240, ~52 at 280)
    band(L, 'mid', 150, 178, 85, 80, 2)
    band(L, 'high', 244, 266, 66, 63, 2)
    floor(L, 118, 158, 4)
    jf = WORK / 'pillar_runes.job'; jf.write_text('\n'.join(L) + '\n')
    r = subprocess.run(['/home/claude/tools/fxwrite', str(jf)], capture_output=True, text=True)
    print(r.stdout.strip()[-200:], r.stderr.strip()[-300:])
    if r.returncode: raise SystemExit('fxwrite failed')


if __name__ == '__main__':
    main()
