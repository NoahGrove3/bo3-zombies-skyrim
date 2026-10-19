"""Stage 2's art (src/Echo.cpp, sheets/echo.json): the Reliquary of the Tongues under High Hrothgar, the sealed door
that leads to it, the four pedestals the wonder weapons are forged on, the Echo Stone that rises by a freed altar, the
Tongue-fire braziers and banners a freed city burns, and the guarded relic shrines in the wilds. Everything is our own
2K texture work (no game textures referenced), lit stone with blue runes that breathe, and unlit additive flipbook
fire (24 fps).

    python tools/reliquary.py <out Data dir>       (assets/fx)

Model frames: origin on the floor, +Y forward (the side you face it from), +Z up. Room: floor centre at the origin,
walls at +-600, ceiling 640; the exit door stands against the south wall (y -600) facing north.
"""
import math, pathlib, random, subprocess, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFilter
sys.path.insert(0, str(pathlib.Path(__file__).parent))
import stand as S
import altarart as AA
from rockart import tileable

OUT = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else pathlib.Path(__file__).resolve().parent.parent / 'assets' / 'fx')
WORK = pathlib.Path(__file__).resolve().parent.parent / 'build' / 'reliquary'
TP = 'CurseOfAlduin\\reliquary\\'
BLUE = (0.35, 0.68, 1.0)
WONDERS = {'gravecaller': '7CFFB0', 'frostbinder': '9FE4FF', 'wolfcrown': 'C08CFF', 'barrowhorn': 'FFC060'}
STROKES = AA.BASE_GLYPHS


# ------------------------------------------------------------------------------------------------ texture helpers
def save(im, name, alpha=False):
    """DDS with a full mip chain (big textures seen from afar shimmer without)"""
    p = OUT / 'Textures' / 'CurseOfAlduin' / 'reliquary' / name
    p.parent.mkdir(parents=True, exist_ok=True)
    tmp = WORK / (name + '.png'); tmp.parent.mkdir(parents=True, exist_ok=True)
    im.convert('RGBA' if alpha else 'RGB').save(tmp)
    subprocess.run(['convert', str(tmp), '-define', f'dds:compression={"dxt5" if alpha else "dxt1"}', '-define', 'dds:mipmaps=12', str(p)], check=True)
    return TP + name


def img(a):
    return Image.fromarray(np.clip(a, 0, 255).astype(np.uint8), 'RGB' if a.shape[2] == 3 else 'RGBA')


def normal_map(height, strength, shine):
    gy, gx = np.gradient(height * strength)
    nrm = np.dstack([-gx, -gy, np.ones_like(gx)])
    nrm /= np.linalg.norm(nrm, axis=2, keepdims=True)
    return img(np.dstack([(nrm[..., 0] * 0.5 + 0.5) * 255, (nrm[..., 1] * 0.5 + 0.5) * 255, (nrm[..., 2] * 0.5 + 0.5) * 255, np.clip(shine * 255, 0, 255)]))


def blur(a, r):
    return np.asarray(Image.fromarray(np.clip(a * 255, 0, 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(r)), np.float32) / 255


def glyph_mask(w, h, cells, seed, width_frac=0.085, vertical=False, wrap=False):
    """a line of runes: cells (cx, cy, half-size) in pixels, each one of the Dirge's glyphs, drawn as carved strokes"""
    rnd = random.Random(seed)
    glyphs = AA.glyphs(24, seed)
    m = Image.new('L', (w, h), 0)
    d = ImageDraw.Draw(m)
    for cx, cy, hs in cells:
        g = glyphs[rnd.randrange(len(glyphs))]
        sx, sy = hs * 0.62, hs
        for x0, y0, x1, y1 in g:
            for off in ((-w, 0), (0, 0), (w, 0)) if wrap else ((0, 0),):
                d.line([(cx + x0 * sx + off[0], cy + y0 * sy + off[1]), (cx + x1 * sx + off[0], cy + y1 * sy + off[1])], fill=255, width=max(2, int(hs * 2 * width_frac)))
    return np.asarray(m.filter(ImageFilter.GaussianBlur(max(1, w / 1400))), np.float32) / 255


def ashlar(n=2048, rows=4, seed=11, tint=(92, 96, 108), rough=1.0):
    """seamless dressed stone blocks in courses (both ways), bevelled edges, worn faces: (height, colour, edge)"""
    rng = np.random.default_rng(seed)
    yy, xx = np.mgrid[0:n, 0:n]
    rh = n // rows
    dist = np.zeros((n, n), np.float32)        # distance to the block's edge (px)
    tone = np.zeros((n, n), np.float32)
    for r in range(rows):
        y0 = r * rh
        cuts = [0]
        while cuts[-1] < n:
            cuts.append(cuts[-1] + int(rng.uniform(0.32, 0.6) * n / 2))
        cuts[-1] = n
        if n - cuts[-2] < n * 0.1: cuts.pop(-2)
        shift = int(rng.uniform(0, n))
        band = slice(y0, y0 + rh)
        xs = (xx[band] + shift) % n
        for a, b in zip(cuts[:-1], cuts[1:]):
            inb = (xs >= a) & (xs < b)
            dx = np.minimum(xs - a, b - 1 - xs)
            dy = np.minimum(yy[band] - y0, y0 + rh - 1 - yy[band])
            dd = np.minimum(dx, dy).astype(np.float32)
            dist[band][inb] = dd[inb]
            t = rng.uniform(-1, 1)
            sub = tone[band]; sub[inb] = t; tone[band] = sub
    bevel = np.clip((dist - 4) / 10.0, 0, 1)
    bevel = bevel * bevel * (3 - 2 * bevel)
    broad, grain, grit = tileable(n, 2.0, seed + 1), tileable(n, 1.3, seed + 2), tileable(n, 0.8, seed + 3)
    pits = (tileable(n, 0.6, seed + 4) > 0.83).astype(np.float32)
    z = lambda a: (a - a.mean()) / (a.std() + 1e-6)
    face = z(0.55 * broad + 0.3 * grain + 0.15 * grit)
    speck = np.clip(z(tileable(n, 0.2, seed + 7)) - 1.3, 0, 2) * 0.5               # flecks of mica and darker grains
    dark = np.clip(-z(tileable(n, 0.3, seed + 8)) - 1.5, 0, 2) * 0.5
    chip = np.clip(1 - dist / 40.0, 0, 1) * (tileable(n, 1.0, seed + 5) > 0.62)      # worn, chipped arrises
    cr = Image.new('L', (n, n), 0); cd = ImageDraw.Draw(cr)                          # a few hairline cracks
    for _ in range(7):
        x, y = rng.uniform(0, n), rng.uniform(0, n); ang = rng.uniform(0, 6.28); pts = [(x, y)]
        for _ in range(10):
            ang += rng.uniform(-0.6, 0.6); x += math.cos(ang) * n * 0.012; y += math.sin(ang) * n * 0.012; pts.append((x, y))
        cd.line(pts, fill=255, width=1)
    crack = np.asarray(cr.filter(ImageFilter.GaussianBlur(1.0)), np.float32) / 255 * bevel
    height = bevel * (0.8 + 0.06 * face * rough) - chip * 0.3 - crack * 0.25 + 0.04 * (1 - bevel) * z(grit)
    base = np.array(tint, np.float32)
    vy = (yy % rh) / rh                                                             # grime gathers low on each course
    grime = np.clip((vy - 0.6) / 0.4, 0, 1) ** 2 * np.clip(0.5 + 0.3 * z(broad), 0, 1)
    stain = np.clip((tileable(n, 2.6, seed + 6) - 0.62) * 3, 0, 1)
    hue = np.dstack([1 + 0.025 * tone, np.ones_like(tone), 1 - 0.025 * tone])          # each block a little warmer or cooler
    col = base[None, None] * hue * (1.0 + 0.12 * tone[..., None] + 0.11 * face[..., None] + 0.35 * speck[..., None] - 0.35 * dark[..., None])
    col = col * (1 - 0.3 * grime[..., None]) * (1 - 0.22 * stain[..., None]) * (1 - 0.3 * crack[..., None]) * (1 - 0.25 * chip[..., None])
    mortar = np.array([74, 72, 70], np.float32)[None, None] * (0.8 + 0.25 * z(grit)[..., None] * 0.4)
    col = col * bevel[..., None] + mortar * (1 - bevel[..., None])
    col *= (0.82 + 0.18 * np.clip(dist / 10.0, 0, 1))[..., None]                       # (the joint's shadow)
    edge = 1 - bevel
    return height, col, edge


def tex_wall():
    h, c, e = ashlar(2048, 4, 11, (82, 84, 92))
    shine = 0.12 + 0.18 * (1 - e)
    return img(c), normal_map(h, 9.0, shine)


def tex_floor():
    h, c, e = ashlar(2048, 3, 23, (74, 74, 80), 0.7)
    return img(c), normal_map(h, 7.0, 0.18 + 0.2 * (1 - e))


def tex_band(w=2048, h=256):
    """the rune frieze round the walls: a sunk band of dark stone between two rolled mouldings, runes cut into it
    and filled with blue light (u repeats round the room)"""
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    v = yy / h
    noise = tileable(w, 1.4, 31)[:h]
    mould = np.exp(-((v - 0.1) / 0.06) ** 2) + np.exp(-((v - 0.9) / 0.06) ** 2)
    sunk = ((v > 0.2) & (v < 0.8)).astype(np.float32)
    n = 14
    cells = [((i + 0.5) * w / n, h / 2, h * 0.2) for i in range(n)]
    g = glyph_mask(w, h, cells, 5, 0.16, wrap=True)
    height = 0.6 + 0.35 * mould - 0.25 * sunk + 0.08 * noise - 0.45 * g
    col = np.array([70, 74, 86], np.float32)[None, None] * (0.6 + 0.5 * mould[..., None] + 0.3 * noise[..., None]) * (1 - 0.3 * sunk[..., None])
    col = col * (1 - 0.6 * g[..., None]) + np.array([30, 70, 130]) * g[..., None]
    halo = blur(g, 6)
    glow = np.clip(g + 0.5 * halo, 0, 1)
    gl = np.dstack([BLUE[0] * glow, BLUE[1] * glow, BLUE[2] * glow]) * 255
    return img(col), normal_map(height, 6.0, 0.25 + 0.3 * mould), img(gl)


def sigil(d, cx, cy, R, wd, fill=255):
    """the Tongues' sigil: rings round three claw-strokes (the three words), a dial of ticks, a crown of rays"""
    for r, w in ((1.0, 1.0), (0.93, 0.5), (0.64, 0.8), (0.58, 0.4)):
        d.ellipse([cx - R * r, cy - R * r, cx + R * r, cy + R * r], outline=fill, width=max(2, int(wd * w)))
    for k in range(36):
        a = 2 * math.pi * k / 36
        r0, r1 = (0.66, 0.9) if k % 3 == 0 else (0.7, 0.84)
        d.line([(cx + math.cos(a) * R * r0, cy + math.sin(a) * R * r0), (cx + math.cos(a) * R * r1, cy + math.sin(a) * R * r1)], fill=fill, width=max(2, int(wd * 0.45)))
    for k in (-1, 0, 1):   # three claw marks, the middle one tallest
        x = cx + k * R * 0.2
        L = R * (0.42 if k == 0 else 0.34)
        pts = [(x - R * 0.05, cy - L), (x + R * 0.02, cy - L * 0.2), (x - R * 0.02, cy + L * 0.4), (x + R * 0.04, cy + L)]
        d.line(pts, fill=fill, width=max(3, int(wd * 1.1)), joint='curve')


def tex_slab(w=1024, h=2048, seed=41, door=True):
    """the sealed door's slab (door) or the Echo Stone's face (not door): dark slate, a carved border, the sigil (door)
    or a column of runes (stone); the carving holds blue light"""
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    broad, grain = tileable(h, 2.1, seed)[:, :w], tileable(h, 1.2, seed + 1)[:, :w]
    strata = 0.5 + 0.5 * np.sin((yy / h * 31 + broad * 3) * 2 * math.pi)
    stone = 0.62 * broad + 0.32 * grain + 0.06 * strata
    m = Image.new('L', (w, h), 0)
    d = ImageDraw.Draw(m)
    if door:
        d.rectangle([w * 0.06, h * 0.03, w * 0.94, h * 0.97], outline=255, width=int(w * 0.012))
        d.rectangle([w * 0.1, h * 0.05, w * 0.9, h * 0.95], outline=255, width=int(w * 0.006))
        sigil(d, w / 2, h * 0.36, w * 0.34, w * 0.022)
        cells = [(w / 2, h * (0.62 + 0.07 * k), w * 0.08) for k in range(5)]
        m2 = glyph_mask(w, h, cells, seed, 0.14)
        lines = np.maximum(np.asarray(m.filter(ImageFilter.GaussianBlur(1.5)), np.float32) / 255, m2)
    else:
        cells = [(w / 2, h * (0.1 + 0.078 * k), w * 0.12) for k in range(11)]
        lines = glyph_mask(w, h, cells, seed, 0.15)
        side = np.zeros_like(lines)
        for x in (0.24, 0.76):
            side += np.exp(-((xx / w - x) / 0.006) ** 2) * ((yy / h > 0.05) & (yy / h < 0.95))
        lines = np.maximum(lines, np.clip(side, 0, 1))
    height = stone * 0.4 + 0.6 - 0.5 * lines
    col = np.array([62, 64, 76], np.float32)[None, None] * (0.5 + 0.9 * stone[..., None])
    col = col * (1 - 0.55 * lines[..., None]) + np.array([20, 60, 120]) * lines[..., None]
    glow = np.clip(lines + 0.55 * blur(lines, 9), 0, 1)
    gl = np.dstack([BLUE[0] * glow, BLUE[1] * glow, BLUE[2] * glow]) * 255
    return img(col), normal_map(height, 7.0, 0.2 + 0.15 * stone), img(gl)


def tex_banner(w=1024, h=2048):
    """the Tongues' banner: deep blue wool, a woven border, the sigil in pale silver-blue; a swallowtail hem (alpha)"""
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    weave = 0.5 + 0.25 * np.sin(xx * 1.7) * np.sin(yy * 1.7) + 0.25 * tileable(h, 1.0, 51)[:, :w]
    folds = 0.5 + 0.5 * np.sin(xx / w * 2 * math.pi * 3.5 + tileable(h, 2.4, 52)[:, :w] * 2)
    col = np.array([22, 40, 92], np.float32)[None, None] * (0.6 + 0.5 * weave[..., None]) * (0.75 + 0.35 * folds[..., None])
    m = Image.new('L', (w, h), 0)
    d = ImageDraw.Draw(m)
    d.rectangle([w * 0.05, h * 0.02, w * 0.95, h * 0.84], outline=255, width=int(w * 0.025))
    d.rectangle([w * 0.09, h * 0.04, w * 0.91, h * 0.82], outline=255, width=int(w * 0.008))
    sigil(d, w / 2, h * 0.36, w * 0.33, w * 0.03)
    em = np.asarray(m.filter(ImageFilter.GaussianBlur(1.2)), np.float32) / 255
    col = col * (1 - em[..., None]) + np.array([190, 210, 235], np.float32) * em[..., None] * (0.7 + 0.3 * weave[..., None])
    # swallowtail: the hem cut up into a V from 0.86 to 0.98 of the height
    v = yy / h; u = np.abs(xx / w - 0.5) * 2
    alpha = np.where(v < 0.86 + 0.12 * u, 255, 0).astype(np.float32)
    alpha[:, :4] = 0; alpha[:, -4:] = 0
    hgt = 0.5 + 0.2 * weave + 0.3 * folds + 0.15 * em
    return img(np.dstack([col, alpha])), normal_map(hgt, 5.0, 0.05 + 0.1 * em)


def tex_iron(n=512):
    """blackened iron for the braziers' bowls"""
    g = tileable(n, 1.5, 61)
    col = np.dstack([40 + 30 * g, 40 + 30 * g, 46 + 34 * g])
    return img(col), normal_map(g, 3.0, 0.45 + 0.2 * g)


def tex_tongue_fire(N=256, cols=8, rows=8):
    """Tongue-fire: a brazier's column of blue flame, white at its root, licking up into thin cyan tongues; a seamless
    loop of 64 frames (an 8x8 atlas of 256 px, 2K), played at 24 a second (one loop ~2.7 s)"""
    F = cols * rows
    P = 512
    A = tileable(P, 1.9, 71); B = tileable(P, 1.5, 72); C = tileable(P, 1.1, 73)
    yy, xx = np.mgrid[0:N, 0:N].astype(np.float32) / N
    x = xx * 2 - 1; y = 1 - yy                                       # x -1..1, y 0 (root) .. 1 (top)
    atlas = np.zeros((N * rows, N * cols, 4), np.float32)
    from scipy.ndimage import map_coordinates

    def samp(tex, u, v):
        return map_coordinates(tex, [(v * P) % P, (u * P) % P], order=1, mode='grid-wrap')

    for f in range(F):
        t = f / F
        n1 = samp(A, xx * 0.7, yy * 0.9 + t * 2.0)                    # (scrolls whole periods: the loop is seamless)
        n2 = samp(B, xx * 1.3 + 0.37, yy * 1.6 + t * 3.0)
        n3 = samp(C, xx * 2.6 + 0.11, yy * 3.0 + t * 5.0)
        warp = (n1 - 0.5) * 0.55 * y + (n2 - 0.5) * 0.25 * y
        width = 0.62 * (1 - y) ** 0.85 + 0.04
        body = np.clip(1 - np.abs(x + warp) / width, 0, 1)
        lick = np.clip((n2 * 0.6 + n3 * 0.4) - (y - 0.15) * 0.95, 0, 1) * 1.6
        root = np.clip(1 - y / 0.12, 0, 1)
        inten = np.clip(body ** 0.8 * (lick + root * 0.6), 0, 1)
        inten *= np.clip(y / 0.04, 0, 1)                             # (fades in above the coals)
        heat = np.clip(inten * (1.2 - y * 0.8), 0, 1)
        r = np.clip(0.05 + 0.9 * heat ** 2.2, 0, 1)
        g = np.clip(0.25 + 0.75 * heat ** 1.3, 0, 1) * np.clip(inten * 1.6, 0, 1)
        b = np.clip(inten * 2.2, 0, 1)
        r = r * np.clip(inten * 1.5, 0, 1)
        cy, cx = divmod(f, cols)
        atlas[cy * N:(cy + 1) * N, cx * N:(cx + 1) * N] = np.dstack([r, g, b, inten])
    return Image.fromarray((atlas * 255).astype(np.uint8), 'RGBA')


def tex_flat(rgb, n=32):
    return Image.new('RGB', (n, n), rgb)


# ------------------------------------------------------------------------------------------------ geometry helpers
def reset():
    S.parts.clear()


def boxm(mat, x0, y0, z0, x1, y1, z1, tile=128.0, skip=()):
    S.box(mat, x0, y0, z0, x1, y1, z1, tile, skip)


def prism(mat, cx, cy, z0, z1, r0, r1, seg=8, tile=128.0, rot=math.pi / 8, cap=True, vb=None):
    """an upright prism / frustum: radius r0 at z0, r1 at z1; vb: (v0, v1) maps its height onto that band of the texture"""
    lo = [(cx + r0 * math.cos(rot + 2 * math.pi * k / seg), cy + r0 * math.sin(rot + 2 * math.pi * k / seg), z0) for k in range(seg)]
    hi = [(cx + r1 * math.cos(rot + 2 * math.pi * k / seg), cy + r1 * math.sin(rot + 2 * math.pi * k / seg), z1) for k in range(seg)]
    acc = 0.0
    for k in range(seg):
        a, b, c, d = lo[k], lo[(k + 1) % seg], hi[(k + 1) % seg], hi[k]
        L = math.hypot(b[0] - a[0], b[1] - a[1])
        if vb:
            uv = ((acc / tile, vb[0]), ((acc + L) / tile, vb[0]), ((acc + L) / tile, vb[1]), (acc / tile, vb[1]))
        else:
            uv = ((acc / tile, z0 / tile), ((acc + L) / tile, z0 / tile), ((acc + L) / tile, z1 / tile), (acc / tile, z1 / tile))
        S.quad(mat, a, b, c, d, uv=uv)
        acc += L
    if cap:
        S.add(mat, [(cx, cy, z1)] + hi, [(0.5, 0.5)] + [(0.5 + (p[0] - cx) / tile, 0.5 + (p[1] - cy) / tile) for p in hi], [(0, 1 + k, 1 + (k + 1) % seg) for k in range(seg)])


def panel(mat, x0, x1, z0, z1, y, uv=((0, 0), (1, 0), (1, 1), (0, 1)), back=False):
    """a vertical face at y facing +y (or -y with back)"""
    if back:
        S.quad(mat, (x0, y, z0), (x1, y, z0), (x1, y, z1), (x0, y, z1), uv=uv)
    else:
        S.quad(mat, (x1, y, z0), (x0, y, z0), (x0, y, z1), (x1, y, z1), uv=uv)


def convert(name, collide, mats, sub='reliquary'):
    WORK.mkdir(parents=True, exist_ok=True)
    obj = WORK / f'{name}.obj'
    S.write_obj(obj)
    out = OUT / 'Meshes' / 'CurseOfAlduin' / sub / f'{name}.nif'
    out.parent.mkdir(parents=True, exist_ok=True)
    job = [f'in {obj}', f'out {out}', 'origin 1'] + [f'collidebox2 {" ".join(f"{v:.1f}" for v in b)}' for b in collide]
    job += [m for m in mats if m.split()[1] in S.parts]
    jf = WORK / f'{name}.job'
    jf.write_text('\n'.join(job) + '\n')
    r = subprocess.run([S.CONV, str(jf)], capture_output=True, text=True)
    print(name, '|', r.stdout.strip() or r.stderr.strip())
    if r.returncode:
        raise SystemExit('modelconv failed')
    return out


def anim(nif, lines):
    jf = WORK / (nif.stem + '_anim.job')
    jf.write_text(f'in {nif}\nout {nif}\n' + '\n'.join(lines) + '\n')
    r = subprocess.run(['/home/claude/tools/nifanim', str(jf)], capture_output=True, text=True)
    print(nif.stem, 'anim |', r.stdout.strip() or r.stderr.strip())
    if r.returncode:
        raise SystemExit('nifanim failed')


def flames(nif, fire, spots, h=70.0, w=34.0):
    """Tongue-fire on a model: flipbook quads that turn to face you, one pair per spot (x, y, z of the coals)"""
    job = [f'base {nif}', f'out {nif}']
    for i, (x, y, z) in enumerate(spots):
        for k, (sc, ph, col) in enumerate([(1.0, 0.0, '1,1,1,1'), (0.8, 1.3, '0.7,0.85,1,0.7')]):
            job.append(f'shape tongue{i}_{k} tex={fire} color={col} scale=1.6 blend=add billboard=2 flip=8x8 fps=24 phase={ph + i * 0.7:.2f} at={x:.1f},{y:.1f},{z + h * sc * 0.5:.1f}')
            hh, ww = h * sc * 0.5, w * sc
            for X, Y, u, v in [(-ww, hh, 0, 0), (ww, hh, 1 / 8, 0), (ww, -hh, 1 / 8, 1 / 8), (-ww, -hh, 0, 1 / 8)]:
                job.append(f'v {X:.2f} {Y:.2f} {2.0 - k:.2f} {u} {v}')
            job += ['t 0 1 2', 't 0 2 3']
    jf = WORK / (nif.stem + '_fx.job')
    jf.write_text('\n'.join(job) + '\n')
    r = subprocess.run(['/home/claude/tools/fxwrite', str(jf)], capture_output=True, text=True)
    print(nif.stem, 'fire |', r.stdout.strip() or r.stderr.strip())
    if r.returncode:
        raise SystemExit('fxwrite failed')


# ------------------------------------------------------------------------------------------------ the models
def m_stone_mat(name, d, n, g, emit=BLUE, mult=2.2, spec=0.3):
    return f'mat {name} diffuse={d} normal={n} glow={g} emit={emit[0]},{emit[1]},{emit[2]} emitmult={mult} spec={spec} gloss=30'


def build_door(T):
    """the sealed door: a carved stone frame round a slab with the Tongues' sigil in blue light (300 wide, 430 high)"""
    reset()
    W, H, D = 150, 430, 45
    # plinth step
    boxm('wall', -W - 40, -D - 30, 0, W + 40, D + 30, 14)
    # jambs and lintel
    boxm('wall', -W, -D, 14, -W + 50, D, H - 60)
    boxm('wall', W - 50, -D, 14, W, D, H - 60)
    boxm('wall', -W - 30, -D - 8, H - 60, W + 30, D + 8, H)
    boxm('wall', -45, -D - 14, H - 85, 45, D + 14, H + 25)                 # the keystone
    # the frieze over the slab, both faces
    for back in (False, True):
        y = (-D - 8.5) if back else (D + 8.5)
        panel('band', -W - 30, W + 30, H - 52, H - 8, y, uv=((0, 0), (0.45, 0), (0.45, 1), (0, 1)), back=back)
    # the slab, recessed (front and back)
    for back in (False, True):
        y = -6 if back else 6
        panel('slab', -W + 50, W - 50, 14, H - 60, y, back=back)
    boxm('wall', -W + 50, -6, 14, W - 50, 6, 15, skip=('front', 'back'))
    T['door'] = convert('door', [(-W - 40, -D - 30, 0, W + 40, D + 30, H + 25)], [T['wall'], T['band'], T['slab']])


def build_stone(T):
    """the Echo Stone: a tall monolith of slate (300 high), its two broad faces carved with a column of runes that glow,
    on a heap of broken stone; the runes breathe"""
    reset()
    rnd = random.Random(3)
    Wb, Wt, Db, Dt, H = 52, 40, 22, 17, 300
    # the broad faces (front +y, back -y) carry the rune texture; the sides plain slate
    top_l, top_r = H - 18, H + 6
    uv = ((0, 0), (1, 0), (1, 1), (0, 1))
    S.quad('rune', (Wb, Db, 10), (-Wb, Db, 10), (-Wt, Dt, top_l), (Wt, Dt, top_r), uv=uv)        # front (+y)
    S.quad('rune', (-Wb, -Db, 10), (Wb, -Db, 10), (Wt, -Dt, top_r), (-Wt, -Dt, top_l), uv=uv)    # back (-y)
    for sx in (-1, 1):
        a, b = (sx * Wb, -Db, 10), (sx * Wb, Db, 10)
        c, d = (sx * Wt, Dt, top_r if sx > 0 else top_l), (sx * Wt, -Dt, top_r if sx > 0 else top_l)
        if sx < 0:
            S.quad('slate', b, a, d, c, uv=((0, 0), (0.3, 0), (0.3, 2.2), (0, 2.2)))
        else:
            S.quad('slate', a, b, c, d, uv=((0, 0), (0.3, 0), (0.3, 2.2), (0, 2.2)))
    S.quad('slate', (-Wt, -Dt, top_l), (Wt, -Dt, top_r), (Wt, Dt, top_r), (-Wt, Dt, top_l), uv=((0, 0), (0.6, 0), (0.6, 0.3), (0, 0.3)))
    # rubble round its foot
    for k in range(9):
        a = 2 * math.pi * k / 9 + rnd.uniform(-0.2, 0.2)
        r = rnd.uniform(55, 75); s = rnd.uniform(14, 24)
        x, y = r * math.cos(a), r * math.sin(a) * 0.7
        prism('slate', x, y, -4, s * 0.8, s, s * 0.6, seg=5, tile=90, rot=rnd.uniform(0, 6))
    prism('slate', 0, 0, -6, 12, 70, 58, seg=9, tile=120)
    nif = convert('echostone', [(-Wb, -Db - 4, 0, Wb, Db + 4, H)], [T['rune'], T['slate']])
    anim(nif, ['pulse rune 0.3 3.4 2.4'])
    T['echostone'] = nif


def build_brazier(T, fire):
    """a Tongue-fire brazier: a carved stone pillar (95 high) with an iron bowl, blue flame burning in it"""
    reset()
    prism('wall', 0, 0, 0, 10, 30, 28, seg=8, tile=100)
    prism('wall', 0, 0, 10, 82, 20, 17, seg=8, tile=100, cap=False)
    prism('band', 0, 0, 52, 66, 18.5, 18.3, seg=8, tile=60, cap=False, vb=(0.0, 1.0))
    prism('wall', 0, 0, 82, 90, 24, 26, seg=8, tile=100)
    prism('iron', 0, 0, 90, 108, 22, 36, seg=12, tile=60, cap=False)
    prism('coal', 0, 0, 90, 101, 22, 30, seg=12, tile=60)
    nif = convert('tonguefire', [(-26, -26, 0, 26, 26, 108)], [T['wall'], T['band'], T['iron'], T['coal']])
    flames(nif, fire, [(0, 0, 100)], h=78, w=30)
    T['tonguefire'] = nif


def build_banner(T):
    """a banner of the Tongues on its pole: a crossbar at 330, the cloth hanging 240 to just over your head"""
    reset()
    prism('wood', 0, 0, 0, 340, 5, 4, seg=6, tile=80, cap=True)
    prism('wall', 0, 0, 0, 16, 22, 16, seg=8, tile=100)
    S.slanted('wood', (-75, 0, 330), (75, 0, 330), 6, 6)
    for x in (-75, 75):
        prism('iron', x, 0, 322, 338, 5, 5, seg=6, tile=30)
    for back in (False, True):
        panel('cloth', -66, 66, 88, 328, 4.5 if not back else 3.5, back=back)
    T['banner'] = convert('banner', [(-24, -24, 0, 24, 24, 340)], [T['wood'], T['wall'], T['iron'], T['cloth']])


def build_pedestal(T):
    """a wonder's pedestal: an eight-sided plinth (110 high) with a band of runes, a shallow dish on top with three
    sockets for its parts (gem.nif, placed by the game as you find them); the forged weapon floats over it"""
    reset()
    prism('wall', 0, 0, 0, 14, 58, 54, seg=8, tile=110)
    prism('wall', 0, 0, 14, 92, 40, 36, seg=8, tile=110, cap=False)
    prism('band', 0, 0, 46, 72, 38.6, 37.6, seg=8, tile=72, cap=False, vb=(0.0, 1.0))
    prism('wall', 0, 0, 92, 104, 46, 52, seg=8, tile=110)
    prism('dish', 0, 0, 104, 108, 46, 42, seg=16, tile=110)
    for k in range(3):
        a = math.pi / 2 + 2 * math.pi * k / 3
        prism('iron', 28 * math.cos(a), 28 * math.sin(a), 104, 110, 8, 7, seg=8, tile=30)
    T['pedestal'] = convert('pedestal', [(-58, -58, 0, 58, 58, 110)], [T['wall'], T['band'], T['dish'], T['iron']])


def build_gems(T):
    """a part set in its socket: a slim crystal of the wonder's colour, glowing and breathing (and the forged
    wonder's light: a bigger one, wonderglow)"""
    for wid, col in WONDERS.items():
        reset()
        rgb = tuple(int(col[i:i + 2], 16) for i in (0, 2, 4))
        e = tuple(c / 255 for c in rgb)
        prism('gem', 0, 0, 0, 22, 4, 6, seg=6, tile=20, cap=False)
        prism('gem', 0, 0, 22, 34, 6, 0.5, seg=6, tile=20, cap=False)
        d = save(tex_flat(tuple(int(c * 0.6) for c in rgb)), f'gem_{wid}_d.dds'); g = save(tex_flat(rgb), f'gem_{wid}_g.dds')
        nif = convert(f'gem_{wid}', [], [f'mat gem diffuse={d} normal={T["flat"]} glow={g} emit={e[0]:.2f},{e[1]:.2f},{e[2]:.2f} emitmult=3.0 spec=1.0 gloss=120'])
        anim(nif, ['pulse gem 0.35 2.6 3.0'])


def build_relic(T, fire):
    """a relic's shrine in the wilds: a cairn of grey stone with Tongue-fire burning in its crown and a carved casket
    floating over it, its runes alight (the part sealed inside)"""
    reset()
    rnd = random.Random(9)
    prism('slate', 0, 0, -30, 26, 78, 64, seg=9, tile=120)
    for k in range(7):
        a = 2 * math.pi * k / 7 + rnd.uniform(-0.2, 0.2)
        prism('slate', 46 * math.cos(a), 46 * math.sin(a), 20, 60 + rnd.uniform(0, 26), rnd.uniform(18, 24), rnd.uniform(12, 16), seg=5, tile=90, rot=rnd.uniform(0, 6))
    prism('slate', 0, 0, 26, 70, 40, 30, seg=8, tile=100)
    prism('coal', 0, 0, 70, 74, 30, 26, seg=10, tile=80)
    # the casket (a box of runes) over the fire
    boxm('casket', -24, -15, 150, 24, 15, 178, tile=48)
    boxm('slate', -27, -18, 146, 27, 18, 151)
    boxm('slate', -27, -18, 177, 27, 18, 182)
    nif = convert('relic', [(-78, -78, -30, 78, 78, 74)], [T['coal'], T['slate'], f'mat casket diffuse={T["slab_d"]} normal={T["slab_n"]} glow={T["slab_g"]} emit=0.45,0.75,1.0 emitmult=2.8 spec=0.4 gloss=40'])
    flames(nif, fire, [(0, 0, 72)], h=60, w=26)
    anim(nif, ['pulse casket 0.35 2.8 2.8'])
    T['relic'] = nif


def build_room(T):
    """the Reliquary of the Tongues: a square hall 1200 across under a beamed ceiling, the Tongues' sigil worked in gold
    across its floor, walls of dressed stone with pilasters and a frieze of glowing runes all round"""
    reset()
    Rw, H, th = 600, 640, 40
    # floor and ceiling slabs
    boxm('floor', -Rw - th, -Rw - th, -40, Rw + th, Rw + th, 0, tile=384)
    boxm('ceil', -Rw - th, -Rw - th, H, Rw + th, Rw + th, H + 40, tile=320)
    # the gold sigil on the floor (a disc laid just over it)
    seg, R = 96, 300
    ring = [(R * math.cos(2 * math.pi * k / seg), R * math.sin(2 * math.pi * k / seg), 0.6) for k in range(seg)]
    S.add('sigil', [(0, 0, 0.6)] + ring, [(0.5, 0.5)] + [(0.5 + x / (2 * R), 0.5 + y / (2 * R)) for x, y, _ in ring], [(0, 1 + k, 1 + (k + 1) % seg) for k in range(seg)])
    # walls (slabs: their inner faces look into the room)
    boxm('wall', -Rw - th, Rw, 0, Rw + th, Rw + th, H, tile=320)
    boxm('wall', -Rw - th, -Rw - th, 0, Rw + th, -Rw, H, tile=320)
    boxm('wall', -Rw - th, -Rw, 0, -Rw, Rw, H, tile=320)
    boxm('wall', Rw, -Rw, 0, Rw + th, Rw, H, tile=320)
    # skirting plinth and the cornice
    for (x0, y0, x1, y1) in ((-Rw, Rw - 20, Rw, Rw), (-Rw, -Rw, Rw, -Rw + 20), (-Rw, -Rw, -Rw + 20, Rw), (Rw - 20, -Rw, Rw, Rw)):
        boxm('wall', x0, y0, 0, x1, y1, 26, tile=200)
        boxm('wall', x0, y0, H - 34, x1, y1, H, tile=200)
    # the frieze of runes, at 300-360, all round (u in wall units / 600: the band texture repeats twice a wall)
    zb0, zb1 = 300, 356
    band_uv = lambda L: ((0, 0), (L / 600, 0), (L / 600, 1), (0, 1))
    a, b = Rw - 1, -Rw + 1
    S.quad('band', (b, a, zb0), (a, a, zb0), (a, a, zb1), (b, a, zb1), uv=band_uv(1200))   # north wall, facing -y
    S.quad('band', (a, b, zb0), (b, b, zb0), (b, b, zb1), (a, b, zb1), uv=band_uv(1200))   # south wall, facing +y
    S.quad('band', (b, b, zb0), (b, a, zb0), (b, a, zb1), (b, b, zb1), uv=band_uv(1200))   # west wall, facing +x
    S.quad('band', (a, a, zb0), (a, b, zb0), (a, b, zb1), (a, a, zb1), uv=band_uv(1200))   # east wall, facing -x
    # pilasters: four a wall, rising to carry the ceiling beams
    cols = []
    for t in (-450, -150, 150, 450):
        cols += [(t, Rw, 'n'), (t, -Rw, 's'), (-Rw, t, 'w'), (Rw, t, 'e')]
    for x, y, side in cols:
        shaft, base = {'n': ((x - 30, y - 34, x + 30, y), (x - 38, y - 40, x + 38, y)),
                       's': ((x - 30, y, x + 30, y + 34), (x - 38, y, x + 38, y + 40)),
                       'w': ((x, y - 30, x + 34, y + 30), (x, y - 38, x + 40, y + 38)),
                       'e': ((x - 34, y - 30, x, y + 30), (x - 40, y - 38, x, y + 38))}[side]
        boxm('wall', shaft[0], shaft[1], 0, shaft[2], shaft[3], H, tile=200)
        boxm('wall', base[0], base[1], 0, base[2], base[3], 34, tile=160)
    # ceiling beams between facing pilasters, and a ring beam round the centre
    for t in (-450, -150, 150, 450):
        boxm('beam', t - 22, -Rw, H - 60, t + 22, Rw, H, tile=160)
        boxm('beam', -Rw, t - 22, H - 60, Rw, t + 22, H, tile=160)
    # the four corner niches' lintels (where the pedestals stand) - dark stone ledges
    coll = [(-Rw - th, -Rw - th, -40, Rw + th, Rw + th, 0), (-Rw - th, -Rw - th, H, Rw + th, Rw + th, H + 40),
            (-Rw - th, Rw, 0, Rw + th, Rw + th, H), (-Rw - th, -Rw - th, 0, Rw + th, -Rw, H),
            (-Rw - th, -Rw, 0, -Rw, Rw, H), (Rw, -Rw, 0, Rw + th, Rw, H)]
    fd, fn, fg, _ = AA.floor_maps(2048)
    sig = (f'mat sigil diffuse={save(fd, "floorsigil_d.dds")} normal={save(fn.convert("RGBA"), "floorsigil_n.dds", True)} '
           f'glow={save(fg, "floorsigil_g.dds")} emit=1.0,0.78,0.42 emitmult=1.4 spec=0.45 gloss=45')
    T['room'] = convert('room', coll, [T['wall'], T['floor'], T['band'], T['ceil'], T['beam'], sig])


def main():
    WORK.mkdir(parents=True, exist_ok=True)
    T = {}
    T['flat'] = save(Image.new('RGBA', (64, 64), (128, 128, 255, 90)), 'flat_n.dds', True)
    wd, wn = tex_wall()
    T['wall'] = f'mat wall diffuse={save(wd, "wall_d.dds")} normal={save(wn, "wall_n.dds", True)} spec=0.35 gloss=28'
    T['ceil'] = f'mat ceil diffuse={TP}wall_d.dds normal={TP}wall_n.dds spec=0.2 gloss=20'
    fd, fn = tex_floor()
    T['floor'] = f'mat floor diffuse={save(fd, "floor_d.dds")} normal={save(fn, "floor_n.dds", True)} spec=0.4 gloss=34'
    bd, bn, bg = tex_band()
    T['band'] = m_stone_mat('band', save(bd, 'band_d.dds'), save(bn, 'band_n.dds', True), save(bg, 'band_g.dds'), mult=2.0)
    sd, sn, sg = tex_slab(door=True)
    T['slab_d'], T['slab_n'], T['slab_g'] = save(sd, 'door_d.dds'), save(sn, 'door_n.dds', True), save(sg, 'door_g.dds')
    T['slab'] = m_stone_mat('slab', T['slab_d'], T['slab_n'], T['slab_g'], mult=2.4)
    rd, rn, rg = tex_slab(seed=43, door=False)
    T['rune'] = m_stone_mat('rune', save(rd, 'stone_d.dds'), save(rn, 'stone_n.dds', True), save(rg, 'stone_g.dds'), mult=2.6)
    T['slate'] = f'mat slate diffuse={TP}stone_d.dds normal={TP}stone_n.dds spec=0.25 gloss=26'
    T['beam'] = f'mat beam diffuse={TP}floor_d.dds normal={TP}floor_n.dds spec=0.2 gloss=20'
    idd, idn = tex_iron()
    T['iron'] = f'mat iron diffuse={save(idd, "iron_d.dds")} normal={save(idn, "iron_n.dds", True)} spec=0.8 gloss=60'
    T['coal'] = f'mat coal diffuse={save(tex_flat((40, 70, 120)), "coal_d.dds")} normal={T["flat"]} glow={save(tex_flat((90, 170, 255)), "coal_g.dds")} emit=0.4,0.7,1.0 emitmult=2.6 spec=0.1'
    T['dish'] = f'mat dish diffuse={TP}door_d.dds normal={TP}door_n.dds glow={TP}door_g.dds emit=0.35,0.68,1.0 emitmult=1.2 spec=0.4 gloss=40'
    cd, cn = tex_banner()
    T['cloth'] = f'mat cloth diffuse={save(cd, "banner_d.dds", True)} normal={save(cn, "banner_n.dds", True)} spec=0.05 gloss=10 doublesided=1 alpha=1'
    T['wood'] = f'mat wood diffuse={S.W}{S.TEX["wood"][0]}.dds normal={S.W}{S.TEX["wood"][1]}.dds spec=0.2 gloss=30'
    fire = save(tex_tongue_fire(), 'tonguefire.dds', True)
    build_door(T)
    build_stone(T)
    build_brazier(T, fire)
    build_banner(T)
    build_pedestal(T)
    build_gems(T)
    build_relic(T, fire)
    build_room(T)


if __name__ == '__main__':
    main()
