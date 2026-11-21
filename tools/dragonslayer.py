"""(Noah, 2026-11-20: "our own Dragon Slayer sword" - modelled from scratch; a katana "keep its moveset", "Dragonbone +
scales", "Faint ember glow in runes") the Dragon Slayer: a katana whose blade was cut from a dragon's bone - bone-white,
polished to a soft sheen, a pale wavering temper line along its keen edge - with the Dragon tongue's letters cut along its
spine and glowing faintly like banked embers; a guard of bone with two dragon fangs curving up along the blade; a long
grip bound in overlapping black dragon scales between collars of blackened iron; a bone pommel gripped by a small claw.

As Skyrim's swords are: the grip at the origin, the blade up +Y, Prn WeaponSword; built along Z with
tools/priestweapons.py's parts and turned at the end. Its proportions are a katana's: a long gently curved blade (about
72 units), a long two-hand grip (about 26).

    python3 tools/dragonslayer.py [--keep]
      -> assets/fx/Meshes/CurseOfAlduin/slayer/dragonslayer.nif, Textures/CurseOfAlduin/slayer/slay_*.dds"""
import math, pathlib, shutil, subprocess, sys
import numpy as np
from scipy.ndimage import gaussian_filter

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import priestweapons as pw
from throwdagger import Mesh, tileable, height_to_normal, tool, unit, grid_faces

g = pw.g
ROOT = g.ROOT
pw.MESHDIR = ROOT / 'assets' / 'fx' / 'Meshes' / 'CurseOfAlduin' / 'slayer'
g.TEXDIR = ROOT / 'assets' / 'fx' / 'Textures' / 'CurseOfAlduin' / 'slayer'
g.TEXREL = 'CurseOfAlduin\\slayer\\'
pw.WORK = g.WORK = ROOT / 'build' / 'dragonslayer'
# (Noah, 2026-11-21: "its placement in my hand is off" - the fingers lay against the fangs) the hand sits four units further down
# the grip than it did: the guard 8.5 above the hand, the pommel at -20.5 as Animated Armoury's katana's is
pw.FIT['dragonslayer'] = (-1.1, 1.6)   # (the model's height that lands 4 units above the hand; model units -> game units)
S = 2048


def bone_blade_tex():
    """the blade's sheet - u across (the spine at 0, the edge at 1), v along (the guard at 0): dragon bone, its grain running
    the blade's length, polished; the spine a little darker and warmer; along the edge a ground bevel, whiter, and the
    temper line wavering where the bevel meets the body; hairline age crazing here and there; a faint warm stain near the
    guard where hands and blood have been"""
    v, u = (np.mgrid[0:S, 0:S].astype(np.float32) + 0.5) / S
    grain = tileable(S, S, 0.7, 2101, aniso=(0.03, 1.0))
    fibre = tileable(S, S, 0.25, 2102, aniso=(0.06, 1.0))
    blot = tileable(S, S, 1.8, 2103)
    wav = tileable(S, S, 1.2, 2104, aniso=(1.0, 0.25))
    hamon = 0.66 + 0.025 * (wav - 0.5) * 2 + 0.008 * np.sin(v * 2 * np.pi * 34)   # (Noah: "make sure the edge is fully finished" - a calm, even temper line)
    bev = np.clip((u - hamon) / 0.015, 0, 1)
    line = np.exp(-((u - hamon) / 0.008) ** 2)
    spine = np.clip(1 - u / 0.22, 0, 1) ** 1.5
    hone = np.clip((u - 0.965) / 0.01, 0, 1)   # (the honed micro-bevel right at the edge: a crisp bright line the whole length)
    d1, d2 = g.cells(1024, 260, 2105)
    craze = np.kron(np.clip(1 - (np.sqrt(d2) - np.sqrt(d1)) / 1.2, 0, 1), np.ones((2, 2))) * (blot > 0.55) * (1 - bev)
    lum = 0.9 + 0.08 * (grain - 0.5) + 0.05 * (fibre - 0.5) + 0.06 * (blot - 0.5) - 0.1 * spine
    # (Noah, 2026-11-21: "you didn't remove the weapon glow on the model itself ... Keep the glow only in the blades dragon language
    # engravings") an older, duller ivory - under the game's light the near-white bone read as lit from within
    col = np.array([196, 184, 158], np.float32) * lum[..., None]
    col = col * (1 - spine[..., None] * 0.25) + np.array([190, 168, 128]) * spine[..., None] * 0.25
    col = col * (1 - bev[..., None]) + np.array([214, 206, 186]) * (0.94 + 0.06 * (fibre[..., None] - 0.5)) * bev[..., None]
    col = col * (1 - hone[..., None]) + np.array([222, 216, 200]) * hone[..., None]
    col = col + np.array([16, 14, 10]) * line[..., None] - np.array([26, 30, 36]) * (np.clip((hamon - u) / 0.05, 0, 1) * (1 - bev))[..., None] * 0.6
    guardstain = np.clip(1 - v / 0.12, 0, 1) ** 2 * (0.4 + 0.6 * blot)
    col = col * (1 - 0.18 * guardstain[..., None]) + np.array([150, 104, 70]) * 0.18 * guardstain[..., None]
    col = col * (1 - 0.35 * craze[..., None]) + np.array([150, 136, 110]) * 0.35 * craze[..., None]
    D = pw.save(col, 'slay_bone_d.dds')
    h = 0.35 * grain + 0.25 * fibre - 0.6 * craze + 0.2 * bev
    spec = np.clip(45 + 30 * bev + 20 * hone + 10 * line - 30 * craze + 10 * (grain - 0.5), 0, 255)   # (Noah: "less shiny")
    N = pw.save(np.dstack([height_to_normal(gaussian_filter(h, 0.7), 5.0), spec]), 'slay_bone_n.dds', True)
    return D, N


def hilt_bone_tex():
    """the guard and pommel: older, yellower bone with the grain of the fang it was cut from"""
    grain = tileable(S // 2, S // 2, 0.6, 2111, aniso=(0.05, 1.0)); blot = tileable(S // 2, S // 2, 1.5, 2112)
    por = tileable(S // 2, S // 2, 0.25, 2113); pores = np.clip((por - 0.7) * 5, 0, 1)
    col = np.array([214, 198, 160], np.float32) * (0.84 + 0.16 * (grain[..., None] - 0.5) + 0.12 * (blot[..., None] - 0.5))
    col = col * (1 - 0.4 * pores[..., None]) + np.array([96, 78, 52]) * 0.4 * pores[..., None]
    D = pw.save(col, 'slay_hiltbone_d.dds')
    N = pw.save(np.dstack([height_to_normal(gaussian_filter(0.5 * grain - 0.6 * pores + 0.3 * blot, 0.7), 6.0), np.clip(100 + 50 * grain - 70 * pores, 0, 255)]), 'slay_hiltbone_n.dds', True)
    return D, N


def scale_field(w, h, cols, rows, seed):
    """black dragon scales laid row over row, each a low dome with a ridge down its middle, each row's rounded free edge over
    the next row's root: (height, edge, tint) over w x h px, tiling both ways (painted over a wider sheet in one fixed order,
    then the middle cut out)"""
    cw, rh = w / cols, h / rows
    P = int(2 * max(rh, cw)) + 2; mh, mw = h + 2 * P, w + 2 * P
    H = np.zeros((mh, mw), np.float32); EDGE = np.zeros((mh, mw), np.float32); TINT = np.zeros((mh, mw), np.float32)
    tint = np.random.default_rng(seed).random((rows, cols))
    for j in range(rows + 2, -3, -1):
        for i in range(-2, cols + 2):
            cx = (i + 0.5 * (j % 2)) * cw + P; cy = j * rh + P
            ax, ay = cw * 0.62, rh * 1.25
            ys = np.arange(max(0, int(cy - 0.15 * ay) - 1), min(mh, int(cy + ay) + 2)); xs = np.arange(max(0, int(cx - ax) - 1), min(mw, int(cx + ax) + 2))
            if not len(ys) or not len(xs): continue
            dy = ((ys - cy) / ay)[:, None]; dx = ((xs - cx) / ax)[None, :]
            rr = dx * dx + dy * dy
            inside = (rr < 1) & (dy > -0.15)
            dome = np.sqrt(np.clip(1 - rr, 0, 1))
            ridge = np.exp(-(dx / 0.12) ** 2) * np.clip(dy, 0, 1) * 0.25
            e = np.clip(1 - (1 - rr) / 0.15, 0, 1)
            sub = (slice(ys[0], ys[-1] + 1), slice(xs[0], xs[-1] + 1))
            H[sub][inside] = (dome + ridge)[inside]; EDGE[sub][inside] = e[inside]; TINT[sub][inside] = tint[j % rows, i % cols]
    return tuple(A[P:P + h, P:P + w] for A in (H, EDGE, TINT))


def scales_tex():
    """the grip's binding: black dragon scales overlapping toward the pommel; a faint oily green-blue sheen. Tiles both ways
    (u round the grip, v along it)"""
    n = 1024
    H, EDGE, TINT = scale_field(n, n, 8, 16, 2121)
    fine = tileable(n, n, 0.3, 2122)
    lum = 0.55 + 0.5 * H - 0.35 * EDGE + 0.1 * (fine - 0.5) + 0.12 * (TINT - 0.5)
    col = np.array([30, 32, 34], np.float32) * lum[..., None]
    col = col + np.array([10, 22, 24]) * (H * (1 - EDGE))[..., None] * 0.6   # (the sheen on each scale's crown)
    D = pw.save(col, 'slay_scales_d.dds')
    N = pw.save(np.dstack([height_to_normal(gaussian_filter(H - 0.4 * EDGE + 0.05 * fine, 0.8), 7.0), np.clip(70 + 120 * H * (1 - EDGE), 0, 255)]), 'slay_scales_n.dds', True)
    return D, N


SCB_U = 0.75   # (the scabbard's sheet: u 0..0.75 its lacquered scales, the whole length down v; u 0.75..1 the bone of its fittings)


def scabbard_tex():
    """(Noah, 2026-11-20: a scabbard so it sheathes) one sheet for the whole scabbard - Skyrim leaves one shape named Scb at the
    hip when the sword is drawn, so the scabbard is one shape on one sheet: down its left three quarters black dragon scales
    under a low lacquer, finer than the grip's and pressed flatter, running the scabbard's length; down its right quarter
    old bone for its mouth, its bands and its tip"""
    Wd, Hh = 1024, 4096
    wb = int(Wd * SCB_U)
    H, EDGE, TINT = scale_field(wb, Hh, 6, 118, 2161)
    fine = tileable(Hh, wb, 0.3, 2162)
    lum = 0.6 + 0.35 * H - 0.3 * EDGE + 0.08 * (fine - 0.5) + 0.1 * (TINT - 0.5)
    body = np.array([26, 26, 28], np.float32) * lum[..., None] + np.array([12, 18, 20]) * (H * (1 - EDGE))[..., None] * 0.5
    grain = tileable(Hh, Wd - wb, 0.6, 2163, aniso=(1.0, 0.05)); blot = tileable(Hh, Wd - wb, 1.5, 2164)
    por = tileable(Hh, Wd - wb, 0.25, 2165); pores = np.clip((por - 0.7) * 5, 0, 1)
    bone = np.array([214, 198, 160], np.float32) * (0.84 + 0.16 * (grain[..., None] - 0.5) + 0.12 * (blot[..., None] - 0.5))
    bone = bone * (1 - 0.4 * pores[..., None]) + np.array([96, 78, 52]) * 0.4 * pores[..., None]
    D = pw.save(np.hstack([body, bone]), 'slay_scb_d.dds')
    hb = gaussian_filter(0.7 * H - 0.3 * EDGE + 0.04 * fine, 0.8); hn = gaussian_filter(0.5 * grain - 0.6 * pores + 0.3 * blot, 0.7)
    nb = height_to_normal(hb, 6.0); nn = height_to_normal(hn, 6.0)
    sb = np.clip(60 + 80 * H * (1 - EDGE), 0, 255); sn = np.clip(80 + 40 * grain - 60 * pores, 0, 255)
    N = pw.save(np.dstack([np.hstack([nb, nn]), np.hstack([sb, sn])]), 'slay_scb_n.dds', True)
    return D, N


def scabbard(M, mat, y0, length, width, curve, mouth=2.0):
    """the scabbard round the blade as pw.blade() builds it (from y0, length long, width wide, its curve): a lacquered tube
    of rounded oval section following the blade's sweep, a bone mouth at the guard, two bone bands, a bone tip closing it,
    and a small bone knob on its face for the cord"""
    def cx(z): s = np.clip((z - y0) / length, 0, 1); return curve * s ** 2 * length
    def half(z): s = np.clip((z - y0) / length, 0, 1); return width * 0.5 * (1 - 0.18 * s) + 0.32, 0.56 - 0.12 * s
    z0, z1 = mouth, y0 + length + 0.9
    nu = 40
    th = np.linspace(0, 2 * np.pi, nu + 1)
    e = 2.0 / 3.2
    def ring(z, grow=1.0, close=1.0):
        a, b = half(z); a, b = a * grow * close, b * grow * close
        c, sn = np.cos(th), np.sin(th)
        return np.stack([cx(z) + a * np.sign(c) * np.abs(c) ** e, b * np.sign(sn) * np.abs(sn) ** e, np.full_like(th, z)], -1)
    def tube(zs, grow, closes, u0, u1, v0, v1):
        V = np.array([ring(z, grow, k) for z, k in zip(zs, closes)])
        UV = np.stack(np.meshgrid(np.linspace(0, 1, len(zs)), th / (2 * np.pi), indexing='ij')[::-1], -1)
        UV = np.stack([u0 + (u1 - u0) * UV[..., 0], v0 + (v1 - v0) * UV[..., 1]], -1)
        du = np.gradient(V, axis=1); dz = np.gradient(V, axis=0)
        if len(zs) < 2: dz = np.zeros_like(V) + [0, 0, 1]
        N = unit(np.cross(du, dz))
        cen = np.array([[cx(z), 0, z] for z in zs])[:, None, :]
        out = V - cen; out[..., 2] = 0
        bad = (N * (out + 1e-9)).sum(-1) < 0
        N[bad] = -N[bad]
        F = np.array(grid_faces(len(zs), nu + 1))
        P_ = V.reshape(-1, 3); Nn = N.reshape(-1, 3)
        fn = np.cross(P_[F[:, 1]] - P_[F[:, 0]], P_[F[:, 2]] - P_[F[:, 0]])
        flip = (fn * Nn[F].mean(1)).sum(1) < 0
        F[flip] = F[flip][:, [0, 2, 1]]
        M.add('Scb', mat, P_, Nn, UV.reshape(-1, 2), F)
    # the lacquered body (its scales: u round it, v down its whole length)
    zs = np.linspace(z0 + 0.6, z1 - 1.5, 160)
    tube(zs, 1.0, np.ones(len(zs)), 0.0, SCB_U, 0.0, 1.0)
    def fitting(za, zb, grow, n=10, cap=None):
        zz = np.linspace(za, zb, n)
        k = np.ones(n)
        if cap == 'end':   # (rounded shut)
            t = np.clip((zz - (zb - 1.0)) / 1.0, 0, 1); k = np.sqrt(np.clip(1 - t * t, 0.0, 1)); k[-1] = 0.0
        if cap == 'mouth':  # (a lip, then shut flat round the blade's slot)
            k[0] = 0.0
        tube(zz, grow, k, SCB_U + 0.01, 0.99, (za % 7) / 7, (za % 7) / 7 + (zb - za) / 12)
    fitting(z0, z0 + 1.4, 1.06, 8, 'mouth')
    fitting(10.5, 11.1, 1.05, 5)
    fitting(30.0, 30.5, 1.05, 5)
    fitting(z1 - 2.8, z1, 1.07, 16, 'end')
    # the knob for the cord, on the scabbard's face below the mouth
    zk = mouth + 5.0; a, b = half(zk)
    V, Nn, UV, F = pw.superbox(np.array([cx(zk) - a * 0.35, b + 0.18, zk]), 0.35, 0.25, 0.6, 3.0, 16, 8)
    UV = np.asarray(UV) % 1.0 * [0.2, 0.05] + [SCB_U + 0.02, 0.5]
    M.add('Scb', mat, V, Nn, UV, F)


VANILLA = ROOT / 'build' / 'vanilla'   # (the player's own Skyrim, read out of its archives - never committed as it is)


def skull_tex():
    """(Noah, 2026-11-21: "Match the bone colour") Skyrim's DragonSkeleton texture recoloured to the blade's ivory bone: its own
    light and shade and every crack kept, its dirt-brown taken to ivory - read from build/vanilla (the player's own Skyrim)"""
    src = VANILLA / 'dragonskeleton_2k.png'
    if not src.exists():
        subprocess.run(['convert', str(VANILLA / 'dragonskeleton.dds'), '-resize', '2048x2048', str(src)], check=True)
    from PIL import Image
    a = np.asarray(Image.open(src).convert('RGB'), np.float32)
    lum = a @ np.array([0.299, 0.587, 0.114], np.float32)
    lo, hi = np.percentile(lum, 2), np.percentile(lum, 98)
    t = np.clip((lum - lo) / max(1.0, hi - lo), 0, 1)
    ivory = np.array([206, 194, 164], np.float32)
    col = ivory * (0.32 + 0.78 * t[..., None] ** 0.9)
    col = col * 0.88 + a * (col.mean() / max(1.0, a.mean())) * 0.12   # (a breath of its own staining kept)
    return pw.save(np.clip(col, 0, 255), 'slay_skull_d.dds')


def skull(M):
    """(Noah, 2026-11-21: "use a skyrim dragons actual skull shrunk down and aligned" - "Merge into our sword file") Skyrim's own
    dragon skull (meshes\\clutter\\deadanimals\\dragonbonesskull01.nif, its own DragonSkeleton textures) shrunk to the guard: its
    snout along the blade, its crown over the spine, the blade running out of its mouth; the back of its head over the grip.
    Read out of Skyrim - Meshes0.bsa into build/vanilla (tools/bsalz4.py) and turned to OBJ (tools/modelconv/shapeobj)"""
    src = VANILLA / 'skull.obj'
    if not src.exists():
        subprocess.run([tool('shapeobj'), str(VANILLA / 'dragonbonesskull01.nif'), str(src)], check=True)
    V, T, N, F = [], [], [], []
    for l in open(src):
        if l.startswith('v '): V.append([float(x) for x in l.split()[1:4]])
        elif l.startswith('vt '): T.append([float(x) for x in l.split()[1:3]])
        elif l.startswith('vn '): N.append([float(x) for x in l.split()[1:4]])
        elif l.startswith('f '): F.append([int(x.split('/')[0]) - 1 for x in l.split()[1:4]])
    V, T, N, F = np.array(V), np.array(T), np.array(N), np.array(F)
    # (Noah, 2026-11-21: "Angle the jaw up more so the blade is coming from the center of the dragon skull") its lower jaw (its own
    # pieces in the model: the jaw's two parts and its teeth) swung up about its hinge, closing on the blade
    from scipy.sparse import coo_matrix
    from scipy.sparse.csgraph import connected_components
    r_, c_ = np.r_[F[:, 0], F[:, 1], F[:, 2]], np.r_[F[:, 1], F[:, 2], F[:, 0]]
    _, lab = connected_components(coo_matrix((np.ones(len(r_)), (r_, c_)), shape=(len(V), len(V))), directed=False)
    cz = np.array([V[lab == q, 2].mean() for q in range(lab.max() + 1)])
    cy = np.array([V[lab == q, 1].mean() for q in range(lab.max() + 1)])
    top = np.array([V[lab == q, 2].max() for q in range(lab.max() + 1)])
    size = np.bincount(lab)
    jawpart = ((size > 50) & (top < 275) & (cz < 245)) | ((size < 20) & (top < 215))   # (the jaw's two halves, and the teeth along them)
    jaw = jawpart[lab]
    a = np.radians(SKULL_JAW_UP)
    py, pz = 268.0, 255.0                                      # (its hinge)
    dy, dz = V[jaw, 1] - py, V[jaw, 2] - pz
    V[jaw, 1], V[jaw, 2] = py + dy * np.cos(a) - dz * np.sin(a), pz + dy * np.sin(a) + dz * np.cos(a)
    ny, nz = N[jaw, 1].copy(), N[jaw, 2].copy()
    N[jaw, 1], N[jaw, 2] = ny * np.cos(a) - nz * np.sin(a), ny * np.sin(a) + nz * np.cos(a)
    V[jaw, 2] -= SKULL_JAW_DROP
    # (Noah, 2026-11-21: "Lower the back of the skull, open the jaws more and make the skull flush with the bottom jaw on one side
    # of the blade and the top jaw on the other side") the whole skull pitched snout-up about its mouth, the back of its head down
    # toward the grip; its palate then lies along the blade's spine and its chin along the edge (the jaw opened to the blade's width)
    b = np.radians(SKULL_PITCH)
    py, pz = 300.0, 240.0
    dy, dz = V[:, 1] - py, V[:, 2] - pz
    V[:, 1], V[:, 2] = py + dy * np.cos(b) - dz * np.sin(b), pz + dy * np.sin(b) + dz * np.cos(b)
    ny, nz = N[:, 1].copy(), N[:, 2].copy()
    N[:, 1], N[:, 2] = ny * np.cos(b) - nz * np.sin(b), ny * np.sin(b) + nz * np.cos(b)
    k = SKULL_SCALE
    mouth_z, back_y = SKULL_MOUTH, 239.0   # (its mouth's line, and the back of its head, in its own space: z up, its snout toward +y)
    P = np.stack([-(V[:, 2] - mouth_z) * k, -V[:, 0] * k, (V[:, 1] - back_y) * k + SKULL_BACK], -1)
    NN = np.stack([-N[:, 2], -N[:, 0], N[:, 1]], -1)
    UV = np.stack([T[:, 0], 1.0 - T[:, 1]], -1)
    M.add('Skull', 'vskull', P, NN, UV, F)


SKULL_SCALE = 0.051    # (Noah, 2026-11-21: "Bigger, wrapped round blade" - 144 long in its own units: 7.3 model units here, 11.8 in the game)
SKULL_JAW_UP = 10.0    # (degrees the jaw is swung up about its hinge: its top line then parallel to its palate's)
SKULL_JAW_DROP = 34.0  # (and lowered, the jaws held apart by the blade's width: its palate on the spine, its jaw along the edge)
SKULL_PITCH = 26.0     # (degrees the whole skull is tipped snout-up: its palate along the blade's spine, the back of its head lowered)
SKULL_MOUTH = 234.0    # (the blade's middle line through it, in its own height: its palate 255 and its jaw 213, the blade 42 wide)
SKULL_BACK = -1.35     # (the back of its head down over the grip's top, its horns swept back over the grip)


def build():
    bD, bN = bone_blade_tex()
    hD, hN = hilt_bone_tex()
    sD, sN = scales_tex()
    cD, cN = scabbard_tex()
    kD = skull_tex()
    rD, rN, rG = pw.glyph_strip('slay_runes', (196, 184, 156), glow_rgb=(255, 112, 36), seed=2131, lit=(214, 100, 42))
    iD, iN = pw.metal_tex('slay_iron', (52, 50, 48), dark=0.9, seed=2141)
    M = Mesh()
    # (the blade's material 'dbone', not 'bone': kept out of roughen()'s forge dents, so its edge runs true and keen)
    # the blade: single-edged (its spine at -x, its keen edge at +x), curving back toward its spine so the edge rides the
    # outside of the curve as a katana's does, a short sharp point; the letters cut along the spine
    pw.blade(M, 'dbone', 1.5, 46.1, 2.15, 0.55, tip=0.1, seed=2151, ns=150, curve=-0.055, single=0.3,
             runes=('runes', 0.28, 0.11, 0.72, 9.6, -0.42))
    # (Noah, 2026-11-20) the scabbard, round the blade: one shape named Scb, left at the hip when the sword is drawn - its mouth
    # just past the skull's snout
    scabbard(M, 'scb', 1.5, 46.1, 2.15, -0.055, mouth=6.1)
    # (Noah, 2026-11-21: "redesign the hilt (where the blade connects to the handle)" - "Dragon skull guard", "use a skyrim dragons
    # actual skull") Skyrim's own dragon skull round the blade's root
    skull(M)
    # the grip: iron collars at each end, between them the binding of black scales
    M.add('Iron', 'iron', *pw.lathe([(1.5, 0.86), (1.3, 0.94), (0.95, 0.9)], 24))
    M.add('Grip', 'scales', *pw.lathe([(1.0, 0.82)] + [(z, 0.82 + 0.06 * math.sin(math.pi * (1.0 - z) / 16.0)) for z in np.linspace(0.95, -14.4, 30)] + [(-14.5, 0.82)], 28,
                                      uvs=(1.0, 3.0, 0.0, 0.0)))
    M.add('Iron', 'iron', *pw.lathe([(-14.4, 0.9), (-14.7, 0.98), (-15.0, 0.9)], 24))
    # the pommel: a cap of bone, a small claw of iron gripping it
    pc = -15.7
    M.add('Pommel', 'hiltbone', *pw.lathe([(pc + 0.75 * math.cos(a), 0.85 * math.sin(a)) for a in np.linspace(0, math.pi, 14)], 22))
    for k in range(3):
        a = 2 * math.pi * k / 3 + 0.4
        d = np.array([math.cos(a), math.sin(a), 0.0])
        pw.talon(M, 'iron', np.array([d[0] * 0.75, d[1] * 0.75, -15.0]), d * 0.4 + np.array([0, 0, -0.7]), -d * 0.8 + np.array([0, 0, -0.5]), 1.5, 0.16)
    mats = {'dbone': f'diffuse={bD} normal={bN} spec=0.3 gloss=28 alpha=0 doublesided=0',
            'runes': f'diffuse={rD} normal={rN} glow={rG} emit=1,0.47,0.16 emitmult=1.7 spec=0.4 gloss=40 alpha=blend doublesided=0',
            'hiltbone': f'diffuse={hD} normal={hN} spec=0.3 gloss=22 alpha=0 doublesided=0',
            'vskull': f'diffuse={kD} normal=actors\\dragon\\DragonSkeleton_n.dds spec=0.3 gloss=24 alpha=0 doublesided=0',
            'scales': f'diffuse={sD} normal={sN} spec=0.5 gloss=40 alpha=0 doublesided=0',
            'scb': f'diffuse={cD} normal={cN} spec=0.45 gloss=30 alpha=0 doublesided=0',
            'iron': f'diffuse={iD} normal={iN} spec=0.45 gloss=24 alpha=0 doublesided=0'}
    # (Noah, 2026-11-21: "like my fist isn't fully closed around the handle" in third person) the hand's pose is Animated Armoury's
    # katana's, closed round its oval grip - 2.6 across the edge's way, 1.65 through the flat's: ours was round, 2.8 both ways. The
    # grip, its collars and the pommel pressed to that oval (through the flat: model y)
    flat = 0.6
    G = []
    for gn, m, V, N, UV, F in M.groups:
        V = np.asarray(V, float).copy(); N = np.asarray(N, float).copy()
        sel = np.zeros(len(V), bool)
        if m in ('scales', 'iron'): sel = V[:, 2] < 1.55            # (the grip's binding and its iron collars and claws)
        if m == 'hiltbone': sel = V[:, 2] < -14.0                    # (the pommel)
        V[sel, 1] *= flat
        N[sel, 1] /= flat
        N[sel] /= np.linalg.norm(N[sel], axis=1, keepdims=True) + 1e-9
        G.append((gn, m, V, N, UV, F))
    M.groups = G
    # (Noah, 2026-11-21: "the sword edge is facing my player instead of away from it") the whole sword turned half round its own
    # length - the edge leading, the letters on the spine toward you
    M.groups = [(gn, m, np.asarray(V, float) * [-1, -1, 1], np.asarray(N, float) * [-1, -1, 1], UV, F) for gn, m, V, N, UV, F in M.groups]
    obj = pw.finish(M, 'dragonslayer', mats, None, 'WeaponBack')   # (Noah, 2026-11-21: two-handed - carried on the back)
    nif = str(pw.MESHDIR / 'dragonslayer.nif')
    subprocess.run([tool('shapename'), nif, nif, 'Scb:', 'Scb'], check=True)   # (the scabbard: Skyrim's own name for it)
    return obj


if __name__ == '__main__':
    pw.WORK.mkdir(parents=True, exist_ok=True)
    tool('addprn')
    build()
    if '--keep' not in sys.argv:
        shutil.rmtree(pw.WORK, ignore_errors=True)
