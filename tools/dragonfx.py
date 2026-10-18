"""The glider dragon's wingtip trails (src/Movement.cpp: DragonWind, TrailPlace) - unlit additive strips
(tools/modelconv/fxwrite); the game sets how bright they are each frame (0 in the nif: unseen until it does).
    python tools/dragonfx.py <in.nif> <out.nif> <Textures dir> [job.points.json]
(Noah: see-through trails that flow with the wings, never a solid line; only at the wingtips) with our own dragon the trails
are ribbons skinned to twelve bones each (DG_TrlL0..11 / DG_TrlR0..11, under DG_Space) that the game puts where the wingtip
was a moment ago - DG_TipL / DG_TipR mark the tips it follows; their texture is broken wisps of air. (Without the points: a
plain strip DG_TrailL / DG_TrailR under each wrist.) The breath is the Fire Breath shout's own fire, not part of the nif."""
import subprocess, sys, pathlib, math, json
import numpy as np
from PIL import Image
sys.path.insert(0, str(pathlib.Path(__file__).parent))
from dragontex import dds

FXWRITE = '/home/claude/tools/fxwrite'
TEX = r'textures\CurseOfAlduin\Dragon\wind.dds'
TRAIL = r'textures\CurseOfAlduin\Dragon\trail.dds'
TRAIL_BONES, TRAIL_SPACING = 12, 80.0
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


def trail_texture(out):
    """broken wisps of air streaming along the ribbon (v): several thin threads, each coming and going, with gaps between -
    soft across, so the ribbon reads as moving air, not a band (it tiles along v)"""
    W, H = 64, 256
    u = (np.arange(W) + 0.5) / W
    v = (np.arange(H) + 0.5) / H
    rng = np.random.default_rng(11)
    a = np.zeros((H, W))
    for k in range(9):
        c = 0.5 + rng.uniform(-0.28, 0.28)
        f = int(rng.integers(1, 4))
        ph = rng.uniform()
        cc = c + 0.05 * np.sin(2 * math.pi * (f * v[:, None] + ph))
        wd = rng.uniform(0.03, 0.09)
        across = np.exp(-((u[None, :] - cc) / wd) ** 2)
        on = np.clip(np.sin(2 * math.pi * (int(rng.integers(2, 6)) * v[:, None] + rng.uniform())), 0, 1) ** rng.uniform(1.0, 2.5)
        a += across * on * rng.uniform(0.35, 0.9)
    edge = np.exp(-((u - 0.5) / 0.3) ** 4)   # (soft to nothing at the ribbon's edges)
    a = np.clip(a / np.percentile(a, 99.5), 0, 1) * edge[None, :]
    img = np.zeros((H, W, 4), np.uint8)
    img[..., :3] = 255
    img[..., 3] = (a * 255).astype(np.uint8)
    dds(Image.fromarray(img, 'RGBA'), out, 'DXT5')


def skinned_trail(lines, sd, tip):
    """a ribbon off the wingtip back along twelve bones (each cross-section on its own bone - the game moves them), two
    planes crossed (flat across the wing, and edge-up) so it reads from any side; faint, faded in at the tip, out at its end"""
    bones = [f'DG_Trl{sd}{k}' for k in range(TRAIL_BONES)]
    for k, b in enumerate(bones):
        p = tip + np.array([0.0, -TRAIL_SPACING * k, 0.0])
        lines.append(f'node {b} DG_Space {p[0]:.2f} {p[1]:.2f} {p[2]:.2f}')
    lines.append(f'shape DG_Trail{sd} tex={TRAIL} color=0.88,0.94,1.0,1.0 scale=0 blend=add falloff=1 parent=DG_Space skin={",".join(bones)}')
    n = 0
    tris = []
    for ax, nrm in ((np.array([1.0, 0, 0]), np.array([0, 0, 1.0])), (np.array([0, 0, 1.0]), np.array([1.0, 0, 0]))):
        base = n
        for k in range(TRAIL_BONES):
            t = k / (TRAIL_BONES - 1)
            p = tip + np.array([0.0, -TRAIL_SPACING * k, 0.0])
            w = 8 + 18 * t
            al = 0.38 * (0.75 if k == 0 else 1.0) * (1 - t) ** 1.3   # (Noah: joined to the wingtip, see-through, fading back)
            for s2 in (-1, 1):
                q = p + ax * (w / 2 * s2)
                lines.append(f'v {q[0]:.2f} {q[1]:.2f} {q[2]:.2f} {0.5 + 0.5 * s2:.3f} {t * 2.0:.3f} 1 1 1 {al:.3f}')
                lines.append(f'n {nrm[0]:.4f} {nrm[1]:.4f} {nrm[2]:.4f}')
                lines.append(f'w {k} 1.0')
                n += 1
        for k in range(TRAIL_BONES - 1):
            a, b, c, e = base + 2 * k, base + 2 * k + 1, base + 2 * k + 2, base + 2 * k + 3
            tris += [(a, b, c), (b, e, c)]
    for t in tris:
        lines.append('t %d %d %d' % t)


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
    # (our own dragon: tools/dragonbuild.py writes where its bones are beside its job)
    pts = json.loads(pathlib.Path(sys.argv[4]).read_text()) if len(sys.argv) > 4 else None
    texture(texdir / 'CurseOfAlduin' / 'Dragon' / 'wind.dds')
    L = [f'out {out}', f'base {src}']
    if pts:
        trail_texture(texdir / 'CurseOfAlduin' / 'Dragon' / 'trail.dds')
    for sd in ('L', 'R'):
        if pts:   # from the tip of the wing's long finger (it moves with the finger as the wing folds)
            parent = f'DG_Wing{sd}F22'
            w, tip = np.array(pts[parent]), np.array(pts['tip' + sd])
            L.append(f'node DG_Tip{sd} {parent} {tip[0] - w[0]:.2f} {tip[1] - w[1]:.2f} {tip[2] - w[2]:.2f}')
            skinned_trail(L, sd, tip)
            continue
        else:
            parent = f'DG_Wing{sd}3'
            w, tip = np.array(WRIST[sd]), np.array(TIP[sd])
        p0 = tip - w
        p1 = p0 + np.array([0, -900, 10])
        strip(L, f'DG_Trail{sd}', parent, p0, p1, 18, 64, 1.0, 0.0, '0.85,0.93,1.0,1.0', 0.7, segs=10, fade_in=0.04)
    job = pathlib.Path(out).with_suffix('.job.txt')
    # alpha along the dive streaks: 0 at both ends, full in the middle
    out_lines, cur = [], None
    for line in L:
        if line.startswith('shape '):
            cur = line.split()[1]
        if line.startswith('v ') and cur and cur.startswith('DG_Wind'):
            p = line.split()
            t = float(p[5]) / 2.0
            p[-1] = '%.3f' % (0.4 * math.sin(math.pi * t) ** 0.8)
            line = ' '.join(p)
        out_lines.append(line)
    job.write_text('\n'.join(out_lines) + '\n')
    r = subprocess.run([FXWRITE, str(job)], capture_output=True, text=True)
    print(r.stdout, r.stderr)
    if r.returncode:
        raise SystemExit('fxwrite failed')


if __name__ == '__main__':
    main()
