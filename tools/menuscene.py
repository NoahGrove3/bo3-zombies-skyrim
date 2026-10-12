"""Undying Nights main menu: a CG night composed from scratch and rendered offline (tools/scene3d.py), in parallax layers
the menu animates. Our own undead (the risen, the Hollow Bride, the Drowned Wailer, the Abomination, the reaper, the
necromancer) come down from a barrow under the Blood Moon, led by the champions (the Horned Bone Knight, the Barrow King,
the Lich of the Dirge); a rune-stone of the Dirge glows in the snow in front; a dark outcrop on the right carries the menu.

    python tools/menuscene.py <out dir> [preview.jpg] [scale]

Writes menu_sky.jpg (from tools/menupaint.py's sky), menu_far.png, menu_mid.png, menu_near.png (2560x1440, alpha) and
menu_glow.json (screen points of the lights the menu pulses: the rune-stone, the lich's staff)."""
import json, math, pathlib, sys
import numpy as np
from PIL import Image, ImageFilter

sys.path.insert(0, str(pathlib.Path(__file__).parent))
import scene3d as S
import menupaint as MP

OUT = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else 'assets/ui')
PREVIEW = sys.argv[2] if len(sys.argv) > 2 else None
SCALE = float(sys.argv[3]) if len(sys.argv) > 3 else 1.0
W, H = 2560, 1440
RW, RH = int(W * SCALE), int(H * SCALE)
MODELS = {
    'zombie': ('/home/claude/work/out4/Data/Meshes/UndyingNights/zombie/zombie.nif', '/home/claude/work/out4/Data'),
    **{n: (f'/home/claude/work/out3/Data/Meshes/UndyingNights/{n}/{n}.nif', '/home/claude/work/out3/Data')
       for n in ['hooded', 'hag', 'abom', 'reaper', 'necro', 'knight', 'king', 'lich']},
}
CAM = {'eye': (40.0, -160.0, 118.0), 'look': (-230.0, 1500.0, 250.0), 'fov': 40}
MOON_DIR = np.array([-0.52, 1.0, 0.36]); MOON_DIR /= np.linalg.norm(MOON_DIR)   # toward the moon (it hangs upper left)
MOON_C = np.array([1.00, 0.40, 0.28]) * 0.9
SKY_C = np.array([0.075, 0.10, 0.20]); GROUND_C = np.array([0.05, 0.05, 0.075])
FILL_DIR = np.array([0.35, -1.0, 0.55]); FILL_DIR /= np.linalg.norm(FILL_DIR); FILL_C = np.array([0.16, 0.21, 0.38])
FOG_C = np.array([0.085, 0.105, 0.185])

# ---------------------------------------------------------------------------------------------------------- poses
L, Rr = 'L', 'R'
def arm(side, fwd=0.0, out=0.0, elbow=0.0, twist=0.0):
    s = 1 if side == 'R' else -1
    return {f'NPC {side} UpperArm [{side}Uar]': [('x', fwd), ('y', -s * out)], f'NPC {side} Forearm [{side}Lar]': [('x', elbow)]}
def leg(side, swing=0.0, knee=0.0, out=0.0):
    s = 1 if side == 'R' else -1
    return {f'NPC {side} Thigh [{side}Thg]': [('x', swing), ('y', s * out)], f'NPC {side} Calf [{side}Clf]': [('x', -knee)]}
def spine(bend=0.0, side=0.0, twist=0.0, head=0.0, head_turn=0.0, head_tilt=0.0):
    return {'NPC Spine [Spn0]': [('x', -bend * 0.3)], 'NPC Spine1 [Spn1]': [('x', -bend * 0.4), ('y', side)], 'NPC Spine2 [Spn2]': [('x', -bend * 0.3), ('z', twist)],
            'NPC Neck [Neck]': [('x', head * 0.5)], 'NPC Head [Head]': [('x', head * 0.5), ('z', head_turn), ('y', head_tilt)]}
def P(*parts):
    out = {}
    for p in parts:
        for k, v in p.items():
            out.setdefault(k, []).extend(v)
    return out

POSES = {
    'shamble_a': P(spine(24, 6, 8, 18, 10, 12), arm('R', 30, 4, 25), arm('L', 12, 8, 10), leg('L', 22, 25), leg('R', -14, 12)),
    'shamble_b': P(spine(30, -8, -6, 25, -12, -15), arm('R', 8, 10, 5), arm('L', 40, 0, 30), leg('R', 24, 30), leg('L', -16, 10)),
    'reach':     P(spine(18, 0, 4, 10, 4, 6), arm('R', 78, -6, 16), arm('L', 70, -10, 22), leg('L', 26, 22), leg('R', -18, 14)),
    'reach_one': P(spine(20, 5, 10, 14, 15, 0), arm('R', 85, -4, 8), arm('L', 10, 6, 12), leg('R', 20, 26), leg('L', -12, 8)),
    'runner':    P(spine(38, 0, 0, -6, 0, 0), arm('R', -40, 10, 50), arm('L', 55, 0, 60), leg('L', 48, 60), leg('R', -36, 30)),
    'hunch':     P(spine(42, -10, 0, 30, 20, -20), arm('R', 20, 0, 40), arm('L', 20, 0, 40), leg('L', 12, 30), leg('R', -8, 26)),
    'lich':      P(spine(-4, 0, 6, -12, 0, 0), arm('R', 60, 40, 70), arm('L', 30, 10, 20)),
    'knight':    P(spine(10, 0, -12, 6, -10, 0), arm('R', 20, 8, 30), arm('L', 25, 6, 35), leg('R', 24, 18), leg('L', -14, 8)),
    'king':      P(spine(2, 0, 10, -8, 8, 0), arm('R', 6, 6, 10), arm('L', 30, 0, 50), leg('L', 10, 6), leg('R', -6, 4)),
    'wailer':    P(spine(12, 0, 0, -30, 0, 0), arm('R', 30, 40, 20), arm('L', 30, 40, 20), leg('L', 8, 10), leg('R', -6, 8)),
}

# --------------------------------------------------------------------------------------------------------- world
def noise_tex(n=1024, seed=0, beta=2.0):
    """periodic fbm by spectral synthesis (tiles without a seam)"""
    r = np.random.default_rng(seed)
    f = np.fft.fftfreq(n)[:, None] ** 2 + np.fft.fftfreq(n)[None, :] ** 2
    f[0, 0] = 1
    spec = np.fft.fft2(r.normal(size=(n, n))) / f ** (beta / 2)
    spec[0, 0] = 0
    x = np.real(np.fft.ifft2(spec))
    return ((x - x.min()) / np.ptp(x)).astype(np.float32)

NT = noise_tex(1024, 3)
NT2 = noise_tex(1024, 8)

def sample(nt, x, y, scale):
    """bilinear, tiling"""
    n = nt.shape[0]
    fx = (np.asarray(x, float) / scale % 1) * n; fy = (np.asarray(y, float) / scale % 1) * n
    x0 = np.floor(fx).astype(int) % n; y0 = np.floor(fy).astype(int) % n; x1 = (x0 + 1) % n; y1 = (y0 + 1) % n
    tx = fx - np.floor(fx); ty = fy - np.floor(fy)
    return (nt[y0, x0] * (1 - tx) + nt[y0, x1] * tx) * (1 - ty) + (nt[y1, x0] * (1 - tx) + nt[y1, x1] * tx) * ty

def ground_h(x, y):
    """the land: a valley floor near the camera, the barrow ridge rising behind on the left, the outcrop rising right"""
    h = 14 * np.sin(x / 520.0 + 0.6) + 10 * np.cos(y / 380.0)
    t = np.clip((y - 900) / 1700, 0, 1)
    h += t * t * (3 - 2 * t) * 520 * np.exp(-((x + 560) / 1400) ** 2)                       # the ridge to the barrow
    h += np.clip((x - 300) / 500, 0, 1) * 120 * np.exp(-((y - 650) / 700) ** 2)              # rising to the outcrop
    h += (sample(NT, x, y, 3000) - 0.5) * 80
    return h

def ground_n(pw):
    """the ground's normal straight from the height function (smooth, no facets), with fine wind-ripples in the snow"""
    x, y = pw[..., 0], pw[..., 1]; e = 6.0
    hx = (ground_h(x + e, y) - ground_h(x - e, y)) / (2 * e); hy = (ground_h(x, y + e) - ground_h(x, y - e)) / (2 * e)
    rip = (sample(NT2, x * 3, y * 9, 900) - 0.5) * 0.12
    n = np.stack([-hx + rip, -hy + rip * 0.5, np.ones_like(hx)], -1)
    return n / np.linalg.norm(n, axis=-1, keepdims=True)


def snow(pw):
    x, y, z = pw[..., 0], pw[..., 1], pw[..., 2]
    n = sample(NT2, x, y, 900); n2 = sample(NT, x, y, 240)
    base = np.array([0.46, 0.55, 0.76])
    rock = np.array([0.12, 0.12, 0.14])
    patch = np.clip((0.40 - n) * 6, 0, 1) * np.clip((0.5 - n2) * 4 + 0.5, 0, 1)
    n3 = sample(NT2, x * 1.7 + y * 0.3, y * 5.0, 140)        # wind-drift streaks
    n4 = sample(NT, x * 4.0, y * 4.0, 90)                     # crust and footprints of the dead
    c = base * (0.80 + 0.12 * n2 + 0.10 * n3 - 0.08 * np.clip((n4 - 0.6) * 4, 0, 1))[..., None]
    # dirty tracks where the horde has trampled (darker, bluish-grey)
    trod = np.clip((sample(NT2, x * 0.6, y * 0.25, 700) - 0.55) * 5, 0, 1) * np.clip((y - 300) / 600, 0, 1)
    c = c * (1 - 0.35 * trod[..., None]) + np.array([0.22, 0.24, 0.32]) * (0.35 * trod)[..., None]
    return c * (1 - patch[..., None]) + rock * patch[..., None]

def stone(pw):
    x, y, z = pw[..., 0], pw[..., 1], pw[..., 2]
    n = sample(NT, x + z * 0.7, y - z * 0.4, 300); n2 = sample(NT2, x * 1.3, z, 120)
    n3 = sample(NT3, x * 2.2 + z, y * 2.2 - z, 60)
    strata = 0.5 + 0.5 * np.sin(z * 0.07 + n * 5.0)                       # layered rock, bent by the noise
    base = np.array([0.14, 0.15, 0.17]) * (1 - 0.35 * n3)[..., None] + np.array([0.20, 0.19, 0.18]) * (0.35 * n3)[..., None]
    c = base * (0.45 + 0.9 * n)[..., None] * (0.75 + 0.45 * n2)[..., None] * (0.9 + 0.14 * strata)[..., None]
    lichen = np.clip((n2 - 0.62) * 5, 0, 1)
    c = c * (1 - lichen[..., None] * 0.5) + np.array([0.24, 0.26, 0.20]) * lichen[..., None] * 0.5
    snowcap = np.clip((n2 - 0.35) * 3, 0, 1) * 0.0
    return c

NT3 = noise_tex(1024, 17, beta=2.1)   # detail noise: grit, pores, cracks (smooth enough not to sparkle)


def relief(scale, amp, grime=0.0, sparkle=0.0, crack=0.0, fine=False, snowcap=0.0):
    """surface detail in world space: a bump from layered noise (its gradient tilts the normal), grime darkening the
    hollows, ice glints on snow, dark crack lines on stone"""
    def h(p):
        x, y, z = p[..., 0], p[..., 1], p[..., 2]
        v = sample(NT3, x + z * 0.61, y - z * 0.47, scale) * 0.6 + sample(NT, x * 1.9 - z * 0.8, y * 1.9 + z * 0.9, scale * 1.7) * 0.4
        if fine:  # a finer octave: wind-cut ripples and grit
            v = v * 0.75 + sample(NT2, x * 3.1 + z, y * 3.1 - z * 0.5, scale * 0.9) * 0.25
        return v
    def f(pw, nv, alb):
        e = scale * 0.05
        h0 = h(pw)
        g = np.stack([(h(pw + np.array([e, 0, 0])) - h0) / e, (h(pw + np.array([0, e, 0])) - h0) / e, (h(pw + np.array([0, 0, e])) - h0) / e], -1)
        g = g - (g * nv).sum(-1, keepdims=True) * nv
        n2 = nv - g * (scale * amp)
        n2 = n2 / (np.linalg.norm(n2, axis=-1, keepdims=True) + 1e-9)
        a2 = alb * (1 - grime + grime * 2 * np.clip(h0, 0, 1))[..., None]
        if crack > 0:
            c = np.abs(sample(NT3, pw[..., 0] + pw[..., 2], pw[..., 1] - pw[..., 2] * 0.3, scale * 3.0) - 0.5)
            a2 = a2 * (1 - crack * np.clip(1 - c / 0.025, 0, 1))[..., None]
        if snowcap > 0:  # snow lying on the tops of the rocks
            up = np.clip((n2[..., 2] - 0.55) / 0.3, 0, 1) * np.clip((sample(NT, pw[..., 0], pw[..., 1], 180) - 0.3) * 3, 0, 1)
            a2 = a2 * (1 - snowcap * up[..., None]) + np.array([0.50, 0.56, 0.72]) * (snowcap * up)[..., None]
        if sparkle > 0:
            gl = np.clip((sample(NT3, pw[..., 0] * 2.3, pw[..., 1] * 2.3, scale) - 0.965) * 25, 0, 1)
            a2 = a2 + sparkle * gl[..., None]
        return n2, a2
    return f


MTN = None
def far_mountains(sc):
    global MTN
    n = 512
    ridged = MP.fbm(n, n, 7, 3, seed=61, ridged=True)
    MTN = (ridged - ridged.min()) / np.ptp(ridged)
    def h(x, y):
        u = np.clip((np.asarray(x) + 16000) / 32000, 0, 1) * (n - 1); v = np.clip((np.asarray(y) - 5000) / 12000, 0, 1) * (n - 1)
        x0 = np.floor(u).astype(int).clip(0, n - 2); y0 = np.floor(v).astype(int).clip(0, n - 2); tx = u - x0; ty = v - y0
        m = (MTN[y0, x0] * (1 - tx) + MTN[y0, x0 + 1] * tx) * (1 - ty) + (MTN[y0 + 1, x0] * (1 - tx) + MTN[y0 + 1, x0 + 1] * tx) * ty
        rise = np.clip((np.asarray(y) - 5600) / 2600, 0, 1)
        side = np.exp(-((np.asarray(x) - 1500) / 9000) ** 2) * 0.6 + 0.4
        return 200 + m ** 1.6 * 3600 * rise * side
    V, T, UV = S.grid_mesh(-16000, 16000, 5400, 17000, 360, 140, h)
    def sn(pw):
        z = pw[..., 2]; nn = sample(NT, pw[..., 0], pw[..., 1], 2200)
        snowy = np.clip((z - 700 + nn * 700) / 600, 0, 1)
        return np.array([0.16, 0.16, 0.19]) * (1 - snowy[..., None]) + np.array([0.70, 0.74, 0.86]) * snowy[..., None]
    sc.add(V, T, UV, proc=sn).layer = 'far'

def build():
    sc = S.Scene()
    # the land, in two pieces: the near ground (near layer) and everything beyond (mid layer)
    ys = np.r_[np.linspace(-100, 1200, 70), np.linspace(1240, 5600, 90)]
    xs = np.linspace(-5200, 5200, 260)
    X, Y = np.meshgrid(xs, ys); Z = ground_h(X, Y)
    V = np.c_[X.ravel(), Y.ravel(), Z.ravel()]; UV = np.c_[X.ravel() / 200, Y.ravel() / 200]
    idx = np.arange(V.shape[0]).reshape(len(ys), len(xs))
    a_, b_, c_, d_ = idx[:-1, :-1].ravel(), idx[:-1, 1:].ravel(), idx[1:, 1:].ravel(), idx[1:, :-1].ravel()
    T = np.r_[np.c_[a_, b_, c_], np.c_[a_, c_, d_]]
    cy = V[T].mean(1)[:, 1]
    # the ground is one piece in the mid layer (split across layers it showed a seam when the layers lean apart)
    it = sc.add(V, T, UV, proc=snow); it.layer = 'mid'; it.nfun = ground_n; it.detail = relief(70, 0.2, grime=0.12, sparkle=0.4, fine=True)
    _ = cy
    rng = np.random.default_rng(31)
    # the barrow: an arch of standing slabs on the ridge, a lintel, a broken capstone, stones round it
    ax, ay = -560.0, 2650.0; az = float(ground_h(np.array(ax), np.array(ay)))
    def slab(seed, x, y, sx, sy, sz, yaw=0.0, tilt=(0, 0), layer='mid'):
        V, T, UV = S.rock_mesh(seed, sx, sy, sz, sub=3, rough=0.6)
        Rm = S.rot((1, 0, 0), tilt[0]) @ S.rot((0, 1, 0), tilt[1]) @ S.rot((0, 0, 1), yaw)
        V = V @ Rm.T
        z = float(ground_h(np.array(x), np.array(y)))
        it = sc.add(V + np.array([x, y, z + sz * 0.2 - 4]), T, UV, proc=stone); it.layer = layer
        it.detail = relief(70, 0.6, grime=0.4, crack=0.45, fine=True, snowcap=0.7)
        return it
    s1 = slab(1, ax - 190, ay, 62, 52, 290, 8, (0, -3))
    s2 = slab(2, ax + 190, ay + 20, 64, 54, 300, -6, (0, 4))
    top = min(s1.V[:, 2].max(), s2.V[:, 2].max()) - az - 12
    lin = S.rock_mesh(3, 290, 60, 50, sub=3, rough=0.5)
    _it = sc.add(lin[0] + np.array([ax, ay + 10, az + top + 10]), lin[1], lin[2], proc=stone); _it.layer = 'mid'; _it.detail = relief(70, 0.6, grime=0.4, crack=0.45, fine=True, snowcap=0.7)
    cap = S.rock_mesh(4, 110, 60, 56, sub=2)
    _it = sc.add(cap[0] @ S.rot((0, 1, 0), 25).T + np.array([ax - 50, ay + 10, az + top + 62]), cap[1], cap[2], proc=stone); _it.layer = 'mid'; _it.detail = relief(70, 0.6, grime=0.4, crack=0.45, fine=True, snowcap=0.7)
    for i, (dx, dy, hh) in enumerate([(-420, 60, 160), (-300, -40, 110), (340, -30, 150), (470, 80, 200), (620, 0, 100), (-600, 140, 120)]):
        slab(10 + i, ax + dx, ay + dy, 36 + rng.uniform(-6, 8), 30, hh, rng.uniform(-30, 30), (rng.uniform(-6, 6), rng.uniform(-8, 8)))
    # the rune-stone of the Dirge, in front, with glowing runes (a separate emissive strip down its face)
    rx, ry = 95.0, 420.0
    rs = slab(40, rx, ry, 22, 15, 92, 8, (0, 2), layer='near')
    rz = float(ground_h(np.array(rx), np.array(ry)))
    rv, rt, ru = [], [], []
    for i in range(6):
        zc = rz + 22 + i * 12; w_, h_ = 5.0, 6.0
        x0 = rx - 2 + (i % 2) * 2; y0 = ry - 21.0
        base = len(rv)
        rv += [(x0 - w_, y0, zc - h_), (x0 + w_, y0, zc - h_), (x0 + w_, y0, zc + h_), (x0 - w_, y0, zc + h_)]
        ru += [(0, 0), (1, 0), (1, 1), (0, 1)]; rt += [(base, base + 1, base + 2), (base, base + 2, base + 3)]
    rune_glow = np.zeros((32, 32, 4), np.float32); yy_, xx_ = np.mgrid[0:32, 0:32]
    shape = ((np.abs(xx_ - 16) < 3) | (np.abs((yy_ - 4) - (xx_ - 16) * 1.2) < 3.2) & (yy_ < 18)).astype(np.float32)
    rune_glow[..., 0] = 0.55 * shape; rune_glow[..., 1] = 0.22 * shape; rune_glow[..., 2] = 0.95 * shape; rune_glow[..., 3] = shape
    it = sc.add(np.array(rv), np.array(rt), np.array(ru), albedo=rune_glow * 0.3, glow=rune_glow, emit=3.0, alpha_test=True); it.layer = 'near'
    # graves: leaning burial stones in the snow, near and mid
    for i in range(14):
        gx = rng.uniform(-1300, 300); gy = rng.uniform(380, 1400)
        if abs(gx - rx) < 120 and abs(gy - ry) < 120:
            continue
        slab(60 + i, gx, gy, rng.uniform(18, 28), rng.uniform(8, 12), rng.uniform(34, 60), rng.uniform(-40, 40), (rng.uniform(-14, 14), rng.uniform(-12, 12)),
             layer='near' if gy < 720 else 'mid')
    # the outcrop on the right: big dark boulders stacked up out of the snow (the menu sits over them)
    for i, (x, y, sx, sy, sz) in enumerate([(330, 470, 170, 140, 120), (470, 640, 220, 190, 200), (250, 330, 110, 90, 60), (600, 470, 220, 180, 260), (420, 290, 140, 110, 50), (560, 860, 260, 220, 300)]):
        slab(80 + i, x, y, sx, sy, sz, rng.uniform(0, 180), (rng.uniform(-10, 10), rng.uniform(-10, 10)), layer='near')
    # the undead
    models = {}
    def model(n):
        if n not in models:
            path, tex = MODELS[n]
            models[n] = S.Model(path, tex, f'/tmp/claude-0/ex/{n}', tex_size=1024)
        return models[n]
    import json
    sockets = json.loads((pathlib.Path(__file__).parent / 'eyes_sockets.json').read_text())
    sock_id = {'zombie': 'zombie', 'hooded': 'runner', 'hag': 'wailer', 'abom': 'bloater', 'reaper': 'reaper', 'necro': 'necro',
               'knight': 'knight', 'king': 'king', 'lich': 'lich'}
    camf = np.array(CAM['look'], float) - np.array(CAM['eye'], float); camf /= np.linalg.norm(camf)
    camr = np.cross(camf, [0, 0, 1]); camr /= np.linalg.norm(camr); camu = np.cross(camr, camf)
    glow_disc = np.zeros((32, 32, 4), np.float32); yy_, xx_ = np.mgrid[0:32, 0:32]
    dd = np.sqrt((xx_ - 15.5) ** 2 + (yy_ - 15.5) ** 2) / 16
    glow_disc[..., 0] = 1.0; glow_disc[..., 1] = 0.12; glow_disc[..., 2] = 0.06; glow_disc[..., 3] = (dd < 0.9).astype(np.float32)
    def put(n, pose, x, y, yaw, scale=1.0, layer='mid', emit=1.0):
        m = model(n); posed = m.pose(POSES[pose])
        z = float(ground_h(np.array(x), np.array(y)))
        n0 = len(sc.items)
        xf = S.place(x, y, z - 2, yaw, scale)
        sc.add_model(m, posed, xf, emit=emit)
        for it in sc.items[n0:]:
            it.layer = layer
        # red eyes burning in its sockets: each socket skinned like the nearest vertex of the face, then a small
        # glowing disc there (it lights the bloom; the face around it hides it where it should)
        T = S.SKEL.transforms(POSES[pose])
        for e in sockets.get(sock_id.get(n, ''), []):
            e = np.array(e, float)
            best = None
            for si, sh in enumerate(m.shapes):
                d2 = ((sh.pos - e) ** 2).sum(1); vi = int(np.argmin(d2))
                if best is None or d2[vi] < best[0]:
                    best = (d2[vi], si, vi)
            _, si, vi = best; sh = m.shapes[si]
            acc = np.zeros(3); tot = 0.0
            for k in range(4):
                w = sh.ww[vi, k]
                if w <= 0:
                    continue
                M = T.get(sh.bones[sh.wb[vi, k]], np.eye(4))
                acc += w * (M[:3, :3] @ e + M[:3, 3]); tot += w
            pe = acc / max(tot, 1e-6)
            pw = xf[:3, :3] @ pe + xf[:3, 3]
            pw = pw - camf * 0.6 * scale                      # a hair toward the eye, out of the socket's shadow
            r_ = 1.0 * scale
            V = np.array([pw - camr * r_ - camu * r_, pw + camr * r_ - camu * r_, pw + camr * r_ + camu * r_, pw - camr * r_ + camu * r_])
            it = sc.add(V, np.array([[0, 1, 2], [0, 2, 3]]), np.array([[0, 1], [1, 1], [1, 0], [0, 0]], float), albedo=glow_disc, glow=glow_disc, emit=5.0, alpha_test=True)
            it.layer = layer
            it.detail = relief(22, 0.3, grime=0.3)   # rot, grit and torn skin over the model's own maps
    # champions: the lich in the barrow door, staff raised; the knight and the king leading the horde down the ridge
    put('lich', 'lich', ax + 10, ay - 40, 190, 1.15)
    put('knight', 'knight', -360, 1650, 200, 1.12)
    put('king', 'king', -700, 1820, 170, 1.1)
    # the horde: down the ridge from the barrow, the front ranks close and large on the left of the rune-stone
    kinds = ['zombie', 'zombie', 'zombie', 'hooded', 'hag', 'abom', 'reaper', 'necro']
    poses = ['shamble_a', 'shamble_b', 'reach', 'reach_one', 'runner', 'hunch']
    spots = []
    for i in range(34):
        t = rng.random() ** 0.7
        x = ax + rng.normal(0, 90) - t * 380 + rng.normal(0, 260) * t
        y = ay - 120 - t * 1950 + rng.normal(0, 60)
        spots.append((y, x))
    spots += [(640, -520), (560, -330), (780, -760), (520, -140), (900, -420), (720, -60), (1000, -980), (600, -880)]
    for j, (y, x) in enumerate(sorted(spots, reverse=True)):
        k = kinds[j % len(kinds)] if y > 1000 else ['zombie', 'hooded', 'abom', 'hag', 'zombie'][j % 5]
        pose = 'wailer' if k == 'hag' and j % 2 else poses[j % len(poses)]
        if k == 'abom':
            pose = 'hunch' if j % 2 else 'shamble_b'
        yaw = 180 + rng.normal(0, 22) + (x + 300) * 0.01
        put(k, pose, x, y, yaw, rng.uniform(0.95, 1.08), layer='near' if y < 760 else 'mid')
    return sc, (rx, ry, rz + 150), (ax, ay)


# ------------------------------------------------------------------------------------------------------- shading
def shade(sc, layer, D, G, camv, sm, rw, rh):
    eye, f, r, u, fl = camv
    Pw, N, A, E = G['P'], G['N'], G['A'], G['E']
    hit = np.isfinite(D)
    V = eye - Pw; dist = np.linalg.norm(V, axis=-1) + 1e-6; V /= dist[..., None]
    ndl = np.clip((N * MOON_DIR).sum(-1), 0, 1)
    lit = S.Scene.shadowed(Pw + N * 14.0, sm, bias=10.0) if sm is not None else 1.0
    ndv = np.clip((N * V).sum(-1), 0, 1)
    rim = (1 - ndv) ** 3 * np.clip((N * MOON_DIR).sum(-1) + 0.35, 0, 1) * 1.4
    hemi = 0.5 + 0.5 * N[..., 2]
    amb = SKY_C * hemi[..., None] + GROUND_C * (1 - hemi[..., None])
    fill = np.clip((N * FILL_DIR).sum(-1), 0, 1)
    key = np.where(N[..., 2] > 0.8, 0.2, 0.42)[..., None]
    col = A * (amb * 0.9 + FILL_C * fill[..., None] * 0.6 + MOON_C * (ndl * lit)[..., None] * key) \
        + MOON_C * (rim * (0.35 + 0.65 * lit))[..., None] * (0.25 + A.mean(-1, keepdims=True)) * 2.2 + E * 1.4
    # fog: distance and height, warmer toward the moon
    fz = Pw[..., 2]
    fog = 1 - np.exp(-(dist / 3000.0) ** 1.25) * np.exp(-np.clip(260 - fz, 0, 500) / 1800.0)
    mie = np.clip((-V * MOON_DIR).sum(-1), 0, 1) ** 8
    fc = FOG_C + np.array([0.26, 0.07, 0.04]) * mie[..., None]
    col = col * (1 - fog[..., None]) + fc * fog[..., None]
    if True:  # the foreground sits in shadow: darker the closer it is, so the eye goes to the horde
        col = col * (0.28 + 0.72 * np.clip((dist - 300) / 1400, 0, 1) ** 1.3)[..., None]
    col = tonemap(col)
    return np.where(hit[..., None], col, 0), hit.astype(np.float32)


def tonemap(x, exposure=1.0):
    x = np.maximum(x * exposure, 0)
    a, b, c, d, e = 2.51, 0.03, 2.43, 0.59, 0.14   # ACES (Narkowicz)
    return np.clip((x * (a * x + b)) / (x * (c * x + d) + e), 0, 1) ** (1 / 1.1)


def render_layers():
    sc, glow_pt, arch = build()
    eye = np.array(CAM['eye'])
    print('scene:', len(sc.items), 'items', sum(len(i.T) for i in sc.items), 'triangles')
    sm = sc.shadow_map(MOON_DIR, np.array([-300.0, 1500.0, 100.0]), 5200.0, res=2048)
    out = {}
    for layer in ('mid', 'near'):
        sub = S.Scene(); sub.items = [i for i in sc.items if getattr(i, 'layer', 'mid') == layer]
        D, G, camv = sub.raster(CAM, RW, RH)
        col, a = shade(sub, layer, D, G, camv, sm if layer != 'far' else None, RW, RH)
        out[layer] = (col, a, D, G)
        print(layer, 'done')
    # where the rune-stone and the lich's staff land on screen (for the menu's pulsing lights)
    _, _, camv = S.Scene().raster(CAM, 4, 4, want_shading=False)
    e, f, r, u, fl = camv
    def proj(p):
        rel = np.asarray(p) - e; cz = rel @ f
        return [float(W / 2 + (fl / 4 * H / 4) * 0 + (rel @ r) / cz * (0.5 * H / math.tan(math.radians(CAM['fov']) / 2))),
                float(H / 2 - (rel @ u) / cz * (0.5 * H / math.tan(math.radians(CAM['fov']) / 2)))]
    glow = {'rune': proj(glow_pt), 'arch': proj((arch[0], arch[1], float(ground_h(np.array(arch[0]), np.array(arch[1]))) + 300))}
    return out, glow


def post(col, a, size, G=None):
    if G is not None:
        e = np.clip(G['E'] * 1.4, 0, 4).astype(np.float32)
        from scipy.ndimage import gaussian_filter
        glow = sum(np.dstack([gaussian_filter(e[..., k], rad) for k in range(3)]) * wgt
                   for rad, wgt in ((4 * SCALE + 1, 0.6), (18 * SCALE + 2, 0.5), (60 * SCALE + 4, 0.35)))
        ga = np.clip(glow.max(-1), 0, 1)
        col = col * a[..., None] + glow
        a = np.clip(a + ga * (1 - a), 0, 1)
        col = np.where(a[..., None] > 1e-4, col / np.maximum(a[..., None], 1e-4), 0)
    pm = np.clip(col, 0, 1) * a[..., None]
    rs = lambda x: np.asarray(Image.fromarray(x.astype(np.float32)).resize(size, Image.LANCZOS), np.float32)
    A2 = np.clip(rs(a), 0, 1); C2 = np.dstack([rs(pm[..., k]) for k in range(3)])
    C2 = np.where(A2[..., None] > 1e-3, C2 / np.maximum(A2[..., None], 1e-3), 0)
    return Image.fromarray((np.clip(np.dstack([C2, A2]), 0, 1) * 255).astype(np.uint8), 'RGBA')


def drift_clouds(w=2048, h=640):
    """night clouds that drift across the sky: wisps that tile left-right, lit warm on the moon side, thin at the bottom"""
    a1 = noise_tex(1024, 77, beta=2.3); a2 = noise_tex(1024, 78, beta=1.8)
    x = np.arange(w)[None, :] / w; y = np.arange(h)[:, None] / h
    n = sample(a1, x * 1024, y * 1024 * 0.55, 1024) * 0.7 + sample(a2, x * 1024 * 2, y * 1024 * 1.2, 1024) * 0.3
    a = np.clip((n - 0.42) / 0.38, 0, 1) ** 1.5 * np.clip(1.2 - y * 1.3, 0, 1) * 0.85
    lit = np.clip((n - np.roll(n, 6, axis=1)) * 8 + 0.5, 0, 1)   # the side toward the moon (left) catches its light
    rgb = np.dstack([0.16 + 0.22 * lit, 0.15 + 0.10 * lit, 0.20 + 0.06 * lit]) * np.ones((h, w, 1))
    return Image.fromarray((np.dstack([np.clip(rgb, 0, 1), a]) * 255).astype(np.uint8), 'RGBA')


def ground_fog(w=1024, h=256):
    """the fog that rolls over the ground: wisps that tile left-right (the menu scrolls two copies against each other),
    thin at the top edge, thick low down"""
    a1 = noise_tex(1024, 91, beta=2.2); a2 = noise_tex(1024, 92, beta=1.7)
    x = np.arange(w)[None, :] / w; y = np.arange(h)[:, None] / h
    n = sample(a1, x * 1024 * 1, y * 1024 * 0.25 + 0 * x, 1024) * 0.65 + sample(a2, x * 1024 * 3, y * 1024 * 0.8, 1024) * 0.35
    wisps = np.clip((n - 0.30) / 0.45, 0, 1) ** 1.4
    prof = np.clip(y / 0.45, 0, 1) ** 1.6 * (0.75 + 0.25 * np.clip((y - 0.45) / 0.55, 0, 1))
    a = np.clip(wisps * prof * 1.15, 0, 1)
    rgb = np.ones((h, w, 3), np.float32) * np.array([0.78, 0.82, 0.92])
    return Image.fromarray((np.dstack([rgb, a[..., None] * np.ones((h, w, 1))]) * 255).astype(np.uint8)[..., :4], 'RGBA')


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    sky = MP.sky()
    Image.fromarray((np.clip(MP.grain(sky), 0, 1) * 255).astype(np.uint8)).save(OUT / 'menu_sky.jpg', quality=92)
    cv, ca = MP.clouds_veil(); MP.to_img(cv, ca).save(OUT / 'menu_clouds.png')
    ar, aa = MP.aurora(); MP.to_img(ar, aa).resize((1280, 720), Image.LANCZOS).save(OUT / 'menu_aurora.png')
    ground_fog().save(OUT / 'menu_groundfog.png')
    drift_clouds().save(OUT / 'menu_clouds2.png')
    layers, glow = render_layers()
    far_rgb, far_a, _ = MP.range_layer(61, H * 0.47, H * 0.20, 7, 1.0, 0.55, snow=1.0)
    farther_rgb, farther_a, _ = MP.range_layer(62, H * 0.42, H * 0.16, 6, 0.8, 0.72, snow=1.0, xscale=1.6)
    fa = np.clip(far_a + farther_a * (1 - far_a), 0, 1)
    frgb = (farther_rgb * farther_a[..., None] * (1 - far_a[..., None]) + far_rgb * far_a[..., None]) / np.maximum(fa[..., None], 1e-4)
    from scipy.ndimage import gaussian_filter
    fa = np.clip(gaussian_filter(fa, 2.2) * 1.05, 0, 1)        # no cut-out edges: the peaks melt into the night
    yy = np.mgrid[0:H, 0:W][0] / H
    frgb = frgb * np.array([0.62, 0.62, 0.80]) * 0.58           # darker: far away, under a night sky
    frgb = frgb * (1 - 0.55 * np.clip((yy - 0.38) / 0.12, 0, 1)[..., None]) + np.array([0.09, 0.10, 0.17]) * 0.55 * np.clip((yy - 0.38) / 0.12, 0, 1)[..., None]
    MP.to_img(MP.grain(frgb), fa).save(OUT / 'menu_far.png')
    for name in ('mid', 'near'):
        col, a, _, G = layers[name]
        post(col, a, (W, H), G).save(OUT / f'menu_{name}.png')
    json.dump(glow, open(OUT / 'menu_glow.json', 'w'), indent=1)
    print('glow points', glow)
    if PREVIEW:
        base = Image.open(OUT / 'menu_sky.jpg').convert('RGBA')
        for name in ('far', 'mid', 'near'):
            base.alpha_composite(Image.open(OUT / f'menu_{name}.png'))
        base.convert('RGB').resize((1600, 900), Image.LANCZOS).save(PREVIEW, quality=90)


if __name__ == '__main__':
    main()
