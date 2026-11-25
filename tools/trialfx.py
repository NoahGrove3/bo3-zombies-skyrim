"""(Noah, 2026-11-24: "The souls going into the altar stone on the first trial ... needs to be hyper realistic and really refined" -
"Blue spirit flame"; "make sure the ring for the first quest around the altar is hyper realistic as well ... anything that is
supposed to be glowing or illuminated should look like real light, not a circle in a color") the trials' light, as models in the
world (no more circles drawn over the screen):

  ring_<r>.nif   the altar's ring of light on the ground, r units out: 48 arcs, each its own node S00..S47 (src/Curse.cpp sets each
                 one down on the ground under it and lights them as the trial fills). Each arc: a soft band of light lying on the
                 ground (a bright thread in a wide falloff, shimmering along it), a rune of the Tongues burning in it (its strokes
                 glowing, no disc behind them), and - lit - a low curtain of light rising off it and fading upward, its streaks
                 drifting up, with a mote or two lifting. Unlit, only a faint band and a ghost of the rune ("D" child); lit, the
                 whole of it ("L" child).
  soul.nif       a soul on its way into the altar stone: a wavering blue spirit flame (a flipbook of a flame's tongues, white at
                 the heart, cyan, deep blue at its edges) with a soft halo, trailing a long smoky tail that streams behind it and
                 thins away, a few embers drifting off the tail. Its +Y is the way it flies.
  soul_take.nif  the stone taking a soul: a flash at the heart and a ring of light swelling out from it, played once (0.8 s).

    python3 tools/trialfx.py      -> assets/fx/Meshes/CurseOfAlduin/trial/*.nif, assets/fx/Textures/CurseOfAlduin/trial/*.dds"""
import math, pathlib, subprocess, sys
import numpy as np
from PIL import Image

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import weaponfx as W
from weaponfx import fbm, smooth, warp_sample, lattice_noise

ROOT = pathlib.Path(__file__).resolve().parent.parent
TEX = ROOT / 'assets' / 'fx' / 'Textures' / 'CurseOfAlduin' / 'trial'
MESH = ROOT / 'assets' / 'fx' / 'Meshes' / 'CurseOfAlduin' / 'trial'
WORK = ROOT / 'build' / 'trialfx'
GT = 'CurseOfAlduin\\trial\\'
TOOLS = pathlib.Path('/home/claude/tools')
RADII = (1400, 750, 650)   # (gen kCurseLight, kCurseWhisperRadius, kCurseEchoRadius)
SEGS = 48


def save(rgba, name):
    import assets
    TEX.mkdir(parents=True, exist_ok=True); WORK.mkdir(parents=True, exist_ok=True)
    im = Image.fromarray((np.clip(rgba, 0, 1) * 255).astype(np.uint8), 'RGBA')
    assets.write_dds_mips(im, TEX / f'{name}.dds', 'DXT5')
    im.save(WORK / f'{name}.png')


def grey(v, a):
    v = np.clip(v, 0, 1); return np.dstack([v, v, v, np.clip(a, 0, 1)])


def palette(name, stops):
    """greyscale-to-palette ramp: across, the texture's grey; down, the vertex alpha's fade"""
    x = (np.arange(256) + 0.5) / 256; v = (np.arange(64) + 0.5) / 64
    xs = [s[0] for s in stops]
    rgb = np.zeros((64, 256, 3))
    pos = x[None, :] * (0.55 + 0.45 * v[:, None])
    for i in range(3):
        rgb[..., i] = np.interp(pos, xs, [s[1][i] for s in stops])
    a = np.clip(x[None, :] * 1.25, 0, 1) * (0.25 + 0.75 * v[:, None])
    TEX.mkdir(parents=True, exist_ok=True)
    Image.fromarray((np.dstack([rgb, a]) * 255).astype(np.uint8), 'RGBA').save(TEX / f'pal_{name}.dds')


# ------------------------------------------------------------------ textures
def textures():
    # the altar's light: a pale, cold blue warming to white at its heart (the Tongues' light, not a flat colour)
    palette('altar', [(0.0, (0.02, 0.05, 0.16)), (0.35, (0.12, 0.34, 0.78)), (0.62, (0.42, 0.72, 1.0)), (0.86, (0.80, 0.92, 1.0)), (1.0, (1.0, 1.0, 1.0))])
    # the spirit flame: deep blue at the edges, cyan through it, white at the heart
    # (Noah, 2026-11-24: "Make it like a fireball that's white") white fire: silver-blue only at its thinnest edges
    palette('whitefire', [(0.0, (0.05, 0.07, 0.12)), (0.3, (0.32, 0.40, 0.58)), (0.6, (0.74, 0.84, 0.98)), (0.84, (0.94, 0.97, 1.0)), (1.0, (1.0, 1.0, 1.0))])
    palette('spirit', [(0.0, (0.01, 0.03, 0.14)), (0.3, (0.05, 0.22, 0.70)), (0.58, (0.25, 0.70, 1.0)), (0.82, (0.70, 0.95, 1.0)), (1.0, (1.0, 1.0, 1.0))])

    # the band: across it (u) a thin bright thread in a wide soft falloff; along it (v, wraps) a slow shimmer
    H, Wd = 512, 64
    u = np.linspace(-1, 1, Wd)[None, :].repeat(H, 0)
    sh = fbm(H, Wd, 16, 1, 301)
    shimmer = 0.75 + 0.5 * (sh - 0.5)
    core = np.exp(-(u / 0.07) ** 2)
    soft = np.exp(-(u / 0.42) ** 2) * 0.55 + np.exp(-np.abs(u) / 0.6) * 0.18
    edge = smooth(1.0, 0.86, np.abs(u))   # (to nothing at its borders)
    a = np.clip((core * 1.1 + soft) * shimmer, 0, 1) * edge
    save(grey(0.42 + 0.58 * core + 0.12 * soft, a), 'band')

    # the runes: 32 glyphs of the Tongues (strokes cut like dragon script - claws, bars, dots), an 8 x 4 atlas; each stroke a
    # hot line with its own bloom (the light of the stroke itself, no disc behind it)
    C = 96
    atlas = np.zeros((4 * C, 8 * C))
    rng = np.random.default_rng(23)
    yy, xx = np.mgrid[0:C, 0:C].astype(float)
    def seg(a, b, w):
        pa = np.dstack([xx - a[0], yy - a[1]]); ba = np.array(b) - np.array(a)
        t = np.clip((pa @ ba) / max(1e-6, ba @ ba), 0, 1)
        d = np.hypot(xx - (a[0] + ba[0] * t), yy - (a[1] + ba[1] * t))
        return d
    for g in range(32):
        dmin = np.full((C, C), 1e9)
        cx, cy, s = C / 2, C / 2, C * 0.3
        # a spine and two to three claws off it (the dragon script's wedges)
        top = (cx + rng.uniform(-0.2, 0.2) * s, cy - s); bot = (cx + rng.uniform(-0.2, 0.2) * s, cy + s)
        strokes = [(top, bot)]
        for k in range(rng.integers(2, 4)):
            t = rng.uniform(0.15, 0.85)
            p = (top[0] + (bot[0] - top[0]) * t, top[1] + (bot[1] - top[1]) * t)
            ang = rng.choice([-1, 1]) * rng.uniform(0.5, 1.1)
            L = s * rng.uniform(0.55, 0.95)
            q = (p[0] + math.sin(ang) * L, p[1] - math.cos(ang) * L * 0.6)
            strokes.append((p, q))
        if rng.random() < 0.5:
            px = cx + rng.choice([-1, 1]) * s * 0.75; strokes.append(((px, cy - s * 0.1), (px, cy + s * 0.1)))
        for a_, b_ in strokes: dmin = np.minimum(dmin, seg(a_, b_, 1.0))
        line = np.exp(-(dmin / 2.1) ** 2)
        bloom = np.exp(-(dmin / 7.5) ** 2) * 0.45 + np.exp(-dmin / 16.0) * 0.12
        cellv = np.clip(line + bloom, 0, 1)
        fade = smooth(0.0, 6.0, np.minimum(np.minimum(xx, C - 1 - xx), np.minimum(yy, C - 1 - yy)))   # (nothing at the cell's border)
        r_, c_ = divmod(g, 8)
        atlas[r_ * C:(r_ + 1) * C, c_ * C:(c_ + 1) * C] = cellv * fade
    save(grey(0.5 + 0.5 * np.clip(atlas * 1.2, 0, 1), atlas), 'runes')

    # the curtain: streaks of light rising (v up, wraps; u along the ring, wraps), brightest at the ground, gone at the top
    H, Wd = 256, 256
    st = fbm(H, Wd, 2, 24, 331)
    st2 = fbm(H, Wd, 4, 48, 337)
    streak = np.clip((st * 0.65 + st2 * 0.5) * 1.6 - 0.55, 0, 1)
    save(grey(0.35 + 0.65 * streak, streak), 'curtain')   # (the fade with height: the vertices' alpha)

    # the spirit flame: a 4 x 4 flipbook of a flame's tongues licking up, looping (the noise slides one turn up over the 16
    # frames): a teardrop envelope narrowing to its tip, bent by a slow sway, its edge broken into tongues that rise and tear off
    F = 128
    atlas = np.zeros((4 * F, 4 * F, 4))
    yy, xx = np.mgrid[0:F, 0:F].astype(float)
    x = (xx - F / 2 + 0.5) / (F / 2)
    yu = (F - 0.5 - yy) / F                       # (0 at the cell's foot, 1 at its top)
    sway_n = fbm(F * 2, F, 2, 2, 359, 3)
    tongue_n = fbm(F * 2, F, 5, 6, 351, 5)
    fine_n = fbm(F * 2, F, 10, 10, 353, 4)
    for f in range(16):
        off = int(round(f / 16 * F))
        sw = np.roll(sway_n, off, axis=0)[F:]       # (rolled down: the pattern rises)
        tg = np.roll(tongue_n, off * 2, axis=0)[F:]
        fn = np.roll(fine_n, off * 3, axis=0)[F:]
        h = np.clip((yu - 0.1) / 0.86, 0, 1)
        w = 0.5 * (1.0 - h) ** 0.75 + 0.03
        foot = np.where(yu < 0.1, np.sqrt(np.clip(1 - ((0.1 - yu) / 0.09) ** 2, 0, 1)), 1.0)
        xs = x + (sw - 0.5) * 0.7 * h
        field = (1.0 - np.abs(xs) / (w * foot + 1e-3)) + (tg - 0.5) * 1.3 * (0.25 + h) + (fn - 0.5) * 0.35
        field *= smooth(1.02, 0.86, yu)
        a = smooth(0.0, 0.45, field)
        heat = smooth(0.05, 1.1, field) * (1.0 - 0.55 * h)
        core = np.exp(-((x / 0.16) ** 2 + ((yu - 0.28) / 0.16) ** 2))
        g = np.clip(heat * 0.9 + core * 0.8, 0, 1)
        a = np.clip(a * (0.7 + 0.3 * fn) + core * 0.6, 0, 1) * smooth(0.0, 4.0, np.minimum(np.minimum(xx, F - 1 - xx), np.minimum(yy, F - 1 - yy)))
        r_, c_ = divmod(f, 4)
        atlas[r_ * F:(r_ + 1) * F, c_ * F:(c_ + 1) * F] = grey(g, a)
    save(atlas, 'spiritflame')

    # the fireball's trailing flames (v along the trail from the head, wraps as it scrolls back; u across): tongues torn back by
    # the wind of its flight - widest just behind the head, narrowing and breaking into licks further back
    H, Wd = 512, 128
    u = np.linspace(-1, 1, Wd)[None, :].repeat(H, 0)
    v = np.linspace(0, 1, H, endpoint=False)[:, None].repeat(Wd, 1)
    tn = fbm(H, Wd, 6, 4, 381, 5)
    fn = fbm(H, Wd, 14, 8, 383, 4)
    sw = fbm(H, Wd, 3, 2, 387, 3)
    xs = u + (sw - 0.5) * 0.5
    field = (1.0 - np.abs(xs) / 0.55) + (tn - 0.5) * 1.9 + (fn - 0.5) * 0.7
    a = smooth(0.05, 0.75, field) * smooth(1.0, 0.86, np.abs(u)) * 0.85
    heat = smooth(0.25, 1.6, field)
    save(grey(0.12 + 0.88 * heat, a * (0.6 + 0.4 * fn)), 'trailfire')

    # a soft round glow (the halo, the heart, the embers)
    m = 128; yy, xx = np.mgrid[0:m, 0:m]; r = np.hypot(xx - m / 2 + 0.5, yy - m / 2 + 0.5) / (m / 2)
    save(grey(np.clip(1.0 - r * 0.8, 0, 1), np.clip(np.exp(-(r * 2.2) ** 2) + np.exp(-r * 5.0) * 0.4, 0, 1) * smooth(1.0, 0.9, r)), 'glow')

    # the smoky tail: wisps drawn out along it (v along, wraps), thinning to nothing at both sides (u)
    H, Wd = 512, 96
    u = np.linspace(-1, 1, Wd)[None, :].repeat(H, 0)
    w1 = fbm(H, Wd, 4, 2, 371)
    sm = warp_sample(fbm(H, Wd, 10, 3, 373), (w1 - 0.5) * 30, (w1 - 0.5) * 60)
    strands = np.clip(sm * 1.7 - 0.55, 0, 1)
    across = np.exp(-(u / (0.45 + 0.25 * (w1 - 0.5))) ** 2) * smooth(1.0, 0.82, np.abs(u))
    save(grey(0.35 + 0.65 * strands, strands * across), 'smoke')

    # the stone taking a soul: a 4 x 4 flipbook of a ring of light swelling out and fading, a flash at its heart
    F = 128
    atlas = np.zeros((4 * F, 4 * F, 4))
    yy, xx = np.mgrid[0:F, 0:F].astype(float)
    r = np.hypot(xx - F / 2 + 0.5, yy - F / 2 + 0.5) / (F / 2)
    for f in range(16):
        k = f / 15.0
        R = 0.15 + 0.75 * (1 - (1 - k) ** 2)
        ringv = np.exp(-((r - R) / (0.05 + 0.06 * k)) ** 2) * (1 - k) ** 1.3
        flash = np.exp(-(r / (0.25 + 0.2 * k)) ** 2) * (1 - k) ** 2.2
        g = np.clip(ringv * 0.8 + flash, 0, 1)
        a = np.clip(ringv * 0.9 + flash * 1.1, 0, 1) * smooth(1.0, 0.92, r)
        r_, c_ = divmod(f, 4)
        atlas[r_ * F:(r_ + 1) * F, c_ * F:(c_ + 1) * F] = grey(g, a)
    save(atlas, 'take')


# ------------------------------------------------------------------ models
class Job:
    def __init__(self, out):
        self.rows = [f'out {out}']

    def node(self, name, parent, x=0.0, y=0.0, z=0.0):
        self.rows.append(f'node {name} {parent} {x:.3f} {y:.3f} {z:.3f}')

    def shape(self, name, tex, pal, scale, verts, tris, **kw):
        head = f'shape {name} tex={GT}{tex}.dds color=1,1,1,1 scale={scale:.2f} blend=add palette={GT}pal_{pal}.dds'
        for k, v in kw.items(): head += f' {k}={v}'
        self.rows.append(head)
        for (x, y, z, u, v, a) in verts: self.rows.append(f'v {x:.3f} {y:.3f} {z:.3f} {u:.4f} {v:.4f} 1 1 1 {a:.3f}')
        for t in tris: self.rows.append(f't {t[0]} {t[1]} {t[2]}')

    def card(self, name, tex, pal, scale, size, at, **kw):
        sx, sy = size if isinstance(size, tuple) else (size, size)
        self.shape(name, tex, pal, scale, [(-sx, -sy, 0, 0, 1, 1), (sx, -sy, 0, 1, 1, 1), (sx, sy, 0, 1, 0, 1), (-sx, sy, 0, 0, 0, 1)],
                   [(0, 1, 2), (0, 2, 3)], billboard=1, at=f'{at[0]:.2f},{at[1]:.2f},{at[2]:.2f}', **kw)

    def write(self, name):
        WORK.mkdir(parents=True, exist_ok=True); MESH.mkdir(parents=True, exist_ok=True)
        jf = WORK / f'{name}.job'
        jf.write_text('\n'.join(self.rows) + '\n')
        r = subprocess.run([str(TOOLS / 'fxwrite'), str(jf)], capture_output=True, text=True)
        if r.returncode: raise SystemExit(f'{name}: fxwrite failed: {r.stderr[-500:]}')
        print(name, 'written')


def ring(R):
    J = Job(MESH / f'ring_{R}.nif')
    rng = np.random.default_rng(R)
    Wb = 22.0 + R * 0.012          # the band's width
    Hc = 48.0 + R * 0.025          # the curtain's height
    G = Wb * 0.62                  # a rune's half-size
    da = 2 * math.pi / SEGS
    k = 6                          # (steps along each arc)
    for i in range(SEGS):
        a0 = i * da; ac = a0 + da / 2
        cx, cy = math.cos(ac) * R, math.sin(ac) * R
        S = f'S{i:02d}'
        J.node(S, 'FX', cx, cy, 0.0)
        J.node(S + 'D', S); J.node(S + 'L', S)
        # the arc's points, in the node's own space (its centre at the node)
        pts = []
        for j in range(k + 1):
            a = a0 + da * j / k
            pts.append((math.cos(a), math.sin(a), a))
        def band(width, z, vrep):
            V, T = [], []
            for j, (c, s, a) in enumerate(pts):
                vv = (i * k + j) / (SEGS * k) * vrep
                for side, uu in ((-1, 0.0), (1, 1.0)):
                    rr = R + side * width / 2
                    V.append((c * rr - cx, s * rr - cy, z, uu, vv, 1.0))
            for j in range(k):
                q = j * 2
                T += [(q, q + 1, q + 3), (q, q + 3, q + 2)]
            return V, T
        V, T = band(Wb, 1.5, 6.0)
        J.shape(f'{S}_db', 'band', 'altar', 0.45, V, T, parent=S + 'D', clamp=1, vscroll=14)
        J.shape(f'{S}_lb', 'band', 'altar', 1.5, V, T, parent=S + 'L', clamp=1, vscroll=9)
        V2, T2 = band(Wb * 2.6, 1.2, 4.0)
        J.shape(f'{S}_lh', 'band', 'altar', 0.5, V2, T2, parent=S + 'L', clamp=1, vscroll=17)   # (the wide spill of its light on the ground)
        # the rune: flat on the band, upright toward the altar's outside
        g = int(rng.integers(32)); gr, gc = divmod(g, 8)
        u0, u1, v0, v1 = gc / 8, (gc + 1) / 8, gr / 4, (gr + 1) / 4
        tx, ty = -math.sin(ac), math.cos(ac)      # (along the ring)
        ox, oy = math.cos(ac), math.sin(ac)       # (out from the altar)
        quad = []
        for (sa, sb, uu, vv) in ((-1, -1, u0, v1), (1, -1, u1, v1), (1, 1, u1, v0), (-1, 1, u0, v0)):
            quad.append((tx * sa * G + ox * sb * G, ty * sa * G + oy * sb * G, 2.0, uu, vv, 1.0))
        J.shape(f'{S}_dr', 'runes', 'altar', 0.55, quad, [(0, 1, 2), (0, 2, 3)], parent=S + 'D', clamp=0)
        J.shape(f'{S}_lr', 'runes', 'altar', 2.4, quad, [(0, 1, 2), (0, 2, 3)], parent=S + 'L', clamp=0, pulse=f'{2.0 + rng.random():.2f},0.8,1.25')
        # the curtain: a low wall of light standing on the ring, fading up
        V, T = [], []
        for j, (c, s, a) in enumerate(pts):
            uu = (i * k + j) / (SEGS * k) * (2 * math.pi * R / 260.0)
            for z, vv, al in ((0.0, 0.0, 1.0), (Hc * 0.45, 0.45, 0.55), (Hc, 1.0, 0.0)):
                V.append((c * R - cx, s * R - cy, z, uu, vv, al))
        for j in range(k):
            q = j * 3
            for r_ in range(2):
                T += [(q + r_, q + r_ + 3, q + r_ + 4), (q + r_, q + r_ + 4, q + r_ + 1)]
        J.shape(f'{S}_lc', 'curtain', 'altar', 0.9, V, T, parent=S + 'L', vscroll=3.5)
        J.shape(f'{S}_dc', 'curtain', 'altar', 0.12, V, T, parent=S + 'D', vscroll=5.0)
        # a mote lifting off it (lit)
        if i % 2 == 0:
            J.card(f'{S}_lm', 'glow', 'altar', 1.4, 3.0 + R * 0.002, (0.0, 0.0, Hc * rng.uniform(0.25, 0.8)), parent=S + 'L',
                   pulse=f'{1.2 + rng.random() * 1.6:.2f},0.0,1.0')
    J.write(f'ring_{R}')


def soul():
    """(Noah, 2026-11-24: "remove the cone shape ... Make it like a fireball that's white. So the flames would be falling behind it
    because it's traveling through the sky and there's wind") a white fireball: a white-hot heart in a soft halo, its own fire
    churning round it, and its flames streaming back behind it on the wind of its flight - one flat sheet along -Y that the game
    turns about the line of flight to face you (src/Curse.cpp poses it so), the flames scrolling back along it; sparks shed
    behind"""
    J = Job(MESH / 'soul.nif')
    rng = np.random.default_rng(5)
    # the head (cards that face you): its halo, the fire churning round it, the white heart
    J.card('halo', 'glow', 'whitefire', 0.5, 42.0, (0, 0, 0))
    J.card('fire', 'spiritflame', 'whitefire', 1.6, (15.0, 17.0), (0, 0, 0), flip='4x4', fps=24, phase=0.0)
    J.card('fire2', 'spiritflame', 'whitefire', 1.0, (12.0, 13.0), (0, 0, 0), flip='4x4', fps=19, phase=0.5)
    J.card('heart', 'glow', 'whitefire', 3.0, 9.0, (0, 0, 0))
    # the trail: a sheet in the XY plane from just ahead of the heart back along -Y, widest close behind it
    L, n = 120.0, 16
    for name, sc, wmul, per in (('trail', 1.7, 1.0, 0.55), ('trail2', 0.9, 0.7, 0.4)):
        V, T = [], []
        for j in range(n + 1):
            t = j / n
            y = 6.0 - (L + 6.0) * t
            w = (10.0 + 12.0 * math.sin(min(1.0, t * 2.2) * math.pi * 0.5) * (1.0 - t) ** 0.7 + 2.0) * wmul
            a = smooth(0.0, 0.1, t) * (1.0 - t) ** 1.3 if t > 0.0 else 0.0
            for side, uu in ((-1, 0.0), (1, 1.0)):
                V.append((side * w, y, 0.0, uu, t * 1.6, a))
        for j in range(n):
            q = j * 2
            T += [(q, q + 1, q + 3), (q, q + 3, q + 2)]
        J.shape(name, 'trailfire', 'whitefire', sc, V, T, clamp=1, vscroll=per)
    # sparks shed behind it
    for e in range(6):
        y = -rng.uniform(15, 110); x = rng.uniform(-10, 10); z = rng.uniform(-8, 8)
        J.card(f'spark{e}', 'glow', 'whitefire', 2.0, rng.uniform(1.0, 2.0), (x, y, z), pulse=f'{0.35 + rng.random() * 0.5:.2f},0.0,1.0')
    J.write('soul')


def take():
    J = Job(MESH / 'soul_take.nif')
    J.card('ring', 'take', 'whitefire', 2.0, 70.0, (0, 0, 0), flip='4x4', fps=20, phase=0.0)
    J.card('flash', 'glow', 'whitefire', 1.8, 30.0, (0, 0, 0), pulse='0.8,0.0,1.0')
    J.write('soul_take')


if __name__ == '__main__':
    textures()
    for R in RADII: ring(R)
    soul()
    take()
