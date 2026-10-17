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
DASH = r'textures\CurseOfAlduin\Dragon\wind_dash.dds'
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


def dash_texture(out):
    """the dive's air: thin dashes along the band, each a bright head with a fading tail (it tiles as it scrolls)"""
    W, H = 32, 256
    u = (np.arange(W) + 0.5) / W
    v = (np.arange(H) + 0.5) / H
    across = np.exp(-((u - 0.5) / 0.16) ** 2)
    ph = (v * 3) % 1.0                      # three dashes a tile
    along = np.clip(1 - ph / 0.55, 0, 1) ** 1.6 * (ph < 0.55)
    a = np.clip(across[None, :] * along[:, None], 0, 1)
    img = np.zeros((H, W, 4), np.uint8)
    img[..., :3] = 255
    img[..., 3] = (a * 255).astype(np.uint8)
    dds(Image.fromarray(img, 'RGBA'), out, 'DXT5')


def strip(lines, name, parent, p0, p1, w0, w1, a0, a1, color, scroll, segs=8, fade_in=0.0, tex=TEX):
    """a ribbon from p0 to p1 (two quads crossed, so it reads from any side), w0 -> w1 wide, alpha a0 -> a1 (and in from
    0 over the first fade_in of it)"""
    # (the game scrolls it each frame - src/Movement.cpp DragonWind - so no controller here)
    lines.append(f'shape {name} tex={tex} color={color} scale=0 blend=add parent={parent}')
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
    dash_texture(texdir / 'CurseOfAlduin' / 'Dragon' / 'wind_dash.dds')
    L = [f'out {out}', f'base {src}']
    for sd in ('L', 'R'):
        w, tip = np.array(WRIST[sd]), np.array(TIP[sd])
        p0 = tip - w
        p1 = p0 + np.array([0, -900, 10])
        strip(L, f'DG_Trail{sd}', f'DG_Wing{sd}3', p0, p1, 18, 64, 1.0, 0.0, '0.85,0.93,1.0,1.0', 0.7, segs=10, fade_in=0.04)
    # the dive (Noah: the lines come from ahead of the dragon): streaks starting well ahead of its head, spreading a little
    # as they rush back past it and you, dashes of air streaming along them
    b = np.array(BANK)
    spots = [(-330, 140), (330, 140), (-560, -40), (560, -40), (-240, -260), (240, -260), (-120, 330), (130, -520)]
    for i, (x, z) in enumerate(spots):
        y0 = -150 - 130 * (i % 3)
        y1 = y0 - 1700 - 150 * (i % 2)
        strip(L, f'DG_Wind{i + 1}', 'DG_Bank', np.array([x * 0.6, y0, z * 0.6 + 180]) - b, np.array([x * 1.2, y1, z * 1.15 + 180]) - b,
              45, 100, 0.0, 0.0, '0.9,0.95,1.0,1.0', 0, segs=10, tex=DASH)
    # Bond III's lightning breath: three jagged bolts from its mouth, as far as its breath reaches (1800 game units:
    # the dragon's space is 0.23 of that) - the game flickers them, turns them about their length and aims them
    import random
    random.seed(11)
    for k in range(3):
        # (alpha-blended, a strong electric blue: additive white vanished against snow and sky)
        L.append(f'shape DG_Bolt{k + 1} tex={TEX} color=0.45,0.62,1.0,1.0 scale=0 blend=alpha parent=DG_Mouth')
        n, y1 = 26, 7800.0
        pts = [(0.0, 20.0, 0.0)]
        for i in range(1, n + 1):
            t = i / n
            amp = 40 + 260 * t
            pts.append((random.uniform(-amp, amp), 20 + (y1 - 20) * t, random.uniform(-amp, amp)))
        vi, tris = 0, []
        for ax in ((1, 0, 0), (0, 0, 1)):
            base = vi
            for i, (x, y, z) in enumerate(pts):
                t = i / n
                w = (170 - 80 * t) * (1.0 if k == 0 else 0.6)
                a = min(1.0, t / 0.03) * (1 - t) ** 0.5
                for s in (-1, 1):
                    L.append(f'v {x + ax[0] * w * s:.1f} {y:.1f} {z + ax[2] * w * s:.1f} {0.5 + 0.5 * s:.3f} {t * 4:.3f} 1 1 1 {a:.3f}')
                    vi += 1
            for i in range(n):
                q = base + 2 * i
                tris += [(q, q + 1, q + 2), (q + 1, q + 3, q + 2)]
        for t3 in tris:
            L.append('t %d %d %d' % t3)
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
            p[-1] = '%.3f' % (0.85 * math.sin(math.pi * t) ** 0.8)
            line = ' '.join(p)
        out_lines.append(line)
    job.write_text('\n'.join(out_lines) + '\n')
    r = subprocess.run([FXWRITE, str(job)], capture_output=True, text=True)
    print(r.stdout, r.stderr)
    if r.returncode:
        raise SystemExit('fxwrite failed')


if __name__ == '__main__':
    main()
