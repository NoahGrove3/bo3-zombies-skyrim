"""The Subsuming Vortex's black hole (the Ranger's wonder) - (Noah: "make the impact look like a real black hole: a dark
core with a bright purple accretion ring/disc swirling around it, lensing-like glow, particles being drawn inward").
Replaces tools/vortex.py's model at the same path, CurseOfAlduin\\fx\\vortex.nif (ACTI UN_VortexFx). src/Gun.cpp
TickWells swells it from ~0.05 to burst/420 (about 1.0 - 1.5), spins it about Z at 2.4 rad/s for 2.2 - 3 s, then shrinks
it to a point as it bursts - so everything here is round Z, the disc flat in XY, the lensing on camera-facing cards.

    python tools/blackhole.py [<Data dir>]      (default: assets/fx)

Unlit layers (tools/modelconv/fxwrite):
  BH_core      the event horizon: a black ball, alpha-blended and opaque - the shadow every other layer sits round
  BH_disc      the accretion disc, flat round it (r 34..175): streaks of plasma wound into a spiral, white-hot at the
               inner edge through violet to a deep purple fringe, flowing inward (v scroll) and round (u scroll) on top
               of the whole model's own spin
  BH_disc2     a thinner, tilted, faster inner disc of lilac - it precesses as the hole spins, so it never reads flat
  BH_photon    (camera-facing) the photon ring: a thin bright ring hugging the shadow, brighter on one side (Doppler)
  BH_lens      (camera-facing) the lensed far side of the disc: a broad violet arc bowed over and under the shadow,
               the way the disc behind the hole is bent into view
  BH_halo      (camera-facing) a soft violet glow round it all, dark in the middle
  BH_fall0..2  motes of light falling in: sparse dots on three crossed, tilted funnels, scrolling inward fast
"""
import math, pathlib, subprocess, sys
import numpy as np
from PIL import Image, ImageFilter

ROOT = pathlib.Path(__file__).resolve().parent.parent
FXWRITE = '/home/claude/tools/fxwrite'
WORK = ROOT / 'build' / 'blackhole'
TEXREL = 'CurseOfAlduin\\fx\\'
VIOLET = (0.62, 0.22, 1.0)


def save(img, data, name):
    p = pathlib.Path(data) / 'Textures' / 'CurseOfAlduin' / 'fx' / name
    p.parent.mkdir(parents=True, exist_ok=True); WORK.mkdir(parents=True, exist_ok=True)
    tmp = WORK / (name + '.png'); img.save(tmp)
    subprocess.run(['convert', str(tmp), '-define', 'dds:compression=dxt5', '-define', 'dds:mipmaps=10', str(p)], check=True)
    return TEXREL + name


def tileable(n, beta, seed, aniso=(1.0, 1.0)):
    rng = np.random.default_rng(seed)
    f = np.fft.fftfreq(n); fx, fy = np.meshgrid(f * aniso[0], f * aniso[1]); rad = np.sqrt(fx ** 2 + fy ** 2); rad[0, 0] = 1
    spec = (rng.normal(size=(n, n)) + 1j * rng.normal(size=(n, n))) / rad ** beta; spec[0, 0] = 0
    o = np.real(np.fft.ifft2(spec)).astype(np.float32)
    return (o - o.min()) / (o.max() - o.min())


def rgba(a, rgb=(1, 1, 1)):
    a = np.clip(a, 0, 1)
    out = np.zeros(a.shape + (4,), np.uint8)
    for k in range(3): out[..., k] = int(rgb[k] * 255)
    out[..., 3] = (a * 255).astype(np.uint8)
    return Image.fromarray(out, 'RGBA')


def textures(data):
    T = {}
    # the disc's plasma: long streaks along u (round the hole), knotted and broken, tiling both ways
    n = 1024
    s = tileable(n, 1.6, 11, aniso=(1.0, 0.12))                     # smooth along u (round the hole), varied across: streaks
    k = tileable(n, 1.2, 12)
    a = np.clip((s - 0.35) * 2.2, 0, 1) ** 1.4 * (0.55 + 0.6 * k)
    a = np.clip(a + 0.12, 0, 1)
    T['disc'] = save(rgba(a), data, 'bh_disc.dds')
    # motes: sparse soft dots, tiling
    n = 512
    rng = np.random.default_rng(5)
    yy, xx = np.mgrid[0:n, 0:n].astype(np.float32)
    a = np.zeros((n, n), np.float32)
    for _ in range(40):
        cx, cy, r = rng.uniform(0, n), rng.uniform(0, n), rng.uniform(1.5, 4.0)
        dx = (xx - cx + n / 2) % n - n / 2; dy = (yy - cy + n / 2) % n - n / 2
        a += np.exp(-(dx ** 2 / (r * r) + dy ** 2 / (r * r * 2.2))) * rng.uniform(0.5, 1.0)   # (a little drawn out the way they fall)
    T['dots'] = save(rgba(a), data, 'bh_dots.dds')
    # camera cards, 512, the hole's shadow radius at 0.28 of the half-width (core 30 on a 108 half-size card)
    n = 512
    yy, xx = np.mgrid[0:n, 0:n].astype(np.float32)
    X = (xx + 0.5) / n * 2 - 1; Y = 1 - (yy + 0.5) / n * 2
    R = np.hypot(X, Y); A = np.arctan2(Y, X)
    # photon ring: thin, bright, one side (approaching, left) brighter
    dop = 0.55 + 0.45 * np.cos(A - math.pi)
    ring = np.exp(-((R - 0.33) / 0.016) ** 2) * (0.6 + 0.6 * dop) + 0.35 * np.exp(-((R - 0.345) / 0.05) ** 2) * dop
    T['photon'] = save(rgba(ring), data, 'bh_photon.dds')
    # lensed far disc: an arc over the top and under the bottom, broad, fading toward the sides, never inside the shadow
    band = np.exp(-((R - 0.42) / 0.09) ** 2) * (0.35 + 0.65 * np.abs(np.sin(A)) ** 1.5)
    streak = tileable(512, 1.5, 21, aniso=(1.0, 0.15))             # (bent streaks: sampled round the arc)
    su = ((A / (2 * math.pi)) % 1.0 * 511).astype(int); sv = (np.clip(R, 0, 0.999) * 511).astype(int)
    band *= 0.55 + 0.7 * streak[sv, su]
    band *= np.clip((R - 0.32) / 0.04, 0, 1) * (0.7 + 0.3 * dop)
    T['lens'] = save(rgba(np.clip(band, 0, 1)), data, 'bh_lens.dds')
    # halo: a soft glow round the hole, dark in the middle
    halo = np.exp(-((R - 0.36) / 0.22) ** 2) * np.clip((R - 0.27) / 0.08, 0, 1) * np.clip((1 - R) / 0.2, 0, 1)
    T['halo'] = save(rgba(halo * 0.9), data, 'bh_halo.dds')
    # the horizon: black, opaque
    T['core'] = save(rgba(np.ones((32, 32)), (0, 0, 0)), data, 'bh_core.dds')
    return T


def ball(L, name, r, tex):
    L.append(f'shape {name} tex={tex} color=0,0,0,1 scale=1 blend=alpha single=1')
    nu, nv = 32, 18
    for j in range(nv + 1):
        th = math.pi * j / nv
        for i in range(nu + 1):
            ph = 2 * math.pi * i / nu
            n = (math.sin(th) * math.cos(ph), math.sin(th) * math.sin(ph), math.cos(th))
            L.append(f'v {r * n[0]:.2f} {r * n[1]:.2f} {r * n[2]:.2f} {i / nu:.4f} {j / nv:.4f}')
    for j in range(nv):
        for i in range(nu):
            a = j * (nu + 1) + i; b, c, d = a + 1, a + nu + 1, a + nu + 2
            L += [f't {a} {c} {b}', f't {b} {c} {d}']


def ramp(t, stops):
    for (a, ca), (b, cb) in zip(stops[:-1], stops[1:]):
        if t <= b:
            k = (t - a) / max(1e-6, b - a)
            return tuple(ca[i] * (1 - k) + cb[i] * k for i in range(4))
    return stops[-1][1]


DISC_COL = [(0.0, (1.0, 0.92, 1.0, 0.0)), (0.04, (1.0, 0.9, 1.0, 1.0)), (0.18, (0.86, 0.6, 1.0, 1.0)), (0.45, (0.62, 0.24, 1.0, 0.85)),
            (0.75, (0.36, 0.1, 0.72, 0.45)), (1.0, (0.18, 0.04, 0.4, 0.0))]


def disc(L, name, tex, r0, r1, tilt, twist, uscroll, vscroll, scale, cols=DISC_COL, urep=3, na=72, nr=14, lift=0.0):
    """a flat ring round Z (tilted about X), u round it (urep turns of texture), v inward, the streaks wound into a
    spiral by 'twist'; per-vertex colour and fade from the inner edge out. lift: a funnel's rise at the rim"""
    L.append(f'shape {name} tex={tex} color=1,1,1,1 scale={scale} blend=add uscroll={uscroll} vscroll={vscroll}')
    ct, st = math.cos(tilt), math.sin(tilt)
    for j in range(nr + 1):
        t = j / nr
        r = r0 + (r1 - r0) * t ** 1.3
        c = ramp(t, cols)
        for i in range(na + 1):
            a = 2 * math.pi * i / na
            x, y, z = r * math.cos(a), r * math.sin(a), lift * t ** 2
            y, z = y * ct - z * st, y * st + z * ct
            L.append(f'v {x:.2f} {y:.2f} {z:.2f} {urep * i / na + twist * t:.4f} {-t * 1.5:.4f} {c[0]:.3f} {c[1]:.3f} {c[2]:.3f} {c[3]:.3f}')
    for j in range(nr):
        for i in range(na):
            a = j * (na + 1) + i; b, c, d = a + 1, a + na + 1, a + na + 2
            L += [f't {a} {c} {b}', f't {b} {c} {d}']


def card(L, name, tex, half, col, scale):
    L.append(f'shape {name} tex={tex} color={col[0]},{col[1]},{col[2]},1 scale={scale} blend=add billboard=1')
    h = half
    L += [f'v {-h} {-h} 0 0 1', f'v {h} {-h} 0 1 1', f'v {h} {h} 0 1 0', f'v {-h} {h} 0 0 0', 't 0 1 2', 't 0 2 3']


def job(data):
    T = textures(data)
    out = pathlib.Path(data) / 'Meshes' / 'CurseOfAlduin' / 'fx' / 'vortex.nif'
    out.parent.mkdir(parents=True, exist_ok=True)
    L = [f'out {out}']
    ball(L, 'BH_core', 30, T['core'])
    disc(L, 'BH_disc', T['disc'], 33, 175, 0.0, 1.4, 1.1, 0.9, 1.25)
    disc(L, 'BH_disc2', T['disc'], 34, 95, math.radians(9), 2.2, 0.6, 0.55, 0.9,
         cols=[(0.0, (1, 1, 1, 0)), (0.08, (0.95, 0.85, 1.0, 0.9)), (0.5, (0.75, 0.45, 1.0, 0.5)), (1.0, (0.4, 0.15, 0.8, 0.0))], urep=2, na=64, nr=8)
    for k, (tilt, sp) in enumerate(((math.radians(35), 0.7), (math.radians(-55), 0.55), (math.radians(80), 0.8))):
        disc(L, f'BH_fall{k}', T['dots'], 34, 230, tilt, 0.9, 1.6 + 0.4 * k, sp, 0.8,
             cols=[(0.0, (1, 1, 1, 0)), (0.1, (0.95, 0.85, 1.0, 1.0)), (0.6, (0.7, 0.4, 1.0, 0.8)), (1.0, (0.5, 0.2, 0.9, 0.0))], urep=4, na=48, nr=10, lift=-25.0)
    card(L, 'BH_photon', T['photon'], 100, (0.92, 0.8, 1.0), 1.5)
    card(L, 'BH_lens', T['lens'], 100, (0.72, 0.38, 1.0), 1.5)
    card(L, 'BH_halo', T['halo'], 150, VIOLET, 0.4)
    WORK.mkdir(parents=True, exist_ok=True)
    jf = WORK / 'vortex.job'; jf.write_text('\n'.join(L) + '\n')
    return jf


def main():
    data = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / 'assets' / 'fx')
    jf = job(data)
    r = subprocess.run([FXWRITE, str(jf)], capture_output=True, text=True)
    print(r.stdout.strip(), r.stderr.strip())
    if r.returncode: raise SystemExit('fxwrite failed')


if __name__ == '__main__':
    main()
