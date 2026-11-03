"""The golden light orb (Noah: it replaces the belt lantern - "feel and look like a hyper realistic orb for providing
light ... animated circulating and showing light beams"). A small sun that floats just above the player's head: placed
in the world scene graph and moved there every frame by code (Spellcraft::LoadPath + Fx::Place), so everything that
moves here runs on its own looping controllers from load. Centred on the origin, Z up, no collision, 10 shapes.

    python3 tools/lightorb.py [--preview <png>] [<Data dir>]      (default: assets/fx)
    ->  <Data>/Meshes/CurseOfAlduin/fx/lightorb.nif, <Data>/Textures/CurseOfAlduin/fx/lightorb_*.dds

Unlit, additive layers (tools/modelconv/fxwrite), warm gold-white: white-hot -> pale gold -> amber at every edge.
  camera-facing (NiBillboardNode):
    LO_core     the white-hot heart, ~11 across: a flat-topped bright disc with a soft limb
    LO_corona   gold corona to ~30 across: fine radial filaments on a polar disc (u round it, v out), turning slowly
                (u scroll) and drifting outward (v scroll) - the shimmer
    LO_raysA/B  god rays: two polar discs of thin tapered rays of many lengths (to ~45 out), one turning one way
                and the other the other way (u scroll; B's u runs backwards round the disc) - the rays sweep and cross
    LO_halo     bloom: a broad soft gold glow ~84 wide with a faint lens ghost ring, almost nothing at its edge
    LO_flare    a faint, thin anamorphic streak through the core (lens flare)
  in the world (each under its own spinning node - tools/modelconv/spinwrap):
    LO_spikesA  long thin tapered beams radiating in 3D (crossed cards), turning about Z           (node LO_SpinA)
    LO_spikesB  a second, sparser set turning the other way                                         (node LO_SpinB)
    LO_motesA/B motes of light on two tilted rings, each ring spinning about its own normal,
                opposite ways                                                         (nodes LO_MotesA / LO_MotesB)
build/lightorb is emptied afterwards.
"""
import math, pathlib, shutil, subprocess, sys
import numpy as np
from PIL import Image, ImageDraw

ROOT = pathlib.Path(__file__).resolve().parent.parent
TOOLS = pathlib.Path('/home/claude/tools')
FXWRITE = str(TOOLS / 'fxwrite')
WORK = ROOT / 'build' / 'lightorb'
TEXREL = 'CurseOfAlduin\\fx\\'

# the warm ramp, by radius out of 1: white-hot, pale gold, gold, amber (pushed a little red: the game's tonemap washes
# additive light cold - see tools/blackhole.py)
GOLD = [(0.0, (1.0, 0.98, 0.92)), (0.2, (1.0, 0.88, 0.58)), (0.5, (1.0, 0.68, 0.24)), (0.78, (1.0, 0.48, 0.09)), (1.0, (0.88, 0.3, 0.03))]

# spins (turns a second) and their axes
SPIN_A, SPIN_B = 1 / 26.0, 1 / 34.0          # the 3D beams
MOTE_A, MOTE_B = 1 / 7.0, 1 / 9.5            # the mote rings
TILT_A, TILT_B = (math.radians(28), math.radians(20)), (math.radians(-35), math.radians(110))   # (tilt from Z, azimuth)
# u-scroll periods (s per whole turn of the texture round its disc)
CORONA_U, CORONA_V, RAYS_A, RAYS_B = 40.0, 6.0, 30.0, 44.0


def ramp(t, stops=GOLD):
    t = float(np.clip(t, 0, 1))
    for (a, ca), (b, cb) in zip(stops[:-1], stops[1:]):
        if t <= b:
            k = (t - a) / max(1e-6, b - a)
            return tuple(ca[i] * (1 - k) + cb[i] * k for i in range(3))
    return stops[-1][1]


def ramp_img(R, stops=GOLD):
    out = np.zeros(R.shape + (3,), np.float32)
    xs = [s[0] for s in stops]
    for c in range(3):
        out[..., c] = np.interp(np.clip(R, 0, 1), xs, [s[1][c] for s in stops])
    return out


# ------------------------------------------------------------------ textures
def save(arr, data, name):
    """arr: HxWx4 float 0..1 (rgb, alpha = strength)"""
    p = pathlib.Path(data) / 'Textures' / 'CurseOfAlduin' / 'fx' / name
    p.parent.mkdir(parents=True, exist_ok=True); WORK.mkdir(parents=True, exist_ok=True)
    tmp = WORK / (name + '.png')
    Image.fromarray((np.clip(arr, 0, 1) * 255 + 0.5).astype(np.uint8), 'RGBA').save(tmp)
    subprocess.run(['convert', str(tmp), '-define', 'dds:compression=dxt5', '-define', 'dds:mipmaps=8', str(p)], check=True)
    return TEXREL + name


def tileable(h, w, beta, seed, aniso=(1.0, 1.0)):
    rng = np.random.default_rng(seed)
    fx, fy = np.meshgrid(np.fft.fftfreq(w) * w / max(w, h) * aniso[0], np.fft.fftfreq(h) * h / max(w, h) * aniso[1])
    rad = np.sqrt(fx ** 2 + fy ** 2); rad[0, 0] = 1
    spec = (rng.normal(size=(h, w)) + 1j * rng.normal(size=(h, w))) / rad ** beta; spec[0, 0] = 0
    o = np.real(np.fft.ifft2(spec)).astype(np.float32)
    return (o - o.min()) / (o.max() - o.min())


def polar_grid(n):
    yy, xx = np.mgrid[0:n, 0:n].astype(np.float32)
    X = (xx + 0.5) / n * 2 - 1; Y = 1 - (yy + 0.5) / n * 2
    return X, Y, np.hypot(X, Y)


def white(a):
    return np.dstack([np.ones(a.shape + (3,), np.float32), a[..., None]])


def textures(data):
    T = {}
    # core: a white-hot disc, flat-topped (a ball of light, not a blob), its limb rolling off softly into pale gold,
    # with a short exponential skirt so it melts into the corona (the card is 2x the core's radius)
    X, Y, R = polar_grid(256)
    disc = 1 / (1 + np.exp((R - 0.48) / 0.035))                 # the ball, r 0.48 of the half-width
    skirt = np.exp(-np.maximum(R - 0.45, 0) / 0.11) * 0.55 * np.clip((1 - R) / 0.25, 0, 1)
    a = np.clip(disc + skirt * (1 - disc), 0, 1)
    rgb = ramp_img(np.clip((R - 0.25) / 0.9, 0, 1) * 0.6)      # white in the middle, pale gold at the limb
    rgb = rgb + (1 - rgb) * np.exp(-(R / 0.2) ** 2)[..., None]  # (a pure white-hot heart)
    T['core'] = save(np.dstack([rgb, a]), data, 'lightorb_core.dds')
    # corona filaments, polar: u round (1024, tiling), v out (256); smooth along v (radial strands), fine across u
    h, w = 256, 1024
    f1 = tileable(h, w, 1.5, 3, aniso=(1.0, 0.08))
    f2 = tileable(h, w, 1.1, 4, aniso=(1.0, 0.25))
    v = (np.arange(h, dtype=np.float32)[:, None] + 0.5) / h
    strands = np.clip((f1 - 0.3) * 1.8, 0, 1) ** 1.3 * (0.6 + 0.6 * f2)
    a = 0.35 + 0.75 * strands                                    # (a base glow, the strands on it)
    T['corona'] = save(white(np.clip(a, 0, 1) * np.ones_like(v)), data, 'lightorb_corona.dds')
    # god rays, polar (u round, v out to the tip): thin rays, each its own length and strength, narrowing to the tip,
    # with a faint fine haze of very thin rays between them
    for key, seed, n_rays in (('raysa', 21, 22), ('raysb', 37, 15)):
        rng = np.random.default_rng(seed)
        h, w = 256, 1024
        U = (np.arange(w, dtype=np.float32)[None, :] + 0.5) / w
        V = (np.arange(h, dtype=np.float32)[:, None] + 0.5) / h
        a = np.zeros((h, w), np.float32)
        for i in range(n_rays):
            c = (i + rng.uniform(-0.3, 0.3)) / n_rays
            L = rng.uniform(0.35, 1.0) ** 0.8                  # its length (of the disc's radius)
            wd = rng.uniform(0.0016, 0.0042)                    # its half-width at the base, in turns
            s = rng.uniform(0.35, 1.0)
            du = (U - c + 0.5) % 1.0 - 0.5
            taper = np.clip(1 - V / L, 0, 1)
            width = wd * (0.25 + 0.75 * taper)
            a += s * np.exp(-(du / width) ** 2) * taper ** 1.6
        haze = tileable(h, w, 1.3, seed + 1, aniso=(1.0, 0.04))
        a += 0.22 * np.clip((haze - 0.45) * 2.5, 0, 1) * np.clip(1 - V / 0.8, 0, 1) ** 2
        a *= np.clip(V / 0.12, 0, 1)                              # (nothing at the middle: the core is there)
        T[key] = save(white(np.clip(a, 0, 1)), data, f'lightorb_{key}.dds')
    # halo: a broad soft bloom (two gaussians and a long tail), gold to amber, near nothing at the edge, and a faint
    # thin ghost ring the way a lens catches a bright point
    X, Y, R = polar_grid(256)
    bloom = 0.55 * np.exp(-(R / 0.22) ** 2) + 0.35 * np.exp(-(R / 0.5) ** 2) + 0.12 * np.exp(-R / 0.35)
    ring = 0.045 * np.exp(-((R - 0.62) / 0.025) ** 2) + 0.02 * np.exp(-((R - 0.66) / 0.08) ** 2)
    a = (bloom + ring) * np.clip((1 - R) / 0.3, 0, 1) ** 2
    rgb = ramp_img(0.3 + 0.7 * np.clip(R / 0.8, 0, 1))
    T['halo'] = save(np.dstack([rgb, np.clip(a, 0, 1)]), data, 'lightorb_halo.dds')
    # flare: a thin horizontal streak, white in the middle, amber out along it
    h, w = 64, 512
    xx = np.abs((np.arange(w, dtype=np.float32)[None, :] + 0.5) / w * 2 - 1)
    yy = np.abs((np.arange(h, dtype=np.float32)[:, None] + 0.5) / h * 2 - 1)
    a = np.exp(-(yy / (0.22 * (1 - 0.7 * xx))) ** 2) * (np.exp(-xx / 0.18) * 0.7 + 0.3 * (1 - xx) ** 2.5)
    rgb = ramp_img(np.broadcast_to(0.2 + 0.8 * xx, (h, w)))
    T['flare'] = save(np.dstack([rgb, np.clip(a, 0, 1)]), data, 'lightorb_flare.dds')
    # mote: a soft spark with a pin-point heart
    X, Y, R = polar_grid(64)
    a = 0.9 * np.exp(-(R / 0.16) ** 2) + 0.35 * np.exp(-(R / 0.45) ** 2)
    a *= np.clip((1 - R) / 0.2, 0, 1)
    T['mote'] = save(np.dstack([ramp_img(R * 0.7), np.clip(a, 0, 1)]), data, 'lightorb_mote.dds')
    # spike: a beam along v (base at v=1, tip at v=0), gaussian across u, fading to the tip
    h, w = 256, 32
    V = (np.arange(h, dtype=np.float32)[:, None] + 0.5) / h
    U = np.abs((np.arange(w, dtype=np.float32)[None, :] + 0.5) / w * 2 - 1)
    a = np.exp(-(U / (0.18 + 0.32 * V)) ** 2) * V ** 1.8 * np.clip((1 - V) / 0.08, 0, 1)
    T['spike'] = save(white(np.clip(a, 0, 1)), data, 'lightorb_spike.dds')
    return T


# ------------------------------------------------------------------ geometry (each shape: verts [x y z u v r g b a], tris)
class Shape:
    def __init__(self, name, tex, scale, extra='', billboard=False, col=(1, 1, 1)):
        self.name, self.tex, self.scale, self.extra, self.billboard, self.col = name, tex, scale, extra, billboard, col
        self.V, self.F = [], []
        self.uscroll = self.vscroll = 0.0
        self.spin = None             # (axis, turns/s) for the preview

    def line(self):
        s = f'shape {self.name} tex={self.tex} color={self.col[0]:.3f},{self.col[1]:.3f},{self.col[2]:.3f},1 scale={self.scale} blend=add'
        if self.billboard: s += ' billboard=1'
        if self.uscroll: s += f' uscroll={self.uscroll}'
        if self.vscroll: s += f' vscroll={self.vscroll}'
        return s + (' ' + self.extra if self.extra else '')

    def lines(self):
        L = [self.line()]
        for x, y, z, u, v, r, g, b, a in self.V:
            L.append(f'v {x:.3f} {y:.3f} {z:.3f} {u:.4f} {v:.4f} {r:.3f} {g:.3f} {b:.3f} {a:.3f}')
        L += [f't {a} {b} {c}' for a, b, c in self.F]
        return L


def quad(S, half_w, half_h, col=(1, 1, 1, 1)):
    o = len(S.V)
    for x, y, u, v in ((-half_w, -half_h, 0, 1), (half_w, -half_h, 1, 1), (half_w, half_h, 1, 0), (-half_w, half_h, 0, 0)):
        S.V.append((x, y, 0, u, v) + tuple(col))
    S.F += [(o, o + 1, o + 2), (o, o + 2, o + 3)]


def polar(S, r0, r1, colfn, urep=1, reverse=False, na=72, nr=10):
    """a flat disc in XY (billboarded): u round it (urep turns of texture; backwards when reverse, so a u scroll turns it
    the other way), v from r0 (0) out to r1 (1), per-vertex colour and fade colfn(t)"""
    o = len(S.V)
    for j in range(nr + 1):
        t = j / nr
        r = r0 + (r1 - r0) * t
        c = colfn(t)
        for i in range(na + 1):
            a = 2 * math.pi * i / na
            u = urep * i / na
            S.V.append((r * math.cos(a), r * math.sin(a), 0.0, -u if reverse else u, t) + tuple(c))
    for j in range(nr):
        for i in range(na):
            a = o + j * (na + 1) + i; b, c, d = a + 1, a + na + 1, a + na + 2
            S.F += [(a, b, c), (b, d, c)]


def basis(d):
    d = np.asarray(d, float); d /= np.linalg.norm(d)
    h = np.array([0, 0, 1.0]) if abs(d[2]) < 0.9 else np.array([1.0, 0, 0])
    a = np.cross(d, h); a /= np.linalg.norm(a)
    return d, a, np.cross(d, a)


def spike(S, d, r0, length, half_w, col):
    """a tapered beam out along d from r0: two crossed cards (base at r0 full width, the tip a point), v 1 at the base"""
    d, a, b = basis(d)
    for side in (a, b):
        o = len(S.V)
        p0 = d * r0; p1 = d * (r0 + length)
        for p, u, v, w in ((p0 - side * half_w, 0, 1, 1), (p0 + side * half_w, 1, 1, 1), (p1 + side * half_w * 0.15, 1, 0, 0.0), (p1 - side * half_w * 0.15, 0, 0, 0.0)):
            S.V.append(tuple(p) + (u, v) + tuple(col[:3]) + (col[3] * (0.25 + 0.75 * w),))
        S.F += [(o, o + 1, o + 2), (o, o + 2, o + 3)]


def mote(S, p, half, col):
    """a spark seen from anywhere: three crossed little cards"""
    for ax in range(3):
        o = len(S.V)
        e1 = np.zeros(3); e2 = np.zeros(3); e1[(ax + 1) % 3] = half; e2[(ax + 2) % 3] = half
        for k, (s1, s2, u, v) in enumerate(((-1, -1, 0, 1), (1, -1, 1, 1), (1, 1, 1, 0), (-1, 1, 0, 0))):
            q = np.asarray(p) + s1 * e1 + s2 * e2
            S.V.append(tuple(q) + (u, v) + tuple(col))
        S.F += [(o, o + 1, o + 2), (o, o + 2, o + 3)]


def ring_axis(tilt):
    th, az = tilt
    return np.array([math.sin(th) * math.cos(az), math.sin(th) * math.sin(az), math.cos(th)])


def model(T):
    rng = np.random.default_rng(7)
    S = {}
    # --- the beams in the world, two sets
    a = Shape('LO_spikesA', T['spike'], 1.1)
    for i in range(9):
        z = rng.uniform(-0.75, 0.75); ph = 2 * math.pi * i / 9 + rng.uniform(-0.25, 0.25)
        d = (math.sqrt(1 - z * z) * math.cos(ph), math.sqrt(1 - z * z) * math.sin(ph), z)
        c = ramp(0.35 + 0.3 * rng.random())
        spike(a, d, 4.0, rng.uniform(20, 38), rng.uniform(0.9, 1.5), c + (rng.uniform(0.35, 0.6),))
    a.spin = ((0, 0, 1), SPIN_A); S['spikesA'] = a
    b = Shape('LO_spikesB', T['spike'], 1.0)
    for i in range(6):
        z = rng.uniform(-0.6, 0.85); ph = 2 * math.pi * (i + 0.5) / 6 + rng.uniform(-0.3, 0.3)
        d = (math.sqrt(1 - z * z) * math.cos(ph), math.sqrt(1 - z * z) * math.sin(ph), z)
        c = ramp(0.45 + 0.35 * rng.random())
        spike(b, d, 4.0, rng.uniform(14, 28), rng.uniform(0.7, 1.1), c + (rng.uniform(0.3, 0.5),))
    b.spin = ((0, 0, -1), SPIN_B); S['spikesB'] = b
    # --- motes on two tilted rings
    for key, name, tilt, r, n, rate, sgn in (('motesA', 'LO_motesA', TILT_A, 14.5, 9, MOTE_A, 1), ('motesB', 'LO_motesB', TILT_B, 18.0, 7, MOTE_B, -1)):
        m = Shape(name, T['mote'], 1.6)
        nrm, e1, e2 = basis(ring_axis(tilt))
        for i in range(n):
            ph = 2 * math.pi * i / n + rng.uniform(-0.2, 0.2)
            rr = r + rng.uniform(-1.5, 1.5)
            p = rr * (math.cos(ph) * e1 + math.sin(ph) * e2) + nrm * rng.uniform(-0.8, 0.8)
            c = ramp(0.2 + 0.4 * rng.random())
            mote(m, p, rng.uniform(0.7, 1.3), c + (rng.uniform(0.6, 1.0),))
        m.spin = (tuple(nrm * sgn), rate); S[key] = m
    # --- camera-facing layers (last drawn over first: halo, rays, corona, core, flare)
    h = Shape('LO_halo', T['halo'], 0.55, billboard=True)
    quad(h, 42, 42); S['halo'] = h
    ra = Shape('LO_raysA', T['raysa'], 1.1, billboard=True)
    ra.uscroll = RAYS_A
    polar(ra, 1.0, 46.0, lambda t: ramp(0.15 + 0.85 * t) + (1.0,), urep=1, na=96, nr=12); S['raysA'] = ra
    rb = Shape('LO_raysB', T['raysb'], 0.9, billboard=True)
    rb.uscroll = RAYS_B
    polar(rb, 1.0, 36.0, lambda t: ramp(0.25 + 0.75 * t) + (1.0,), urep=1, reverse=True, na=96, nr=12); S['raysB'] = rb
    co = Shape('LO_corona', T['corona'], 1.0, billboard=True)
    co.uscroll, co.vscroll = CORONA_U, CORONA_V
    polar(co, 0.8, 16.0, lambda t: ramp(0.1 + 0.8 * t) + (float(np.clip(1 - t, 0, 1) ** 2.2),), urep=3, na=96, nr=10); S['corona'] = co
    cr = Shape('LO_core', T['core'], 1.6, billboard=True)
    quad(cr, 11.5, 11.5); S['core'] = cr
    fl = Shape('LO_flare', T['flare'], 0.45, billboard=True)
    quad(fl, 40, 2.2); S['flare'] = fl
    return S


# ------------------------------------------------------------------ build
def fx(job_lines, tag):
    jf = WORK / f'{tag}.job'; jf.write_text('\n'.join(job_lines) + '\n')
    r = subprocess.run([FXWRITE, str(jf)], capture_output=True, text=True)
    print(' ', r.stdout.strip(), r.stderr.strip())
    if r.returncode: raise SystemExit('fxwrite failed')


def spin(src, dst, node, axis, tps):
    r = subprocess.run([str(TOOLS / 'spinwrap'), str(src), str(dst), node, *(f'{x:.5f}' for x in axis), f'{tps:.5f}', '24'],
                       capture_output=True, text=True)
    print(' ', r.stdout.strip(), r.stderr.strip())
    if r.returncode: raise SystemExit('spinwrap failed')


def build(data):
    WORK.mkdir(parents=True, exist_ok=True)
    T = textures(data)
    S = model(T)
    out = pathlib.Path(data) / 'Meshes' / 'CurseOfAlduin' / 'fx' / 'lightorb.nif'
    out.parent.mkdir(parents=True, exist_ok=True)
    # each world layer goes in on its own, then spinwrap hangs it (alone at the root) under its spinning node
    prev = None
    for k, (key, node) in enumerate((('spikesA', 'LO_SpinA'), ('spikesB', 'LO_SpinB'), ('motesA', 'LO_MotesA'), ('motesB', 'LO_MotesB'))):
        a, b = WORK / f'st{k}a.nif', WORK / f'st{k}b.nif'
        fx([f'out {a}'] + ([f'base {prev}'] if prev else []) + S[key].lines(), f'st{k}')
        spin(a, b, node, S[key].spin[0], S[key].spin[1])
        prev = b
    # the camera-facing cards, in draw order
    L = [f'out {out}', f'base {prev}']
    for key in ('halo', 'raysA', 'raysB', 'corona', 'core', 'flare'):
        L += S[key].lines()
    fx(L, 'cards')
    return out, S


# ------------------------------------------------------------------ preview (additive, per-pixel; from the same geometry)
def tex_arr(T, key):
    name = T[key].split('\\')[-1]
    return np.asarray(Image.open(WORK / (name + '.png')).convert('RGBA'), np.float32) / 255


def axis_rot(axis, ang):
    x, y, z = np.asarray(axis, float) / np.linalg.norm(axis); c, s = math.cos(ang), math.sin(ang); C = 1 - c
    return np.array([[c + x * x * C, x * y * C - z * s, x * z * C + y * s], [y * x * C + z * s, c + y * y * C, y * z * C - x * s],
                     [z * x * C - y * s, z * y * C + x * s, c + z * z * C]])


def render(S, T, eye, t, W=420, H=420, fov=38.0):
    eye = np.asarray(eye, float); fwd = -eye / np.linalg.norm(eye)
    right = np.cross(fwd, (0, 0, 1)); right = right / np.linalg.norm(right) if np.linalg.norm(right) > 1e-6 else np.array([1.0, 0, 0])
    up = np.cross(right, fwd)
    f = W / 2 / math.tan(math.radians(fov) / 2)
    acc = np.zeros((H, W, 3), np.float32)
    yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)
    for key, sh in S.items():
        tx = tex_arr(T, {'spikesA': 'spike', 'spikesB': 'spike', 'motesA': 'mote', 'motesB': 'mote', 'raysA': 'raysa',
                         'raysB': 'raysb'}.get(key, key))
        V = np.array(sh.V, np.float32)
        P = V[:, :3].astype(float)
        if sh.billboard:
            P = P[:, :1] * right + P[:, 1:2] * up          # (the card in the camera's plane, through the origin)
        elif sh.spin:
            P = P @ axis_rot(sh.spin[0], 2 * math.pi * sh.spin[1] * t).T
        UV = V[:, 3:5].copy()
        UV[:, 0] += t / sh.uscroll if sh.uscroll else 0
        UV[:, 1] -= t / sh.vscroll if sh.vscroll else 0
        C = V[:, 5:9] * np.array(list(sh.col) + [1], np.float32)
        rel = P - eye
        zc = rel @ fwd
        sx = W / 2 + f * (rel @ right) / zc; sy = H / 2 - f * (rel @ up) / zc
        th, tw = tx.shape[:2]
        for a, b, c in sh.F:
            xs = np.array([sx[a], sx[b], sx[c]]); ys = np.array([sy[a], sy[b], sy[c]])
            x0, x1 = int(max(0, np.floor(xs.min()))), int(min(W - 1, np.ceil(xs.max())))
            y0, y1 = int(max(0, np.floor(ys.min()))), int(min(H - 1, np.ceil(ys.max())))
            if x1 < x0 or y1 < y0: continue
            den = (ys[1] - ys[2]) * (xs[0] - xs[2]) + (xs[2] - xs[1]) * (ys[0] - ys[2])
            if abs(den) < 1e-9: continue
            X = xx[y0:y1 + 1, x0:x1 + 1] + 0.5; Y = yy[y0:y1 + 1, x0:x1 + 1] + 0.5
            w0 = ((ys[1] - ys[2]) * (X - xs[2]) + (xs[2] - xs[1]) * (Y - ys[2])) / den
            w1 = ((ys[2] - ys[0]) * (X - xs[2]) + (xs[0] - xs[2]) * (Y - ys[2])) / den
            w2 = 1 - w0 - w1
            m = (w0 >= 0) & (w1 >= 0) & (w2 >= 0)
            if not m.any(): continue
            # perspective-correct
            iz = np.array([1 / zc[a], 1 / zc[b], 1 / zc[c]])
            q = w0 * iz[0] + w1 * iz[1] + w2 * iz[2]
            k0, k1, k2 = w0 * iz[0] / q, w1 * iz[1] / q, w2 * iz[2] / q
            u = k0 * UV[a, 0] + k1 * UV[b, 0] + k2 * UV[c, 0]
            v = k0 * UV[a, 1] + k1 * UV[b, 1] + k2 * UV[c, 1]
            col = k0[..., None] * C[a] + k1[..., None] * C[b] + k2[..., None] * C[c]
            ti = (np.mod(v, 1) * (th - 1)).astype(int); tj = (np.mod(u, 1) * (tw - 1)).astype(int)
            s = tx[ti, tj]
            add = s[..., :3] * col[..., :3] * (s[..., 3:4] * col[..., 3:4]) * sh.scale
            acc[y0:y1 + 1, x0:x1 + 1][m] += add[m]
    bg = np.array([0.035, 0.04, 0.05], np.float32)
    img = 1 - np.exp(-(acc * 1.15))           # (a soft filmic shoulder, the way the game's tonemap rolls off)
    return np.clip(bg + img * (1 - bg), 0, 1)


def preview(S, T, path):
    views = [('side, t=0', (0, -150, 0), 0.0), ('3/4 above, t=0', (-95, -95, 70), 0.0), ('from below', (40, -60, -130), 0.0),
             ('top (looking down Z)', (0.01, 0, 150), 0.0), ('side, t=2s', (0, -150, 0), 2.0), ('side, t=4s', (0, -150, 0), 4.0)]
    W = H = 420
    out = Image.new('RGB', (W * 3, (H + 22) * 2 + 26), (14, 14, 16)); dr = ImageDraw.Draw(out)
    for i, (label, eye, t) in enumerate(views):
        im = render(S, T, eye, t, W, H)
        x, y = (i % 3) * W, (i // 3) * (H + 22)
        out.paste(Image.fromarray((im * 255).astype(np.uint8)), (x, y))
        dr.text((x + 8, y + H + 4), label, fill=(225, 225, 225))
    dr.text((8, 2 * (H + 22) + 6), 'lightorb.nif - additive preview, camera 150 units out, 38 deg fov; frame ~ 103 units across',
            fill=(180, 180, 180))
    out.save(path)
    print('preview', path)


def main():
    args = [a for a in sys.argv[1:]]
    prev = None
    if '--preview' in args:
        i = args.index('--preview'); prev = args[i + 1]; del args[i:i + 2]
    data = pathlib.Path(args[0] if args else ROOT / 'assets' / 'fx')
    out, S = build(data)
    print('wrote', out)
    if prev:
        T = {k: v for k, v in textures_names().items()}
        preview(S, T, prev)
    shutil.rmtree(WORK, ignore_errors=True)


def textures_names():
    return {k: TEXREL + f'lightorb_{k}.dds' for k in ('core', 'corona', 'raysa', 'raysb', 'halo', 'flare', 'mote', 'spike')}


if __name__ == '__main__':
    main()
