"""(Noah, 2026-11-23: "change the images for the Echo Altars and Waking an Altar tabs" - "our own Echo Altar") the Codex's
paintings for chapters III and IV, rendered from the game's own Echo Altar (assets/fx/Meshes/CurseOfAlduin/shrine: the
platform and columns, the heart stone, the rune rocks over the columns) with tools/scene3d.py's rasteriser:

  III. The Echo Altars  - the altar asleep at night: its runes a faint blue, the heart stone dark, mist on the ground
  IV.  Waking an Altar  - the same altar woken: the lit heart stone, the rune circle over it, the light rising through it,
                          motes climbing, the runes blazing

    python3 tools/codexaltar.py  ->  assets/fx/Textures/CurseOfAlduin/codex/echoaltars.dds, wakingaltar.dds (+ .png previews
                                     in build/codexaltar)"""
import math, pathlib, subprocess, sys
import numpy as np
from PIL import Image, ImageFilter
from scipy.ndimage import gaussian_filter

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import scene3d as S
from assets import write_dds_mips

ROOT = pathlib.Path(__file__).resolve().parent.parent
MESH = ROOT / 'assets' / 'fx' / 'Meshes' / 'CurseOfAlduin' / 'shrine'
TEX = ROOT / 'assets' / 'fx'
WORK = ROOT / 'build' / 'codexaltar'
OUT = ROOT / 'assets' / 'fx' / 'Textures' / 'CurseOfAlduin' / 'codex'
W, H = 1280, 720


def shapes(nif):
    """the mesh's shapes as (positions, triangles, uv, [diffuse, normal, glow] images, alpha)"""
    cache = WORK / nif
    if not (cache / 'nodes.txt').exists():
        cache.mkdir(parents=True, exist_ok=True)
        subprocess.run([S.NIFEXPORT, str(MESH / f'{nif}.nif'), str(cache)], check=True, capture_output=True)
    out = []
    for f in sorted(cache.glob('shape_*.txt')):
        v, t, tex, alpha, xf = [], [], [None] * 3, 0, (0, 0, 0, 1)
        for l in open(f):
            if l.startswith('v '): v.append([float(x) for x in l.split()[1:6]])
            elif l.startswith('f '): t.append([int(x) for x in l.split()[1:]])
            elif l.startswith('tex'):
                k, p = l.rstrip('\n').split('\t'); tex[int(k[3])] = p.strip() or None
            elif l.startswith('alpha'): alpha = int(l.split()[1])
            elif l.startswith('shapexf'): xf = [float(x) for x in l.split()[1:]]
        if not v or not t: continue
        v = np.array(v); P = v[:, :3] * xf[3] + np.array(xf[:3])
        maps = []
        for p in tex:
            img = None
            if p:
                rel = p.replace('\\', '/')
                rel = rel[len('textures/'):] if rel.lower().startswith('textures/') else rel
                fp = TEX / 'Textures' / rel
                if fp.exists(): img = S.load_tex(fp, 1024)
            maps.append(img)
        out.append((P, np.array(t), v[:, 3:5], maps, alpha))
    return out


def add(sc, nif, at=(0, 0, 0), yaw=0.0, emit=1.0, tint=(1, 1, 1), glow_tint=None):
    xf = S.place(*at, yaw=yaw)
    for P, T, UV, maps, alpha in shapes(nif):
        it = sc.add(P, T, UV, albedo=maps[0], normal=maps[1], glow=maps[2], xf=xf, emit=emit, alpha_test=bool(alpha), tint=tint)
        if glow_tint is not None and it.glow is not None:
            it.glow = it.glow.copy(); it.glow[..., :3] *= np.array(glow_tint)


def ground(sc):
    """the ground round the altar: a wide plain, a little uneven"""
    rng = np.random.default_rng(3)
    bumps = [(rng.uniform(-2500, 2500), rng.uniform(-2500, 2500), rng.uniform(150, 600), rng.uniform(-14, 14)) for _ in range(40)]
    def h(x, y):
        z = 46.0
        for bx, by, r, a in bumps: z = z + a * np.exp(-((x - bx) ** 2 + (y - by) ** 2) / (2 * r * r))
        return z
    V, T, UV = S.grid_mesh(-3000, 3000, -3000, 3000, 90, 90, h)
    def soil(p):   # grass and bare earth in patches, flagstones near the altar
        x, y = p[..., 0], p[..., 1]
        n = (np.sin(x * 0.013 + 1.3) * np.cos(y * 0.011) + 0.6 * np.sin(x * 0.037 - y * 0.029) + 0.3 * np.sin(x * 0.11 + y * 0.13)) / 1.9 * 0.5 + 0.5
        f = (np.sin(x * 0.045) * np.sin(y * 0.041) + np.sin(x * 0.021 + y * 0.017)) * 0.5 * 0.5 + 0.5
        grass = np.stack([0.16 + 0.05 * f, 0.20 + 0.07 * f, 0.11 + 0.03 * f], -1)
        earth = np.stack([0.20 + 0.05 * f, 0.17 + 0.04 * f, 0.13 + 0.03 * f], -1)
        m = np.clip((n - 0.45) * 4, 0, 1)[..., None]
        c = grass * m + earth * (1 - m)
        r = np.hypot(x, y)
        stone = np.clip((300 - r) / 60, 0, 1)[..., None] * (0.5 + 0.5 * (np.sin(x * 0.08) * np.sin(y * 0.08) > -0.2))[..., None]
        return c * (1 - stone) + np.array([0.24, 0.24, 0.25]) * f[..., None] * stone + 0.0
    sc.add(V, T, UV, proc=soil)


def render(woken):
    sc = S.Scene()
    ground(sc)
    gt = (1.0, 1.0, 1.0)
    add(sc, 'shrine', emit=(2.6 if woken else 0.55))
    add(sc, 'heartstone_lit' if woken else 'heartstone', at=(0, 0, 175), yaw=20, emit=(3.0 if woken else 0.5))
    for x, y, z in ((194, 0, 204.6), (96.5, -168, 231.6), (-96.6, -167.5, 282.0), (-193.4, 0, 231.6), (-96.6, 167.5, 282.0), (97.6, 168, 282.0)):
        add(sc, 'pillarrock', at=(x, y, z), yaw=x + y, emit=(2.4 if woken else 0.6))
    cam = {'eye': (40, -600, 215), 'look': (0, 0, 175), 'fov': 44}
    D, G, (eye, f, r, u, fl) = sc.raster(cam, W, H)
    hit = np.isfinite(D)
    # light: a cold moon from the upper left, a dim sky fill, and the altar's own light on what is near it
    L = np.array([-0.45, -0.35, 0.82]); L /= np.linalg.norm(L)
    N, A, E, P = G['N'], G['A'], G['E'], G['P']
    lam = np.clip((N * L).sum(-1), 0, 1)
    sky = 0.5 + 0.5 * N[..., 2]
    moon = np.array([0.55, 0.62, 0.80]) * (0.9 if not woken else 0.6)
    fill = np.array([0.10, 0.13, 0.20])
    col = A * (lam[..., None] * moon + sky[..., None] * fill)
    # the altar's light: blue, from its heart (and much stronger woken)
    heart = np.array([0, 0, 175.0])
    dv = heart - P; dist = np.linalg.norm(dv, axis=-1) + 1e-6
    hl = np.clip((N * (dv / dist[..., None])).sum(-1), 0, 1) * (1.0 / (1 + (dist / (420 if woken else 220)) ** 2))
    blue = np.array([0.35, 0.62, 1.0])
    col += A * hl[..., None] * blue * (2.4 if woken else 0.35)
    col += E * np.array([0.55, 0.8, 1.0])
    # the sky behind: night, a band of mist low over the plain
    yy = (np.arange(H)[:, None] / H) * np.ones((1, W))
    bg = np.stack([0.025 + 0.05 * yy, 0.032 + 0.06 * yy, 0.055 + 0.08 * yy], -1)
    rng = np.random.default_rng(7)
    for _ in range(260):   # stars
        sx, sy = rng.integers(0, W), rng.integers(0, int(H * 0.45))
        bg[sy, sx] += rng.uniform(0.15, 0.6)
    ridge = H * 0.47 + 26 * np.sin(np.arange(W) * 0.006 + 1.0) + 14 * np.sin(np.arange(W) * 0.019 + 0.3) + 6 * np.sin(np.arange(W) * 0.07)
    mtn = (np.arange(H)[:, None] > ridge[None, :])[..., None]
    bg = np.where(mtn, np.array([0.045, 0.055, 0.075]), bg)
    img = np.where(hit[..., None], col, bg)
    # depth and height fog: cold mist that thickens with distance and lies low on the ground
    depth = np.where(hit, D, 1e5)
    fogc = np.array([0.07, 0.09, 0.13]) if not woken else np.array([0.08, 0.11, 0.17])
    fd = 1 - np.exp(-np.clip(depth - 700, 0, None) / 3200)
    low = np.where(hit, np.clip((110 - P[..., 2]) / 70, 0, 1), 0) * 0.35
    fa = np.clip(fd + low, 0, 1)[..., None]
    img = img * (1 - fa) + fogc * fa
    if woken:   # the light rising through the heart: a soft column, and motes climbing it
        cx, cy = np.array(W / 2.0), np.array(H / 2.0)
        def proj(p):
            rel = np.asarray(p, float) - eye; cz = rel @ f
            return W / 2 + fl * (rel @ r) / cz, H / 2 - fl * (rel @ u) / cz
        x0, y0 = proj((0, 0, 120)); x1, y1 = proj((0, 0, 1200))
        xs = np.arange(W)[None, :]; ys = np.arange(H)[:, None]
        t = np.clip((ys - y0) / (y1 - y0), 0, 1); xc = x0 + (x1 - x0) * t
        width = 34 + 18 * t
        beam = np.exp(-((xs - xc) / width) ** 2) * (ys < y0) * (1 - t) ** 0.6
        img += beam[..., None] * np.array([0.30, 0.55, 1.0]) * 0.85
        for _ in range(90):
            a = rng.uniform(0, 2 * math.pi); rr = rng.uniform(10, 90); z = rng.uniform(150, 650)
            mx, my = proj((math.cos(a) * rr, math.sin(a) * rr, z))
            if 0 <= mx < W and 0 <= my < H:
                s = rng.uniform(1.2, 3.0)
                yy0, yy1, xx0, xx1 = int(max(0, my - 8)), int(min(H, my + 8)), int(max(0, mx - 8)), int(min(W, mx + 8))
                gy, gx = np.mgrid[yy0:yy1, xx0:xx1]
                img[yy0:yy1, xx0:xx1] += np.exp(-((gx - mx) ** 2 + (gy - my) ** 2) / (2 * s * s))[..., None] * np.array([0.5, 0.8, 1.0]) * rng.uniform(0.4, 1.0)
    # bloom from the bright parts, a vignette, a filmic curve
    bright = np.clip(img - 0.55, 0, None)
    bloom = np.stack([gaussian_filter(bright[..., c], 14) + gaussian_filter(bright[..., c], 40) * 0.6 for c in range(3)], -1)
    img = img + bloom * (1.1 if woken else 0.7)
    vy, vx = np.mgrid[0:H, 0:W]
    vig = 1 - 0.55 * (((vx / W - 0.5) ** 2) * 1.2 + ((vy / H - 0.5) ** 2) * 1.6)
    img = img * vig[..., None]
    img = 1 - np.exp(-img * 1.6)
    img = np.clip(img ** (1 / 1.12), 0, 1)
    return Image.fromarray((img * 255).astype(np.uint8))


if __name__ == '__main__':
    WORK.mkdir(parents=True, exist_ok=True); OUT.mkdir(parents=True, exist_ok=True)
    for woken, name in ((False, 'echoaltars'), (True, 'wakingaltar')):
        im = render(woken)
        im.save(WORK / f'{name}.png')
        write_dds_mips(im, OUT / f'{name}.dds', 'DXT1')
        print('wrote', name)
