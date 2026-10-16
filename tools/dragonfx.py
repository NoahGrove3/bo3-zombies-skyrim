"""The glider dragon's wind (src/Movement.cpp: DragonWind): a streak of air off each wingtip, and rushing streaks round
it in a Bond III dive - unlit additive strips (tools/modelconv/fxwrite) hung under the dragon's own bones, so they move
with it; the game sets how bright they are each frame (0 in the nif: unseen until it does).
    python tools/dragonfx.py <in.nif> <out.nif> <Textures dir>
Writes Textures/CurseOfAlduin/Dragon/wind.dds (a soft, streaky band, tiling along its length) and <out.nif>: <in.nif>
with DG_TrailL / DG_TrailR (under DG_WingL3 / R3, from the wingtip back) and DG_Wind1..8 (under DG_Bank)."""
import subprocess, sys, pathlib, math
import numpy as np
from PIL import Image
sys.path.insert(0, str(pathlib.Path(__file__).parent))
from dragontex import dds

FXWRITE = '/home/claude/tools/fxwrite'
TEX = r'textures\CurseOfAlduin\Dragon\wind.dds'
# bones (tools/modelconv/dragonglider.cpp): a shape under a bone is in the dragon's space less the bone's place
WRIST = {'L': (-530, -1245, 180), 'R': (530, -1245, 180)}
TIP = {'L': (-886, -1258, 46), 'R': (886, -1258, 46)}   # the wing's outermost point (its bind pose)
BANK = (0, -1450, 180)


def texture(out):
    W, H = 64, 256
    u = (np.arange(W) + 0.5) / W
    v = (np.arange(H) + 0.5) / H
    rng = np.random.default_rng(7)
    band = np.zeros(H)
    for f in (3, 5, 8, 13, 21):   # (whole cycles: it tiles as it scrolls)
        band += rng.uniform(0.4, 1.0) / math.sqrt(f) * np.sin(2 * math.pi * (f * v + rng.uniform()))
    band = 0.55 + 0.45 * (band - band.min()) / (band.max() - band.min())
    across = np.exp(-((u - 0.5) / 0.2) ** 2)
    # a few finer threads of air inside the band, drifting
    thread = np.zeros((H, W))
    for k in range(5):
        c = 0.5 + rng.uniform(-0.25, 0.25)
        f = int(rng.integers(2, 7))
        ph = rng.uniform()
        cc = c + 0.04 * np.sin(2 * math.pi * (f * v[:, None] + ph))
        thread += 0.35 * np.exp(-((u[None, :] - cc) / 0.03) ** 2) * (0.5 + 0.5 * np.sin(2 * math.pi * (int(rng.integers(4, 11)) * v[:, None] + ph)))
    a = np.clip(across[None, :] * band[:, None] * 0.75 + thread * across[None, :], 0, 1)
    img = np.zeros((H, W, 4), np.uint8)
    img[..., :3] = 255
    img[..., 3] = (a * 255).astype(np.uint8)
    dds(Image.fromarray(img, 'RGBA'), out, 'DXT5')


def strip(lines, name, parent, p0, p1, w0, w1, a0, a1, color, scroll, segs=8, fade_in=0.0):
    """a ribbon from p0 to p1 (two quads crossed, so it reads from any side), w0 -> w1 wide, alpha a0 -> a1 (and in from
    0 over the first fade_in of it)"""
    lines.append(f'shape {name} tex={TEX} color={color} scale=0 blend=add vscroll={scroll} parent={parent}')
    P0, P1 = np.array(p0, float), np.array(p1, float)
    d = (P1 - P0) / np.linalg.norm(P1 - P0)
    side = np.cross(d, [0, 0, 1.0]); side /= np.linalg.norm(side)
    up = np.cross(side, d)
    n = 0
    tris = []
    for ax in (side, up):
        base = n
        for i in range(segs + 1):
            t = i / segs
            p = P0 + (P1 - P0) * t
            w = w0 + (w1 - w0) * t
            al = a0 + (a1 - a0) * t
            if fade_in > 0:
                al *= min(1.0, t / fade_in)
            for s in (-1, 1):
                q = p + ax * (w / 2 * s)
                lines.append(f'v {q[0]:.2f} {q[1]:.2f} {q[2]:.2f} {0.5 + 0.5 * s:.3f} {t * 2.0:.3f} 1 1 1 {al:.3f}')
                n += 1
        for i in range(segs):
            a, b, c, e = base + 2 * i, base + 2 * i + 1, base + 2 * i + 2, base + 2 * i + 3
            tris += [(a, b, c), (b, e, c)]
    for t in tris:
        lines.append('t %d %d %d' % t)


def main():
    src, out, texdir = sys.argv[1], sys.argv[2], pathlib.Path(sys.argv[3])
    texture(texdir / 'CurseOfAlduin' / 'Dragon' / 'wind.dds')
    L = [f'out {out}', f'base {src}']
    for sd in ('L', 'R'):
        w, tip = np.array(WRIST[sd]), np.array(TIP[sd])
        p0 = tip - w
        p1 = p0 + np.array([0, -900, 10])
        strip(L, f'DG_Trail{sd}', f'DG_Wing{sd}3', p0, p1, 18, 64, 1.0, 0.0, '0.85,0.93,1.0,1.0', 0.7, segs=10, fade_in=0.04)
    # the dive: streaks rushing past round it (and round you, under it), from ahead of its head to well behind you
    b = np.array(BANK)
    spots = [(-330, 140), (330, 140), (-560, -40), (560, -40), (-240, -260), (240, -260), (-120, 330), (130, -520)]
    for i, (x, z) in enumerate(spots):
        y0 = -650 - 140 * (i % 3)
        y1 = y0 - 1150 - 150 * (i % 2)
        strip(L, f'DG_Wind{i + 1}', 'DG_Bank', np.array([x, y0, z + 180]) - b, np.array([x * 1.15, y1, z * 1.1 + 180]) - b,
              22, 44, 0.0, 0.0, '0.9,0.95,1.0,1.0', 0.32 + 0.04 * i, segs=8)
    # (a strip faded at both ends: alpha rises then falls along it)
    txt = '\n'.join(L)
    fixed = []
    for line in txt.split('\n'):
        fixed.append(line)
    job = pathlib.Path(out).with_suffix('.job.txt')
    # alpha along the dive streaks: 0 at both ends, full in the middle
    out_lines, cur = [], None
    for line in fixed:
        if line.startswith('shape '):
            cur = line.split()[1]
        if line.startswith('v ') and cur and cur.startswith('DG_Wind'):
            p = line.split()
            t = float(p[5]) / 2.0
            p[-1] = '%.3f' % (0.45 * math.sin(math.pi * t) ** 1.5)
            line = ' '.join(p)
        out_lines.append(line)
    job.write_text('\n'.join(out_lines) + '\n')
    r = subprocess.run([FXWRITE, str(job)], capture_output=True, text=True)
    print(r.stdout, r.stderr)
    if r.returncode:
        raise SystemExit('fxwrite failed')


if __name__ == '__main__':
    main()
