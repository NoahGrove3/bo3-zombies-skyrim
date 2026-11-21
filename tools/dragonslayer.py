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
    col = np.array([226, 216, 192], np.float32) * lum[..., None]
    col = col * (1 - spine[..., None] * 0.25) + np.array([190, 168, 128]) * spine[..., None] * 0.25
    col = col * (1 - bev[..., None]) + np.array([244, 240, 228]) * (0.94 + 0.06 * (fibre[..., None] - 0.5)) * bev[..., None]
    col = col * (1 - hone[..., None]) + np.array([250, 248, 240]) * hone[..., None]
    col = col + np.array([34, 30, 22]) * line[..., None] - np.array([26, 30, 36]) * (np.clip((hamon - u) / 0.05, 0, 1) * (1 - bev))[..., None] * 0.6
    guardstain = np.clip(1 - v / 0.12, 0, 1) ** 2 * (0.4 + 0.6 * blot)
    col = col * (1 - 0.18 * guardstain[..., None]) + np.array([150, 104, 70]) * 0.18 * guardstain[..., None]
    col = col * (1 - 0.35 * craze[..., None]) + np.array([150, 136, 110]) * 0.35 * craze[..., None]
    D = pw.save(col, 'slay_bone_d.dds')
    h = 0.35 * grain + 0.25 * fibre - 0.6 * craze + 0.2 * bev
    spec = np.clip(70 + 50 * bev + 40 * hone + 20 * line - 40 * craze + 12 * (grain - 0.5), 0, 255)   # (Noah: "less shiny")
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


def scabbard(M, mat, y0, length, width, curve):
    """the scabbard round the blade as pw.blade() builds it (from y0, length long, width wide, its curve): a lacquered tube
    of rounded oval section following the blade's sweep, a bone mouth at the guard, two bone bands, a bone tip closing it,
    and a small bone knob on its face for the cord"""
    def cx(z): s = np.clip((z - y0) / length, 0, 1); return curve * s ** 2 * length
    def half(z): s = np.clip((z - y0) / length, 0, 1); return width * 0.5 * (1 - 0.18 * s) + 0.32, 0.56 - 0.12 * s
    z0, z1 = 2.0, y0 + length + 0.9
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
    zk = 7.0; a, b = half(zk)
    V, Nn, UV, F = pw.superbox(np.array([cx(zk) - a * 0.35, b + 0.18, zk]), 0.35, 0.25, 0.6, 3.0, 16, 8)
    UV = np.asarray(UV) % 1.0 * [0.2, 0.05] + [SCB_U + 0.02, 0.5]
    M.add('Scb', mat, V, Nn, UV, F)


def build():
    bD, bN = bone_blade_tex()
    hD, hN = hilt_bone_tex()
    sD, sN = scales_tex()
    cD, cN = scabbard_tex()
    rD, rN, rG = pw.glyph_strip('slay_runes', (196, 184, 156), glow_rgb=(255, 112, 36), seed=2131, lit=(214, 100, 42))
    iD, iN = pw.metal_tex('slay_iron', (52, 50, 48), dark=0.9, seed=2141)
    M = Mesh()
    # (the blade's material 'dbone', not 'bone': kept out of roughen()'s forge dents, so its edge runs true and keen)
    # the blade: single-edged (its spine at -x, its keen edge at +x), curving back toward its spine so the edge rides the
    # outside of the curve as a katana's does, a short sharp point; the letters cut along the spine
    pw.blade(M, 'dbone', 2.6, 45.0, 2.15, 0.55, tip=0.1, seed=2151, ns=150, curve=-0.055, single=0.3,
             runes=('runes', 0.28, 0.06, 0.66, 9.6, -0.42))
    # (Noah, 2026-11-20) the scabbard, round the blade: one shape named Scb, left at the hip when the sword is drawn
    scabbard(M, 'scb', 2.6, 45.0, 2.15, -0.055)
    # the collar where the blade meets the guard: blackened iron
    M.add('Iron', 'iron', *pw.box(np.array([0.0, 0.0, 2.3]), 1.15, 0.42, 0.35))
    # the guard: an oval of bone, and two fangs rising from its sides, curving in toward the blade
    M.add('Guard', 'hiltbone', *pw.superbox(np.array([0, 0, 1.7]), 2.3, 1.75, 0.22, 3.0, 28, 12))
    for side in (-1, 1):
        base = np.array([side * 1.75, 0.0, 1.8])
        pw.talon(M, 'hiltbone', base, np.array([side * 0.45, 0, 1.0]), np.array([-side * 0.25, 0, 1.0]), 5.0, 0.42, 18)
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
    mats = {'dbone': f'diffuse={bD} normal={bN} spec=0.55 gloss=40 alpha=0 doublesided=0',
            'runes': f'diffuse={rD} normal={rN} glow={rG} emit=1,0.47,0.16 emitmult=1.7 spec=0.4 gloss=40 alpha=blend doublesided=0',
            'hiltbone': f'diffuse={hD} normal={hN} spec=0.3 gloss=22 alpha=0 doublesided=0',
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
    obj = pw.finish(M, 'dragonslayer', mats, None, 'WeaponSword')
    nif = str(pw.MESHDIR / 'dragonslayer.nif')
    subprocess.run([tool('shapename'), nif, nif, 'Scb:', 'Scb'], check=True)   # (the scabbard: Skyrim's own name for it)
    return obj


if __name__ == '__main__':
    pw.WORK.mkdir(parents=True, exist_ok=True)
    tool('addprn')
    build()
    if '--keep' not in sys.argv:
        shutil.rmtree(pw.WORK, ignore_errors=True)
