"""(Noah: "these particles need to be much higher quality - the floor effects from the flasks and the Magnus staff should be very
small animated hyper realistic particles from the area of damage coming from the ground in the colour of the effect"; the
sun and gold bursts read as big blobby stars) fine particles, drawn here frame by frame:

  motes_rise.dds   a looping flipbook (16x16, 60 fps) of tiny glowing motes rising off the ground - each a hot pin-point with a soft
                   halo, drifting and flickering, born at the bottom, gone near the top
  sparks_burst.dds a one-shot flipbook (16x8, 60 fps) of a burst of fine sparks: a white flash, then streaks thrown out and falling
                   under their own weight, cooling as they go
  glow_disc.dds    a soft round glow for the ground under the motes

  motes_<element>.nif  a field of the rising motes over the area (~100 units across at scale 1, cards facing you, each its
                       own phase) with the faint glow under it - hung as a hazard where a flask's pool or the Staff of
                       Magnus's blast lies (src/Gun.cpp Motes)
  burst_sun.nif, burst_gold.nif  the fine spark burst in place of the star cards (tools/elemburst.py makes the others)

    python3 tools/motes.py"""
import math, pathlib, random, subprocess
import numpy as np
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parent.parent
MESH = ROOT / 'assets' / 'fx' / 'Meshes' / 'CurseOfAlduin' / 'fx'
TEX = ROOT / 'assets' / 'fx' / 'Textures' / 'CurseOfAlduin' / 'fx'
WORK = ROOT / 'build' / 'motes'
TEXREL = 'CurseOfAlduin\\fx\\'
# (Noah: "I want them to be 60 FPS. So it looks smooth") every flipbook here plays at 60 frames a second
MOTES = (16, 16)   # motes_rise: 256 frames, a 4.3 s loop
SPARKS = (16, 8)   # sparks_burst: 128 frames, the burst over in about a second, the rest dark
GROUNDG = (16, 10)  # ground_<element> (tools/alchfx.py)

# element: (mote colour, glow colour) - a little warm-pushed (the tonemap drifts additive light blue)
EL = {
    'fire':   ('1,0.45,0.1', '0.55,0.15,0.02'),
    'frost':  ('0.6,0.88,1', '0.15,0.35,0.6'),
    'shock':  ('0.72,0.6,1', '0.3,0.2,0.7'),
    'wind':   ('0.75,1,0.9', '0.25,0.45,0.4'),
    'sun':    ('1,0.85,0.35', '0.55,0.38,0.06'),
    'blood':  ('0.8,0.05,0.05', '0.35,0.0,0.02'),
    'void':   ('1,0.3,0.9', '0.45,0.06,0.4'),
    'poison': ('0.5,1,0.2', '0.15,0.4,0.03'),
    'gold':   ('1,0.8,0.3', '0.6,0.4,0.05'),
}


def splat(img, x, y, core, glow, a):
    """a pin-point at (x, y): a hot core of radius `core` px and a soft halo of `glow` px, additive"""
    h, w = img.shape
    r = int(glow * 3 + 2)
    x0, x1, y0, y1 = max(0, int(x) - r), min(w, int(x) + r + 1), max(0, int(y) - r), min(h, int(y) + r + 1)
    if x0 >= x1 or y0 >= y1: return
    yy, xx = np.mgrid[y0:y1, x0:x1]
    d2 = (xx - x) ** 2 + (yy - y) ** 2
    img[y0:y1, x0:x1] += a * (np.exp(-d2 / (2 * core * core)) + 0.35 * np.exp(-d2 / (2 * glow * glow)))


def streak(img, p0, p1, core, a):
    n = max(2, int(math.hypot(p1[0] - p0[0], p1[1] - p0[1]) / 0.7) + 1)
    for k in range(n):
        t = k / (n - 1)
        splat(img, p0[0] + (p1[0] - p0[0]) * t, p0[1] + (p1[1] - p0[1]) * t, core, core * 2.2, a * (0.35 + 0.65 * t) / n * 2.2)


def save_dds(frames, fw, fh, name, grid):
    cols, rows = grid
    sheet = np.zeros((fh * rows, fw * cols), np.float32)
    for i, f in enumerate(frames):
        r, c = divmod(i, cols)
        sheet[r * fh:(r + 1) * fh, c * fw:(c + 1) * fw] = f
    v = np.clip(1 - np.exp(-sheet * 1.6), 0, 1)  # (a soft shoulder: hot points stay round, never a clipped block)
    g = (v * 255).astype(np.uint8)
    img = Image.fromarray(np.dstack([g, g, g, g]), 'RGBA')
    TEX.mkdir(parents=True, exist_ok=True)
    WORK.mkdir(parents=True, exist_ok=True)
    png = WORK / (name + '.png'); img.save(png)
    subprocess.run(['convert', str(png), '-define', 'dds:compression=dxt5', '-define', 'dds:mipmaps=6', str(TEX / (name + '.dds'))], check=True)
    print(name, img.size)


def motes_rise():
    fw, fh, N = 96, 192, MOTES[0] * MOTES[1]
    px = 0.75  # (frames 3/4 the old size)
    rng = np.random.default_rng(42)
    parts = []
    for _ in range(90):
        parts.append(dict(x=rng.uniform(9, fw - 9), ph=rng.uniform(0, 1), k=int(rng.choice([1, 1, 2])), sway=rng.uniform(1, 4),
                          sf=int(rng.integers(1, 4)), sp=rng.uniform(0, 6.28), core=rng.uniform(0.55, 1.25), fl=int(rng.integers(3, 9)),
                          fp=rng.uniform(0, 6.28), top=rng.uniform(0.55, 0.95), b=rng.uniform(0.5, 1.0)))
    frames = []
    for i in range(N):
        t = i / N
        img = np.zeros((fh, fw), np.float32)
        for p in parts:
            u = (p['ph'] + p['k'] * t) % 1.0               # its rise, 0 at the ground .. 1 at its top
            hgt = u * p['top']
            y = fh - 5 - hgt * (fh - 10)
            x = p['x'] + px * p['sway'] * math.sin(2 * math.pi * p['sf'] * t + p['sp']) * (0.4 + u)
            env = min(1.0, u / 0.12) * (1 - max(0.0, (u - 0.6) / 0.4)) ** 1.5
            flick = 0.7 + 0.3 * math.sin(2 * math.pi * p['fl'] * t + p['fp'])
            splat(img, x, y, max(0.5, px * p['core'] * (1.0 - 0.35 * u)), 2.0, 0.9 * p['b'] * env * flick)
        frames.append(img)
    save_dds(frames, fw, fh, 'motes_rise', MOTES)


def sparks_burst():
    fw = fh = 192
    N = SPARKS[0] * SPARKS[1]
    q = 0.75  # (frames 3/4 the old size)
    rng = np.random.default_rng(7)
    sp = []
    for _ in range(110):
        a = rng.uniform(0, 2 * math.pi)
        up = rng.uniform(-0.2, 1.0)
        v = rng.uniform(180, 480)
        sp.append(dict(vx=math.cos(a) * v, vy=-(up * 0.8 + 0.2) * v * rng.uniform(0.6, 1.2), life=rng.uniform(0.45, 1.0), core=rng.uniform(0.45, 0.9), b=rng.uniform(0.25, 0.55)))
    dt = 1.0 / 60.0  # (played at 60 fps: the burst is over in about a second, the rest of the loop dark)
    frames = []
    for i in range(N):
        img = np.zeros((fh, fw), np.float32)
        t0, t1 = max(0.0, (i - 1.0) * dt), i * dt
        if i < 12:  # the flash
            splat(img, fw / 2, fh / 2, q * (5.0 + 1.25 * i), q * 18.0, (1.0 - i / 12.0) * 2.2)
        for s in sp:
            if t1 > s['life']: continue
            def pos(t):
                dr = (1 - math.exp(-2.2 * t)) / 2.2  # drag
                return (fw / 2 + q * s['vx'] * dr, fh / 2 + q * (s["vy"] * dr + 230 * t * t))
            heat = 1 - t1 / s['life']
            streak(img, pos(t0), pos(t1), max(0.45, q * s['core'] * (0.5 + 0.5 * heat)), s['b'] * heat ** 1.3 * 1.25)
        frames.append(img)
    save_dds(frames, fw, fh, 'sparks_burst', SPARKS)


def glow_disc():
    n = 256
    y, x = np.mgrid[-1:1:complex(n), -1:1:complex(n)]
    r = np.hypot(x, y)
    v = np.clip(np.exp(-(r / 0.45) ** 2) * (r < 1), 0, 1)
    g = (v * 255).astype(np.uint8)
    TEX.mkdir(parents=True, exist_ok=True); WORK.mkdir(parents=True, exist_ok=True)
    png = WORK / 'glow_disc.png'
    Image.fromarray(np.dstack([g, g, g, g]), 'RGBA').save(png)
    subprocess.run(['convert', str(png), '-define', 'dds:compression=dxt5', '-define', 'dds:mipmaps=8', str(TEX / 'glow_disc.dds')], check=True)


def card(L, name, tex, col, sc, w, h0, h1, at, flip, fps, phase, bb=2):
    """flip: the flipbook's (cols, rows), or None"""
    L.append(f'shape {name} tex={tex} color={col},1 scale={sc} blend=add billboard={bb} ' + (f'flip={flip[0]}x{flip[1]} fps={fps} phase={phase:.2f} ' if flip else '') + f'at={at[0]:.1f},{at[1]:.1f},{at[2]:.1f}')
    cu, cv = (1 / flip[0], 1 / flip[1]) if flip else (1, 1)
    for X, Y, u, v in ((-w, h1, 0, 0), (w, h1, cu, 0), (w, h0, cu, cv), (-w, h0, 0, cv)):
        L.append(f'v {X:.2f} {Y:.2f} 0 {u} {v} 1 1 1 1')
    L += ['t 0 1 2', 't 0 2 3']


def fxwrite(L, out):
    jf = WORK / (out.stem + '.job'); jf.write_text('\n'.join([f'out {out}'] + L) + '\n')
    r = subprocess.run(['/home/claude/tools/fxwrite', str(jf)], capture_output=True, text=True)
    print(out.name, r.stdout.strip(), r.stderr.strip())
    if r.returncode: raise SystemExit('fxwrite failed')


def motes_nif(el):
    col, glow = EL[el]
    random.seed(sum(map(ord, el)) * 3)
    L = []
    # the faint glow lying on the ground under it (flat in XY)
    L.append(f'shape glow tex={TEXREL}glow_disc.dds color={glow},1 scale=1.0 blend=add single=0')
    R = 115.0
    for X, Y, u, v in ((-R, -R, 0, 1), (R, -R, 1, 1), (R, R, 1, 0), (-R, R, 0, 0)):
        L.append(f'v {X:.1f} {Y:.1f} 1.5 {u} {v} 1 1 1 1')
    L += ['t 0 1 2', 't 0 2 3']
    k = 0
    for ring, n, rad in ((0, 3, 25.0), (1, 6, 60.0), (2, 7, 92.0)):
        for i in range(n):
            a = 2 * math.pi * (i + random.uniform(-0.3, 0.3)) / n + ring * 0.7
            r = rad * random.uniform(0.75, 1.1)
            w = random.uniform(16, 24)
            card(L, f'm{k}', TEXREL + 'motes_rise.dds', col, random.uniform(1.4, 1.9), w, 0.0, w * 2.4,
                 (r * math.cos(a), r * math.sin(a), random.uniform(-4, 4)), MOTES, random.uniform(54, 66), random.uniform(0, 4.2))
            k += 1
    fxwrite(L, MESH / f'motes_{el}.nif')


def spark_nif(el, out_name):
    col, glow = EL[el]
    random.seed(len(out_name) * 11)
    L = []
    for i, (sz, sc, ph) in enumerate(((60, 2.0, 0.0), (48, 1.7, 0.06), (72, 1.4, 0.12))):
        card(L, f's{i}', TEXREL + 'sparks_burst.dds', col, sc, sz, -sz, sz,
             (random.uniform(-6, 6), random.uniform(-6, 6), 40 + random.uniform(-6, 6)), SPARKS, 60, ph)
    # a small hot glint at the heart for the first instant (the flipbook's flash), and its light on the ground
    card(L, 'g', TEXREL + 'glow_disc.dds', glow, 1.3, 70, -70, 70, (0, 0, 40), None, 0, 0)
    fxwrite(L, MESH / out_name)


# (Noah: "remove the puddle effect on the floor and just keep it elemental effects - the lightning bottle should leave lightning on
# the ground instead of just a blue puddle") each flask's ground effect: its element's own flipbook (the altar bows' arrowhead
# fire, frost-mist, arcs, sun-rays, blood-mist - tools/bowarrows.py) burning low over the area in many small cards, each its own
# phase and size, with the fine motes rising through it
GROUND = {
    'fire':   ('emberstring', ('1,0.55,0.15', '1,0.4,0.08', '1,0.7,0.25'), 1.6, 1.0),
    'frost':  ('rimehorn', ('0.6,0.88,1', '0.75,0.95,1', '0.5,0.8,1'), 1.4, 0.8),
    'shock':  ('stormreach', ('0.75,0.6,1', '0.6,0.55,1', '0.9,0.8,1'), 1.8, 0.7),
    'poison': ('thirstbow', ('0.45,1,0.15', '0.3,0.85,0.1', '0.6,1,0.3'), 1.4, 0.9),
    'sun':    ('dawnspire', ('1,0.85,0.35', '1,0.75,0.2', '1,0.92,0.55'), 1.3, 0.8),
    'blood':  ('thirstbow', ('0.7,0.03,0.04', '0.55,0.01,0.03', '0.85,0.08,0.06'), 1.5, 0.9),
}


def ground_nif(el):
    fb, cols, sc, hmul = GROUND[el]
    tex = f'{TEXREL}ground_{el}.dds'  # (its own 60 fps flipbook: tools/alchfx.py)
    random.seed(sum(map(ord, el)) * 7)
    L = []
    k = 0
    for ring, n, rad in ((0, 4, 22.0), (1, 8, 55.0), (2, 10, 88.0)):
        for i in range(n):
            a = 2 * math.pi * (i + random.uniform(-0.35, 0.35)) / n + ring * 0.9
            r = rad * random.uniform(0.7, 1.12)
            w = random.uniform(10, 17) * (1.15 - 0.15 * ring)
            card(L, f'e{k}', tex, random.choice(cols), sc, w, -w * 0.15, w * 2.0 * hmul,
                 (r * math.cos(a), r * math.sin(a), 0.0), GROUNDG, random.uniform(54, 66), random.uniform(0, 2.6))
            k += 1
    fxwrite(L, MESH / f'ground_{el}.nif')


def main():
    WORK.mkdir(parents=True, exist_ok=True); MESH.mkdir(parents=True, exist_ok=True)
    motes_rise(); sparks_burst(); glow_disc()
    for el in EL: motes_nif(el)
    spark_nif('sun', 'burst_sun.nif')
    spark_nif('gold', 'burst_gold.nif')
    for el in GROUND: ground_nif(el)


if __name__ == '__main__':
    main()
