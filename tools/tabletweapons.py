"""(Noah: the Tablet of Mysteries' donated weapons - docs/TABLET_OF_MYSTERIES.md - "used and forgotten", "donated
weapons") the weapons the tablet gives, modelled here from scratch (no one else's mesh), each with its own 2K textures:
old things that were loved once and worn hard since - nicked, patched, bound with cloth and twine - and still a step or
two above your own blade.

Built with the Dragon Priests' weapon kit (tools/priestweapons.py: its shapes, its textures, its finish into a nif) and
at its sizes, fitted in the hand as those were (FIT below). As Skyrim's weapons are: the grip at the origin, the head up
+Y; Prn by kind. The same model is the one that rises out of the tablet's pool (its STAT, tabshow_<id>).

    python3 tools/tabletweapons.py [names...] [--keep]
      -> assets/fx/Meshes/CurseOfAlduin/tablet/<id>.nif
         assets/fx/Textures/CurseOfAlduin/tablet/w_<id>_*.dds"""
import math, pathlib, shutil, sys
import numpy as np
from scipy.ndimage import gaussian_filter
from PIL import Image, ImageDraw

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import priestweapons as pw
from priestweapons import g, Mesh, unit, grid_faces, tileable, height_to_normal, tool, metal_tex, leather_tex, save, \
    lathe, rod, grip_wrap, superbox, torus, talon, blade, blade_tex, rotm, Fx

ROOT = pw.ROOT
pw.MESHDIR = ROOT / 'assets' / 'fx' / 'Meshes' / 'CurseOfAlduin' / 'tablet'
g.TEXDIR = ROOT / 'assets' / 'fx' / 'Textures' / 'CurseOfAlduin' / 'tablet'
g.TEXREL = 'CurseOfAlduin\\tablet\\'
pw.WORK = g.WORK = ROOT / 'build' / 'tabletweapons'
FX = pw.FX
# (as the priests' weapons were fitted in the hand: the grip's top (raw units) and the scale)
pw.FIT.update({'gravebell': (1.0, 1.5), 'saltrice': (0.8, 1.75), 'riverstone': (12.0, 1.15), 'eclipse': (3.6, 1.3)})
S = 4096                 # (Noah: "4k textures")
KQ = S / 2048            # (a 2K sheet's pixel sizes, made at 4K)
pw.TEXSIZE = S


# ================================================================== textures
def uvgrid():
    v, u = (np.mgrid[0:S, 0:S].astype(np.float32) + 0.5) / S
    return u, v


def wood_tex(name, base_rgb, grey=0.0, cracks=0.6, seed=0):
    """2048 tiling: old wood along v - long grain, ring lines, splits; grey (0..1): bleached and weathered like driftwood"""
    grain = tileable(S, S, 1.2, 600 + seed, aniso=(0.04, 1.0))
    fine = tileable(S, S, 0.5, 601 + seed, aniso=(0.15, 1.0))
    big = tileable(S, S, 2.2, 602 + seed)
    rings = 0.5 + 0.5 * np.sin((grain * 38.0 + big * 4.0) * math.pi)
    split = tileable(S, S, 1.0, 603 + seed, aniso=(0.012, 1.0))
    sp = np.exp(-((split - 0.5) / 0.006) ** 2) * np.clip((tileable(S, S, 1.6, 604 + seed) - 0.5) * 4, 0, 1) * cracks
    lum = 0.78 + 0.22 * (rings - 0.5) + 0.25 * (big - 0.5) + 0.12 * (fine - 0.5) - 0.55 * sp
    col = np.array(base_rgb, np.float32) * lum[..., None]
    if grey:
        gl = col.mean(-1, keepdims=True) * np.array([1.02, 1.0, 0.96]) * 1.15 + 18
        col = col * (1 - grey) + gl * grey
    D = save(col, name + '_d.dds')
    h = 0.35 * rings + 0.3 * fine + 0.25 * big - 1.2 * sp
    N = save(np.dstack([height_to_normal(gaussian_filter(h, 0.6 * KQ), 9.0 * KQ), np.clip(30 + 25 * rings - 30 * sp, 0, 255)]), name + '_n.dds', True)
    return D, N


def bronze_tex(name, seed=0, crack=None, etch=None):
    """old temple bronze - dull brown-gold, rubbed bright where struck, green verdigris in the low places; an optional
    crack (an array 0..1) cut dark through it; an optional etching (runes, knotwork) cut in"""
    D0, N0 = metal_tex(name + '_base', (150, 112, 62), rust=0.0, dark=0.5, etch=etch, seed=seed)
    col = np.asarray(Image.open(g.WORK / (name + '_base_d.dds.png')).convert('RGB'), np.float32)
    nrm = np.asarray(Image.open(g.WORK / (name + '_base_n.dds.png')).convert('RGBA'), np.float32)
    patch = tileable(S, S, 1.4, 650 + seed); fine = tileable(S, S, 0.4, 651 + seed)
    low = np.clip((0.5 - col.mean(-1) / 255.0) * 3.5, 0, 1)
    verd = np.clip((patch - 0.55) * 3 + low * 0.8, 0, 1) * (0.6 + 0.4 * fine)
    col = col * (1 - 0.8 * verd[..., None]) + np.array([70, 118, 96]) * (0.75 + 0.5 * fine[..., None]) * 0.8 * verd[..., None]
    if crack is not None:
        col = col * (1 - 0.85 * crack[..., None])
        nrm[..., :3] = nrm[..., :3] * (1 - crack[..., None]) + np.array([128, 128, 255]) * crack[..., None]
    nrm[..., 3] = np.clip(nrm[..., 3] * (1 - 0.7 * verd), 0, 255)
    return save(col, name + '_d.dds'), save(nrm, name + '_n.dds', True)


def carved_tex(name, base_rgb, seed=0):
    """a wooden band carved with two rows of runes between scored lines (u round, v along: the rows across v)"""
    D0, N0 = wood_tex(name + '_base', base_rgb, grey=0.1, cracks=0.3, seed=seed)
    col = np.asarray(Image.open(g.WORK / (name + '_base_d.dds.png')).convert('RGB'), np.float32)
    nrm = np.asarray(Image.open(g.WORK / (name + '_base_n.dds.png')).convert('RGBA'), np.float32)
    m = np.rot90(rune_bands([0.3, 0.7], seed, h=int(S * 0.13)))   # (the rows running round the stick)
    col = col * (1 - 0.6 * m[..., None]) + np.array([24, 18, 14]) * 0.6 * m[..., None]
    gy, gx = np.gradient(gaussian_filter(-m, 2 * KQ))
    nn = unit(np.stack([-gx * 30 * KQ, gy * 30 * KQ, np.ones_like(m)], -1)) * 0.5 + 0.5
    nrm[..., :3] = nrm[..., :3] * (1 - m[..., None]) + nn * 255 * m[..., None]
    return save(col, name + '_d.dds'), save(nrm, name + '_n.dds', True)


def cloth_tex(name, base_rgb, seed=0):
    """2048 tiling: an old wrapping cloth - a plain weave, faded and stained, frayed thin places"""
    u, v = uvgrid()
    weave = (0.5 + 0.5 * np.sin(u * S * 0.9) * np.sign(np.sin(v * S * 0.9))) * 0.5 + 0.5 * (0.5 + 0.5 * np.sin(v * S * 0.9))
    stain = tileable(S, S, 2.0, 660 + seed); fine = tileable(S, S, 0.6, 661 + seed)
    lum = 0.72 + 0.18 * (weave - 0.5) + 0.3 * (stain - 0.5) + 0.1 * (fine - 0.5)
    col = np.array(base_rgb, np.float32) * lum[..., None]
    col = col * (1 - 0.35 * np.clip((0.4 - stain) * 3, 0, 1)[..., None]) + np.array([40, 32, 24]) * 0.35 * np.clip((0.4 - stain) * 3, 0, 1)[..., None]
    D = save(col, name + '_d.dds')
    N = save(np.dstack([height_to_normal(gaussian_filter(0.6 * weave + 0.3 * fine, 0.4 * KQ), 5.0 * KQ), np.full((S, S), 18.0)]), name + '_n.dds', True)
    return D, N


def rope_tex(name, base_rgb=(122, 98, 66), seed=0):
    """2048: twisted cord - three strands spiralling along v, fibres, old dirt (u round the cord, v along it)"""
    u, v = uvgrid()
    strand = 0.5 + 0.5 * np.cos(2 * math.pi * (u * 3 + v * 9))
    fib = tileable(S, S, 0.3, 670 + seed, aniso=(0.2, 1.0)); dirt = tileable(S, S, 1.8, 671 + seed)
    lum = 0.55 + 0.45 * strand ** 0.6 + 0.15 * (fib - 0.5) - 0.2 * np.clip(0.45 - dirt, 0, 1)
    col = np.array(base_rgb, np.float32) * lum[..., None]
    D = save(col, name + '_d.dds')
    N = save(np.dstack([height_to_normal(gaussian_filter(strand + 0.2 * fib, 0.5 * KQ), 7.0 * KQ), np.full((S, S), 22.0)]), name + '_n.dds', True)
    return D, N


def stone_tex(name, base_rgb, moss=0.0, seed=0, runes=None):
    """2048 tiling: a river stone worn smooth - mottled grey-green, fine speckle, a pale quartz vein or two, moss low"""
    u, v = uvgrid()
    mott = tileable(S, S, 2.1, 680 + seed); spk = tileable(S, S, 0.2, 681 + seed); mid = tileable(S, S, 1.3, 682 + seed)
    vein = np.exp(-((tileable(S, S, 1.5, 683 + seed, aniso=(1.0, 0.3)) - 0.5) / 0.008) ** 2)
    lum = 0.75 + 0.3 * (mott - 0.5) + 0.1 * (spk - 0.5)
    col = np.array(base_rgb, np.float32) * lum[..., None] + np.array([60, 58, 52]) * 0.6 * vein[..., None]
    rm = 0
    if runes is not None:   # (a line of runes cut across it, weathered soft)
        im = Image.new('L', (S, S), 0); dr = ImageDraw.Draw(im)
        rune_row(dr, int(S * 0.08), int(S * 0.92), int(S * 0.44), int(S * 0.11), runes, int(S * 0.012))
        rm = gaussian_filter(np.asarray(im, np.float32) / 255, 3 * KQ)
        col = col * (1 - 0.55 * rm[..., None])
    if moss:
        m = np.clip((mid - 0.62) * 4, 0, 1) * moss
        col = col * (1 - m[..., None]) + np.array([52, 70, 34]) * (0.7 + 0.6 * spk[..., None]) * m[..., None]
    D = save(col, name + '_d.dds')
    N = save(np.dstack([height_to_normal(gaussian_filter(0.4 * mott + 0.2 * spk + 0.3 * vein - 1.5 * rm, 1.0 * KQ), 4.0 * KQ), np.clip(60 + 40 * mott - (80 * m if moss else 0), 0, 255)]), name + '_n.dds', True)
    return D, N


# (Noah: "fantastic Nordic design") the old Nords' runes: each a stave and its branches, in a box 0.6 wide, 1 tall
RUNES = [
    [(0, 0, 0, 1), (0, 0.15, 0.5, 0.0), (0, 0.4, 0.5, 0.25)],            # (wealth)
    [(0, 0, 0, 1), (0, 0, 0.5, 0.3), (0.5, 0.3, 0.5, 1)],                  # (aurochs)
    [(0, 0, 0, 1), (0, 0.25, 0.45, 0.5), (0.45, 0.5, 0, 0.75)],            # (thorn)
    [(0, 0, 0, 1), (0, 0.0, 0.45, 0.3), (0, 0.3, 0.45, 0.6)],              # (god)
    [(0, 0, 0, 1), (0, 0, 0.45, 0.25), (0.45, 0.25, 0, 0.5), (0, 0.5, 0.5, 1)],   # (ride)
    [(0, 0, 0, 1), (0.5, 0, 0.5, 1), (0, 0.2, 0.5, 0.45)],                 # (hail)
    [(0.25, 0, 0.25, 1), (0.0, 0.35, 0.5, 0.65)],                          # (need)
    [(0.25, 0, 0.25, 1)],                                                  # (ice)
    [(0.25, 0, 0.25, 1), (0.25, 0.35, 0.55, 0.0), (0.25, 0.35, -0.05, 0.0)],   # (elk)
    [(0, 0, 0.5, 0.5), (0.5, 0.5, 0, 1), (0.5, 0, 0, 0.5), (0, 0.5, 0.5, 1)],  # (day)
    [(0.25, 0, 0.25, 1), (0.25, 0, 0.0, 0.3), (0.25, 0, 0.5, 0.3)],        # (tyr)
    [(0, 0, 0, 1), (0, 0, 0.45, 0.25), (0.45, 0.25, 0, 0.5), (0, 0.5, 0.45, 0.75), (0.45, 0.75, 0, 1)],   # (birch)
    [(0, 1, 0, 0), (0, 0, 0.5, 0.35), (0.5, 0.35, 0.5, 1)],                # (horse)
    [(0, 0, 0.5, 1), (0.5, 0, 0, 1), (0.25, 0.5, 0.25, 1)],                # (man)
]


def rune_row(dr, x0, x1, y, h, seed, width, fill=255):
    """a row of runes along x from x0 to x1 at y (their tops), h tall"""
    r = np.random.default_rng(seed)
    x = x0
    while x < x1 - h * 0.6:
        for (a, b, c, d) in RUNES[r.integers(len(RUNES))]:
            dr.line([(x + a * h, y + b * h), (x + c * h, y + d * h)], fill=fill, width=width)
        x += h * (0.85 + 0.25 * r.random())
        if r.random() < 0.12: x += h * 0.5   # (a word's end)


def rune_bands(rows, seed, width=None, h=None):
    """an S x S mask (0..1): rune rows at the given v positions (0..1, their middles), each between two scored lines"""
    im = Image.new('L', (S, S), 0); dr = ImageDraw.Draw(im)
    h = h or int(S * 0.045); width = width or max(2, int(h * 0.11))
    for k, vc in enumerate(rows):
        y = int(vc * S - h / 2)
        rune_row(dr, -h, S + h, y, h, seed + k, width)
        for yy in (y - int(h * 0.3), y + int(h * 1.3)):
            dr.line([(0, yy), (S, yy)], fill=255, width=max(2, width // 2))
    return gaussian_filter(np.asarray(im, np.float32) / 255, KQ * 0.8)


def knot(seed=0, cells=6):
    """(Nordic knotwork, tools/priestweapons.py) the bands woven over and under, cut into the metal"""
    return pw.knotwork(S, cells, 0.1, seed)


def crack_mask(seed, n=3, length=0.5):
    """a few jagged dark cracks (0..1) wandering down the sheet"""
    im = Image.new('L', (S, S), 0); dr = ImageDraw.Draw(im); r = np.random.default_rng(seed)
    for _ in range(n):
        x, y = r.random() * S, r.random() * S * 0.3
        a = math.pi / 2 + (r.random() - 0.5) * 0.6
        w = 7 * KQ
        for _k in range(int(60 * length)):
            a += (r.random() - 0.5) * 0.9
            nx, ny = x + math.cos(a) * 22 * KQ, y + math.sin(a) * 22 * KQ
            dr.line([(x, y), (nx, ny)], fill=255, width=max(1, int(w)))
            if r.random() < 0.12:   # (a hairline branching off)
                bx, by, ba = x, y, a + (r.random() - 0.5) * 2
                for _q in range(6):
                    ba += (r.random() - 0.5) * 0.8
                    ex, ey = bx + math.cos(ba) * 14 * KQ, by + math.sin(ba) * 14 * KQ
                    dr.line([(bx, by), (ex, ey)], fill=200, width=int(2 * KQ)); bx, by = ex, ey
            x, y = nx, ny; w = max(KQ, w * 0.985)
    return gaussian_filter(np.asarray(im, np.float32) / 255, KQ)


# ================================================================== shapes
def slab(rows, M, group, mat, uvscale=0.27, seed=0):
    """a solid from rings of points (each row one ring round it, the rings in order): closed round, normals outward"""
    V = np.array(rows, float); V = np.concatenate([V, V[:, :1]], 1)
    du = np.gradient(V, axis=0); dv = np.gradient(V, axis=1)
    Nn = unit(np.cross(du, dv))
    cen = V.mean(1, keepdims=True); Nn = Nn * np.sign(((V - cen) * Nn).sum(-1, keepdims=True) + 1e-9)
    nu, nr = V.shape[0], V.shape[1]
    UV = np.stack([V[..., 0] * uvscale, V[..., 2] * uvscale], -1) + np.random.default_rng(seed).random(2)
    F = np.array(grid_faces(nu, nr)); P = V.reshape(-1, 3); N = Nn.reshape(-1, 3)
    fn = np.cross(P[F[:, 1]] - P[F[:, 0]], P[F[:, 2]] - P[F[:, 0]]); flip = (fn * N[F].mean(1)).sum(1) < 0; F[flip] = F[flip][:, [0, 2, 1]]
    M.add(group, mat, P, N, UV.reshape(-1, 2), F)


def lens_ring(c, across, thick_dir, half_w, t_spine, t_edge, nr=28, edge_side=1.0):
    """a cross-section ring: across (unit) from spine (-) to edge (+edge_side), thick_dir its thickness; the spine
    t_spine thick, thinning to t_edge at the edge"""
    out = []
    for j in range(nr):
        th = 2 * math.pi * j / nr
        cc, ss = math.cos(th), math.sin(th)
        a = np.sign(cc) * abs(cc) ** 0.55                       # (-1 spine .. 1 edge)
        t = t_spine + (t_edge - t_spine) * (a * 0.5 + 0.5) ** 1.3
        out.append(c + across * a * half_w * edge_side + thick_dir * np.sign(ss) * abs(ss) ** 0.6 * t)
    return out


def bezier(P, n):
    P = np.asarray(P, float); t = np.linspace(0, 1, n)[:, None]
    return (1 - t) ** 3 * P[0] + 3 * (1 - t) ** 2 * t * P[1] + 3 * (1 - t) * t ** 2 * P[2] + t ** 3 * P[3]


def wrap_strips(M, mat, z0, z1, r, n=3, seed=0, width=1.1):
    """loose strips of cloth wound round a shaft, a little slack and uneven, their ends hanging"""
    rr = np.random.default_rng(seed)
    for k in range(n):
        za = z0 + (z1 - z0) * k / n; zb = za + (z1 - z0) / n * (0.8 + 0.3 * rr.random())
        nr, nc = 60, 24
        zz = np.linspace(za, zb, nr); th = np.linspace(0, 2 * np.pi, nc + 1)
        Z, T = np.meshgrid(zz, th, indexing='ij')
        ph = T / (2 * np.pi) + (Z - za) / width
        rad = r + 0.05 + 0.07 * np.abs(np.sin(np.pi * ph)) ** 0.4 + 0.03 * np.sin(Z * 2.1 + k)
        V = np.stack([rad * np.cos(T), rad * np.sin(T), Z], -1)
        N = np.stack([np.cos(T), np.sin(T), np.zeros_like(T)], -1)
        UV = np.stack([T / (2 * np.pi) * 2, (Z - za) / 3.0], -1)
        M.add('Cloth', mat, V.reshape(-1, 3), N.reshape(-1, 3), UV.reshape(-1, 2), grid_faces(nr, nc + 1))
        if k == n - 1 or rr.random() < 0.5:   # (a frayed end hanging loose)
            a0 = rr.random() * 2 * math.pi
            pts = [np.array([(r + 0.12) * math.cos(a0), (r + 0.12) * math.sin(a0), zb]) + np.array([math.cos(a0) * 0.4 * s, math.sin(a0) * 0.4 * s, -3.2 * s - 0.6 * s * s]) for s in np.linspace(0, 1, 10)]
            M.add('Cloth', mat, *rod(np.array(pts), np.linspace(0.32, 0.12, 10), 6))


# ================================================================== the worn weapons
def gravebell():
    """(Gravebell Mace - "a cracked temple bell on an iron pole") an old temple bell of Arkay, its bronze gone brown and
    green, a crack running up from its lip, hung mouth-down on an iron pole that runs up through its crown into an iron
    loop; its clapper still inside. The pole's grip is old leather, a strip of a priest's grey stole tied under the bell.
    A faint grave-dust drifts round the bell."""
    hz = 19.0   # (the bell's lip)
    out = [(hz, 4.35), (hz + 0.5, 4.2), (hz + 1.6, 3.55), (hz + 3.2, 3.05), (hz + 5.0, 2.9), (hz + 6.2, 2.85), (hz + 6.9, 2.55), (hz + 7.4, 1.6), (hz + 7.6, 0.0)]
    inn = [(hz + 7.0, 0.0), (hz + 6.8, 1.5), (hz + 6.2, 2.45), (hz + 5.0, 2.55), (hz + 3.2, 2.7), (hz + 1.6, 3.2), (hz + 0.4, 3.85), (hz, 3.95)]
    prof = inn + [(hz - 0.15, 4.12)] + out
    pa = np.asarray(prof); L = np.concatenate([[0], np.cumsum(np.hypot(np.diff(pa[:, 0]), np.diff(pa[:, 1])))]); L /= L[-1]
    vz = lambda z: float(np.interp(z, pa[len(inn) + 1:, 0], L[len(inn) + 1:]))   # (v of a height on the outside)
    runes = rune_bands([vz(hz + 4.15), vz(hz + 2.0)], 704, h=int(S * 0.028))
    cr = crack_mask(701, n=1, length=0.9)
    cr = np.roll(cr, int(S * (vz(hz) - 0.05)), axis=0) * (np.mgrid[0:S, 0:S][0] / S > vz(hz) - 0.02)
    bD, bN = bronze_tex('w_gravebell_bronze', seed=702, crack=cr, etch=runes)
    iD, iN = metal_tex('w_gravebell_iron', (70, 70, 72), rust=0.5, dark=0.8, seed=703)
    kD, kN = metal_tex('w_gravebell_knot', (78, 76, 74), rust=0.3, dark=0.9, etch=knot(705), seed=706)
    lD, lN = leather_tex('w_gravebell_leather', (48, 36, 28), seed=704)
    cD, cN = cloth_tex('w_gravebell_cloth', (120, 118, 112), seed=705)
    M = Mesh()
    # the pole
    M.add('Iron', 'iron', *lathe([(-8.2, 0.0), (-8.2, 0.8), (-7.8, 0.9)] + [(z, 0.72 - 0.002 * z) for z in np.linspace(-7.4, hz + 9.5, 8)] + [(hz + 9.8, 0.0)], 20))
    grip_wrap(M, 'leather', -7.2, 0.8, 0.8, 1.0)
    for z, hh in ((-7.5, 0.35), (1.0, 0.6), (9.0, 1.1), (hz - 1.2, 0.8)):   # (knotworked iron collars)
        M.add('Knot', 'knot', *lathe([(z - hh, 0.78), (z - hh + 0.1, 1.0), (z + hh - 0.1, 1.0), (z + hh, 0.78)], 24))
    M.add('Iron', 'iron', *lathe([(-8.2, 0.0), (-8.2, 0.95), (-9.0, 1.2), (-9.8, 0.9), (-10.1, 0.0)], 20))   # (a knob of a pommel)
    # the bell: a closed shell (inside wall, lip, outside wall over the crown), mouth down at hz
    M.add('Bell', 'bronze', *lathe(prof, 64, uvs=(1, 1, 0, 0)))
    for z, rr in ((hz + 1.15, 3.78), (hz + 5.6, 2.98), (hz + 6.0, 2.98)):   # (the cast rings round it)
        M.add('Bell', 'bronze', *torus(np.array([0, 0, z]), rr, 0.14, (0, 0, 1), 64, 8))
    # the crown's iron strap and the loop on top
    M.add('Iron', 'iron', *lathe([(hz + 7.3, 1.25), (hz + 7.9, 1.0), (hz + 8.6, 0.85)], 20))
    M.add('Iron', 'iron', *torus(np.array([0, 0, hz + 10.8]), 1.3, 0.32, (1, 0, 0), 28, 10))
    # the clapper on a little chain inside
    for k in range(3):
        M.add('Iron', 'iron', *torus(np.array([0, 0, hz + 5.7 - 0.75 * k]), 0.32, 0.09, (1, 0, 0) if k % 2 == 0 else (0, 1, 0), 14, 6, stretch=1.6, long=(0, 0, 1)))
    M.add('Iron', 'iron', *lathe([(hz + 3.4, 0.0), (hz + 3.3, 0.3), (hz + 1.8, 0.25), (hz + 1.2, 0.85), (hz + 0.6, 0.95), (hz + 0.1, 0.6), (hz - 0.05, 0.0)], 16))
    # (the grey stole tied under the bell, its ends hanging)
    wrap_strips(M, 'cloth', hz - 3.0, hz - 0.6, 0.72, n=1, seed=706, width=0.8)
    fx = Fx()
    fx.sheath('GraveDust', f'tex={FX["motes"]} color=0.8,0.78,0.7,0.35 scale=1.4 blend=add uscroll=3 vscroll=-12', hz - 1.0, hz + 8.0, 5.4, 4.0, 22, 8, 2.0)
    mats = {'bronze': f'diffuse={bD} normal={bN} spec=0.60 gloss=24 alpha=0 doublesided=1',
            'iron': f'diffuse={iD} normal={iN} spec=0.25 gloss=13 alpha=0 doublesided=0',
            'knot': f'diffuse={kD} normal={kN} spec=0.3 gloss=16 alpha=0 doublesided=0',
            'leather': f'diffuse={lD} normal={lN} spec=0.15 gloss=8 alpha=0 doublesided=0',
            'cloth': f'diffuse={cD} normal={cN} spec=0.10 gloss=8 alpha=0 doublesided=1'}
    return pw.finish(M, 'gravebell', mats, fx.L, 'WeaponMace')


def saltrice():
    """(Saltrice Sickle - "a salt-crusted sickle") a field sickle from the salt marshes: a crescent of old steel, keen on
    its inner edge, its back crusted grey with dried salt; a turned handle of pale wood bound with twine, a brass ferrule"""
    u, v = uvgrid()
    crust = np.clip((tileable(S, S, 0.6, 711) - 0.42) * 3, 0, 1) * np.clip((0.45 - u) / 0.3, 0, 1)   # (salt toward the spine)
    sD, sN, sG = blade_tex('w_saltrice_blade', (104, 106, 108), edge_rgb=(186, 190, 194), script=False, pattern='plain', seed=712, mask=crust * 0.75, mask_rgb=(150, 150, 146))
    wD, wN = wood_tex('w_saltrice_wood', (150, 124, 92), grey=0.3, seed=713)
    rD, rN = rope_tex('w_saltrice_twine', (138, 116, 82), seed=714)
    bD, bN = metal_tex('w_saltrice_brass', (150, 120, 70), dark=0.6, etch=knot(717, 5), seed=715)
    M = Mesh()
    # the handle
    M.add('Wood', 'wood', *lathe([(-5.2, 0.74), (-3.0, 0.7), (-1.0, 0.64), (0.2, 0.6), (0.6, 0.55)], 20))
    M.add('Brass', 'brass', *lathe([(-5.2, 0.76), (-5.5, 0.86), (-6.1, 0.8), (-6.5, 0.5), (-6.7, 0.0)], 20))   # (its knotworked end cap)
    grip_wrap(M, 'twine', -4.2, -0.6, 0.72, 0.35)
    M.add('Brass', 'brass', *lathe([(0.3, 0.58), (0.45, 0.7), (1.4, 0.62), (1.6, 0.4), (1.6, 0.0)], 20))
    # the crescent: a single-edged blade along a curve, its keen edge on the inside of the curve
    path = bezier([(0, 0, 1.4), (-1.6, 0, 8.5), (5.6, 0, 15.5), (10.6, 0, 9.4)], 70)
    rows = []
    mid = len(path) // 2
    tgm = unit(path[mid + 1] - path[mid - 1])
    side = 1.0 if np.dot(np.cross(tgm, [0, 1.0, 0]), path.mean(0) - path[mid]) > 0 else -1.0   # (toward the inside of the curve)
    for i, c in enumerate(path):
        s = i / (len(path) - 1)
        tg = unit(path[min(i + 1, len(path) - 1)] - path[max(i - 1, 0)])
        across = side * unit(np.cross(tg, np.array([0, 1.0, 0])))   # (in the blade's plane: its keen edge on the inside)
        hw = 1.25 * (1 - s) ** 0.5 + 0.05
        rows.append(lens_ring(c + across * hw * 0.2, across, np.array([0, 1.0, 0]), hw, 0.24 * (1 - 0.6 * s) + 0.03, 0.025, 26))
    # (u across the blade: the slab's x/z mapping does for a curve)
    slab(rows, M, 'Blade', 'blade', 0.09, 716)
    for sy in (-1, 1):   # (the rivet through its tang)
        M.add('Brass', 'brass', *superbox(np.array([-0.2, sy * 0.3, 2.6]), 0.32, 0.12, 0.32, 2.2, 12, 6))
    fx = Fx()
    fx.sheath('SaltGlint', f'tex={FX["motes"]} color=0.95,0.95,0.9,0.4 scale=1.0 blend=add uscroll=2 vscroll=-3', 2.0, 14.5, 4.5, 3.5, 16, 8, 2.0)
    mats = {'blade': f'diffuse={sD} normal={sN} spec=0.70 gloss=48 alpha=0 doublesided=0',
            'wood': f'diffuse={wD} normal={wN} spec=0.15 gloss=9 alpha=0 doublesided=0',
            'twine': f'diffuse={rD} normal={rN} spec=0.15 gloss=8 alpha=0 doublesided=0',
            'brass': f'diffuse={bD} normal={bN} spec=0.50 gloss=24 alpha=0 doublesided=0'}
    return pw.finish(M, 'saltrice', mats, fx.L, 'WeaponDagger')


def riverstone():
    """(Riverstone Axe - "waterlogged") a woodsman's axe pulled out of a river: a grey-green river stone knapped to an edge
    and lashed into a split haft of driftwood with rope gone dark with water, moss in its seams; water still drips off it"""
    sD, sN = stone_tex('w_riverstone_stone', (108, 116, 108), moss=0.6, seed=721, runes=724)
    kD, kN = metal_tex('w_riverstone_knot', (74, 72, 70), rust=0.55, dark=0.85, etch=knot(726, 5), seed=727)
    wD, wN = wood_tex('w_riverstone_wood', (96, 84, 70), grey=0.75, seed=722)
    rD, rN = rope_tex('w_riverstone_rope', (84, 74, 56), seed=723)
    M = Mesh()
    # the haft, a little crooked, thicker toward the head
    path = np.array([(0.25 * math.sin(z * 0.08), 0.0, z) for z in np.linspace(-22.0, 45.0, 40)])
    M.add('Wood', 'wood', *rod(path, np.linspace(1.0, 1.25, 40), 18))
    for end, d in ((path[0], -1), (path[-1], 1)):
        M.add('Wood', 'wood', *lathe([(end[2], 1.0 + 0.2 * (d > 0)), (end[2] + d * 0.25, 0.7), (end[2] + d * 0.3, 0.0)], 18, cx=end[0]))
    grip_wrap(M, 'rope', -18.0, -6.0, 1.06, 0.6)
    grip_wrap(M, 'rope', 4.0, 11.0, 1.1, 0.6)
    # the stone bit: a flat, rounded river stone, thick at the haft, its front knapped to an edge
    hz = 38.0
    rr = np.random.default_rng(724)
    rows = []
    nu, nr = 40, 36
    for i in range(nu + 1):
        s = i / nu
        H = 3.0 + 1.2 * s + 4.6 * s ** 2.6   # (narrow at the haft, flaring to a broad bit)
        t = 1.35 * (1 - 0.15 * s) if s < 0.62 else max(0.06, 1.35 * 0.9 * (1 - (s - 0.62) / 0.38) ** 1.1)
        ring = []
        for j in range(nr):
            th = 2 * math.pi * j / nr
            cc, ss = math.cos(th), math.sin(th)
            vv = np.sign(cc) * abs(cc) ** 0.45
            yy = t * np.sign(ss) * abs(ss) ** 0.5
            x = -1.6 + s * 11.6 - 3.2 * vv * vv * s ** 1.5   # (the bit's edge curved, its horns swept back)
            ring.append((x + 0.12 * rr.random() * s, yy, hz + vv * H))
        rows.append(ring)
    V = np.array(rows)
    knap = 0.3 * np.abs(np.sin(V[..., 2] * 2.2 + 1.3)) * np.clip((np.arange(nu + 1)[:, None] / nu - 0.84) / 0.16, 0, 1)
    V[..., 0] -= knap
    slab(V, M, 'Stone', 'stone', 0.12, 725)
    # the lashing: rope wound round the haft above and below the stone and crossed over its back
    for z in (hz - 4.6, hz - 3.8, hz + 3.8, hz + 4.6):
        M.add('Rope', 'rope', *torus(np.array([0.1, 0, z]), 1.35, 0.26, (0, 0, 1), 28, 8))
    for z, hh in ((hz - 6.4, 0.7), (hz + 6.4, 0.7)):   # (iron bands either side of the lashing)
        M.add('Knot', 'knot', *lathe([(z - hh, 1.22), (z - hh + 0.12, 1.45), (z + hh - 0.12, 1.45), (z + hh, 1.22)], 24, cx=0.25 * math.sin(z * 0.08)))
    for sy in (-1, 1):
        for k in range(2):
            a = np.linspace(-1, 1, 24)
            pts = np.stack([-0.2 + 1.4 * (1 - a * a) + 0.0 * a, sy * (1.45 + 0.1 * (1 - a * a)), hz + a * 4.2 * (1 - 2 * k) * 1.0], -1)
            M.add('Rope', 'rope', *rod(pts, 0.24, 8))
    fx = Fx()
    fx.sheath('Drips', f'tex={FX["motes"]} color=0.7,0.82,0.85,0.55 scale=1.2 blend=add vscroll=-14', hz - 7.0, hz + 6.0, 6.5, 6.0, 18, 8, 2.5)
    mats = {'stone': f'diffuse={sD} normal={sN} spec=0.45 gloss=36 alpha=0 doublesided=0',
            'wood': f'diffuse={wD} normal={wN} spec=0.17 gloss=10 alpha=0 doublesided=0',
            'rope': f'diffuse={rD} normal={rN} spec=0.15 gloss=8 alpha=0 doublesided=0',
            'knot': f'diffuse={kD} normal={kN} spec=0.3 gloss=16 alpha=0 doublesided=0'}
    return pw.finish(M, 'riverstone', mats, fx.L, 'WeaponBack')


def lanternwick():
    """(Lanternwick Staff - "a cracked lantern on a stick") a crooked walking stick of blackthorn, its top bent over into a
    hook, from which a little iron lantern hangs on a chain - one pane cracked, one missing - a stub of candle still
    burning inside it. A traveller's rag tied at the grip."""
    wD, wN = wood_tex('w_lanternwick_wood', (70, 52, 40), grey=0.1, seed=731)
    iD, iN = metal_tex('w_lanternwick_iron', (64, 62, 60), rust=0.55, dark=0.85, etch=knot(739, 7) * 0.8, seed=732)
    cvD, cvN = carved_tex('w_lanternwick_carved', (70, 52, 40), 740)
    cD, cN = cloth_tex('w_lanternwick_rag', (126, 86, 58), seed=733)
    u, v = uvgrid()
    gl = np.dstack([np.full((S, S), 200.0), np.full((S, S), 178.0), np.full((S, S), 120.0)]) * (0.85 + 0.3 * tileable(S, S, 1.5, 734)[..., None])
    cr = crack_mask(735, n=2, length=0.35)
    glass_a = np.clip(70 + 60 * tileable(S, S, 1.2, 736) + 160 * cr, 0, 255)
    gD = save(np.dstack([gl * (1 - 0.5 * cr[..., None]), glass_a]), 'w_lanternwick_glass_d.dds', True)
    candle = save(np.dstack([np.full((256, 256), 214.0), np.full((256, 256), 196.0), np.full((256, 256), 150.0)]) * (0.85 + 0.2 * tileable(256, 256, 1.0, 737)[..., None]), 'w_lanternwick_wax_d.dds')
    M = Mesh()
    # the stick: crooked, a knot or two, bent over at the top into a hook
    zs = np.linspace(-20.0, 44.0, 48)
    shaft = [np.array([0.35 * math.sin(z * 0.11) + 0.2 * math.sin(z * 0.31), 0.25 * math.sin(z * 0.07 + 1), z]) for z in zs]
    a = np.linspace(0, math.pi * 1.15, 22)[1:]
    top = shaft[-1]
    hook = [top + np.array([2.6 - 2.6 * math.cos(x), 0, 2.6 * math.sin(x)]) for x in a]
    path = np.array(shaft + hook)
    radius = np.concatenate([np.linspace(1.0, 0.82, len(shaft)), np.linspace(0.82, 0.55, len(hook))])
    radius = radius + 0.06 * np.sin(np.arange(len(path)) * 1.7) ** 8   # (the knots)
    M.add('Wood', 'wood', *rod(path, radius, 18))
    M.add('Wood', 'wood', *lathe([(-20.0, 0.95), (-20.4, 0.7), (-20.5, 0.0)], 16, cx=path[0][0], cy=path[0][1]))
    tip = path[-1]
    # the iron ferrule at the foot and a band where the hook starts
    M.add('Iron', 'iron', *lathe([(-20.6, 0.0), (-20.6, 0.8), (-17.8, 1.08), (-17.6, 0.98)], 18, cx=path[0][0], cy=path[0][1]))
    wrap_strips(M, 'rag', -3.0, 4.0, 0.95, n=2, seed=738, width=1.2)
    band = np.array([p_ for p_ in path if 26.0 < p_[2] < 33.0])   # (a carved band of runes on the stick)
    P_, N_, UV_, F_ = rod(band, 0.86 + 0.0 * band[:, 2], 24); UV_ = UV_ * [2.0, 1.0]
    M.add('Carved', 'carved', P_, N_, UV_, F_)
    # the chain from the hook's tip and the lantern hanging from it
    hang = tip + np.array([0, 0, -0.4])
    for k in range(4):
        M.add('Iron', 'iron', *torus(hang + np.array([0, 0, -0.62 * k]), 0.28, 0.08, (1, 0, 0) if k % 2 == 0 else (0, 1, 0), 14, 6, stretch=1.5, long=(0, 0, 1)))
    lc = hang + np.array([0, 0, -6.4])   # (the lantern's middle)
    hw, hh = 1.35, 2.2
    M.add('Iron', 'iron', *lathe([(lc[2] + hh, 1.75), (lc[2] + hh + 0.5, 1.5), (lc[2] + hh + 1.6, 0.5), (lc[2] + hh + 1.9, 0.0)], 4, cx=lc[0], cy=lc[1]))   # (its pyramid roof)
    M.add('Iron', 'iron', *torus(np.array([lc[0], lc[1], lc[2] + hh + 2.2]), 0.5, 0.11, (1, 0, 0), 14, 6))
    M.add('Iron', 'iron', *superbox(lc + np.array([0, 0, -hh - 0.2]), hw + 0.25, hw + 0.25, 0.25, 4.0, 14, 6))   # (its base plate)
    for sx in (-1, 1):
        for sy in (-1, 1):
            M.add('Iron', 'iron', *superbox(lc + np.array([sx * hw, sy * hw, 0]), 0.16, 0.16, hh, 4.0, 6, 10))   # (the corner posts)
    for k, (dx, dy) in enumerate(((1, 0), (-1, 0), (0, 1), (0, -1))):
        if k == 2: continue   # (one pane long gone)
        c = lc + np.array([dx * hw * 0.98, dy * hw * 0.98, 0])
        M.add('Glass', 'glass', *superbox(c, 0.03 if dx else hw * 0.92, 0.03 if dy else hw * 0.92, hh * 0.96, 6.0, 10, 10))
        M.add('Iron', 'iron', *superbox(c + np.array([dx * 0.05, dy * 0.05, 0]), 0.06 if dx else hw, 0.06 if dy else hw, 0.08, 3.0, 8, 4))   # (a bar across it)
    # the candle stub
    M.add('Wax', 'wax', *lathe([(lc[2] - hh, 0.0), (lc[2] - hh, 0.55), (lc[2] - 0.6, 0.5), (lc[2] - 0.45, 0.38), (lc[2] - 0.4, 0.0)], 14, cx=lc[0], cy=lc[1]))
    fx = Fx()
    fl = lc + np.array([0, 0, 0.4])
    fx.card('Flame', FX['core'], tuple(fl), 0.9, '1,0.72,0.35,1', 2.2, (4, 4, 14, 0))
    fx.card('FlameGlow', FX['glow'], tuple(fl), 4.2, '1,0.62,0.28,0.55', 1.0)
    fx.card('LanternHalo', FX['glow'], tuple(lc), 9.0, '1,0.6,0.3,0.22', 1.0)
    mats = {'wood': f'diffuse={wD} normal={wN} spec=0.15 gloss=9 alpha=0 doublesided=0',
            'iron': f'diffuse={iD} normal={iN} spec=0.25 gloss=13 alpha=0 doublesided=0',
            'rag': f'diffuse={cD} normal={cN} spec=0.10 gloss=8 alpha=0 doublesided=1',
            'carved': f'diffuse={cvD} normal={cvN} spec=0.15 gloss=9 alpha=0 doublesided=0',
            'glass': f'diffuse={gD} normal={iN} spec=2.0 gloss=150 alpha=blend doublesided=1',
            'wax': f'diffuse={candle} normal={cN} glow={candle} emit=1,0.7,0.4 emitmult=0.35 spec=0.20 gloss=18 alpha=0 doublesided=0'}
    return pw.finish(M, 'lanternwick', mats, fx.L, 'WeaponStaff')


# ================================================================== the battered weapons
def eclipse():
    """(Eclipse Edge - "sun and moon halves") a greatsword forged in two halves down its length and welded: one edge pale
    sun-gold, the other dark moon-steel, the seam between them a bright line; a guard of a crescent moon cradling a
    sun disc; a long grip of old leather, one turn of it come loose; the pommel half gold, half black. Its gold edge
    glows warm and its dark edge cold. Nicked and scratched from long use."""
    u, v = uvgrid()
    gold = np.clip((0.5 - u) / 0.01, 0, 1)
    seam = np.exp(-((u - 0.5) / 0.004) ** 2)
    _d, bN, _g = blade_tex('w_eclipse_base', (74, 80, 96), edge_rgb=(160, 172, 196), script=False, pattern='folded', seed=741)
    base = np.asarray(Image.open(g.WORK / 'w_eclipse_base_d.dds.png').convert('RGB'), np.float32)
    lum = base.mean(-1, keepdims=True) / 90.0
    goldc = np.array([214, 166, 82], np.float32) * np.clip(lum, 0.4, 1.6)
    col = base * (1 - gold[..., None]) + goldc * gold[..., None]
    col = col * (1 - seam[..., None]) + np.array([250, 236, 200]) * seam[..., None]
    im = Image.new('L', (S, S), 0); dr = ImageDraw.Draw(im)   # (runes down each half of the fuller)
    for k, uc in enumerate((0.42, 0.58)):
        x = int(uc * S); y = int(S * 0.06)
        rr_ = np.random.default_rng(747 + k)
        hh = int(S * 0.034)
        while y < S * 0.62:
            for (a, b, c, d) in RUNES[rr_.integers(len(RUNES))]:
                dr.line([(x - hh * 0.3 + a * hh, y + b * hh), (x - hh * 0.3 + c * hh, y + d * hh)], fill=255, width=max(3, int(hh * 0.12)))
            y += int(hh * 1.35)
    rmask = gaussian_filter(np.asarray(im, np.float32) / 255, KQ)
    col = col * (1 - 0.7 * rmask[..., None]) + np.array([20, 18, 16]) * 0.7 * rmask[..., None]
    nick = np.clip((tileable(S, S, 1.6, 746, aniso=(1.0, 0.25)) - 0.8) * 8, 0, 1) * np.clip(1 - np.minimum(u, 1 - u) / 0.06, 0, 1)   # (nicks along both edges)
    col = col * (1 - 0.6 * nick[..., None])
    bD = save(col, 'w_eclipse_blade_d.dds')
    edgeL = np.exp(-(u / 0.035) ** 2); edgeR = np.exp(-((1 - u) / 0.035) ** 2)
    glow = np.array([255, 196, 110]) * edgeL[..., None] + np.array([150, 180, 255]) * 0.8 * edgeR[..., None] + np.array([255, 230, 180]) * seam[..., None] * 0.7
    bG = save(glow * (1 - nick[..., None]), 'w_eclipse_blade_g.dds')
    gD, gN = metal_tex('w_eclipse_gold', (180, 140, 66), dark=0.6, etch=knot(748, 6), seed=742)
    dD, dN = metal_tex('w_eclipse_dark', (58, 62, 76), dark=0.8, etch=knot(749, 6), seed=743)
    lD, lN = leather_tex('w_eclipse_leather', (52, 36, 26), seed=744)
    M = Mesh()
    blade(M, 'blade', 6.6, 60.0, 7.0, 1.35, tip=0.17, fuller=0.6, seed=745)
    # the guard: a crescent moon (dark steel) lying across, a sun disc (gold) in its cradle with short rays
    a = np.linspace(-1.0, 1.0, 60)
    cres = np.stack([a * 7.0, np.zeros_like(a), 5.6 + 2.4 * a * a], -1)
    M.add('Dark', 'dark', *rod(cres, 0.75 * (1 - 0.75 * a * a) + 0.12, 12))
    M.add('Gold', 'gold', *superbox(np.array([0, 0, 5.7]), 2.2, 0.75, 2.2, 2.0, 32, 16))   # (the sun disc)
    for k in range(10):
        th = 2 * math.pi * k / 10 + 0.31
        d = np.array([math.cos(th), 0, math.sin(th)])
        if d[2] < -0.5: continue
        talon(M, 'gold', np.array([0, 0, 5.7]) + d * 2.0, d, d, 1.6 if k % 2 else 1.1, 0.32)
    # the grip, collars, a loose turn of the wrap, the pommel
    grip_wrap(M, 'leather', -11.0, 3.6, 1.1, 1.25)
    for z, m in ((3.9, 'gold'), (-4.0, 'dark'), (-11.3, 'gold')):
        M.add(m.capitalize(), m, *lathe([(z - 0.4, 1.14), (z - 0.3, 1.38), (z + 0.3, 1.38), (z + 0.4, 1.14)], 24))
    loose = [np.array([1.25 * math.cos(t), 1.25 * math.sin(t), -6.0 - 0.25 * t]) for t in np.linspace(0, 1.8, 10)] + [np.array([1.3 + 0.5 * s, 0.95, -6.6 - 3.5 * s]) for s in np.linspace(0.1, 1, 8)]
    M.add('Grip', 'leather', *rod(np.array(loose), 0.2, 6))
    # (a Nordic pommel: a gold bar under three lobes - the middle lobe dark, the outer two gold)
    M.add('Gold', 'gold', *superbox(np.array([0, 0, -12.0]), 2.3, 0.95, 0.55, 3.0, 24, 10))
    for k, (x, m, hgt) in enumerate(((-1.55, 'gold', 1.05), (0.0, 'dark', 1.45), (1.55, 'gold', 1.05))):
        M.add(m.capitalize(), m, *superbox(np.array([x, 0, -12.55 - hgt * 0.75]), 0.85 if k != 1 else 1.0, 0.9, hgt, 2.2, 20, 12))
    fx = Fx()
    fx.sheath('SunEdge', f'tex={FX["swirl"]} color=1,0.78,0.4,0.35 scale=1.2 blend=add falloff=1 uscroll=4 vscroll=2.5', 6.5, 66.0, 4.3, 2.2, 18, 12, 3.0)
    fx.card('SunGlow', FX['glow'], (0, 0, 5.7), 6.0, '1,0.76,0.35,0.5', 1.0)
    fx.card('SunCore', FX['core'], (0, 0, 5.7), 2.6, '1,0.82,0.45,1', 1.2, (4, 4, 6, 0))
    fx.card('MoonGlint', FX['glow'], (-7.0, 0, 8.0), 3.0, '0.6,0.7,1,0.45', 1.0)
    mats = {'blade': f'diffuse={bD} normal={bN} glow={bG} emit=1,1,1 emitmult=1.4 spec=0.80 gloss=54 alpha=0 doublesided=0',
            'gold': f'diffuse={gD} normal={gN} spec=0.60 gloss=30 alpha=0 doublesided=1',
            'dark': f'diffuse={dD} normal={dN} spec=0.35 gloss=21 alpha=0 doublesided=0',
            'leather': f'diffuse={lD} normal={lN} spec=0.15 gloss=8 alpha=0 doublesided=0'}
    return pw.finish(M, 'eclipse', mats, fx.L, 'WeaponBack')


def wanderstar():
    """(The Wandering Star - "a dim caged star") a pilgrim's staff of pale ash wound with faded blue cloth, its head an old
    armillary of three bronze rings - one bent - caging a small star that has nearly burnt out, pulsing dim white-gold"""
    wD, wN = wood_tex('w_wanderstar_wood', (170, 150, 120), grey=0.45, seed=751)
    bD, bN = bronze_tex('w_wanderstar_bronze', seed=752, etch=np.rot90(rune_bands([0.5], 756, h=int(S * 0.2))))   # (runes running round the rings)
    cD, cN = cloth_tex('w_wanderstar_cloth', (70, 92, 128), seed=753)
    M = Mesh()
    zs = np.linspace(-20.0, 46.5, 36)
    path = np.array([(0.15 * math.sin(z * 0.09), 0.1 * math.cos(z * 0.13), z) for z in zs])
    M.add('Wood', 'wood', *rod(path, np.linspace(0.95, 0.78, 36), 18))
    M.add('Bronze', 'bronze', *lathe([(-20.4, 0.0), (-20.4, 0.75), (-18.4, 1.02), (-18.2, 0.95)], 18))
    wrap_strips(M, 'cloth', -4.0, 5.0, 0.9, n=3, seed=754, width=1.0)
    wrap_strips(M, 'cloth', 30.0, 36.0, 0.82, n=1, seed=755, width=0.9)
    # the head: a bronze cup on the shaft, three prongs up to the rings, the rings round the star
    sc = np.array([0.15 * math.sin(46.5 * 0.09), 0.1 * math.cos(46.5 * 0.13), 52.5])
    M.add('Bronze', 'bronze', *lathe([(45.6, 0.8), (46.0, 1.05), (47.2, 1.3), (47.6, 1.15), (47.8, 0.0)], 20, cx=sc[0], cy=sc[1]))
    for k in range(3):
        th = 2 * math.pi * k / 3
        d = np.array([math.cos(th), math.sin(th), 0])
        pts = [np.array([sc[0], sc[1], 47.4]) + d * (0.9 + 2.6 * math.sin(math.pi * 0.5 * s)) + np.array([0, 0, 5.2 * s]) for s in np.linspace(0, 1, 14)]
        M.add('Bronze', 'bronze', *rod(np.array(pts), np.linspace(0.3, 0.2, 14), 8))
    for k, (ax, R_, bend) in enumerate((((1, 0, 0), 3.4, 0.0), ((0, 1, 0), 3.2, 0.0), ((0.7, 0.7, 0.2), 3.0, 0.5))):
        a = np.linspace(0, 2 * np.pi, 97)
        n = unit(np.array(ax, float)); e1 = unit(np.cross(n, [0.1, 0.2, 1.0])); e2 = np.cross(n, e1)
        rr = R_ * (1 - bend * 0.18 * np.exp(-((a - 1.0) / 0.4) ** 2))   # (the third one bent in)
        ring = sc + (np.outer(np.cos(a) * rr, e1) + np.outer(np.sin(a) * rr, e2))
        P_, N_, UV_, F_ = rod(ring, 0.2, 10); UV_ = UV_ * [1.0, 9.0]
        M.add('Bronze', 'bronze', P_, N_, UV_, F_)
    fx = Fx()
    fx.card('StarCore', FX['core'], tuple(sc), 1.3, '1,0.92,0.75,1', 1.4, (4, 4, 5, 0))
    fx.card('StarGlow', FX['glow'], tuple(sc), 5.5, '1,0.88,0.6,0.5', 1.0)
    fx.card('StarHalo', FX['glow'], tuple(sc), 11.0, '0.8,0.85,1,0.16', 1.0)
    fx.sheath('Stardust', f'tex={FX["motes"]} color=1,0.95,0.8,0.6 scale=1.5 blend=add uscroll=-4 vscroll=-6', 47.0, 57.5, 4.6, 3.6, 22, 8, 1.6)
    mats = {'wood': f'diffuse={wD} normal={wN} spec=0.15 gloss=9 alpha=0 doublesided=0',
            'bronze': f'diffuse={bD} normal={bN} spec=0.60 gloss=24 alpha=0 doublesided=0',
            'cloth': f'diffuse={cD} normal={cN} spec=0.10 gloss=8 alpha=0 doublesided=1'}
    return pw.finish(M, 'wanderstar', mats, fx.L, 'WeaponStaff')


WEAPONS = {'gravebell': gravebell, 'saltrice': saltrice, 'riverstone': riverstone, 'lanternwick': lanternwick,
           'eclipse': eclipse, 'wanderstar': wanderstar}


# ================================================================== the bows (on Skyrim's bow rig: tools/priestbows.py)
def bone_tex(name, seed=0):
    """2048 tiling: old bone - yellowed ivory, darker in the pores and cracks, a waxy sheen where handled"""
    mott = tileable(S, S, 1.8, 760 + seed); pore = tileable(S, S, 0.25, 761 + seed); streak = tileable(S, S, 1.0, 762 + seed, aniso=(0.08, 1.0))
    cr = crack_mask(763 + seed, n=5, length=0.4) * 0.8
    lum = 0.8 + 0.18 * (mott - 0.5) + 0.12 * (streak - 0.5) - 0.25 * np.clip((pore - 0.7) * 4, 0, 1)
    col = np.array([196, 180, 148], np.float32) * lum[..., None]
    grime = np.clip((0.45 - mott) * 2.5, 0, 1)
    col = col * (1 - 0.5 * grime[..., None]) + np.array([96, 78, 56]) * 0.5 * grime[..., None]
    col = col * (1 - 0.75 * cr[..., None])
    sc = np.rot90(rune_bands([0.5], 764 + seed, h=int(S * 0.16))) * 0.85   # (scrimshaw: runes scratched along it, inked dark)
    col = col * (1 - 0.7 * sc[..., None]) + np.array([40, 30, 22]) * 0.7 * sc[..., None]
    D = save(col, name + '_d.dds')
    h = -0.6 * sc + 0.3 * mott + 0.25 * streak - 0.5 * np.clip((pore - 0.7) * 4, 0, 1) - 0.8 * cr
    N = save(np.dstack([height_to_normal(gaussian_filter(h, 0.6 * KQ), 7.0 * KQ), np.clip(70 + 40 * mott - 60 * grime - 60 * cr, 0, 255)]), name + '_n.dds', True)
    return D, N


def leaf_tex(name, seed=0):
    """1024: a sprig of three leaves on a transparent ground (alpha), veined, some edges browned"""
    W = 1024
    im = Image.new('RGBA', (W, W), (0, 0, 0, 0)); dr = ImageDraw.Draw(im); r = np.random.default_rng(seed)
    for k, (cx, cy, ang, ln) in enumerate(((512, 560, -90, 420), (360, 640, -140, 300), (664, 640, -40, 300))):
        a = math.radians(ang); ux, uy = math.cos(a), math.sin(a); px_, py_ = -uy, ux
        pts = []
        for t in np.linspace(0, 1, 30):
            w = ln * 0.32 * math.sin(math.pi * t) ** 0.8
            pts.append((cx + ux * ln * t + px_ * w, cy + uy * ln * t + py_ * w))
        for t in np.linspace(1, 0, 30):
            w = ln * 0.32 * math.sin(math.pi * t) ** 0.8
            pts.append((cx + ux * ln * t - px_ * w, cy + uy * ln * t - py_ * w))
        g_ = int(110 + 40 * r.random())
        dr.polygon(pts, fill=(int(60 + 30 * r.random()), g_, 46, 255))
        dr.line([(cx, cy), (cx + ux * ln, cy + uy * ln)], fill=(150, 170, 90, 255), width=7)
        for t in np.linspace(0.15, 0.85, 7):
            bx, by = cx + ux * ln * t, cy + uy * ln * t
            for sd in (-1, 1):
                dr.line([(bx, by), (bx + (ux * 0.35 + px_ * sd) * ln * 0.2, by + (uy * 0.35 + py_ * sd) * ln * 0.2)], fill=(120, 150, 70, 255), width=3)
    dr.line([(512, 560), (512, 1010)], fill=(80, 64, 40, 255), width=14)   # (the sprig's stem)
    a = np.asarray(im, np.float32)
    fine = tileable(W, W, 0.6, 770 + seed)
    a[..., :3] *= (0.8 + 0.4 * fine[..., None])
    brown = np.clip((tileable(W, W, 1.6, 771 + seed) - 0.62) * 4, 0, 1)
    a[..., :3] = a[..., :3] * (1 - brown[..., None]) + np.array([120, 84, 40]) * brown[..., None] * (a[..., 3:] > 0)
    D = save(a, name + '_d.dds', True)
    hgt = gaussian_filter(a[..., 1] / 255.0, 2.0)
    N = save(np.dstack([height_to_normal(hgt, 3.0), np.full((W, W), 40.0)]), name + '_n.dds', True)
    return D, N


def rattlebone():
    """(Rattlebone Bow - "sinew and old bones") a short hunting bow pieced together from bone: each limb a run of
    vertebrae lashed along a bone stave with dried sinew, a jawbone for the riser, the grip bound in rawhide, bone spurs
    at the tips that rattle as the string looses"""
    import priestbows as pb
    bD, bN = bone_tex('w_rattlebone_bone', seed=780)
    sD, sN = leather_tex('w_rattlebone_sinew', (150, 120, 84), seed=781)
    M = Mesh()
    pb.limbs(M, 'bone')
    pb.grip(M, 'sinew')
    # the vertebrae: knobbed rings along the back of each limb, close set
    for sy in (-1, 1):
        for k in range(14):
            y = sy * (7.0 + k * 3.05)
            if abs(y) > pb.STRING_Y - 2: break
            pb.band(M, 'bone', y, 0.32, 1.3)
            line = pb.limb_line(y, y + 0.01, 2)[0]
            wz, tx = pb.limb_profile(np.array([y]))
            M.add('Bone', 'bone', *superbox(line + np.array([tx[0] + 0.35, 0, 0]), 0.45, 0.55, 0.38, 3.0, 12, 8))   # (the spinous knob)
            if k % 3 == 1:
                pb.band(M, 'sinew', y + sy * 1.5, 0.14, 0.7)
    # the jawbone riser: a curved plate along the front of the grip, teeth along its edge
    a = np.linspace(-1, 1, 30)
    jaw = np.stack([pb.GRIP_X + 1.6 + 0.9 * (1 - a * a), a * 7.0, np.zeros_like(a)], -1)
    M.add('Bone', 'bone', *pb.sweep(jaw, np.full(30, 1.25), np.full(30, 0.42), 14, 2.6))
    for k in range(7):
        y = -5.0 + k * 1.6
        M.add('Bone', 'bone', *superbox(np.array([pb.GRIP_X + 2.5 + 0.25 * (k % 2), y, 0.0]), 0.5, 0.3, 0.35, 2.2, 10, 6))   # (its teeth)
    for sy in (-1, 1):   # (bone spurs past the nocks)
        tip = pb.limb_line(sy * (pb.STRING_Y - 1.0), sy * (pb.STRING_Y + 4.2), 12)
        M.add('Bone', 'bone', *pb.sweep(tip, np.linspace(0.75, 0.12, 12), np.linspace(0.6, 0.1, 12), 12, 2.4))
    mats = {'bone': f'diffuse={bD} normal={bN} spec=0.30 gloss=24', 'sinew': f'diffuse={sD} normal={sN} spec=0.15 gloss=9',
            'string': f'diffuse={sD} normal={sN} spec=0.15 gloss=12'}
    pb.write('rattlebone', M, mats)


def whisperwood():
    """(Whisperwood Longbow - "still sprouting leaves") a longbow cut from a living whisperwood sapling that never knew
    it was cut: dark green-brown bark, vines spiralling round both limbs, sprigs of leaves breaking out along them and
    at the tips, a knot of root for the riser. Green motes drift about it."""
    import priestbows as pb
    wD, wN = wood_tex('w_whisperwood_bark', (74, 66, 46), grey=0.0, cracks=0.9, seed=790)
    vD, vN = wood_tex('w_whisperwood_vine', (66, 86, 44), grey=0.0, cracks=0.2, seed=791)
    fD, fN = leaf_tex('w_whisperwood_leaf', seed=792)
    lD, lN = leather_tex('w_whisperwood_grip', (70, 54, 36), seed=793)
    M = Mesh()
    pb.limbs(M, 'bark')
    pb.grip(M, 'grip')
    rr = np.random.default_rng(794)
    for sy in (-1, 1):   # (the vines winding round each limb)
        ys = np.linspace(sy * 5.5, sy * 50.0, 260)
        line = pb.limb_line(ys[0], ys[-1], 260)
        T = unit(np.gradient(line, axis=0)); Nn = unit(np.cross([0, 0, 1.0], T))
        wz, tx = pb.limb_profile(ys)
        ph = np.linspace(0, 2 * np.pi * 4.5, 260) + (0 if sy > 0 else 1.3)
        pts = line + Nn * ((tx + 0.22) * np.cos(ph))[:, None] + np.array([0, 0, 1.0]) * ((wz + 0.22) * np.sin(ph))[:, None]
        M.add('Vine', 'vine', *rod(pts, np.linspace(0.24, 0.1, 260), 8))
    # the leaves: sprigs breaking out along the limbs and bunched at the tips (each a card, both faces)
    def sprig(at, out, size, tilt):
        up = unit(out); side = unit(np.cross(up, [0, 0, 1.0]) if abs(up[2]) < 0.9 else np.cross(up, [1.0, 0, 0]))
        side = side * math.cos(tilt) + np.cross(up, side) * math.sin(tilt)
        c = [at - side * size * 0.5, at + side * size * 0.5, at + side * size * 0.5 + up * size, at - side * size * 0.5 + up * size]
        n = unit(np.cross(side, up))
        M.add('Leaf', 'leaf', np.array(c), np.tile(n, (4, 1)), np.array([[0, 1], [1, 1], [1, 0], [0, 0]], float), np.array([[0, 1, 2], [0, 2, 3]]))
    for sy in (-1, 1):
        for k in range(12):
            y = sy * (8.0 + k * 3.6 + rr.random() * 1.5)
            if abs(y) > pb.STRING_Y: continue
            line = pb.limb_line(y, y + 0.01, 2)[0]
            ang = rr.random() * 2 * math.pi
            out = np.array([math.cos(ang) * 0.8, sy * 0.6, math.sin(ang)])
            sprig(line, out, 5.5 + 2.0 * rr.random(), rr.random() * 3)
        tip = pb.limb_line(sy * pb.STRING_Y, sy * (pb.STRING_Y + 0.5), 2)[0]
        for k in range(4):
            ang = k * 1.7
            sprig(tip, np.array([math.cos(ang) * 0.6 + 0.3, sy * 1.0, math.sin(ang) * 0.6]), 6.5, k * 0.8)
    # the root-knot riser: lumps of root gnarled over the grip's ends
    for sy in (-1, 1):
        for k in range(3):
            c = pb.limb_line(sy * (5.6 + k * 1.2), sy * (5.6 + k * 1.2) + 0.01, 2)[0]
            M.add('Bark', 'bark', *superbox(c + np.array([0.3 * (k - 1), 0, 0.2 * (k - 1)]), 1.7 - 0.3 * k, 1.1 - 0.15 * k, 1.9 - 0.3 * k, 2.4, 16, 10))
    fx = []
    for k, (y0, y1) in enumerate(((6.0, 52.0), (-52.0, -6.0))):   # (green motes drifting along the limbs)
        fx.append(f'shape WhisperMotes{k} tex={FX["motes"]} color=0.55,1,0.45,0.55 scale=1.5 blend=add uscroll=3 vscroll={-4 if k == 0 else 4}')
        ln = pb.limb_line(y0, y1, 24); nu = 16
        for i, c in enumerate(ln):
            fade = math.sin(math.pi * i / (len(ln) - 1))
            for j in range(nu + 1):
                th = 2 * math.pi * j / nu
                p_ = c + np.array([3.0 * math.cos(th), 0, 3.0 * math.sin(th)])
                fx.append(f'v {p_[0]:.4f} {p_[1]:.4f} {p_[2]:.4f} {j / nu * 2:.4f} {i / (len(ln) - 1) * 3:.4f} 1 1 1 {fade:.3f}')
                fx.append(f'n {math.cos(th):.4f} 0 {math.sin(th):.4f}')
        for i in range(len(ln) - 1):
            for j in range(nu):
                a_ = i * (nu + 1) + j; b_ = a_ + 1; c_ = a_ + nu + 1; d_ = c_ + 1
                fx += [f't {a_} {c_} {b_}', f't {b_} {c_} {d_}']
    mats = {'bark': f'diffuse={wD} normal={wN} spec=0.15 gloss=8', 'vine': f'diffuse={vD} normal={vN} spec=0.15 gloss=10',
            'leaf': f'diffuse={fD} normal={fN} spec=0.20 gloss=18 alpha=test doublesided=1', 'grip': f'diffuse={lD} normal={lN} spec=0.15 gloss=8',
            'string': f'diffuse={lD} normal={lN} spec=0.15 gloss=12'}
    pb.write('whisperwood', M, mats, fx)


# ================================================================== on the hand (fists, the Codex): the hand bone's frame
# (tools/gauntlet.py: fingers up +Z, the back of the hand +Y, the thumb's side +X on the right hand); hung on
# "NPC R Hand [RHnd]" / "NPC L Hand [LHnd]" by src/Tablet.cpp while the weapon is held
def hand_wrap(M, mat, z0, z1, lift, pitch, group='Wrap', seed=0):
    """a strip wound round the hand in a helix, its turns standing proud (the glove's envelope, tools/gauntlet.py)"""
    nr = max(40, int((z1 - z0) / pitch * 12))
    Z = np.linspace(z0, z1, nr)[:, None] * np.ones(g.NC)[None, :]
    T = np.broadcast_to(g.TH, Z.shape)
    ph = T / (2 * np.pi) + (Z - z0) / pitch
    ridge = np.abs(np.sin(np.pi * ph)) ** 0.4
    P = g.surf(T, Z, lift + 0.12 * ridge + 0.03 * np.sin(Z * 3.1 + seed))
    N = g.outward(g.grid_n(P), P, g.C0)
    U = np.linspace(0, 2, g.NC + 1)
    V = (np.concatenate([Z, Z[:, :1]], 1) - z0) / 3.0
    UV = np.stack([np.broadcast_to(U, V.shape), V], -1)
    M.add(group, mat, *g.seam_grid(P, N, UV))


def hand_band(M, mat, z0, z1, lift_in, lift_out, group='Band'):
    """a solid band round the hand (rings: inside at z0, outside at z0, outside at z1, inside at z1, back)"""
    T = g.TH
    rings = [g.surf(T, z0, lift_in), g.surf(T, z0 - 0.05, lift_out), g.surf(T, z1 + 0.05, lift_out), g.surf(T, z1, lift_in), g.surf(T, z0, lift_in)]
    slab([list(r_) for r_ in rings], M, group, mat, 0.3)


def back_point(x, z, lift):
    """the point on the back of the hand (lift off it) at x across it, height z"""
    best = None
    for th in np.linspace(0.15, math.pi - 0.15, 200):
        p_ = g.surf(np.array(th), np.array(z), lift)
        if best is None or abs(p_[0] - x) < abs(best[0] - x): best = p_
    return np.asarray(best)


def write_hand(M, name, mats, fx_lines=None, anim=None):
    """the model for each hand (the left mirrored), as tools/gauntlet.py writes the glove: modelconv, then its effects"""
    import subprocess
    from throwdagger import rename_root as rr_
    W = pw.WORK; W.mkdir(parents=True, exist_ok=True); pw.MESHDIR.mkdir(parents=True, exist_ok=True)
    pw.roughen(M, len(name))
    base_groups = M.groups
    for side in ('r', 'l'):
        mirror = side == 'l'
        merged = {}
        for gn, m, V, N, UV, F in base_groups:
            V = np.asarray(V, float); N = np.asarray(N, float); F = np.asarray(F)
            if mirror: V = V * [-1, 1, 1]; N = N * [-1, 1, 1]; F = F[:, [0, 2, 1]]
            key = gn if gn.startswith('Page') else m
            e = merged.setdefault(key, [gn if gn.startswith('Page') else m.capitalize(), m, [], [], [], [], 0])
            e[2].append(V); e[3].append(N); e[4].append(UV); e[5].append(F + e[6]); e[6] += len(V)
        Mo = Mesh()
        Mo.groups = [(e[0], e[1], np.vstack(e[2]), np.vstack(e[3]), np.vstack(e[4]), np.vstack(e[5])) for e in merged.values()]
        obj = W / f'{name}_{side}.obj'; Mo.write_obj(obj)
        static = W / f'{name}_{side}_static.nif'
        job = [f'in {obj}', f'out {static}', 'scale 1', 'ground 0', 'origin 1', 'offset 0 0 0'] + [f'mat {k} {v}' for k, v in mats.items()]
        (W / f'{name}_{side}.job').write_text('\n'.join(job) + '\n')
        r = subprocess.run([str(pw.TOOLS / 'modelconv'), str(W / f'{name}_{side}.job')], capture_output=True, text=True)
        if r.returncode: print(r.stdout, r.stderr); raise SystemExit('modelconv failed')
        import throwdagger; throwdagger.WORK.mkdir(parents=True, exist_ok=True)
        rr_(static, ''.join(w.capitalize() for w in name.split('_')))
        nif = pw.MESHDIR / f'{name}_{side}.nif'
        cur = static
        if anim:
            aj = W / f'{name}_{side}.anim'
            lines = [f'in {cur}', f'out {W / (name + "_" + side + "_anim.nif")}']
            for a_ in anim:
                if mirror and a_.startswith('flip'):   # (mirrored in x: the pivot's x, and the turn the other way round)
                    q = a_.split(); q[2] = str(-float(q[2])); q[6] = str(-float(q[6])); q[7] = str(-float(q[7]))
                    a_ = ' '.join(q)
                lines.append(a_)
            aj.write_text('\n'.join(lines) + '\n')
            r = subprocess.run([str(pw.TOOLS / 'nifanim'), str(aj)], capture_output=True, text=True)
            if r.returncode: print(r.stdout, r.stderr); raise SystemExit('nifanim failed')
            cur = W / f'{name}_{side}_anim.nif'
        if fx_lines:
            L = []
            card = False
            for l in fx_lines:
                q = l.split(' ')
                if q[0] == 'shape':
                    card = any(w.startswith('billboard=') and w != 'billboard=0' for w in q)
                    if mirror:
                        for i, w in enumerate(q):
                            if w.startswith('at='):
                                x, y, z = (float(t) for t in w[3:].split(','))
                                q[i] = f'at={-x:.3f},{y:.3f},{z:.3f}'
                elif q[0] in ('v', 'n') and mirror and not card:
                    q[1] = f'{-float(q[1]):.4f}'
                elif q[0] == 't' and mirror and not card:
                    q = [q[0], q[1], q[3], q[2]]
                L.append(' '.join(q))
            L = [f'base {cur}', f'out {nif}'] + L
            (W / f'{name}_{side}_fx.job').write_text('\n'.join(g.fx_fix(L)) + '\n')
            r = subprocess.run([str(pw.TOOLS / 'fxwrite'), str(W / f'{name}_{side}_fx.job')], capture_output=True, text=True)
            if r.returncode or 'no node' in r.stderr: print(r.stdout, r.stderr); raise SystemExit('fxwrite failed')
        else:
            shutil.copy(cur, nif)
        lo, hi = Mo.bounds()
        print(f'{nif.name}: x {lo[0]:.1f}..{hi[0]:.1f}  y {lo[1]:.1f}..{hi[1]:.1f}  z {lo[2]:.1f}..{hi[2]:.1f}; {sum(len(q[5]) for q in Mo.groups)} triangles')


def gem_tex(name, rgb, seed=0):
    """1024: a cut gem's inside - deep colour, bright facet lines, a cloud in its heart; and its glow"""
    W = 1024
    fac = np.zeros((W, W), np.float32); r = np.random.default_rng(seed)
    yy, xx = np.mgrid[0:W, 0:W].astype(np.float32) / W
    for _ in range(14):
        a = r.random() * math.pi; c = r.random()
        fac += np.exp(-((xx * math.cos(a) + yy * math.sin(a) - c) / 0.004) ** 2)
    cloud = tileable(W, W, 1.6, 800 + seed)
    col = np.array(rgb, np.float32) * (0.45 + 0.6 * cloud[..., None]) + 255 * 0.45 * np.clip(fac, 0, 1)[..., None]
    D = save(col, name + '_d.dds')
    G = save(np.array(rgb, np.float32) * (0.4 + 0.8 * cloud[..., None]) + 200 * np.clip(fac, 0, 1)[..., None], name + '_g.dds')
    N = save(np.dstack([np.full((W, W, 3), (128, 128, 255), np.float32), np.full((W, W), 200.0)]), name + '_n.dds', True)
    return D, N, G


def stormknuckles():
    """(Stormbound Knuckles - "brass knuckles wrapped round cracked storm gems", Noah: "Galvaknuckles") a pit-fighter's
    brass knuckle-dusters across the fingers, four cracked storm gems set in the bar over the knuckles, the hand and wrist
    bound in old leather strapping; blue-white sparks crawl over the gems"""
    bD, bN = metal_tex('w_stormknuckles_brass', (168, 128, 66), dark=0.55, seed=810)
    lD, lN = leather_tex('w_stormknuckles_leather', (62, 44, 32), seed=811)
    gD, gN, gG = gem_tex('w_stormknuckles_gem', (90, 150, 255), seed=812)
    M = Mesh()
    hand_wrap(M, 'leather', -1.0, 2.2, 0.05, 0.9, 'Wrap', 1)
    hand_wrap(M, 'leather', 4.6, 6.6, 0.05, 0.9, 'Wrap', 2)
    hand_band(M, 'brass', 7.35, 8.25, 0.05, 0.42)
    gems = []
    for k, x in enumerate((-2.5, -0.85, 0.85, 2.5)):
        c = back_point(x, 7.8, 0.55)
        n = unit(np.array([0.08 * x, 1.0, 0.15]))
        M.add('Brass', 'brass', *superbox(c - n * 0.1, 0.78, 0.36, 0.62, 3.0, 16, 8, R=np.column_stack([unit(np.cross(n, [0, 0, 1.0])), n, [0, 0, 1.0]])))   # (the gem's setting)
        M.add('Gem', 'gem', *superbox(c + n * 0.32, 0.56, 0.3, 0.44, 1.6, 12, 8, R=np.column_stack([unit(np.cross(n, [0, 0, 1.0])), n, [0, 0, 1.0]])))
        gems.append(c + n * 0.55)
    for z in (-1.15, 2.35):   # (brass buckles on the wrist strap)
        c = back_point(0.0, z, 0.35)
        M.add('Brass', 'brass', *superbox(c, 0.7, 0.18, 0.4, 2.6, 12, 6))
    fx = Fx()
    for k, c in enumerate(gems):
        fx.card(f'Spark{k}', FX['core'], tuple(c), 0.9, '0.6,0.8,1,1', 2.4, (4, 4, 18, k * 3))
        fx.card(f'GemGlow{k}', FX['glow'], tuple(c), 2.2, '0.45,0.7,1,0.45', 1.0)
    mats = {'brass': f'diffuse={bD} normal={bN} spec=0.70 gloss=36 alpha=0 doublesided=0',
            'leather': f'diffuse={lD} normal={lN} spec=0.15 gloss=8 alpha=0 doublesided=1',
            'gem': f'diffuse={gD} normal={gN} glow={gG} emit=0.6,0.8,1 emitmult=2.4 spec=2.5 gloss=200 alpha=0 doublesided=0'}
    write_hand(M, 'stormknuckles', mats, fx.L, ['pulse Gem 1.4 3.6 0.7'])


def emberclaw():
    """(Emberclaw Wraps - "charred wraps with coals sewn in") a fire-dancer's hand wraps, scorched black at the knuckles,
    live coals sewn into pockets over the back of the hand and the knuckles, glowing as they breathe; embers drift up"""
    cD, cN = cloth_tex('w_emberclaw_wrap', (92, 72, 58), seed=820)
    char = np.asarray(Image.open(g.WORK / 'w_emberclaw_wrap_d.dds.png').convert('RGB'), np.float32)
    u, v = uvgrid()
    scorch = np.clip((tileable(S, S, 1.5, 821) - 0.45) * 3, 0, 1)
    char = char * (1 - 0.8 * scorch[..., None]) + np.array([16, 12, 10]) * 0.8 * scorch[..., None]
    cD = save(char, 'w_emberclaw_wrap_d.dds')
    W = 1024
    cl = tileable(W, W, 1.2, 822); cr = np.clip(1 - np.abs(tileable(W, W, 0.9, 823) - 0.5) / 0.05, 0, 1)
    coalD = save(np.array([30, 22, 18], np.float32) * (0.6 + 0.8 * cl[..., None]) + np.array([255, 110, 30]) * cr[..., None] * 0.9, 'w_emberclaw_coal_d.dds')
    coalG = save(np.array([255, 120, 30], np.float32) * (0.2 + 0.6 * cl[..., None] + cr[..., None]), 'w_emberclaw_coal_g.dds')
    coalN = save(np.dstack([height_to_normal(gaussian_filter(cl - cr, 1.0 * KQ), 6.0 * KQ), np.full((W, W), 30.0)]), 'w_emberclaw_coal_n.dds', True)
    M = Mesh()
    hand_wrap(M, 'wrap', -1.6, 7.9, 0.05, 1.05, 'Wrap', 3)
    hand_wrap(M, 'wrap', 0.8, 5.6, 0.22, 1.4, 'Wrap', 4)
    coals = []
    rr = np.random.default_rng(824)
    for k, (x, z) in enumerate(((-1.8, 4.4), (0.2, 4.9), (2.0, 4.2), (-1.0, 2.6), (1.1, 2.4), (-2.4, 7.7), (0.0, 7.95), (2.3, 7.6))):
        c = back_point(x, z, 0.38)
        M.add('Coal', 'coal', *superbox(c, 0.55 + 0.15 * rr.random(), 0.42, 0.5 + 0.15 * rr.random(), 2.0, 12, 8))
        coals.append(c + np.array([0, 0.5, 0]))
    fx = Fx()
    for k, c in enumerate(coals[:5]):
        fx.card(f'CoalGlow{k}', FX['glow'], tuple(c), 1.8, '1,0.5,0.15,0.5', 1.0)
    fx.sheath('Embers', f'tex={FX["motes"]} color=1,0.55,0.2,0.85 scale=1.6 blend=add uscroll=2 vscroll=-10', 0.0, 12.0, 4.6, 3.2, 18, 8, 2.0)
    mats = {'wrap': f'diffuse={cD} normal={cN} spec=0.10 gloss=8 alpha=0 doublesided=1',
            'coal': f'diffuse={coalD} normal={coalN} glow={coalG} emit=1,0.5,0.15 emitmult=2.0 spec=0.15 gloss=12 alpha=0 doublesided=0'}
    write_hand(M, 'emberclaw', mats, fx.L, ['pulse Coal 0.8 2.8 1.6'])


def paper_tex(name, seed=0):
    """2048: the open pages (left half u 0..0.5, right 0.5..1): old vellum, foxed and stained toward the edges, lines of
    the Codex's glyphs in dark ink - and its glow: the ink lit as the book wakes"""
    S = 2048
    v, u = (np.mgrid[0:S, 0:S].astype(np.float32) + 0.5) / S
    fine = tileable(S, S, 0.5, 830 + seed); big = tileable(S, S, 2.0, 831 + seed)
    edge = np.clip(1 - np.minimum(np.minimum(u % 0.5, 0.5 - u % 0.5) / 0.06, np.minimum(v, 1 - v) / 0.06), 0, 1)
    col = np.array([206, 188, 150], np.float32) * (0.85 + 0.15 * (fine[..., None] - 0.5) + 0.12 * (big[..., None] - 0.5))
    col = col * (1 - 0.45 * edge[..., None]) + np.array([120, 92, 58]) * 0.45 * edge[..., None]
    ink = Image.new('L', (S, S), 0); dr = ImageDraw.Draw(ink); r = np.random.default_rng(832 + seed)
    circ = []
    for half in (0, 1):
        x0 = half * S // 2 + 140; x1 = (half + 1) * S // 2 - 140
        if half == 1:   # (a page's diagram: a circle of glyphs)
            cx, cy, R_ = (x0 + x1) // 2, 560, 300
            dr.ellipse([cx - R_, cy - R_, cx + R_, cy + R_], outline=255, width=9)
            dr.ellipse([cx - R_ + 60, cy - R_ + 60, cx + R_ - 60, cy + R_ - 60], outline=255, width=5)
            for k in range(12):
                a = 2 * math.pi * k / 12
                gx, gy = cx + math.cos(a) * (R_ - 30), cy + math.sin(a) * (R_ - 30)
                for q in range(3):
                    b = r.random() * math.pi * 2
                    dr.line([(gx, gy), (gx + math.cos(b) * 18, gy + math.sin(b) * 18)], fill=255, width=6)
            for k in range(6):
                a = 2 * math.pi * k / 6
                dr.line([(cx + math.cos(a) * (R_ - 60), cy + math.sin(a) * (R_ - 60)), (cx + math.cos(a + 2.09) * (R_ - 60), cy + math.sin(a + 2.09) * (R_ - 60))], fill=255, width=4)
            ys = range(980, S - 160, 74)
        else:
            ys = range(180, S - 160, 74)
        for y in ys:
            x = x0
            while x < x1 - 40:
                w = int(16 + 30 * r.random())
                for q in range(int(2 + 3 * r.random())):   # (a glyph: a few hooked strokes)
                    ax, ay = x + r.random() * w, y + r.random() * 34
                    b = r.random() * math.pi * 2
                    dr.line([(ax, ay), (ax + math.cos(b) * 16, ay + math.sin(b) * 16)], fill=255, width=4)
                x += w + int(12 + 30 * (r.random() < 0.18) * r.random() * 3)
    inkm = gaussian_filter(np.asarray(ink, np.float32) / 255, 1.2)
    col = col * (1 - 0.8 * inkm[..., None]) + np.array([30, 18, 14]) * 0.8 * inkm[..., None]
    D = g.save(col, name + '_d.dds', False, 4096)
    G = g.save(np.array([255, 170, 90], np.float32) * inkm[..., None] * 1.0, name + '_g.dds', False, 4096)
    N = g.save(np.dstack([height_to_normal(gaussian_filter(0.3 * fine - 0.4 * inkm, 0.8), 3.0), np.full((S, S), 16.0)]), name + '_n.dds', True, 4096)
    return D, N, G


def codex():
    """(The Unwritten Codex - Noah: "an actual spell book in hand", "one handed", "it needs to look and feel strong") a
    great old grimoire held open on the palm: boards of black-red leather worn through to the wood at the corners, brass
    corner-guards and a brass sigil on its back board, its clasp hanging loose; pages of foxed vellum written with the
    Codex's glyphs and a circle drawn on the right-hand page; a red ribbon hanging from its spine. Its pages turn on their
    own; its ink burns ember-bright; glyphs lift off it and a spell circle turns in the air before it."""
    lD, lN = leather_tex('w_codex_cover', (58, 22, 20), seed=840)
    tool_ = knot(845, 4)   # (the boards tooled with knotwork, pressed dark into the leather)
    cv = np.asarray(Image.open(g.WORK / 'w_codex_cover_d.dds.png').convert('RGB'), np.float32)
    lD = save(cv * (1 - 0.45 * tool_[..., None]), 'w_codex_cover_d.dds')
    bD, bN = metal_tex('w_codex_brass', (160, 122, 62), dark=0.7, etch=knot(846, 5), seed=841)
    pD, pN, pG = paper_tex('w_codex_page', seed=842)
    eD, eN = wood_tex('w_codex_edge', (196, 176, 140), grey=0.2, cracks=0.0, seed=843)   # (the page block's edge: fine lines)
    rD, rN = cloth_tex('w_codex_ribbon', (140, 26, 24), seed=844)
    M = Mesh()
    a = math.radians(14)
    sp = np.array([0.3, -4.9, 4.4])           # (the spine, on the palm)
    zc, hz = 4.4, 6.2                          # (the pages' middle height, half their height)
    hw = 4.8                                   # (a page's width)
    for side in (1, -1):
        d = np.array([side * math.cos(a), -math.sin(a), 0.0])
        n = np.array([-side * math.sin(a), -math.cos(a), 0.0]) * 1.0
        n = unit(n)
        R_ = np.column_stack([d, -n, [0, 0, 1.0]])
        M.add('Cover', 'cover', *superbox(sp + d * (hw + 0.1) - n * 0.15, hw + 0.25, 0.2, hz + 0.3, 5.0, 24, 12, R=R_))
        M.add('Edge', 'edge', *superbox(sp + d * hw + n * 0.5, hw - 0.1, 0.55, hz - 0.15, 8.0, 24, 12, R=R_))
        top = sp + n * 1.06
        q = [top + d * 0.15 - [0, 0, hz - 0.3], top + d * (2 * hw - 0.25) - [0, 0, hz - 0.3], top + d * (2 * hw - 0.25) + [0, 0, hz - 0.3], top + d * 0.15 + [0, 0, hz - 0.3]]
        q = [np.asarray(x, float) for x in q]
        u0, u1 = (0.5, 1.0) if side > 0 else (0.5, 0.0)
        M.add('Vellum', 'vellum', np.array(q), np.tile(n, (4, 1)), np.array([[u0, 1], [u1, 1], [u1, 0], [u0, 0]], float), np.array([[0, 1, 2], [0, 2, 3]] if side > 0 else [[0, 2, 1], [0, 3, 2]]))
        for zz in (zc - hz - 0.2, zc + hz + 0.2):   # (brass corner-guards)
            for k in (0,):
                c = sp + d * (2 * hw + 0.1) - n * 0.15 + [0, 0, 0]
                c = c.copy(); c[2] = zz
                M.add('Brass', 'brass', *superbox(c, 0.7, 0.3, 0.7, 2.6, 12, 8, R=R_))
        if side < 0:   # (the brass sigil on the back board and the loose clasp hanging off it)
            c = sp + d * (hw + 0.1) - n * 0.42; c = c.copy(); c[2] = zc
            M.add('Brass', 'brass', *torus(c, 1.6, 0.18, -n, 40, 8))
            M.add('Brass', 'brass', *superbox(c, 0.5, 0.12, 0.5, 2.0, 10, 6, R=R_))
            hang = sp + d * (2 * hw + 0.3) - n * 0.2; hang = hang.copy(); hang[2] = zc
            strap = np.array([hang + d * 0.4 * t_ + np.array([0, 0, -3.2 * t_ * t_]) - n * 0.3 * t_ for t_ in np.linspace(0, 1, 12)])
            M.add('Cover', 'cover', *rod(strap, 0.28, 8))
            M.add('Brass', 'brass', *superbox(strap[-1] + [0, 0, -0.4], 0.45, 0.2, 0.55, 3.0, 10, 6))
    M.add('Cover', 'cover', *lathe([(zc - hz - 0.3, 0.0), (zc - hz - 0.3, 0.75), (zc + hz + 0.3, 0.75), (zc + hz + 0.3, 0.0)], 18, cx=sp[0], cy=sp[1] + 0.35))   # (the spine)
    rib = np.array([sp + np.array([0.15 * t_, -1.2 - 0.3 * t_, -hz + zc - 0.4 - 5.0 * t_ - zc]) + [0, 0, zc] for t_ in np.linspace(0, 1, 14)])
    rib[:, 2] = zc - hz + 0.2 - np.linspace(0, 5.0, 14)
    M.add('Ribbon', 'ribbon', *rod(rib, 0.16, 6))
    # the turning pages: thin leaves lying on the right-hand page, hinged at the spine
    d = np.array([math.cos(a), -math.sin(a), 0.0]); n = unit(np.array([-math.sin(a), -math.cos(a), 0.0]))
    for k in range(4):
        top = sp + n * (1.1 + 0.025 * k)
        q = [top + d * 0.15 - [0, 0, hz - 0.45], top + d * (2 * hw - 0.4) - [0, 0, hz - 0.45], top + d * (2 * hw - 0.4) + [0, 0, hz - 0.45], top + d * 0.15 + [0, 0, hz - 0.45]]
        q = np.array([np.asarray(x, float) for x in q])
        M.add(f'Page{k}', 'leaf', q, np.tile(n, (4, 1)), np.array([[0.5, 1], [1, 1], [1, 0], [0.5, 0]], float), np.array([[0, 1, 2], [0, 2, 3]]))
    # its effects: the ember-lit ink's halo, glyphs rising off the pages, the spell circle turning before it
    fx = Fx()
    face = sp + np.array([0, -2.0, zc - sp[2]])
    fx.card('BookGlow', FX['glow'], tuple(face), 9.0, '1,0.6,0.3,0.35', 1.0)
    rise = []
    nu = 20
    V_, N_, UV_, F_, A_ = [], [], [], [], []
    for i in range(9):
        t_ = i / 8
        for j in range(nu + 1):
            th = 2 * math.pi * j / nu
            rr_ = 6.2 - 2.2 * t_
            V_.append((sp[0] + rr_ * math.cos(th), sp[1] - 1.0 - 8.0 * t_, zc + rr_ * 0.85 * math.sin(th)))
            N_.append((math.cos(th), 0, math.sin(th))); UV_.append((j / nu * 2, t_ * 1.5)); A_.append(math.sin(math.pi * t_) ** 1.2)
    for i in range(8):
        for j in range(nu):
            a0 = i * (nu + 1) + j; b0 = a0 + 1; c0 = a0 + nu + 1; d0 = c0 + 1
            F_ += [(a0, c0, b0), (b0, c0, d0)]
    fx.mesh(f'Glyphs tex={FX["motes"]} color=1,0.7,0.4,0.9 scale=1.8 blend=add vscroll=-5 uscroll=1.5', V_, N_, UV_, F_, A_)
    for k, (R0, R1, yy, us, al) in enumerate(((5.6, 7.4, -9.5, 2.5, 0.85), (3.6, 4.6, -9.7, -3.5, 0.7))):
        V_, N_, UV_, F_ = [], [], [], []
        nu = 64
        for i, rr_ in enumerate((R0, R1)):
            for j in range(nu + 1):
                th = 2 * math.pi * j / nu
                V_.append((sp[0] + rr_ * math.cos(th), sp[1] + yy, zc + rr_ * math.sin(th))); N_.append((0, -1, 0)); UV_.append((j / nu * 6, i))
        for j in range(nu):
            a0 = j; b0 = j + 1; c0 = j + nu + 1; d0 = c0 + 1
            F_ += [(a0, b0, c0), (b0, d0, c0), (a0, c0, b0), (b0, c0, d0)]
        fx.mesh(f'SpellCircle{k} tex=CurseOfAlduin\\priests\\moro_runeband.dds color=1,0.65,0.35,{al} scale=2.6 blend=add uscroll={us}', V_, N_, UV_, F_)
    mats = {'cover': f'diffuse={lD} normal={lN} spec=0.17 gloss=10 alpha=0 doublesided=0',
            'brass': f'diffuse={bD} normal={bN} spec=0.65 gloss=30 alpha=0 doublesided=0',
            'vellum': f'diffuse={pD} normal={pN} glow={pG} emit=1,0.65,0.35 emitmult=1.6 spec=0.15 gloss=8 alpha=0 doublesided=0',
            'leaf': f'diffuse={pD} normal={pN} glow={pG} emit=1,0.65,0.35 emitmult=1.6 spec=0.15 gloss=8 alpha=0 doublesided=1',
            'edge': f'diffuse={eD} normal={eN} spec=0.15 gloss=8 alpha=0 doublesided=0',
            'ribbon': f'diffuse={rD} normal={rN} spec=0.15 gloss=8 alpha=0 doublesided=0'}
    write_hand(M, 'codex', mats, fx.L, [f'flip Page {sp[0]:.3f} {sp[1] + 0.0:.3f} 0 0 0 -1 168 4.2', 'pulse Vellum 1.0 2.6 2.1', 'pulse Page 1.0 2.6 2.1'])


WEAPONS.update({'rattlebone': rattlebone, 'whisperwood': whisperwood, 'codex': codex})
# (Noah: "they look like paper thin rings instead of real hand wraps or elemental brass knuckles") the fist weapons are
# gloves on the hands' own bones now: tools/tabletgloves.py. stormknuckles() and emberclaw() above are kept only as the
# first try; nothing builds them

if __name__ == '__main__':
    pw.WORK.mkdir(parents=True, exist_ok=True)
    tool('addprn')
    names = [a for a in sys.argv[1:] if not a.startswith('--')] or list(WEAPONS)
    for n in names:
        WEAPONS[n]()
    if '--keep' not in sys.argv:
        shutil.rmtree(pw.WORK, ignore_errors=True)
