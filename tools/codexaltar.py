"""(Noah, 2026-11-23: "change the images for the Echo Altars and Waking an Altar tabs" - "our own Echo Altar" - "they need to match
the art style of the other ones in the lore book") the Codex's paintings for chapters III and IV: the game's own Echo Altar
(assets/fx/Meshes/CurseOfAlduin/shrine: the platform and columns, the heart stone, the rune rocks over the columns) rendered
with tools/scene3d.py's rasteriser in a ruined, misty glade, then painted over in the manner of the book's Lore Load Screens
paintings: a low, close view; dead trees and broken stones round it; moonlight shafts through the mist; brushwork (a
Kuwahara pass, directional strokes, a canvas grain); their colour grading.

  III. The Echo Altars  - the altar asleep under the moon: green-teal mist, light shafts, its runes a faint blue
  IV.  Waking an Altar  - the altar woken: the heart stone blazing, a column of blue light, shards and sparks flung out

    python3 tools/codexaltar.py [echoaltars|wakingaltar]  ->  assets/fx/Textures/CurseOfAlduin/codex/<name>.dds (+ .png
                                                              previews in build/codexaltar)"""
import math, pathlib, subprocess, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFilter
from scipy.ndimage import gaussian_filter, uniform_filter, map_coordinates

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import scene3d as S
from assets import write_dds_mips

ROOT = pathlib.Path(__file__).resolve().parent.parent
MESH = ROOT / 'assets' / 'fx' / 'Meshes' / 'CurseOfAlduin' / 'shrine'
TEX = ROOT / 'assets' / 'fx'
WORK = ROOT / 'build' / 'codexaltar'
OUT = ROOT / 'assets' / 'fx' / 'Textures' / 'CurseOfAlduin' / 'codex'
W, H = 1600, 900            # (painted at this size, delivered at 1280 x 720)


def fbm(h, w, seed, octaves=5, base=6):
    r = np.random.default_rng(seed); out = np.zeros((h, w), np.float32); amp = 1.0; tot = 0
    for o in range(octaves):
        n = base * 2 ** o; m = max(1, int(round(n * w / max(h, 1))))
        g = r.random((n + 1, m + 1)).astype(np.float32)
        yy = np.linspace(0, n, h)[:, None] * np.ones((1, w)); xx = np.linspace(0, m, w)[None, :] * np.ones((h, 1))
        out += amp * map_coordinates(g, [yy, xx], order=3, mode='reflect'); tot += amp; amp *= 0.5
    out /= tot
    return (out - out.min()) / (out.max() - out.min() + 1e-9)


def shapes(nif):
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


def add(sc, nif, at=(0, 0, 0), yaw=0.0, emit=1.0):
    for P, T, UV, maps, alpha in shapes(nif):
        sc.add(P, T, UV, albedo=maps[0], normal=maps[1], glow=maps[2], xf=S.place(*at, yaw=yaw), emit=emit, alpha_test=bool(alpha))


def ground(sc, seed):
    rng = np.random.default_rng(seed)
    bumps = [(rng.uniform(-2500, 2500), rng.uniform(-1500, 3500), rng.uniform(150, 700), rng.uniform(-20, 30)) for _ in range(50)]
    def h(x, y):
        z = 46.0 + np.clip((y - 900) * 0.10, 0, 900)       # (the ground rising gently behind the altar, into the mist)
        for bx, by, r, a in bumps: z = z + a * np.exp(-((x - bx) ** 2 + (y - by) ** 2) / (2 * r * r))
        return z
    V, T, UV = S.grid_mesh(-9000, 9000, -1500, 14000, 160, 150, h)
    def soil(p):   # moss, earth and old flagstones
        x, y = p[..., 0], p[..., 1]
        n = (np.sin(x * 0.013 + 1.3) * np.cos(y * 0.011) + 0.6 * np.sin(x * 0.037 - y * 0.029) + 0.3 * np.sin(x * 0.07 + y * 0.05)) / 1.9 * 0.5 + 0.5
        f = (np.sin(x * 0.045) * np.sin(y * 0.041) + np.sin(x * 0.021 + y * 0.017)) * 0.25 + 0.5
        moss = np.stack([0.13 + 0.05 * f, 0.19 + 0.08 * f, 0.10 + 0.03 * f], -1)
        earth = np.stack([0.17 + 0.05 * f, 0.15 + 0.04 * f, 0.12 + 0.03 * f], -1)
        m = np.clip((n - 0.42) * 4, 0, 1)[..., None]
        c = moss * m + earth * (1 - m)
        r = np.hypot(x, y)
        flag = np.clip((330 - r) / 70, 0, 1) * ((np.sin(x * 0.06) * np.sin(y * 0.06 + 0.7)) > -0.35)
        return c * (1 - flag[..., None]) + np.array([0.22, 0.23, 0.22]) * (0.7 + 0.3 * f[..., None]) * flag[..., None]
    sc.add(V, T, UV, proc=soil)
    # broken stones round the altar, and the leaning slabs of an older ring behind it
    for i in range(26):
        a = rng.uniform(0, 2 * math.pi); r = rng.uniform(330, 1100)
        x, y = math.cos(a) * r, math.sin(a) * r * 1.2 + 120
        if y < -250 and abs(x) < 300: continue   # (nothing between us and the altar)
        s = rng.uniform(18, 70)
        Vr, Tr, UVr = S.rock_mesh(seed * 100 + i, s * rng.uniform(0.8, 1.6), s * rng.uniform(0.7, 1.3), s * rng.uniform(0.5, 1.4))
        tone = rng.uniform(0.17, 0.27)
        sc.add(Vr, Tr, UVr, color=(tone, tone * 1.05, tone * 0.95), xf=S.place(x, y, 40, yaw=rng.uniform(0, 360)))
    for i in range(9):
        a = math.radians(-20 + 220 * i / 8); r = 950 + rng.uniform(-80, 80)
        x, y = math.cos(a) * r, math.sin(a) * r + 400
        Vr, Tr, UVr = S.rock_mesh(seed * 300 + i, 45, 30, rng.uniform(120, 230), rough=0.5)
        lean = np.eye(4); lean[:3, :3] = S.rot((1, 0, 0), rng.uniform(-14, 14))
        sc.add(Vr, Tr, UVr, color=(0.2, 0.21, 0.2), xf=S.place(x, y, 40, yaw=rng.uniform(0, 360)) @ lean)


def tree(draw, x, y, h, rng, w0, col):
    """a dead tree, painted as a dark silhouette: a trunk forking into crooked branches"""
    def branch(x, y, ang, ln, wd, d):
        if d > 7 or ln < 6: return
        x2, y2 = x + math.cos(ang) * ln, y - math.sin(ang) * ln
        draw.line([(x, y), (x2, y2)], fill=col, width=max(1, int(wd)))
        for _ in range(2 if d < 5 else int(rng.integers(1, 3))):
            branch(x2, y2, ang + rng.uniform(-0.7, 0.7), ln * rng.uniform(0.6, 0.8), wd * 0.66, d + 1)
    branch(x, y, math.pi / 2 + rng.uniform(-0.1, 0.1), h * 0.42, w0, 0)


def backdrop(woken, seed):
    """the sky, the far cliffs and the dead trees, painted flat (behind all the rasteriser drew)"""
    rng = np.random.default_rng(seed)
    yy = (np.arange(H)[:, None] / H) * np.ones((1, W))
    if woken: top, bot = np.array([0.05, 0.08, 0.16]), np.array([0.12, 0.20, 0.32])
    else:     top, bot = np.array([0.03, 0.06, 0.07]), np.array([0.10, 0.17, 0.17])
    img = top * (1 - yy[..., None]) + bot * yy[..., None]
    cl = fbm(H, W, seed, 6, 3)
    img = img + (cl[..., None] - 0.45).clip(0) * (np.array([0.18, 0.26, 0.34]) if woken else np.array([0.16, 0.24, 0.22]))
    mx, my = W * 0.18, H * 0.10   # the moon's glow, upper left
    d = np.hypot(np.arange(W)[None, :] - mx, np.arange(H)[:, None] - my)
    img = img + np.exp(-(d / 260) ** 2)[..., None] * np.array([0.30, 0.38, 0.40]) * (0.5 if woken else 1.0)
    img = img + np.exp(-(d / 40) ** 2)[..., None] * np.array([0.6, 0.7, 0.7]) * (0.4 if woken else 0.8)
    for k, (base, amp, tone) in enumerate(((0.30, 90, 0.45), (0.40, 70, 0.65))):   # far cliffs, lost in the mist
        ridge = H * base + amp * (fbm(1, W, seed + 10 + k, 5, 2)[0] - 0.5) * 2
        m = (np.arange(H)[:, None] > ridge[None, :])
        img = np.where(m[..., None], img * (1 - tone * 0.5) + np.array([0.04, 0.06, 0.07]) * tone * 0.5, img)
    layer = Image.new('L', (W, H), 0); dr = ImageDraw.Draw(layer)   # dead trees at both sides
    for i in range(16):
        x = rng.uniform(-40, W * 0.3) if i % 2 == 0 else rng.uniform(W * 0.7, W + 40)
        tree(dr, x, H * rng.uniform(0.50, 0.56), H * rng.uniform(0.35, 0.6), rng, rng.uniform(3, 7), 150)
    far = np.asarray(layer.filter(ImageFilter.GaussianBlur(1.6)), np.float32)[..., None] / 255
    img = img * (1 - far * 0.6) + np.array([0.05, 0.08, 0.08]) * far * 0.6
    layer = Image.new('L', (W, H), 0); dr = ImageDraw.Draw(layer)
    for i in range(6):
        x = rng.uniform(-80, W * 0.16) if i % 2 == 0 else rng.uniform(W * 0.84, W + 80)
        tree(dr, x, H * rng.uniform(0.58, 0.66), H * rng.uniform(0.7, 1.0), rng, rng.uniform(8, 14), 255)
    near = np.asarray(layer.filter(ImageFilter.GaussianBlur(0.8)), np.float32)[..., None] / 255
    return img * (1 - near * 0.8) + np.array([0.02, 0.035, 0.04]) * near * 0.8


def kuwahara(img, r):
    """the painter's filter: each pixel takes the mean of whichever of its four quadrants is calmest"""
    lum = img.mean(-1)
    m1 = np.stack([uniform_filter(img[..., c], r + 1) for c in range(3)], -1)
    l1 = uniform_filter(lum, r + 1); var = uniform_filter(lum * lum, r + 1) - l1 * l1
    best, bestv = None, None
    for dy in (-(r // 2), r // 2):
        for dx in (-(r // 2), r // 2):
            mv = np.roll(np.roll(m1, dy, 0), dx, 1); vv = np.roll(np.roll(var, dy, 0), dx, 1)
            if best is None: best, bestv = mv.copy(), vv.copy()
            else:
                k = vv < bestv; best[k] = mv[k]; bestv[k] = vv[k]
    return best


def brush(h, w, seed):
    """directional brush strokes and a canvas grain: a multiplier near 1"""
    rng = np.random.default_rng(seed)
    layer = Image.new('L', (w, h), 128); dr = ImageDraw.Draw(layer)
    for _ in range(9000):
        x, y = rng.uniform(0, w), rng.uniform(0, h)
        a = rng.normal(0.35, 0.5); l = rng.uniform(8, 40)
        dr.line([(x, y), (x + math.cos(a) * l, y + math.sin(a) * l)], fill=int(np.clip(128 + rng.normal(0, 26), 0, 255)), width=int(rng.integers(2, 6)))
    b = np.asarray(layer.filter(ImageFilter.GaussianBlur(1.0)), np.float32) / 128.0
    grain = 1 + (rng.random((h, w)).astype(np.float32) - 0.5) * 0.05
    return (1 + (b - 1) * 0.10) * grain


def render(woken):
    sc = S.Scene()
    ground(sc, 5)
    add(sc, 'shrine', emit=(2.8 if woken else 0.65))
    add(sc, 'heartstone_lit' if woken else 'heartstone', at=(0, 0, 175), yaw=20, emit=(3.2 if woken else 0.5))
    for x, y, z in ((194, 0, 204.6), (96.5, -168, 231.6), (-96.6, -167.5, 282.0), (-193.4, 0, 231.6), (-96.6, 167.5, 282.0), (97.6, 168, 282.0)):
        add(sc, 'pillarrock', at=(x, y, z + (25 if woken else 0)), yaw=x + y, emit=(2.6 if woken else 0.55))
    cam = {'eye': (70, -470, 105), 'look': (0, 0, 205), 'fov': 54}
    D, G, (eye, f, r, u, fl) = sc.raster(cam, W, H)
    hit = np.isfinite(D)
    L = np.array([-0.55, -0.45, 0.70]); L /= np.linalg.norm(L)   # the moon: upper left, in front of the altar
    R = np.array([0.45, 0.80, 0.40]); R /= np.linalg.norm(R)      # a cold rim from behind
    N, A, E, P = G['N'], G['A'], G['E'], G['P']
    lam = np.clip((N * L).sum(-1), 0, 1)
    sky = 0.5 + 0.5 * N[..., 2]
    moon = np.array([0.55, 0.70, 0.72]) if not woken else np.array([0.40, 0.48, 0.62])
    fill = np.array([0.07, 0.12, 0.12]) if not woken else np.array([0.07, 0.10, 0.18])
    rim = np.clip((N * R).sum(-1), 0, 1) ** 2
    col = A * 1.35 * (lam[..., None] * moon + sky[..., None] * fill + rim[..., None] * np.array([0.18, 0.30, 0.30]))
    heart = np.array([0, 0, 175.0])
    dv = heart - P; dist = np.linalg.norm(dv, axis=-1) + 1e-6
    hl = np.clip((N * (dv / dist[..., None])).sum(-1), 0, 1) * (1.0 / (1 + (dist / (520 if woken else 240)) ** 2))
    col = col + A * hl[..., None] * np.array([0.35, 0.62, 1.0]) * (3.0 if woken else 0.35)
    col = col + E * np.array([0.55, 0.82, 1.0])
    img = np.where(hit[..., None], col, backdrop(woken, 11))
    # mist: deepens with distance, pools on the ground, drifts in bands
    depth = np.where(hit, D, 1e5)
    mist = fbm(H, W, 21, 5, 4)
    fogc = np.array([0.12, 0.19, 0.18]) if not woken else np.array([0.10, 0.16, 0.26])
    fd = 1 - np.exp(-np.clip(depth - 700, 0, None) / 1300)
    low = np.where(hit, np.clip((95 - P[..., 2]) / 60, 0, 1), 0) * (0.35 + 0.4 * mist)
    fa = np.where(hit, np.clip(fd * (0.75 + 0.35 * mist) + low, 0, 0.85), 0.22 + 0.15 * mist)[..., None]
    img = img * (1 - fa) + fogc * fa
    def proj(p):
        rel = np.asarray(p, float) - eye; cz = rel @ f
        return W / 2 + fl * (rel @ r) / cz, H / 2 - fl * (rel @ u) / cz
    xs = np.arange(W)[None, :]; ys = np.arange(H)[:, None]
    rng = np.random.default_rng(31)
    if not woken:   # moonlight shafts slanting down through the mist from the upper left
        ang = math.radians(28)
        t = (xs * math.cos(ang) + ys * math.sin(ang))
        stripes = fbm(1, 2600, 41, 6, 6)[0]
        sh = stripes[np.clip((t + 400).astype(int), 0, 2599)]
        sh = np.clip((sh - 0.55) * 3.0, 0, 1) * np.clip(1 - ys / (H * 0.95), 0, 1) * np.clip(1.4 - xs / W * 1.3, 0, 1)
        img = img + gaussian_filter(sh, 9)[..., None] * np.array([0.13, 0.19, 0.17])
    else:           # the light rising through the heart, the shards and sparks it throws out
        x0, y0 = proj((0, 0, 140)); x1, y1 = proj((0, 0, 1600))
        t = np.clip((ys - y0) / (y1 - y0), 0, 1); xc = x0 + (x1 - x0) * t
        for wdt, a in ((60, 0.9), (150, 0.35), (320, 0.15)):
            width = wdt * (1 + 0.6 * t)
            img = img + (np.exp(-((xs - xc) / width) ** 2) * np.clip((y0 + 60 - ys) / 90, 0, 1) * (1 - t) ** 0.5)[..., None] * np.array([0.32, 0.58, 1.0]) * a
        cx, cy = proj((0, 0, 200))
        d = np.hypot(xs - cx, ys - cy)
        img = img + np.exp(-(d / 420) ** 2)[..., None] * np.array([0.10, 0.22, 0.42])
        lay = Image.new('RGB', (W, H), (0, 0, 0)); dr = ImageDraw.Draw(lay)
        for _ in range(28):   # broken shards flung out, dark against the light, blurred along their flight
            a = rng.uniform(0, 2 * math.pi); rr = rng.uniform(240, 900); s_ = rng.uniform(6, 22)
            px, py = cx + math.cos(a) * rr * 1.3, cy + math.sin(a) * rr * 0.75
            pts = [(px + math.cos(b_) * s_ * rng.uniform(0.5, 1.2), py + math.sin(b_) * s_ * rng.uniform(0.4, 1.0)) for b_ in np.sort(rng.uniform(0, 2 * math.pi, 6))]
            for k in range(6):   # (a short smear behind it)
                o = k * s_ * 0.25
                v = int(14 + 3 * k)
                dr.polygon([(x_ - math.cos(a) * o, y_ - math.sin(a) * o) for x_, y_ in pts], fill=(v, v + 4, v + 10))
        sh = np.asarray(lay.filter(ImageFilter.GaussianBlur(1.2)), np.float32) / 255
        mask = (sh.sum(-1, keepdims=True) > 0.02)
        img = np.where(mask, sh + img * 0.18, img)
        for _ in range(260):  # sparks
            a = rng.uniform(0, 2 * math.pi); rr = rng.uniform(40, 1000) ** 0.9
            px, py = cx + math.cos(a) * rr * 1.2, cy + math.sin(a) * rr * 0.9 - rng.uniform(0, 300)
            if 0 <= px < W and 0 <= py < H:
                s = rng.uniform(0.8, 2.6); a0, a1, b0, b1 = int(max(0, py - 9)), int(min(H, py + 9)), int(max(0, px - 9)), int(min(W, px + 9))
                gy, gx = np.mgrid[a0:a1, b0:b1]
                img[a0:a1, b0:b1] += np.exp(-((gx - px) ** 2 + (gy - py) ** 2) / (2 * s * s))[..., None] * np.array([0.55, 0.82, 1.0]) * rng.uniform(0.5, 1.4)
    # a dark foreground: grass and a rock at the bottom corners, out of focus
    fg = Image.new('L', (W, H), 0); dr = ImageDraw.Draw(fg)
    for side in (0, 1):
        bx = -80 if side == 0 else W - 300
        dr.polygon([(bx, H), (bx + 40, H - 120), (bx + 150, H - 160), (bx + 300, H - 90), (bx + 380, H)], fill=255)
    fgm = np.asarray(fg.filter(ImageFilter.GaussianBlur(6)), np.float32)[..., None] / 255
    img = img * (1 - fgm * 0.85) + np.array([0.02, 0.03, 0.035]) * fgm * 0.85
    # bloom, then the paint: Kuwahara, strokes, a soft unsharp, the book's grade, a vignette
    bright = np.clip(img - 0.5, 0, None)
    img = img + np.stack([gaussian_filter(bright[..., c], 12) + gaussian_filter(bright[..., c], 44) * 0.7 for c in range(3)], -1) * (1.2 if woken else 0.8)
    img = 1 - np.exp(-img * 1.7)
    pad = np.pad(img.astype(np.float32), ((12, 12), (12, 12), (0, 0)), mode='reflect')
    painted = kuwahara(pad, 6)[12:-12, 12:-12]
    painted = 0.78 * painted + 0.22 * img
    blur = np.stack([gaussian_filter(painted[..., c], 2.0) for c in range(3)], -1)
    painted = painted + (painted - blur) * 0.9
    painted = painted * brush(H, W, 51)[..., None]
    lum = painted.mean(-1, keepdims=True)
    gsh = np.array([0.80, 1.00, 0.98]) if not woken else np.array([0.78, 0.92, 1.12])
    ghi = np.array([1.04, 1.02, 0.94]) if not woken else np.array([0.96, 1.02, 1.08])
    painted = painted * (gsh * (1 - lum) + ghi * lum)
    painted = (painted - 0.5) * 1.12 + 0.5 - 0.03
    vy, vx = np.mgrid[0:H, 0:W]
    painted = painted * (1 - 0.6 * (((vx / W - 0.5) ** 2) * 1.3 + ((vy / H - 0.5) ** 2) * 1.8))[..., None]
    painted = np.clip(painted, 0, 1) ** (1 / 1.05)
    return Image.fromarray((painted * 255).astype(np.uint8)).resize((1280, 720), Image.LANCZOS)


if __name__ == '__main__':
    WORK.mkdir(parents=True, exist_ok=True); OUT.mkdir(parents=True, exist_ok=True)
    which = sys.argv[1:] or ['echoaltars', 'wakingaltar']
    for woken, name in ((False, 'echoaltars'), (True, 'wakingaltar')):
        if name not in which: continue
        im = render(woken)
        im.save(WORK / f'{name}.png')
        write_dds_mips(im, OUT / f'{name}.dds', 'DXT1')
        print('wrote', name)
