"""(Noah: "tier 1 sends out arc slashes from the blade ... that pierce enemies, swing in the color of the sword"; "the slashes
from the swords should look hyper realistic") the swords' crescent slashes: a true crescent - its curve along X, bowed forward
(+Y, the way it flies), thick in the middle and drawn out to nothing at both tips - turned to the swing's angle by
src/Gun.cpp. Built in layers, all added light:

  - the body: a white-hot cutting edge at the front fading back through the sword's colour into frayed, streaked trails,
    drawn as a fan of planes tilted about the crescent's span (so it reads as a solid sweep from behind, above or the side;
    each plane fades as you see it edge-on)
  - the edge: a thin blazing line along the front, a little wider than the body's own
  - the shimmer: a wider wake of fine streaks that slide along the curve as it flies
  - the haze: a soft bloom round the whole of it

One model per colour.

    python3 tools/arcfx.py [--preview out.json]  ->  assets/fx/Meshes/CurseOfAlduin/fx/arc_<colour>.nif,
                                                      Textures/CurseOfAlduin/fx/arc_slash.dds, arc_wisp.dds, arc_haze.dds"""
import json, math, pathlib, subprocess, sys
import numpy as np
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parent.parent
MESH = ROOT / 'assets' / 'fx' / 'Meshes' / 'CurseOfAlduin' / 'fx'
TEX = ROOT / 'assets' / 'fx' / 'Textures' / 'CurseOfAlduin' / 'fx'
WORK = ROOT / 'build' / 'arcfx'
FX = 'CurseOfAlduin\\fx\\'
COLOURS = {   # glow, core
    'purple': ('0.62,0.22,1', '0.95,0.8,1'), 'carian': ('0.35,0.62,1', '0.85,0.95,1'), 'white': ('0.9,0.92,1', '1,1,1'),
    'red': ('1,0.16,0.08', '1,0.75,0.6'), 'fire': ('1,0.45,0.06', '1,0.9,0.6'), 'frost': ('0.45,0.8,1', '0.92,0.98,1'),
    'shock': ('0.6,0.5,1', '0.95,0.92,1'), 'sun': ('1,0.82,0.3', '1,0.98,0.85'), 'blood': ('0.6,0.02,0.04', '1,0.4,0.35'),
    'gold': ('1,0.62,0.12', '1,0.95,0.7'), 'bane': ('0.92,0.95,1', '1,1,1'),
}


def fbm(n, octaves, rng, smooth=1.0):
    """1D fractal noise, wrapping, 0..1"""
    out = np.zeros(n)
    amp, tot = 1.0, 0.0
    for o in range(octaves):
        k = 2 ** (o + 2)
        pts = rng.random(k)
        x = np.arange(n) * k / n
        i = np.floor(x).astype(int)
        f = x - i
        f = f * f * (3 - 2 * f)
        out += amp * (pts[i % k] * (1 - f) + pts[(i + 1) % k] * f)
        tot += amp
        amp *= 0.55 * smooth
    return out / tot


def save(img, name):
    TEX.mkdir(parents=True, exist_ok=True); WORK.mkdir(parents=True, exist_ok=True)
    png = WORK / (name + '.png')
    Image.fromarray(np.clip(img * 255, 0, 255).astype(np.uint8)).convert('RGB').save(png)
    subprocess.run(['convert', str(png), '-define', 'dds:compression=dxt1', '-define', 'dds:mipmaps=9', str(TEX / (name + '.dds'))], check=True)


def textures():
    rng = np.random.default_rng(11)
    W, H = 1024, 256
    u = np.linspace(0, 1, W)[None, :]
    v = np.linspace(0, 1, H)[:, None]                   # 0: the leading edge .. 1: the trailing end
    # streaks running along the sweep (u): each row of the band its own brightness, wobbling slowly along the curve
    rows = fbm(H, 6, rng)[:, None]
    wob = fbm(W, 4, rng)[None, :]
    rows2 = fbm(H, 7, np.random.default_rng(5))[:, None]
    streak = np.clip(0.3 + 1.6 * (rows - 0.5) + 1.0 * (rows2 - 0.5) * (0.6 + wob), 0, 1.5) ** 1.3
    # the body: bright behind the edge, dying away to the back; frayed - its trails break into wisps as they fall behind
    body = np.exp(-v * 3.0) * (0.45 + 0.55 * streak)
    fray = np.clip((fbm(W, 6, np.random.default_rng(9))[None, :] * 0.9 + rows * 0.6) - v * 1.15 + 0.35, 0, 1)
    body = body * (0.35 + 0.65 * fray)
    edge = np.exp(-((v - 0.035) / 0.022) ** 2) * 1.0 + np.exp(-((v - 0.035) / 0.07) ** 2) * 0.35
    a = np.clip(edge + body * 0.95, 0, 1)
    a[0:2, :] = 0                                         # (a clean front)
    save(a, 'arc_slash')
    # the shimmer: fine streaks only (no edge), for the scrolling wake
    s = np.clip((rows2 - 0.45) * 2.2, 0, 1) ** 1.6 * np.exp(-v * 1.6) * (0.5 + 0.5 * wob)
    s = s * np.clip(np.sin(np.pi * v) * 1.6, 0, 1)
    save(np.clip(s * 1.3, 0, 1), 'arc_wisp')
    # the haze: a soft glow across the band
    hz = np.exp(-((v - 0.32) / 0.3) ** 2) * np.ones_like(u)
    save(hz * 0.9, 'arc_haze')
    # the sparks: motes of light shed off the sweep, drawn out along it
    sp = np.zeros((H, W))
    r3 = np.random.default_rng(21)
    for _ in range(420):
        y = int(r3.beta(1.4, 3.0) * (H - 1)); x = int(r3.random() * W); ln = int(r3.integers(6, 40)); b = r3.random() ** 2
        xs = (x + np.arange(ln)) % W
        sp[y, xs] += b * np.linspace(1, 0, ln) ** 1.5
    from PIL import ImageFilter
    im = Image.fromarray(np.clip(sp * 255, 0, 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(1.1))
    sp = np.asarray(im, dtype=np.float32) / 255
    save(np.clip(sp * 3.5, 0, 1), 'arc_spark')


def crescent(L, name, tex, col, scale, *, plane='face', tilt=0.0, ro=160.0, half=38.0, depth=24.0, v0=0.0, v1=1.0, segs=44,
             rows=6, uscroll=None, bow=18.0, prev=None):
    """a crescent band from its outer curve (v=v0, the hot edge) in toward its inner curve (v=v1), as deep as `depth` at its
    middle and nothing at its tips. plane 'face': standing across the flight (X across, Z up - what you see from behind),
    its middle bowed forward (+Y) by `bow`; 'flat': lying in the flight's plane (X across, Y forward). tilt: turned about X"""
    L.append(f'shape {name} tex={FX}{tex} color={col},1 scale={scale} blend=add single=0 falloff=1' + (f' uscroll={uscroll}' if uscroll else ''))
    c0 = math.cos(math.radians(half))
    ca, sa = math.cos(tilt), math.sin(tilt)
    def place(x, up, fwd):   # (x, up-in-its-plane, forward) -> the model's x, y, z, then tilted about X
        y, z = (fwd, up) if plane == 'face' else (up, fwd)
        return (x, y * ca - z * sa, y * sa + z * ca)
    n0 = place(0, 0, 1) if plane == 'face' else place(0, 0, 1)
    nrm = (0.0, -sa, ca) if plane == 'flat' else (0.0, ca, sa)
    verts = []
    for k in range(segs + 1):
        f = k / segs
        th = math.radians(-half + 2 * half * f)
        nx, nu = math.sin(th), math.cos(th)
        m = (math.cos(th) - c0) / (1 - c0)               # 1 at the middle, 0 at the tips
        x0, u0 = ro * nx, ro * (math.cos(th) - c0)
        fwd = bow * m
        fade = max(0.0, m) ** 0.55
        w = depth * max(0.0, m) ** 0.9
        for j in range(rows + 1):
            g = j / rows
            d = w * (v0 + (v1 - v0) * g)
            p = place(x0 - nx * d, u0 - nu * d, fwd - 0.35 * d)
            L.append(f'v {p[0]:.2f} {p[1]:.2f} {p[2]:.2f} {f * 2.0:.4f} {g:.4f} {fade:.3f} {fade:.3f} {fade:.3f} 1')
            L.append(f'n {nrm[0]:.4f} {nrm[1]:.4f} {nrm[2]:.4f}')
            verts.append((p, (f * 2.0, g), fade))
    tris = []
    for k in range(segs):
        for j in range(rows):
            a0 = k * (rows + 1) + j
            b0 = a0 + rows + 1
            tris += [(a0, b0, b0 + 1), (a0, b0 + 1, a0 + 1)]
    for t in tris: L.append(f't {t[0]} {t[1]} {t[2]}')
    if prev is not None:
        prev.append({'name': name, 'tex': tex, 'col': [float(c) for c in col.split(',')], 'scale': scale,
                     'v': [p for p, _, _ in verts], 'uv': [uv for _, uv, _ in verts], 'c': [c for _, _, c in verts], 't': tris,
                     'uscroll': uscroll or 0})


def build(key, glow, core, prev=None):
    L = [f'out {MESH / ("arc_" + key + ".nif")}']
    # (Noah: "the slashes are also facing the wrong way instead of facing where the slash is moving towards") the crescent lies
    # in the swing's own plane, its curve bowed forward the way it flies - a fan of three planes tilted about its span gives it
    # depth seen from behind
    for i, (t, sc) in enumerate(((0.0, 1.5), (0.32, 0.8), (-0.32, 0.8))):
        crescent(L, f'body{i}', 'arc_slash.dds', glow, sc, plane='flat', tilt=t, ro=150.0, half=40.0, depth=24.0, bow=0.0, prev=prev)
    for i, t in enumerate((0.0, 0.45, -0.45)):
        crescent(L, f'edge{i}', 'arc_slash.dds', core, 1.7 if t == 0 else 1.0, plane='flat', tilt=t, ro=150.0, half=39.0, depth=8.0, v0=0, v1=0.3, rows=2, bow=0.0, prev=prev)
    crescent(L, 'wisp', 'arc_wisp.dds', glow, 1.0, plane='flat', ro=150.0, half=44.0, depth=44.0, uscroll=0.5, bow=0.0, prev=prev)
    crescent(L, 'spark', 'arc_spark.dds', core, 1.2, plane='flat', tilt=0.2, ro=150.0, half=46.0, depth=52.0, uscroll=0.3, bow=0.0, prev=prev)
    crescent(L, 'haze', 'arc_haze.dds', glow, 0.4, plane='flat', ro=156.0, half=44.0, depth=60.0, rows=4, bow=0.0, prev=prev)
    # (a faint standing glow along its front, so a flat cut seen dead edge-on still shows its curve)
    crescent(L, 'stand', 'arc_haze.dds', glow, 0.35, ro=160.0, half=40.0, depth=14.0, rows=3, bow=10.0, prev=prev)
    jf = WORK / f'arc_{key}.job'; jf.write_text('\n'.join(L) + '\n')
    r = subprocess.run(['/home/claude/tools/fxwrite', str(jf)], capture_output=True, text=True)
    print(key, r.stdout.strip()[-200:], r.stderr.strip()[-400:])
    if r.returncode: raise SystemExit('fxwrite failed')


if __name__ == '__main__':
    textures()
    MESH.mkdir(parents=True, exist_ok=True)
    prev = {}
    for k, (g, c) in COLOURS.items():
        prev[k] = []
        build(k, g, c, prev[k])
    if '--preview' in sys.argv:
        out = pathlib.Path(sys.argv[sys.argv.index('--preview') + 1])
        out.write_text(json.dumps({k: prev[k] for k in ('carian', 'purple', 'white', 'red')}))
