"""The glider dragon's wind (src/Movement.cpp: DragonWind): a streak of air off each wingtip, and rushing streaks round
it in a Bond III dive - unlit additive strips (tools/modelconv/fxwrite) hung under the dragon's own bones, so they move
with it; the game sets how bright they are each frame (0 in the nif: unseen until it does).
    python tools/dragonfx.py <in.nif> <out.nif> <Textures dir>
Writes Textures/CurseOfAlduin/Dragon/wind.dds (a soft, streaky band, tiling along its length) and <out.nif>: <in.nif>
with DG_TrailL / DG_TrailR (under DG_WingL3 / R3, from the wingtip back) and DG_Wind1..8 (under DG_Bank).
(Noah: see-through trails that flow with the wings, never a solid line) with our own dragon the trails are ribbons skinned to
twelve bones each (DG_TrlL0..11 / DG_TrlR0..11, under DG_Space) that the game puts where the wingtip was a moment ago
(src/Movement.cpp TrailPlace) - DG_TipL / DG_TipR mark the tips it follows; their texture is broken wisps of air.
(Noah: one beam of fire) DG_Beam1..3 under DG_Mouth: the breath's white-hot core, the flame round it and a flare at its jaws,
along +Y (the game turns them to where it breathes and tints them its bond's colour: src/Movement.cpp BeamPose)."""
import subprocess, sys, pathlib, math, json
import numpy as np
from PIL import Image
sys.path.insert(0, str(pathlib.Path(__file__).parent))
from dragontex import dds

FXWRITE = '/home/claude/tools/fxwrite'
TEX = r'textures\CurseOfAlduin\Dragon\wind.dds'
DASH = r'textures\CurseOfAlduin\Dragon\wind_dash.dds'
TRAIL = r'textures\CurseOfAlduin\Dragon\trail.dds'
BEAM = r'textures\CurseOfAlduin\Dragon\beam.dds'
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


def beam_texture(out):
    """flame for the breath's beam: turbulent tongues streaming along v (it tiles along v, and across u as it wraps round),
    white - the game tints it - its alpha the flame's brightness, hottest down the middle"""
    W, H = 128, 512
    u = (np.arange(W) + 0.5) / W
    v = (np.arange(H) + 0.5) / H
    U, V = np.meshgrid(u, v)
    rng = np.random.default_rng(3)
    n = np.zeros((H, W))
    for f, amp in ((2, 1.0), (3, 0.8), (5, 0.6), (8, 0.45), (13, 0.3), (21, 0.2)):
        for _ in range(2):
            fu = int(rng.integers(1, 4))
            n += amp * np.sin(2 * math.pi * (f * V + fu * U + rng.uniform())) * np.sin(2 * math.pi * (int(rng.integers(1, 5)) * U + 0.3 * f * V + rng.uniform()))
    n = (n - n.min()) / (n.max() - n.min())
    core = np.exp(-((U - 0.5) / 0.22) ** 2)
    tongues = np.clip((n - 0.35) / 0.5, 0, 1) ** 1.3
    a = np.clip(core * (0.45 + 0.75 * tongues), 0, 1)
    img = np.zeros((H, W, 4), np.uint8)
    img[..., 0] = 255
    img[..., 1] = (200 + 55 * core).astype(np.uint8)
    img[..., 2] = (150 + 105 * core).astype(np.uint8)
    img[..., 3] = (a * 255).astype(np.uint8)
    dds(Image.fromarray(img, 'RGBA'), out, 'DXT5')


def beam(lines, name, length, r0, r1, a0, a1, tiles, sides=12, segs=26, fade_out=0.2, blend='add'):
    """the breath along +Y from the mouth: a cone of flame (a tube widening from the jaws), seen whole from any side - from
    behind the dragon you look into it along its length - faded in at the jaws and out at its end; its texture wraps twice
    round it and streams along it"""
    # (skinned to one bone, DG_BeamBone, that the game turns to where it breathes: shapes moved on their own under the
    # dragon's animated nodes were never drawn, while its skinned trails always are)
    lines.append(f'shape {name} tex={BEAM} color=1,1,1,1 scale=0 blend={blend} parent=DG_Mouth skin=DG_BeamBone')
    for i in range(segs + 1):
        t = i / segs
        y = 10 + (length - 10) * t
        r = r0 + (r1 - r0) * t ** 0.8
        al = (a0 + (a1 - a0) * t) * (min(1.0, t / 0.03) if t > 0 else 0.0) * min(1.0, (1 - t) / fade_out)
        for k in range(sides + 1):
            ang = 2 * math.pi * k / sides
            x, z = r * math.cos(ang), r * math.sin(ang)
            lines.append(f'v {x:.2f} {y:.2f} {z:.2f} {2.0 * k / sides:.4f} {t * tiles:.3f} 1 1 1 {al:.3f}')
            lines.append(f'n {math.cos(ang):.4f} 0 {math.sin(ang):.4f}')  # (an effect shape without normals is never drawn)
            lines.append('w 0 1.0')
    for i in range(segs):
        for k in range(sides):
            a = i * (sides + 1) + k
            b, c, d = a + 1, a + sides + 1, a + sides + 2
            lines.append(f't {a} {c} {b}')
            lines.append(f't {b} {c} {d}')


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
            w = 16 + 20 * t
            al = 0.5 * min(1.0, (k + 0.35) / 1.6) * (1 - t) ** 1.4
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
    dash_texture(texdir / 'CurseOfAlduin' / 'Dragon' / 'wind_dash.dds')
    L = [f'out {out}', f'base {src}']
    if pts:
        trail_texture(texdir / 'CurseOfAlduin' / 'Dragon' / 'trail.dds')
        beam_texture(texdir / 'CurseOfAlduin' / 'Dragon' / 'beam.dds')
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
    # the dive (Noah: very subtle, in front of its nose): fourteen fine streaks in a loose cone ahead of the head, each
    # running back past the nose and fading before the neck - the game streams dashes of air along each at its own pace
    b = np.array(pts['DG_Bank'] if pts else BANK)
    nose = np.array(pts['DG_Mouth']) if pts else np.array([0.0, -1012.0, 150.0])
    rng = np.random.default_rng(5)
    for i in range(14):
        a = 2 * math.pi * i / 14 + rng.uniform(-0.2, 0.2)
        r0, r1 = rng.uniform(70, 150), rng.uniform(150, 300)
        ahead = rng.uniform(700, 1500)
        p0 = nose + np.array([r0 * math.cos(a), ahead, r0 * math.sin(a) * 0.8])
        p1 = nose + np.array([r1 * math.cos(a), -rng.uniform(150, 350), r1 * math.sin(a) * 0.8])
        strip(L, f'DG_Wind{i + 1}', 'DG_Bank', p0 - b, p1 - b, 8, 18, 0.0, 0.0, '0.9,0.95,1.0,1.0', 0, segs=10, tex=DASH)
    if pts:   # the breath: core, flame, flare
        L.append('node DG_BeamBone DG_Mouth 0 0 0')
        beam(L, 'DG_Beam1', 7000, 18, 150, 0.9, 0.55, 5.8)                       # the white-hot core
        beam(L, 'DG_Beam2', 7000, 55, 430, 0.6, 0.18, 4.2, blend='alpha')        # the flame round it (seen against the sky too)
        beam(L, 'DG_Beam3', 900, 50, 230, 0.9, 0.0, 1.0, segs=8, fade_out=0.6)  # the flare at its jaws
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
