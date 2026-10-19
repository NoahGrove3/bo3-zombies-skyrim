"""The altar's bows' arrows (Noah's reference: Der Eisendrache's bows - the arrowhead itself burning with the bow's
element): each bow's ammunition model is Skyrim's Daedric arrow (its quiver, its shaft) with an ornate runic arrowhead
of light over its tip and the element alive round it, all inside the arrow's own model, so the effect rides the nocked
arrow exactly as the string draws it back (no drifting). 24 fps flipbooks, 2K atlases.

    fire   tongues of flame streaming back off a white-hot head, embers
    frost  a crystal head breathing cold mist, glints of frost
    shock  a violet head wrapped in crackling arcs of lightning
    wind   a pale head with gusts spiralling round it
    sun    a golden head throwing slow-turning rays, a hot core
    blood  a crimson head weeping a swirl of blood-mist

    python tools/bowarrows.py <Daedric arrow nif> <out Data dir>
"""
import json, math, pathlib, subprocess, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFilter
from scipy.ndimage import map_coordinates

ROOT = pathlib.Path(__file__).resolve().parent.parent
BASE = sys.argv[1] if len(sys.argv) > 1 else '/home/claude/work/arrow/daedricarrow.nif'
OUT = pathlib.Path(sys.argv[2] if len(sys.argv) > 2 else ROOT / 'assets' / 'fx')
WORK = ROOT / 'build' / 'bowarrows'
N, COLS, ROWS = 256, 8, 8
F = COLS * ROWS
TIP = 60.0   # the Daedric arrow's tip on +Y


def tileable(n, beta, seed):
    rng = np.random.default_rng(seed)
    f = np.fft.fftfreq(n); fx, fy = np.meshgrid(f, f); rad = np.sqrt(fx ** 2 + fy ** 2); rad[0, 0] = 1
    spec = (rng.normal(size=(n, n)) + 1j * rng.normal(size=(n, n))) / rad ** beta; spec[0, 0] = 0
    o = np.real(np.fft.ifft2(spec)).astype(np.float32)
    return (o - o.min()) / (o.max() - o.min())


def samp(tex, u, v):
    n = tex.shape[0]
    return map_coordinates(tex, [(v * n) % n, (u * n) % n], order=1, mode='grid-wrap')


def ramp(x, stops):
    x = np.clip(x, 0, 1); out = np.zeros(x.shape + (3,), np.float32)
    for (a, ca), (b, cb) in zip(stops[:-1], stops[1:]):
        t = np.clip((x - a) / max(1e-6, b - a), 0, 1)[..., None] * ((x >= a) & (x <= b))[..., None]
        out = np.where(((x >= a) & (x <= b))[..., None], np.array(ca) * (1 - t) + np.array(cb) * t, out)
    return out / 255.0


def atlas(frame_fn):
    at = np.zeros((N * ROWS, N * COLS, 4), np.float32)
    for f in range(F):
        rgb, a = frame_fn(f / F, f)
        cy, cx = divmod(f, COLS)
        at[cy * N:(cy + 1) * N, cx * N:(cx + 1) * N] = np.dstack([rgb * a[..., None], a])
    return Image.fromarray((np.clip(at, 0, 1) * 255).astype(np.uint8), 'RGBA')


yy, xx = np.mgrid[0:N, 0:N].astype(np.float32) / N
X, Y = xx * 2 - 1, 1 - yy * 2          # frame coords: x -1..1, y -1..1 (y up = toward the arrow's point)
R = np.hypot(X, Y); ANG = np.arctan2(Y, X)
A = tileable(512, 1.9, 1); B = tileable(512, 1.4, 2); C = tileable(512, 1.0, 3)


def fx_fire(t, f):
    """flames streaming back from the head (the head at the top of the frame, the flames trailing down)"""
    v = (Y + 1) / 2   # 0 bottom (tail) .. 1 top (head)
    n1 = samp(A, xx * 0.8, yy * 0.9 - t * 2.0); n2 = samp(B, xx * 1.5 + 0.3, yy * 1.7 - t * 3.0); n3 = samp(C, xx * 3, yy * 3 - t * 5.0)
    warp = (n1 - 0.5) * 0.5 * (1 - v) + (n2 - 0.5) * 0.25 * (1 - v)
    width = 0.18 + 0.75 * (1 - v) ** 0.7 * v ** 0.3
    body = np.clip(1 - np.abs(X + warp) / np.maximum(width, 0.02), 0, 1)
    lick = np.clip(n2 * 0.75 + n3 * 0.15 + n1 * 0.1 - (1 - v) * 0.8 + 0.2, 0, 1) * 1.8
    core = np.exp(-((X / 0.12) ** 2 + ((Y - 0.55) / 0.22) ** 2))
    rng = np.random.default_rng(77)
    emb = np.zeros_like(R)
    for k in range(9):   # embers riding back on the flames
        ph = (t * (1.2 + 0.3 * (k % 3)) + k / 9) % 1.0
        ex, ey = rng.uniform(-0.35, 0.35) + 0.1 * math.sin(ph * 6 + k), 0.5 - ph * 1.5
        emb += np.exp(-(((X - ex) / 0.018) ** 2 + ((Y - ey) / 0.018) ** 2)) * (1 - ph)
    inten = np.clip(body ** 0.8 * lick + core + emb, 0, 1)
    rgb = ramp(inten, [(0, (40, 4, 0)), (0.3, (200, 40, 4)), (0.6, (255, 130, 20)), (0.85, (255, 210, 90)), (1, (255, 250, 220))])
    return rgb, np.clip(inten * 1.2, 0, 1)


def fx_frost(t, f):
    """cold mist curling round the head, glints of frost"""
    ang = ANG + t * 2 * np.pi * 0.5
    n1 = samp(A, np.cos(ang) * R * 0.3 + 0.5, np.sin(ang) * R * 0.3 + t)
    mist = np.clip(1 - R / 0.95, 0, 1) ** 1.1 * (0.5 + 1.0 * n1)
    core = np.exp(-(R / 0.09) ** 2)
    shards = (0.5 + 0.5 * np.cos(ANG * 6 + 0.3)) ** 30 * np.clip(1 - R / 0.55, 0, 1) * (R > 0.05)   # six crystal points
    rng = np.random.default_rng(f // 4)
    glint = np.zeros_like(R)
    for _ in range(6):
        gx, gy = rng.uniform(-0.6, 0.6), rng.uniform(-0.6, 0.6)
        d = np.hypot(X - gx, Y - gy)
        glint += np.exp(-(d / 0.025) ** 2) + 0.4 * np.exp(-((X - gx) / 0.005) ** 2) * np.exp(-((Y - gy) / 0.1) ** 2) + 0.4 * np.exp(-((Y - gy) / 0.005) ** 2) * np.exp(-((X - gx) / 0.1) ** 2)
    inten = np.clip(mist * 0.9 + core + 0.9 * shards + glint * (0.5 + 0.5 * math.sin(t * 2 * math.pi * 3)), 0, 1)
    rgb = ramp(inten, [(0, (10, 30, 60)), (0.4, (90, 170, 240)), (0.75, (190, 235, 255)), (1, (255, 255, 255))])
    return rgb, inten


def fx_shock(t, f):
    """arcs of lightning crackling out from the head and round it (a new path every two frames)"""
    rng = np.random.default_rng(1000 + f // 2)
    im = Image.new('L', (N, N), 0); d = ImageDraw.Draw(im)
    for k in range(5):
        a = rng.uniform(0, 2 * np.pi); x, y = N / 2, N / 2; pts = [(x, y)]
        L = rng.uniform(0.25, 0.48) * N
        for s in range(10):
            a += rng.uniform(-0.7, 0.7); x += math.cos(a) * L / 10; y += math.sin(a) * L / 10; pts.append((x, y))
        d.line(pts, fill=255, width=3)
        if rng.random() < 0.6:
            j = int(rng.integers(3, 8)); bx, by = pts[j]; ba = a + rng.uniform(-1.2, 1.2); bp = [(bx, by)]
            for s in range(5):
                ba += rng.uniform(-0.8, 0.8); bx += math.cos(ba) * L / 14; by += math.sin(ba) * L / 14; bp.append((bx, by))
            d.line(bp, fill=200, width=1)
    arcs = np.asarray(im, np.float32) / 255
    halo = np.asarray(im.filter(ImageFilter.GaussianBlur(4)), np.float32) / 255
    core = np.exp(-(R / 0.16) ** 2) * (0.85 + 0.15 * math.sin(t * 40))
    inten = np.clip(arcs * 0.8 + 2.4 * halo + core, 0, 1)
    rgb = ramp(inten, [(0, (40, 10, 110)), (0.3, (120, 60, 255)), (0.75, (190, 150, 255)), (1, (245, 235, 255))])
    return rgb, inten


def fx_wind(t, f):
    """gusts spiralling round the head"""
    spiral = np.sin(ANG * 3 - np.log(R + 0.05) * 6 + t * 2 * np.pi * 2)
    n1 = samp(B, xx + t, yy)
    band = np.clip(spiral * 1.5 - 0.6, 0, 1) * np.clip(1 - R / 0.95, 0, 1) * (0.6 + 0.6 * n1)
    core = np.exp(-(R / 0.15) ** 2)
    inten = np.clip(band + core, 0, 1)
    rgb = ramp(inten, [(0, (20, 60, 50)), (0.4, (120, 230, 200)), (0.8, (220, 255, 245)), (1, (255, 255, 255))])
    return rgb, inten * 0.95


def fx_sun(t, f):
    """slow-turning rays round a white-gold core"""
    rays = (0.5 + 0.5 * np.cos(ANG * 12 + t * 2 * np.pi)) ** 6 * np.clip(1 - R / 0.95, 0, 1) ** 1.5
    rays2 = (0.5 + 0.5 * np.cos(ANG * 7 - t * 2 * np.pi)) ** 8 * np.clip(1 - R / 0.8, 0, 1) ** 1.5
    core = np.exp(-(R / (0.2 + 0.03 * math.sin(t * 2 * math.pi * 2))) ** 2)
    inten = np.clip(rays * 0.8 + rays2 * 0.6 + core + 0.25 * np.clip(1 - R / 0.6, 0, 1), 0, 1)
    rgb = ramp(inten, [(0, (80, 40, 0)), (0.4, (255, 170, 40)), (0.8, (255, 230, 150)), (1, (255, 255, 240))])
    return rgb, inten


def fx_blood(t, f):
    """a swirl of blood-mist, drops wept back from the head"""
    ang = ANG - t * 2 * np.pi
    n1 = samp(A, np.cos(ang) * R * 0.35 + 0.5, np.sin(ang) * R * 0.35 + 0.5)
    mist = np.clip(1 - R / 0.9, 0, 1) ** 1.0 * (0.5 + 1.1 * n1)
    drops = np.zeros_like(R)
    for k in range(4):
        x0 = -0.3 + 0.2 * k; ph = (t * 1.5 + k * 0.27) % 1.0
        y0 = 0.3 - ph * 1.2
        drops += np.exp(-(((X - x0) / 0.035) ** 2 + ((Y - y0) / 0.06) ** 2)) * (1 - ph)
    core = np.exp(-(R / 0.16) ** 2)
    inten = np.clip(mist * 0.75 + drops + core, 0, 1)
    rgb = ramp(inten, [(0, (90, 0, 8)), (0.35, (200, 16, 28)), (0.75, (255, 70, 70)), (1, (255, 210, 200))])
    return rgb, inten


FX = {'fire': fx_fire, 'frost': fx_frost, 'shock': fx_shock, 'wind': fx_wind, 'sun': fx_sun, 'blood': fx_blood}


def head_texture(seed):
    """the ornate arrowhead of light: a long central blade with two swept barbs, runes down its spine (white, the shape
    tints it), 256 x 512 pointing up"""
    W, H = 256, 512
    S = 4
    im = Image.new('L', (W * S, H * S), 0); d = ImageDraw.Draw(im)
    P = lambda pts: [(x * W * S, y * H * S) for x, y in pts]
    blade = [(0.5, 0.0), (0.62, 0.22), (0.58, 0.55), (0.53, 0.78), (0.47, 0.78), (0.42, 0.55), (0.38, 0.22)]
    d.polygon(P(blade), fill=150)
    for sx in (1, -1):
        barb = [(0.5 + sx * 0.06, 0.35), (0.5 + sx * 0.44, 0.62), (0.5 + sx * 0.3, 0.66), (0.5 + sx * 0.38, 0.86), (0.5 + sx * 0.1, 0.62)]
        d.polygon(P(barb), fill=120)
    d.line(P(blade + [blade[0]]), fill=255, width=6 * S)
    for sx in (1, -1):
        d.line(P([(0.5 + sx * 0.06, 0.35), (0.5 + sx * 0.44, 0.62), (0.5 + sx * 0.3, 0.66), (0.5 + sx * 0.38, 0.86), (0.5 + sx * 0.1, 0.62)]), fill=255, width=4 * S)
    for k, (a, b) in enumerate((((0.5, 0.18), (0.5, 0.7)),)):
        d.line(P([a, b]), fill=255, width=3 * S)
    glyph = [[(0, -1, 0, 1), (0, -0.3, 0.6, -0.9)], [(-0.5, 1, 0, -1), (0.5, 1, 0, -1)], [(0, -1, 0, 1), (-0.6, 0, 0.6, 0)], [(-0.6, -1, 0.6, 1), (0.6, -1, -0.6, 1)]]
    for k, g in enumerate(glyph):
        cy = (0.26 + 0.12 * k) * H * S; hs = 0.035 * H * S
        for x0, y0, x1, y1 in g:
            d.line([(W * S / 2 + x0 * hs * 0.7, cy + y0 * hs), (W * S / 2 + x1 * hs * 0.7, cy + y1 * hs)], fill=255, width=2 * S)
    im = im.resize((W, H), Image.LANCZOS)
    a = np.asarray(im, np.float32) / 255
    halo = np.asarray(im.filter(ImageFilter.GaussianBlur(10)), np.float32) / 255
    return np.clip(a + 0.6 * halo, 0, 1)


def save_dds(img, name):
    p = OUT / 'Textures' / 'CurseOfAlduin' / 'bow' / name
    p.parent.mkdir(parents=True, exist_ok=True); WORK.mkdir(parents=True, exist_ok=True)
    tmp = WORK / (name + '.png'); img.save(tmp)
    subprocess.run(['convert', str(tmp), '-define', 'dds:compression=dxt5', '-define', 'dds:mipmaps=12', str(p)], check=True)
    return 'CurseOfAlduin\\bow\\' + name


def build(bid, element, glow):
    fxtex = save_dds(atlas(FX[element]), f'{bid}_arrowfx.dds')
    head = head_texture(len(bid))
    htex = save_dds(Image.fromarray((np.dstack([np.ones_like(head), np.ones_like(head), np.ones_like(head), head]) * 255).astype(np.uint8), 'RGBA'), 'arrowhead.dds')
    out = OUT / 'Meshes' / 'CurseOfAlduin' / 'bow' / f'{bid}_arrow.nif'
    out.parent.mkdir(parents=True, exist_ok=True)
    g = ','.join(f'{c:.3f}' for c in glow)
    pale = ','.join(f'{min(1.0, c * 0.5 + 0.5):.3f}' for c in glow)
    job = [f'base {BASE}', f'out {out}']
    # the arrowhead of light: two crossed planes over the steel tip, its point a little past it
    for k, (col, sc, w) in enumerate(((g, 1.6, 4.2), (pale, 2.0, 3.0))):
        job.append(f'shape head{k} tex={htex} color={col},1 scale={sc} blend=add')
        for ax in ((1, 0, 0), (0, 0, 1)):
            for (du, dv, u, v) in ((-1, -1, 0, 1), (1, -1, 1, 1), (1, 1, 1, 0), (-1, 1, 0, 0)):
                x = ax[0] * du * w; z = ax[2] * du * w; y = TIP - 6.0 + dv * 9.0
                job.append(f'v {x:.3f} {y:.3f} {z:.3f} {u} {v}')
        job += ['t 0 1 2', 't 0 2 3', 't 4 5 6', 't 4 6 7']
    # the element round it: a flipbook that turns to face you (fire streams back down the shaft: a taller quad)
    tall = element == 'fire'
    for k, (col, sc, s, ph) in enumerate(((('1,1,1'), 1.7, 1.0, 0.0), (g, 1.2, 1.35, 1.7))):
        hh = (13.0 if tall else 8.0) * s; ww = (6.5 if tall else 8.0) * s
        cy = TIP - (8.0 if tall else 2.0)
        job.append(f'shape fx{k} tex={fxtex} color={col},1 scale={sc} blend=add billboard=2 flip={COLS}x{ROWS} fps=24 phase={ph} at=0,{cy:.2f},0')
        for X_, Y_, u, v in ((-ww, hh, 0, 0), (ww, hh, 1 / COLS, 0), (ww, -hh, 1 / COLS, 1 / ROWS), (-ww, -hh, 0, 1 / ROWS)):
            job.append(f'v {X_:.2f} {Y_:.2f} {0.5 - k:.2f} {u} {v}')
        job += ['t 0 1 2', 't 0 2 3']
    jf = WORK / f'{bid}_arrow.job'; jf.write_text('\n'.join(job) + '\n')
    r = subprocess.run(['/home/claude/tools/fxwrite', str(jf)], capture_output=True, text=True)
    print(bid, '|', r.stdout.strip() or r.stderr.strip())
    if r.returncode:
        raise SystemExit('fxwrite failed')


def main():
    WORK.mkdir(parents=True, exist_ok=True)
    for r in json.load(open(ROOT / 'sheets' / 'bows.json'))['rows']:
        build(r['id'], r['element'], tuple(r['glow']))


if __name__ == '__main__':
    main()
