"""(Noah: the Mutagen Gauntlet - "Alchemist's glove", "make it very very high quality 2k textures and hyper realistic with
passive animations around the gauntlet") the Alchemist's wonder, modelled here from scratch (no one else's mesh): a
fingerless oxblood leather glove fitted to Skyrim's hand, a stitched cuff with two buckled straps, six glass vials of the
elements tucked under the straps (fire, frost, shock, acid, holy, void), six glass tubes carrying each element's light up
over the hand into a glass orb set in a brass bezel in the palm, and a brass plate on the back of the hand engraved with
the alchemists' circle round a small lens.

Passive animation (all looping from load, no code): the orb's two swirling shells of the six colours turning against
each other, its pulsing core and halo; light pulses running up every tube into the orb; bubbles rising in every vial;
the lens's swirl; and motes of the six colours circling the wrist.

Hung by src/WonderProps.inl (TickGauntlet) on the hand bone that holds it - "NPC R Hand [RHnd]" or "NPC L Hand [LHnd]" -
in first person and third. The model is in that bone's own frame (read from Skyrim's 1stpersonmalehands_1.nif: fingers
up +Z, the back of the hand +Y, the thumb's side +X on the right hand, -X on the left; tools/data/gauntlet_fit.json holds
the hand's measured cross-sections the glove is fitted over). The weapon in the hand (UN_Flask_philosopher) shows
gauntlet_hold.nif: nothing visible.

    python3 tools/gauntlet.py [--preview <png>]
      -> assets/fx/Meshes/CurseOfAlduin/alchemist/gauntlet_r.nif, gauntlet_l.nif, gauntlet_hold.nif
         assets/fx/Textures/CurseOfAlduin/alchemist/gauntlet_*.dds   (2048: leather, brass; 1024: glass, liquid, fx)

Pipeline: mesh -> OBJ -> modelconv (lit shapes) -> fxwrite base= (the additive, scrolling and flipbook shapes)."""
import json, math, pathlib, shutil, subprocess, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFilter
from scipy.ndimage import gaussian_filter, map_coordinates

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from throwdagger import Mesh, unit, grid_faces, tileable, height_to_normal, tool

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT = ROOT / 'assets' / 'fx'
MESHDIR = OUT / 'Meshes' / 'CurseOfAlduin' / 'alchemist'
TEXDIR = OUT / 'Textures' / 'CurseOfAlduin' / 'alchemist'
TEXREL = 'CurseOfAlduin\\alchemist\\'
WORK = ROOT / 'build' / 'gauntlet'
TOOLS = pathlib.Path('/home/claude/tools')
FIT = json.load(open(ROOT / 'tools' / 'data' / 'gauntlet_fit.json'))

# the six elements in the cluster's order (src/Classes.cpp): fire, frost, shock, acid, holy, void
ELS = ['fire', 'frost', 'shock', 'acid', 'holy', 'void']
ELC = {'fire': (1.0, 0.42, 0.08), 'frost': (0.45, 0.82, 1.0), 'shock': (0.68, 0.42, 1.0), 'acid': (0.45, 1.0, 0.12),
       'holy': (1.0, 0.82, 0.32), 'void': (0.72, 0.82, 1.0)}
rng = np.random.default_rng(5150)

# ------------------------------------------------------------------ the hand's envelope
C0 = np.array(FIT['c'])
FZ = np.array(FIT['z'])
FR = np.array(FIT['r'])                    # rows z, 72 columns round from +X (0) through +Y (back, 90deg)
NT = FR.shape[1]


def env(theta, z):
    """the hand's surface distance from the axis (C0) at angle theta, height z (bilinear, wrapped round)"""
    theta, z = np.broadcast_arrays(np.asarray(theta, float), np.asarray(z, float))
    shp = theta.shape
    j = (theta.ravel() % (2 * np.pi)) / (2 * np.pi) * NT
    i = np.interp(np.clip(z.ravel(), FZ[0], FZ[-1]), FZ, np.arange(len(FZ)))
    return map_coordinates(np.pad(FR, ((0, 0), (0, 1)), mode='wrap'), [i, j], order=1, mode='nearest').reshape(shp)


def surf(theta, z, lift):
    """a point on (lift units off) the glove's skin: the hand's envelope, eased fuller over the wrist"""
    theta, z = np.broadcast_arrays(np.asarray(theta, float), np.asarray(z, float))
    r = env(theta, z) + 0.42 + lift
    return np.stack([C0[0] + r * np.cos(theta), C0[1] + r * np.sin(theta), z], -1)


def grid_n(P, wrap=True):
    dr = np.gradient(P, axis=0)
    dc = (np.roll(P, -1, 1) - np.roll(P, 1, 1)) if wrap else np.gradient(P, axis=1)
    return unit(np.cross(dc, dr))


def outward(N, P, centre):
    """normals turned to point away from the axis line through centre (x, y) along Z"""
    o = P.copy(); o[..., 0] -= centre[0]; o[..., 1] -= centre[1]; o[..., 2] = 0
    s = np.sign((N * o).sum(-1, keepdims=True)); s[s == 0] = 1
    return N * s


def seam_grid(P, N, UV):
    """a wrapped grid (rows, cols) made into a mesh with its seam column repeated (so the texture doesn't smear back)"""
    P = np.concatenate([P, P[:, :1]], 1); N = np.concatenate([N, N[:, :1]], 1)
    return P.reshape(-1, 3), N.reshape(-1, 3), UV.reshape(-1, 2), grid_faces(P.shape[0], P.shape[1])


# ------------------------------------------------------------------ the glove
NC = 96   # columns round the hand
TH = np.linspace(0, 2 * np.pi, NC, endpoint=False)
THUMB_A = np.array([2.86, -0.92, 3.17]) - np.array([-1.69, 1.64, -2.22]) * 0.35   # the thumb's root, a little back
THUMB_B = np.array([4.55, -2.56, 5.39])                                           # its first joint
THUMB_TH = math.atan2((THUMB_A[1] + THUMB_B[1]) / 2 - (-1.0), (THUMB_A[0] + THUMB_B[0]) / 2 - 0.3)   # the thumb's way out, round the hand


def glove(M):
    """the fingerless glove: the hand's envelope from the wrist (z -0.6) to just short of the knuckles, cut on a slant
    (the back reaches the knuckles, the palm stops at its crease) - single leather, both faces drawn - a hole for the
    thumb. UV: u round (+X = 0), v up the leather sheet's glove band (0.02 .. 0.54)"""
    nr = 44
    z0 = -0.6
    yback = np.sin(TH)
    zend = 6.55 + 0.75 * np.clip(yback, 0, 1) - 0.25 * np.clip(-yback, 0, 1)
    # (the thumb leaves through a notch cut down the glove's side, not a hole: it never pulls through the leather)
    dth = np.angle(np.exp(1j * (TH - THUMB_TH)))
    zend = zend - (zend - 2.7) * np.exp(-(dth / 0.62) ** 2)
    t = np.linspace(0, 1, nr)[:, None]
    Z = z0 + t * (zend[None, :] - z0)
    T = np.broadcast_to(TH, Z.shape)
    P = surf(T, Z, 0.0)
    N = outward(grid_n(P), P, C0)
    U = np.linspace(0, 1, NC + 1)
    V = 0.02 + 0.52 * (np.concatenate([Z, Z[:, :1]], 1) - z0) / 8.0
    UV = np.stack([np.broadcast_to(U, V.shape), V], -1)
    Vt, Nt, UVt, F = seam_grid(P, N, UV)
    M.add('Glove', 'leather', Vt, Nt, UVt, F)
    # the hem: a rolled lip round the top edge, and one round the thumb's hole
    lip = P[-1]
    hem_tube(M, np.concatenate([lip, lip[:1]]), 0.13, 'leather', (0.55, 0.56), closed=True)
    F = np.array(F)
    return P


def tube_frames(path):
    T = np.gradient(path, axis=0); T = unit(T)
    ref = np.array([0, 0, 1.0]) if abs(T[0][2]) < 0.9 else np.array([1.0, 0, 0])
    Nn = unit(np.cross(T[0], ref)); out = []
    for k in range(len(path)):
        Nn = unit(Nn - T[k] * (Nn @ T[k]))
        out.append((Nn, np.cross(T[k], Nn)))
    return T, out


def tube(path, r, nseg=12, vrange=(0, 1), urange=(0, 1)):
    """a tube swept along a path (rows along it); r a number or one per point. UV: u round, v along"""
    path = np.asarray(path, float)
    T, fr = tube_frames(path)
    r = np.broadcast_to(np.asarray(r, float), (len(path),))
    a = np.linspace(0, 2 * np.pi, nseg + 1)
    P = np.array([[path[k] + r[k] * (fr[k][0] * np.cos(x) + fr[k][1] * np.sin(x)) for x in a] for k in range(len(path))])
    N = np.array([[fr[k][0] * np.cos(x) + fr[k][1] * np.sin(x) for x in a] for k in range(len(path))])
    L = np.concatenate([[0], np.cumsum(np.linalg.norm(np.diff(path, axis=0), axis=1))])
    vv = vrange[0] + (vrange[1] - vrange[0]) * L / max(L[-1], 1e-6)
    uu = urange[0] + (urange[1] - urange[0]) * a / (2 * np.pi)
    UV = np.stack(np.meshgrid(uu, vv), -1)
    return P.reshape(-1, 3), N.reshape(-1, 3), UV.reshape(-1, 2), grid_faces(len(path), nseg + 1)


def hem_tube(M, path, r, mat, vband, closed=False):
    M.add('Hem', mat, *tube(path, r, 8, vrange=(vband[0], vband[1]), urange=(0, 30)))


def spline(pts, n):
    """Catmull-Rom through the points, n samples"""
    pts = np.asarray(pts, float)
    P = np.vstack([pts[0] * 2 - pts[1], pts, pts[-1] * 2 - pts[-2]])
    out = []
    seg = len(pts) - 1
    for k in range(n):
        u = k / (n - 1) * seg; i = min(int(u), seg - 1); t = u - i
        p0, p1, p2, p3 = P[i], P[i + 1], P[i + 2], P[i + 3]
        out.append(0.5 * ((2 * p1) + (-p0 + p2) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t * t + (-p0 + 3 * p1 - 3 * p2 + p3) * t ** 3))
    return np.array(out)


# ------------------------------------------------------------------ the cuff
CUFF_Z = (-4.7, 0.9)


def cuff_base(theta, z):
    """the cuff's inner line: the wrist's ring (z 0.2) a touch fuller, flaring a little toward the elbow"""
    return env(theta, 0.2) + 0.62 + 0.30 * np.clip((0.9 - z) / 5.6, 0, 1)


def cuff_pt(theta, z, d):
    r = cuff_base(theta, z) + d
    return np.stack([C0[0] + r * np.cos(theta), C0[1] + r * np.sin(theta), np.broadcast_to(z, np.shape(r))], -1)


def cuff(M):
    """the cuff: thick leather swept from a profile (inner face, a rolled lip at each end, the outer face). UV: u round,
    v down the leather sheet's cuff band (0.58 .. 0.82)"""
    zb, zt = CUFF_Z
    prof = [(0.0, zt - 0.05)]
    for a in np.linspace(0, np.pi, 9):      # the top lip, rolled over
        prof.append((0.17 - 0.17 * math.cos(a), zt + 0.17 * math.sin(a)))
    prof.append((0.34, zt - 0.05))
    for z in np.linspace(zt - 0.4, zb + 0.4, 18):
        prof.append((0.34 + 0.03 * math.sin((z - zb) * 1.3), z))
    prof.append((0.34, zb + 0.05))
    for a in np.linspace(0, np.pi, 9):      # the bottom lip
        prof.append((0.17 + 0.17 * math.cos(a), zb - 0.17 * math.sin(a)))
    prof.append((0.0, zb + 0.05))
    prof = np.array(prof)
    P = np.array([[cuff_pt(t, z, d) for t in TH] for d, z in prof])
    N = outward(grid_n(P), P, C0)
    inner = prof[:, 0] < 0.02
    N[(np.arange(len(prof)) == 0) | (np.arange(len(prof)) == len(prof) - 1)] *= -1
    L = np.concatenate([[0], np.cumsum(np.linalg.norm(np.diff(prof, axis=0), axis=1))])
    vv = 0.58 + 0.24 * L / L[-1]
    UV = np.stack(np.meshgrid(np.linspace(0, 2, NC + 1), vv), -1)
    M.add('Cuff', 'leather', *seam_grid(P, N, UV))


# ------------------------------------------------------------------ vials, straps, buckles
VIAL_TH = np.radians([28, 54, 80, 106, 132, 158])     # round the back of the cuff (+Y), the thumb's side first
VIAL_Z = (-4.1, -0.75)
VIAL_R = 0.44
VIAL_D = 0.34 + VIAL_R + 0.06                          # out from the cuff's inner line: on its outer face
STRAP_Z = (-3.45, -1.55)


def vial_axis(k):
    t = VIAL_TH[k]
    c = cuff_pt(t, (VIAL_Z[0] + VIAL_Z[1]) / 2, VIAL_D)
    return np.array([c[0], c[1]]), t


def lathe_z(prof, cx, cy, nc=24, u0=0.0, u1=1.0, v0=0.0, v1=1.0):
    """a turned part round a line along Z through (cx, cy): profile (z, r)"""
    prof = np.asarray(prof, float)
    th = np.linspace(0, 2 * np.pi, nc + 1)
    P = np.zeros((len(prof), nc + 1, 3))
    P[..., 0] = cx + prof[:, 1:2] * np.cos(th)
    P[..., 1] = cy + prof[:, 1:2] * np.sin(th)
    P[..., 2] = prof[:, 0:1]
    dz = np.gradient(prof[:, 0]); dr = np.gradient(prof[:, 1])
    nr, nz = dz, -dr
    l = np.hypot(nr, nz) + 1e-9; nr, nz = nr / l, nz / l
    N = np.zeros_like(P)
    N[..., 0] = nr[:, None] * np.cos(th); N[..., 1] = nr[:, None] * np.sin(th); N[..., 2] = nz[:, None]
    L = np.concatenate([[0], np.cumsum(np.hypot(np.diff(prof[:, 0]), np.diff(prof[:, 1])))])
    UV = np.stack(np.meshgrid(u0 + (u1 - u0) * th / (2 * np.pi), v0 + (v1 - v0) * L / max(L[-1], 1e-6)), -1)
    return P.reshape(-1, 3), N.reshape(-1, 3), UV.reshape(-1, 2), grid_faces(len(prof), nc + 1)


NSL = 6   # the liquid in each vial in this many stacked slices: the code hides them from the top down as the vial drains


def vial_el(k, mirror):
    """(Noah: "cycle through all six left to right") the element in vial k (counted from the thumb's side): seen from
    the back of the hand held out, left to right is fire .. void on either hand - the right hand's thumb is on the left,
    the left hand's on the right, so its vials run the other way"""
    return 5 - k if mirror else k


def vials(M, mirror=False):
    """each vial: a glass tube with a rounded foot, a waist and a lipped neck; the liquid inside (its column of the
    liquid sheet); a brass cap with a tube spigot; a brass foot cup"""
    tops = []
    z0, z1 = VIAL_Z
    for k in range(6):
        (cx, cy), t = vial_axis(k)
        R = VIAL_R
        prof = [(z0 + 0.02, 0.0), (z0 + 0.05, R * 0.55), (z0 + 0.15, R * 0.88), (z0 + 0.32, R)] + \
               [(z, R) for z in np.linspace(z0 + 0.5, z1 - 0.75, 6)] + \
               [(z1 - 0.55, R * 0.92), (z1 - 0.4, R * 0.62), (z1 - 0.25, R * 0.58), (z1 - 0.18, R * 0.7), (z1 - 0.1, R * 0.7), (z1 - 0.05, R * 0.55)]
        M.add('Glass', 'glass', *lathe_z(prof, cx, cy, 28))
        e = vial_el(k, mirror)
        fill = z0 + 0.12 + (z1 - z0 - 0.9) * 0.84
        # the liquid in NSL slices (Liq<element>_<slice>, the bottom first), each closed at its top: the bottom one the rounded
        # foot; src/WonderProps.inl hides them from the top as the vial drains and shows them again as it fills
        zs = np.linspace(z0 + 0.36, fill, NSL + 1)
        for j in range(NSL):
            lo_, hi_ = zs[j], zs[j + 1]
            if j == 0:
                lp = [(z0 + 0.07, 0.0), (z0 + 0.1, R * 0.5), (z0 + 0.2, R * 0.8), (lo_, R * 0.9)]
            else:
                lp = [(lo_ + 0.002, R * 0.9)]
            lp = lp + [(hi_ - 0.01, R * 0.9), (hi_, R * 0.86), (hi_ + 0.002, 0.0)]
            M.add(f'Liq{e}_{j}', 'liquid', *lathe_z(lp, cx, cy, 20, u0=e / 6 + 0.01, u1=(e + 1) / 6 - 0.01, v0=j / NSL, v1=(j + 1) / NSL))
        # the brass cap (a domed cap, a knurled band) and the spigot the tube leaves from
        cp = [(z1 - 0.2, R * 0.74), (z1 - 0.2, R * 0.82), (z1 + 0.05, R * 0.82), (z1 + 0.08, R * 0.78), (z1 + 0.2, R * 0.62),
              (z1 + 0.3, R * 0.3), (z1 + 0.5, R * 0.2), (z1 + 0.7, R * 0.22), (z1 + 0.72, 0.0)]
        M.add('Brass', 'brass', *lathe_z(cp, cx, cy, 24, u0=0.02, u1=0.3, v0=0.05 + 0.05 * k, v1=0.08 + 0.05 * k))
        fp = [(z0 - 0.15, 0.0), (z0 - 0.15, R * 0.75), (z0 - 0.1, R * 1.06), (z0 + 0.25, R * 1.08), (z0 + 0.3, R * 1.0)]
        M.add('Brass', 'brass', *lathe_z(fp, cx, cy, 24, u0=0.02, u1=0.3, v0=0.4 + 0.05 * k, v1=0.43 + 0.05 * k))
        tops.append(np.array([cx, cy, z1 + 0.68]))
    return tops


def straps(M):
    """two straps round the cuff, riding up over the vials (they hold them), each with a brass buckle on the thumb's side
    (-30deg: under the wrist's turn) and its tongue tucked through a keeper. UV: the leather sheet's strap band"""
    for si, zc in enumerate(STRAP_Z):
        w = 0.36
        bump = np.zeros(NC)
        for t in VIAL_TH:
            dth = np.angle(np.exp(1j * (TH - t)))
            bump = np.maximum(bump, (VIAL_R * 2 + 0.06) * np.exp(-(dth / 0.17) ** 4))
        d0 = 0.36 + bump
        prof = [(0.0, -w), (0.1, -w - 0.02), (0.12, 0.0), (0.1, w + 0.02), (0.0, w)]
        P = np.array([[cuff_pt(TH[j], zc + dz, d0[j] + dd) for j in range(NC)] for dd, dz in prof])
        N = outward(grid_n(P), P, C0)
        N[0] = [0, 0, -1]; N[-1] = [0, 0, 1]
        UV = np.stack(np.meshgrid(np.linspace(0, 6, NC + 1), np.linspace(0.84 + 0.05 * si, 0.88 + 0.05 * si, len(prof))), -1)
        M.add('Strap', 'leather', *seam_grid(P, N, UV))
        buckle(M, math.radians(-28), zc, 0.36 + 0.12)


def box(c, ex, ey, ez, ax, ay, az):
    """a box centred at c with half-extents along the axes ax, ay, az (unit vectors)"""
    V, N, UV, F = [], [], [], []
    for axis, e, o1, e1, o2, e2 in ((ax, ex, ay, ey, az, ez), (ay, ey, az, ez, ax, ex), (az, ez, ax, ex, ay, ey)):
        for s in (1, -1):
            base = len(V)
            n = axis * s
            for a, b in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
                V.append(c + n * e + o1 * e1 * a + o2 * e2 * b); N.append(n); UV.append(((a + 1) * 0.02 + 0.6, (b + 1) * 0.02 + 0.9))
            F += [(base, base + 1, base + 2), (base, base + 2, base + 3)]
    return np.array(V), np.array(N), np.array(UV), F


def buckle(M, t, zc, d):
    o = cuff_pt(t, zc, d)
    rad = np.array([math.cos(t), math.sin(t), 0.0]); up = np.array([0, 0, 1.0]); tan = np.cross(up, rad)
    # the frame: four bars round a rectangle 0.9 (round) by 0.95 (along Z), raised off the strap
    c = o + rad * 0.07
    for dx, dz, ex, ez in ((0, 0.44, 0.48, 0.055), (0, -0.44, 0.48, 0.055), (0.44, 0, 0.055, 0.44), (-0.44, 0, 0.055, 0.44)):
        M.add('Brass', 'brass', *box(c + tan * dx + up * dz, ex, 0.06, ez, tan, rad, up))
    M.add('Brass', 'brass', *box(c + tan * 0.0, 0.03, 0.05, 0.42, tan, rad, up))          # the prong
    M.add('Strap', 'leather', *box(o + rad * 0.03 + tan * -0.75, 0.32, 0.05, 0.3, tan, rad, up))   # the keeper's loop


def rivets(M):
    """brass rivets round the cuff's two edges and down its seam"""
    def dome(p, n, r):
        n = unit(n); a = unit(np.cross(n, [0.3, 0.2, 0.93])); b = np.cross(n, a)
        prof = [(0, r), (0.35, r * 0.95), (0.7, r * 0.72), (0.92, r * 0.4), (1.0, 0.0)]
        th = np.linspace(0, 2 * np.pi, 13)
        P = np.array([[p + n * (h * r * 0.55) + (a * np.cos(x) + b * np.sin(x)) * rr for x in th] for h, rr in prof])
        Nn = np.array([[unit(n * (h + 0.2) + (a * np.cos(x) + b * np.sin(x)) * (1 - h)) for x in th] for h, rr in prof])
        UV = np.stack(np.meshgrid(np.linspace(0.32, 0.36, 13), np.linspace(0.9, 0.94, len(prof))), -1)
        M.add('Brass', 'brass', P.reshape(-1, 3), Nn.reshape(-1, 3), UV.reshape(-1, 2), grid_faces(len(prof), 13))
    for z in (CUFF_Z[1] - 0.42, CUFF_Z[0] + 0.42):
        for t in np.radians(np.arange(0, 360, 24)):
            if any(abs(np.angle(np.exp(1j * (t - v)))) < 0.2 for v in VIAL_TH): continue
            p = cuff_pt(t, z, 0.36)
            dome(p, [math.cos(t), math.sin(t), 0], 0.13)


# ------------------------------------------------------------------ the palm's orb, the back's plate, the tubes
PALM_TH = math.radians(-90)
ORB_Z = 3.0
ORB_R = 1.05


def orb_frame():
    p = surf(PALM_TH, ORB_Z, 0.0)
    n = unit(surf(PALM_TH, ORB_Z, 1.0) - p)
    # (the palm leans a little: the normal from the glove's own surface there)
    a = surf(PALM_TH + 0.15, ORB_Z, 0) - surf(PALM_TH - 0.15, ORB_Z, 0)
    b = surf(PALM_TH, ORB_Z + 0.3, 0) - surf(PALM_TH, ORB_Z - 0.3, 0)
    n2 = unit(np.cross(a, b))
    if n2 @ n < 0: n2 = -n2
    n = unit(n + n2)
    return p, n


def axis_lathe(prof, c, n, nc=40, u0=0.0, u1=1.0, v0=0.0, v1=1.0):
    """a turned part round the line through c along n: profile (h along n, r)"""
    P, N, UV, F = lathe_z(prof, 0.0, 0.0, nc, u0, u1, v0, v1)
    zax = unit(n); xax = unit(np.cross(zax, [0, 0, 1.0]) if abs(zax[2]) < 0.9 else np.cross(zax, [1.0, 0, 0])); yax = np.cross(zax, xax)
    R = np.stack([xax, yax, zax], 1)
    return P @ R.T + c, N @ R.T, UV, F


def orb(M):
    """the orb: a brass bezel (a flange on the palm, a raised collar, four claws over the glass) and the glass ball;
    six sockets round the flange for the tubes. Returns the ball's centre, the palm's normal and the sockets"""
    p, n = orb_frame()
    c = p + n * 0.55
    fl = [(-0.25, 0.0), (-0.25, 1.62), (-0.15, 1.72), (0.02, 1.7), (0.1, 1.55), (0.14, 1.3), (0.35, 1.12), (0.55, 1.02),
          (0.62, 0.98), (0.6, 0.9), (0.3, 0.85), (0.0, 0.0)]
    M.add('Brass', 'brass', *axis_lathe(fl, p, n, 48, u0=0.02, u1=0.3, v0=0.7, v1=0.78))
    M.add('Glass', 'orbglass', *axis_lathe([(0.55 + ORB_R * math.cos(a), ORB_R * math.sin(a)) for a in np.linspace(0, np.pi, 18)][::-1], p, n, 40))
    # the claws: four thin brass bands curling over the ball
    zax = unit(n); xax = unit(np.cross(zax, [0, 0, 1.0])); yax = np.cross(zax, xax)
    for k in range(4):
        a = math.pi / 4 + k * math.pi / 2
        d = xax * math.cos(a) + yax * math.sin(a)
        pts = [c + (d * math.sin(b) + zax * math.cos(b)) * (ORB_R + 0.06) for b in np.linspace(math.radians(118), math.radians(52), 10)]
        M.add('Brass', 'brass', *tube(pts, np.linspace(0.11, 0.06, 10), 8, vrange=(0.8, 0.83), urange=(0.05, 0.1)))
    socks = []
    down = unit(np.array([0, 0, -1.0]) - zax * (-zax[2]))          # (toward the wrist, in the palm's plane)
    side = np.cross(zax, down)
    for k in range(6):
        a = math.radians(-80 + 32 * k)
        d = down * math.cos(a) + side * math.sin(a)
        s = p + n * 0.12 + d * 1.5
        # a short collar lying along the flange, open outward (the tube comes in along it)
        M.add('Brass', 'brass', *tube([s - d * 0.2, s + d * 0.12, s + d * 0.32], [0.22, 0.24, 0.2], 12, vrange=(0.86, 0.88), urange=(0.05, 0.12)))
        socks.append((s + d * 0.3, d))
    return c, n, socks


def plate(M):
    """the brass plate on the back of the hand: a rounded shield conforming to the glove, its edge rolled, a lens at its
    heart. UV: planar on the brass sheet's engraved half (u 0.5 .. 1)"""
    tc, zc = math.radians(90), 3.9
    hw, hz = 1.15, 2.0                       # half width (radians on the envelope ~ units / r), half height
    nu, nv = 26, 30
    us = np.linspace(-1, 1, nu); vs = np.linspace(-1, 1, nv)
    U, Vv = np.meshgrid(us, vs)
    shape = np.clip(1 - (np.abs(U) ** 2.6 + np.abs(Vv * (1.05 - 0.12 * Vv)) ** 2.6), 0, 1) ** 0.1   # (rounded rectangle mask)
    thc = tc + U * hw * 0.52
    z = zc + Vv * hz
    P = surf(thc, z, 0.14 + 0.05 * shape)
    N = outward(grid_n(P, wrap=False), P, C0)
    UV = np.stack([0.5 + 0.5 * (U + 1) / 2, (Vv + 1) / 2], -1)
    F = np.array(grid_faces(nv, nu))
    keep = (np.abs(U) ** 2.6 + np.abs(Vv) ** 2.6).reshape(-1)[F].max(1) <= 1.0
    M.add('Plate', 'plate', P.reshape(-1, 3), N.reshape(-1, 3), UV.reshape(-1, 2), F[keep])
    # its rolled rim along the outline
    a = np.linspace(0, 2 * np.pi, 120)
    cu = np.sign(np.cos(a)) * np.abs(np.cos(a)) ** (2 / 2.6); cv = np.sign(np.sin(a)) * np.abs(np.sin(a)) ** (2 / 2.6)
    rim = surf(tc + cu * hw * 0.52, zc + cv * hz, 0.2)
    M.add('Brass', 'brass', *tube(np.vstack([rim, rim[:1]]), 0.09, 8, vrange=(0.9, 0.95), urange=(0.05, 0.2)))
    # the lens: a small glass dome in a collar
    lc = surf(tc, zc, 0.2); ln = unit(surf(tc, zc, 1.2) - lc)
    M.add('Brass', 'brass', *axis_lathe([(-0.05, 0.0), (-0.05, 0.62), (0.12, 0.66), (0.22, 0.56), (0.18, 0.46), (0.05, 0.44)], lc, ln, 32, u0=0.02, u1=0.3, v0=0.95, v1=0.98))
    M.add('Glass', 'orbglass', *axis_lathe([(0.05 + 0.4 * math.sin(b) * 0.7, 0.46 * math.cos(b)) for b in np.linspace(0, np.pi / 2, 9)][::-1] + [(0.05, 0.46)][:0], lc, ln, 32))
    return lc, ln


def tubes(M, tops, socks, orbc):
    """six glass tubes, each from its vial's spigot up over the cuff's lip and along the glove to its socket at the orb:
    the thumb side's three round the wrist under the thumb, the little finger's side round its edge. Brass clamps hold
    them down. Returns each tube's path (for the light pulsing up it)"""
    paths = []
    # which socket each vial goes to (the nearer side of the flange)
    order = [5, 4, 3, 2, 1, 0]
    for k in range(6):
        t = VIAL_TH[k]
        s, d = socks[order[k]]
        p0 = tops[k]
        p1 = p0 + np.array([0, 0, 0.5])
        way = [p0, p1, cuff_pt(t, CUFF_Z[1] + 0.25, 0.75)]
        if k < 3:   # round the thumb's side of the wrist, low, under the thumb's root
            for a in np.linspace(t - 0.25, math.radians(-60), 4):
                way.append(surf(a, 0.75 + 0.35 * (k - 1) * 0 + 0.5 * k * 0.3, 0.24))
        else:       # up the back, round the little finger's edge
            for a in np.linspace(t + 0.25, math.radians(250), 4):
                way.append(surf(a, 1.3 + 0.3 * (k - 3), 0.24))
        way.append(s + d * 0.7)
        way.append(s)
        path = spline(np.array(way), 70)
        M.add('Tube', 'tubeglass', *tube(path, 0.16, 10))
        paths.append(path)
        # brass ferrules at both ends and two clamps along it
        for q in (2, len(path) - 3, 26, 46):
            seg = path[max(0, q - 2):q + 3]
            M.add('Brass', 'brass', *tube(seg, 0.21, 10, vrange=(0.97, 0.99), urange=(0.05, 0.12)))
    return paths


# ------------------------------------------------------------------ textures
def save(arr, name, alpha=False, size=None):
    TEXDIR.mkdir(parents=True, exist_ok=True); WORK.mkdir(parents=True, exist_ok=True)
    a = np.clip(arr, 0, 255).astype(np.uint8)
    im = Image.fromarray(a, 'RGBA' if alpha else 'RGB')
    if size: im = im.resize((size, size), Image.LANCZOS)
    tmp = WORK / (name + '.png'); im.save(tmp)
    subprocess.run(['convert', str(tmp), '-define', f'dds:compression={"dxt5" if alpha else "dxt1"}', '-define', 'dds:mipmaps=12',
                    str(TEXDIR / name)], check=True)
    return TEXREL + name


def cells(n, count, seed):
    """a tileable cellular field (distance to the nearest of count points, wrapped): leather's pebbled grain"""
    r = np.random.default_rng(seed)
    pts = r.random((count, 2)) * n
    yy, xx = np.mgrid[0:n, 0:n].astype(np.float32)
    d1 = np.full((n, n), 1e9, np.float32); d2 = np.full((n, n), 1e9, np.float32)
    for px, py in pts:
        dx = np.abs(xx - px); dx = np.minimum(dx, n - dx)
        dy = np.abs(yy - py); dy = np.minimum(dy, n - dy)
        d = dx * dx + dy * dy
        m = d < d1
        d2 = np.where(m, d1, np.minimum(d2, d)); d1 = np.where(m, d, d1)
    return np.sqrt(d1), np.sqrt(d2)


def leather_tex():
    """2048 leather sheet: v 0.02..0.54 the glove (u round from +X: the palm at u 0.75, the back 0.25), 0.55..0.57 the
    hems, 0.58..0.82 the cuff, 0.84..0.97 the two straps. Oxblood-brown full-grain leather, pebbled, creased, its edges
    and high points rubbed lighter, double rows of linen saddle stitching along the seams and edges"""
    S = 2048
    tile = 512
    d1, d2 = cells(tile, 2600, 11)
    peb = np.clip((d2 - d1) / 6.0, 0, 1)                     # ridges between pebbles
    peb = np.tile(peb, (S // tile, S // tile))
    fine = np.tile(tileable(tile, tile, 0.5, 12), (S // tile, S // tile))
    big = tileable(S, S, 2.2, 13)
    mid = tileable(S, S, 1.5, 14)
    crease = tileable(S, S, 1.1, 15, aniso=(1.0, 0.18))       # long creases round the hand
    cr = np.exp(-((crease - 0.5) / 0.012) ** 2) * (mid > 0.45)
    v, u = (np.mgrid[0:S, 0:S].astype(np.float32) + 0.5) / S
    h = 0.55 * (1 - peb) + 0.25 * fine + 0.4 * big - 0.9 * cr
    # the colour: oxblood brown, mottled, lighter where rubbed
    base = np.array([74, 38, 27], np.float32)
    lum = 0.78 + 0.35 * (big - 0.5) + 0.22 * (mid - 0.5) + 0.12 * (fine - 0.5) - 0.18 * (1 - peb) * 0.5 - 0.35 * cr
    col = base * lum[..., None]
    wear = np.clip((mid - 0.62) * 3, 0, 1) * np.clip((fine - 0.3), 0, 1)
    col = col * (1 - 0.5 * wear[..., None]) + np.array([132, 88, 62]) * 0.5 * wear[..., None]
    # bands: the hems darker and oiled, the straps a darker, smoother strap leather with burnished edges
    hem = (v > 0.548) & (v < 0.572)
    strap = (v > 0.835) & (v < 0.975)
    col[hem] *= 0.62; h[hem] = 0.4 + 0.3 * fine[hem]
    sv = ((v - 0.84) % 0.05) / 0.04
    col[strap] = (np.array([46, 28, 20]) * (0.9 + 0.25 * (big[strap] - 0.5)))[..., :] if False else col[strap] * 0.55
    edge = strap & ((sv < 0.12) | (sv > 0.88))
    col[edge] = col[edge] * 1.6 + 10
    # the edges: each band's border burnished a little lighter (the leather turned over there)
    for a, b in ((0.02, 0.54), (0.58, 0.82)):
        e = np.exp(-((v - a) / 0.006) ** 2) + np.exp(-((v - b) / 0.006) ** 2)
        col *= (1 + 0.35 * e)[..., None]
    # saddle stitching: rows of slanted stitches (raised, pale linen) with their dark holes
    thread = np.zeros((S, S), np.float32)
    holes = np.zeros((S, S), np.float32)
    def stitch_row(vc, u0, u1, pitch_px, horizontal=True, uc=None, v0=None, v1=None):
        if horizontal:
            y = int(vc * S)
            for x in np.arange(u0 * S, u1 * S, pitch_px):
                x0, x1 = int(x), int(x + pitch_px * 0.62)
                for xx in range(x0, x1):
                    yy = y + int((xx - x0) * 0.35) - 2
                    for w in range(-2, 3):
                        if 0 <= yy + w < S: thread[yy + w, xx % S] = max(thread[yy + w, xx % S], 1 - abs(w) / 3)
                for hx in (x0 - 2, x1 + 1):
                    holes[max(0, y - 3):y + 4, hx % S] = 1
        else:
            x = int(uc * S)
            for y in np.arange(v0 * S, v1 * S, pitch_px):
                y0, y1 = int(y), int(y + pitch_px * 0.62)
                for yy in range(y0, min(y1, S)):
                    xx = x + int((yy - y0) * 0.35) - 2
                    for w in range(-2, 3):
                        thread[yy, (xx + w) % S] = max(thread[yy, (xx + w) % S], 1 - abs(w) / 3)
                for hy in (y0 - 2, y1 + 1):
                    if 0 <= hy < S: holes[hy, (x - 3) % S:(x + 4) % S] = 1
    for vc in (0.035, 0.045, 0.522, 0.532):          # the glove's wrist and knuckle edges
        stitch_row(vc, 0, 1, 22)
    for vc in (0.595, 0.605, 0.795, 0.805):          # the cuff's edges
        stitch_row(vc, 0, 1, 22)
    for vc in (0.85, 0.875, 0.90, 0.925):            # down each strap
        stitch_row(vc, 0, 1, 20)
    for uc in (0.0, 0.008, 0.5, 0.508):              # the glove's two side seams (thumb side, little finger's side)
        stitch_row(None, 0, 0, 22, False, uc, 0.03, 0.535)
    seam = np.zeros(S, np.float32)
    for uc in (0.004, 0.504):
        seam += np.exp(-((np.linspace(0, 1, S) - uc) / 0.0025) ** 2)
    seamm = (seam[None, :] * ((v > 0.02) & (v < 0.54))).astype(np.float32)
    h -= 0.8 * seamm
    col *= (1 - 0.4 * seamm)[..., None]
    thread = gaussian_filter(thread, 0.7); holes = gaussian_filter(holes, 0.8)
    tc = np.array([196, 178, 142]) * (0.85 + 0.25 * fine[..., None])
    col = col * (1 - 0.6 * holes[..., None])
    col = col * (1 - thread[..., None]) + tc * thread[..., None]
    h = h + 0.9 * thread - 0.6 * holes
    D = save(col, 'gauntlet_leather_d.dds')
    nrm = height_to_normal(gaussian_filter(h, 0.6), 5.0)
    spec = np.clip(40 + 60 * wear + 50 * (strap) + 30 * peb - 25 * cr + 30 * thread, 0, 255)
    N = save(np.dstack([nrm, spec]), 'gauntlet_leather_n.dds', True)
    return D, N


def brass_tex():
    """2048 brass sheet: u 0..0.5 aged brass (fine turning lines, scratches, patina in the low places, its plain parts);
    u 0.5..1 the plate's face, engraved: the alchemists' circle - two rings of script, six element sigils at the points
    of a hexagram round the lens - in dark-filled lines, the field brushed"""
    S = 2048
    v, u = (np.mgrid[0:S, 0:S].astype(np.float32) + 0.5) / S
    brushed = tileable(S, S, 1.2, 31, aniso=(1.0, 0.03))
    fine = tileable(S, S, 0.5, 32)
    big = tileable(S, S, 2.0, 33)
    # scratches: random short lines
    scr = Image.new('L', (S, S), 0); dr = ImageDraw.Draw(scr)
    for _ in range(2600):
        x, y = rng.random(2) * S; a = rng.random() * math.pi; l = 8 + rng.random() ** 2 * 120
        dr.line([(x, y), (x + math.cos(a) * l, y + math.sin(a) * l)], fill=int(60 + 120 * rng.random()), width=1)
    scr = np.asarray(scr, np.float32) / 255
    patina = np.clip((big - 0.58) * 3.2, 0, 1) * np.clip(fine * 1.4 - 0.2, 0, 1)
    base = np.array([196, 150, 78], np.float32)
    lum = 0.82 + 0.22 * (brushed - 0.5) + 0.15 * (big - 0.5) + 0.25 * scr
    col = base * lum[..., None]
    col = col * (1 - 0.65 * patina[..., None]) + np.array([72, 96, 70]) * 0.65 * patina[..., None]
    tarn = np.clip((0.45 - big) * 2, 0, 1)
    col *= (1 - 0.3 * tarn)[..., None]
    h = 0.4 * brushed + 0.2 * fine - 0.5 * scr
    # the engraving on the plate's half
    E = Image.new('L', (S // 2, S), 0); de = ImageDraw.Draw(E)
    cx, cy = S // 4, S // 2
    def circ(r, w): de.ellipse([cx - r, cy - r, cx + r, cy + r], outline=255, width=w)
    for r, w in ((300, 9), (330, 5), (420, 5), (450, 9), (190, 6)):
        circ(r, w)
    # script round the rings: little runic marks
    for ring_r, n in ((375, 60), (255, 40)):
        for k in range(n):
            a = 2 * math.pi * k / n
            x, y = cx + ring_r * math.cos(a), cy + ring_r * math.sin(a)
            kind = rng.integers(0, 5)
            tx, ty = -math.sin(a), math.cos(a); rx, ry = math.cos(a), math.sin(a)
            s = 14 if ring_r > 300 else 11
            segs = [[(-1, -1), (-1, 1)], [(-1, -1), (1, 1)], [(1, -1), (-1, 1)], [(0, -1), (0, 1)], [(-1, 0), (1, 0)]]
            for a0 in range(2):
                (p, q) = segs[(kind + a0 * 2) % 5]
                de.line([(x + (p[0] * tx + p[1] * rx) * s, y + (p[0] * ty + p[1] * ry) * s),
                         (x + (q[0] * tx + q[1] * rx) * s, y + (q[0] * ty + q[1] * ry) * s)], fill=255, width=4)
    # the hexagram, and the six sigils at its points
    pts = [(cx + 300 * math.cos(math.pi / 2 + k * math.pi / 3), cy - 300 * math.sin(math.pi / 2 + k * math.pi / 3)) for k in range(6)]
    for k in range(6):
        de.line([pts[k], pts[(k + 2) % 6]], fill=255, width=6)
    for k, (x, y) in enumerate(pts):
        de.ellipse([x - 46, y - 46, x + 46, y + 46], fill=0, outline=255, width=6)
        g = ELS[k]
        if g == 'fire': de.polygon([(x, y - 28), (x - 25, y + 20), (x + 25, y + 20)], outline=255, width=5)
        elif g == 'frost':
            for a in range(3):
                aa = a * math.pi / 3; de.line([(x - 28 * math.cos(aa), y - 28 * math.sin(aa)), (x + 28 * math.cos(aa), y + 28 * math.sin(aa))], fill=255, width=5)
        elif g == 'shock': de.line([(x + 10, y - 30), (x - 12, y + 2), (x + 12, y - 2), (x - 10, y + 30)], fill=255, width=6)
        elif g == 'acid': de.ellipse([x - 22, y - 10, x + 22, y + 26], outline=255, width=5); de.line([(x, y - 30), (x, y - 10)], fill=255, width=5)
        elif g == 'holy':
            de.ellipse([x - 13, y - 13, x + 13, y + 13], outline=255, width=5)
            for a in range(8):
                aa = a * math.pi / 4; de.line([(x + 18 * math.cos(aa), y + 18 * math.sin(aa)), (x + 30 * math.cos(aa), y + 30 * math.sin(aa))], fill=255, width=4)
        else: de.ellipse([x - 26, y - 26, x + 26, y + 26], outline=255, width=5); de.ellipse([x - 8, y - 18, x + 18, y + 8], fill=255)
    # filigree corners
    for sx in (-1, 1):
        for sy in (-1, 1):
            for k in range(5):
                r = 120 + 26 * k
                ox, oy = cx + sx * 470, cy + sy * 860
                de.arc([ox - r, oy - r, ox + r, oy + r], 0, 360, fill=255, width=4)
    eng = np.asarray(E.filter(ImageFilter.GaussianBlur(1.2)), np.float32) / 255
    mask = u >= 0.5
    full = np.zeros((S, S), np.float32); full[:, S // 2:] = eng
    col[mask] = col[mask] * 1.08
    col = col * (1 - 0.82 * full[..., None]) + np.array([28, 20, 12]) * 0.82 * full[..., None]
    h = h - 1.6 * full
    D = save(col, 'gauntlet_brass_d.dds')
    nrm = height_to_normal(gaussian_filter(h, 0.8), 6.0)
    spec = np.clip(200 + 40 * (brushed - 0.5) - 150 * patina - 120 * full + 30 * scr, 0, 255)
    N = save(np.dstack([nrm, spec]), 'gauntlet_brass_n.dds', True)
    return D, N


def glass_tex():
    """1024: clear glass, faint smudges and a few fine scratches (alpha low), glossy"""
    S = 1024
    sm = tileable(S, S, 1.6, 41); fn = tileable(S, S, 0.6, 42)
    a = 38 + 40 * np.clip((sm - 0.55) * 3, 0, 1) + 10 * fn
    col = np.stack([200 + 20 * fn, 214 + 20 * fn, 226 + 20 * fn, a], -1)
    D = save(col, 'gauntlet_glass_d.dds', True)
    nrm = height_to_normal(sm * 0.3 + fn * 0.1, 2.0)
    N = save(np.dstack([nrm, np.full((S, S), 255.0)]), 'gauntlet_glass_n.dds', True)
    # the tubes' glass: a little tinted, a little more there (sootier)
    col2 = col.copy(); col2[..., 3] = 60 + 50 * np.clip((sm - 0.5) * 3, 0, 1)
    T = save(col2, 'gauntlet_tubeglass_d.dds', True)
    return D, N, T


def liquid_tex():
    """1024 x 1024: six columns, one per vial (its element), swirled; the same as its glow"""
    S = 1024
    v, u = (np.mgrid[0:S, 0:S].astype(np.float32) + 0.5) / S
    sw = tileable(S, S, 1.3, 51, aniso=(0.4, 1.0)); fn = tileable(S, S, 0.7, 52)
    k = np.minimum((u * 6).astype(int), 5)
    cols = np.array([ELC[e] for e in ELS])[k]
    lum = 0.55 + 0.6 * sw + 0.2 * fn
    col = cols * lum[..., None] * 255
    D = save(col, 'gauntlet_liquid_d.dds')
    G = save(np.clip(col * 1.1, 0, 255), 'gauntlet_liquid_g.dds')
    nrm = height_to_normal(sw * 0.4, 2.0)
    N = save(np.dstack([nrm, np.full((S, S), 220.0)]), 'gauntlet_liquid_n.dds', True)
    return D, G, N


def fx_tex():
    """the additive effects' textures: the orb's swirl (the six colours, tileable both ways), the tubes' light pulses,
    the vials' bubbles, the wrist's motes, a soft glow, and the orb's pulsing core (a 4x4 flipbook)"""
    out = {}
    S = 1024
    yy, xx = (np.mgrid[0:S, 0:S].astype(np.float32) + 0.5) / S
    # swirl: nebula of the six colours, tileable (u round, v pole to pole)
    warp1 = tileable(S, S, 1.8, 61); warp2 = tileable(S, S, 1.8, 62)
    hue = (xx + 0.35 * (warp1 - 0.5) + 0.2 * (yy - 0.5)) % 1.0
    k = hue * 6; i0 = np.floor(k).astype(int) % 6; i1 = (i0 + 1) % 6; f = (k - np.floor(k))[..., None]
    C = np.array([ELC[e] for e in ELS])
    colr = C[i0] * (1 - f) + C[i1] * f
    dens = tileable(S, S, 1.4, 63)
    wisps = np.clip((dens - 0.42) * 2.4, 0, 1) ** 1.6
    streak = np.exp(-((tileable(S, S, 1.0, 64, aniso=(0.25, 1.0)) - 0.5) / 0.03) ** 2)
    a = np.clip(wisps * 0.8 + streak * 0.6, 0, 1)
    img = np.dstack([colr * 255 * (0.6 + 0.4 * a[..., None]), a * 255])
    out['swirl'] = save(img, 'gauntlet_fx_swirl.dds', True)
    # pulses along a tube: bright soft dashes down v (tileable), a faint steady line
    v = yy
    pulse = np.exp(-(((v * 3) % 1.0 - 0.5) / 0.09) ** 2) + 0.25
    pulse = pulse * (0.85 + 0.3 * (tileable(S, S, 1.2, 65) - 0.5))
    img = np.dstack([np.full((S, S, 3), 255.0), np.clip(pulse, 0, 1) * 255])
    out['pulse'] = save(img, 'gauntlet_fx_pulse.dds', True, size=512)
    # bubbles: little rings and dots scattered, tileable
    B = Image.new('L', (S, S), 0); db = ImageDraw.Draw(B)
    for _ in range(260):
        x, y = rng.random(2) * S; r = 3 + rng.random() ** 2 * 18
        for ox in (-S, 0, S):
            for oy in (-S, 0, S):
                db.ellipse([x + ox - r, y + oy - r, x + ox + r, y + oy + r], outline=230, width=max(2, int(r / 4)))
                db.ellipse([x + ox - r * 0.35 - r * 0.3, y + oy - r * 0.35 - r * 0.3, x + ox - r * 0.3 + r * 0.05, y + oy - r * 0.3 + r * 0.05], fill=255)
    b = np.asarray(B.filter(ImageFilter.GaussianBlur(1.2)), np.float32)
    out['bubbles'] = save(np.dstack([np.full((S, S, 3), 255.0), b]), 'gauntlet_fx_bubbles.dds', True, size=512)
    # motes: soft dots of the six colours, sparse, tileable
    Mt = np.zeros((S, S, 4), np.float32)
    for _ in range(140):
        x, y = rng.random(2) * S; r = 3 + rng.random() ** 3 * 9
        c = np.array(ELC[ELS[rng.integers(0, 6)]]) * 255
        d = np.hypot(((xx * S - x + S / 2) % S) - S / 2, ((yy * S - y + S / 2) % S) - S / 2)
        g = np.exp(-(d / r) ** 2)
        Mt[..., :3] = np.maximum(Mt[..., :3], c * g[..., None]); Mt[..., 3] = np.maximum(Mt[..., 3], g * 255)
    out['motes'] = save(Mt, 'gauntlet_fx_motes.dds', True)
    # a soft glow
    d = np.hypot(xx - 0.5, yy - 0.5) * 2
    g = np.clip(1 - d, 0, 1) ** 2.2
    out['glow'] = save(np.dstack([np.full((S, S, 3), 255.0), g * 255]), 'gauntlet_fx_glow.dds', True, size=256)
    # the core: a 4x4 flipbook (256 a frame) of a white-hot heart with six coloured rays slowly turning, breathing
    F = 256; atlas = np.zeros((F * 4, F * 4, 4), np.float32)
    fy, fx = (np.mgrid[0:F, 0:F].astype(np.float32) + 0.5) / F - 0.5
    r = np.hypot(fx, fy) * 2; th = np.arctan2(fy, fx)
    for i in range(16):
        t = i / 16; br = 0.8 + 0.2 * math.cos(2 * math.pi * t)
        core = np.exp(-(r / (0.16 * br)) ** 2)
        rays = np.zeros((F, F, 3), np.float32); ra = np.zeros((F, F), np.float32)
        for k in range(6):
            a0 = k * math.pi / 3 + 2 * math.pi * t / 6
            da = np.angle(np.exp(1j * (th - a0)))
            ray = np.exp(-(da / 0.12) ** 2) * np.clip(1 - r, 0, 1) ** 1.5 * (0.6 + 0.4 * math.sin(2 * math.pi * (t + k / 6)))
            rays += np.array(ELC[ELS[k]]) * ray[..., None]; ra = np.maximum(ra, ray)
        halo = np.clip(1 - r, 0, 1) ** 3 * 0.45 * br
        rgb = np.clip(core[..., None] * 1.0 + rays + halo[..., None] * np.array([0.85, 0.8, 1.0]), 0, 1) * 255
        a = np.clip(core + ra * 0.8 + halo, 0, 1) * 255
        rr, cc = divmod(i, 4)
        atlas[rr * F:(rr + 1) * F, cc * F:(cc + 1) * F] = np.dstack([rgb, a])
    out['core'] = save(atlas, 'gauntlet_fx_core.dds', True)
    # (Noah: "shoot a beam coming from the palm/glass ball while holding attack") the beam's three layers, each tileable down
    # its length (v) and soft across it (u): a wide glow full of flowing streaks, two strands twisting round each other, a
    # white-hot core
    B = 512
    by, bx = (np.mgrid[0:B, 0:B].astype(np.float32) + 0.5) / B
    x = bx - 0.5
    flow = tileable(B, B, 1.3, 71, aniso=(1.0, 0.12))
    glow = np.exp(-(x / 0.24) ** 2) * (0.55 + 0.6 * flow) + 0.35 * np.exp(-(x / 0.42) ** 2)
    out['beam_glow'] = save(np.dstack([np.full((B, B, 3), 255.0), np.clip(glow, 0, 1) * 255]), 'gauntlet_beam_glow.dds', True)
    st = np.zeros((B, B), np.float32)
    for ph in (0.0, np.pi):
        c = 0.28 * np.sin(2 * np.pi * by * 2 + ph)
        depth = 0.6 + 0.4 * np.cos(2 * np.pi * by * 2 + ph)          # (the strand passing behind is dimmer)
        st = np.maximum(st, np.exp(-((x - c) / 0.035) ** 2) * depth)
    spark = np.clip((tileable(B, B, 0.4, 72) - 0.82) * 6, 0, 1) * np.exp(-(x / 0.3) ** 2)
    out['beam_strand'] = save(np.dstack([np.full((B, B, 3), 255.0), np.clip(st + spark, 0, 1) * 255]), 'gauntlet_beam_strand.dds', True)
    flick = tileable(B, B, 1.0, 73, aniso=(1.0, 0.3))
    core = np.exp(-(x / (0.07 + 0.03 * flick)) ** 2)
    out['beam_core'] = save(np.dstack([np.full((B, B, 3), 255.0), np.clip(core, 0, 1) * 255]), 'gauntlet_beam_core.dds', True)
    return out


def build_beams():
    """the beam, one model per element: a 64-unit segment along +Y, flat in its XY plane (src/WonderProps.inl lays them
    end to end from the palm, each turned face-on to the camera): the glow, the twisting strands and the core, all
    flowing away from the hand; and the flare (where it leaves the palm, and where it strikes): the orb's pulsing core
    and a glow, always facing you"""
    for e, name in enumerate(ELS):
        c = ELC[name]
        pale = tuple(0.55 + 0.45 * x for x in c)
        L = [f'out {MESHDIR / f"gauntlet_beam_{name}.nif"}']
        for sh, t, w, col, sc, vs in (('BeamGlow', FX['beam_glow'], 15.0, c, 1.5, 0.45), ('BeamStrand', FX['beam_strand'], 11.0, pale, 2.2, 0.3),
                                        ('BeamCore', FX['beam_core'], 5.0, pale, 3.0, 0.6)):
            L.append(f'shape {sh} tex={t} color={col[0]:.2f},{col[1]:.2f},{col[2]:.2f},1 scale={sc} blend=add vscroll=-{vs}')
            for X, Y, uu, vv in ((-w, 0, 0, 0), (w, 0, 1, 0), (w, 64, 1, 1), (-w, 64, 0, 1)):
                L.append(f'v {X} {Y} 0 {uu} {vv}')
            L.extend(['t 0 1 2', 't 0 2 3'])
        (WORK / f'beam_{name}.job').write_text('\n'.join(L) + '\n')
        r = subprocess.run([str(TOOLS / 'fxwrite'), str(WORK / f'beam_{name}.job')], capture_output=True, text=True)
        if r.returncode: print(r.stdout, r.stderr); raise SystemExit('fxwrite failed (beam)')
        L = [f'out {MESHDIR / f"gauntlet_flare_{name}.nif"}']
        for sh, t, hs, col, sc, flip in (('FlareCore', FX['core'], 20.0, '1,1,1,1', 2.4, True), ('FlareTint', FX['core'], 30.0, f'{c[0]:.2f},{c[1]:.2f},{c[2]:.2f},1', 2.0, True),
                                          ('FlareGlow', FX['glow'], 46.0, f'{c[0]:.2f},{c[1]:.2f},{c[2]:.2f},0.6', 1.2, False)):
            extra = ' flip=4x4 fps=18 phase=0' if flip else ''
            L.append(f'shape {sh} tex={t} color={col} scale={sc} blend=add billboard=1 at=0,0,0{extra}')
            cu = 0.25 if flip else 1
            for X, Y, uu, vv in ((-hs, hs, 0, 0), (hs, hs, cu, 0), (hs, -hs, cu, cu), (-hs, -hs, 0, cu)):
                L.append(f'v {X} {Y} 0 {uu} {vv} 1 1 1 1')
            L.extend(['t 0 1 2', 't 0 2 3'])
        (WORK / f'flare_{name}.job').write_text('\n'.join(L) + '\n')
        r = subprocess.run([str(TOOLS / 'fxwrite'), str(WORK / f'flare_{name}.job')], capture_output=True, text=True)
        if r.returncode: print(r.stdout, r.stderr); raise SystemExit('fxwrite failed (flare)')


# ------------------------------------------------------------------ the effect shapes (fxwrite)
def sphere(c, r, nu=32, nv=16):
    V, F = [], []
    for i in range(nv + 1):
        ph = math.pi * i / nv
        for j in range(nu + 1):
            th = 2 * math.pi * j / nu
            n = np.array([math.sin(ph) * math.cos(th), math.sin(ph) * math.sin(th), math.cos(ph)])
            V.append((c + n * r, j / nu, i / nv, n))
    for i in range(nv):
        for j in range(nu):
            a = i * (nu + 1) + j; b = a + 1; cc = a + nu + 1; d = cc + 1
            F += [(a, cc, b), (b, cc, d)]
    return V, F


def fx_job(base, out, mirror, orbc, orbn, paths, vialx, lens):
    L = [f'base {base}', f'out {out}']
    s = -1.0 if mirror else 1.0
    def P(p): return (p[0] * s, p[1], p[2])
    def emit(V, F, normals=True, colour=None):
        for p, uu, vv, n in V:
            q = P(p)
            c = f' {colour[0]:.3f} {colour[1]:.3f} {colour[2]:.3f} {colour[3]:.3f}' if colour is not None else ''
            if callable(colour): c = ' ' + ' '.join(f'{x:.3f}' for x in colour(p, uu, vv))
            L.append(f'v {q[0]:.4f} {q[1]:.4f} {q[2]:.4f} {uu:.4f} {vv:.4f}{c}')
            if normals: m = (n[0] * s, n[1], n[2]); L.append(f'n {m[0]:.4f} {m[1]:.4f} {m[2]:.4f}')
        for a, b, c in F:
            L.append(f't {a} {c} {b}' if mirror else f't {a} {b} {c}')
    tex = FX
    # the orb's two swirling shells, turning against each other, and its rim
    L.append(f'shape OrbSwirlA tex={tex["swirl"]} color=1,1,1,1 scale=1.8 blend=add falloff=1 uscroll=3.2 vscroll=9')
    emit(*sphere(orbc, ORB_R * 0.9))
    L.append(f'shape OrbSwirlB tex={tex["swirl"]} color=1,1,1,0.9 scale=1.5 blend=add falloff=1 uscroll=-4.6 vscroll=-13')
    V, F = sphere(orbc, ORB_R * 0.66, 28, 14)
    V = [(p, uu * 2 + 0.37, vv + 0.21, n) for p, uu, vv, n in V]
    emit(V, F)
    L.append(f'shape OrbRim tex={tex["glow"]} color=0.85,0.8,1,0.6 scale=1.2 blend=add rim=1')
    V, F = sphere(orbc, ORB_R * 1.02, 32, 16)
    emit([(p, 0.5, 0.5, n) for p, uu, vv, n in V], F)
    # its core (a flipbook, always facing you) and a soft halo round it
    def card(name, t, at, hs, col, sc, flip=None):
        extra = f' flip={flip[0]}x{flip[1]} fps={flip[2]} phase={flip[3]}' if flip else ''
        q = P(at)
        L.append(f'shape {name} tex={t} color={col} scale={sc} blend=add billboard=1 at={q[0]:.3f},{q[1]:.3f},{q[2]:.3f}{extra}')
        cu, cv = (1 / flip[0], 1 / flip[1]) if flip else (1, 1)
        for X, Y, uu, vv in ((-hs, hs, 0, 0), (hs, hs, cu, 0), (hs, -hs, cu, cv), (-hs, -hs, 0, cv)):
            L.append(f'v {X:.3f} {Y:.3f} 0 {uu} {vv} 1 1 1 1')
        L.extend(['t 0 1 2', 't 0 2 3'])
    card('OrbCore', tex['core'], orbc, 0.95, '1,1,1,1', 2.2, (4, 4, 10, 0))
    card('OrbHalo', tex['glow'], orbc + orbn * 0.2, 2.6, '0.8,0.75,1,0.5', 0.9)
    # the orb flooded with one element's light while that vial pours into it (OrbTint<e>: hidden unless it is the one;
    # src/WonderProps.inl), its glow spilling out round the palm (OrbFlare<e>)
    for e in range(6):
        c = ELC[ELS[e]]
        L.append(f'shape OrbTint{e} tex={tex["swirl"]} color={c[0]:.2f},{c[1]:.2f},{c[2]:.2f},1 scale=3.0 blend=add falloff=1 uscroll=1.6 vscroll=4')
        emit(*sphere(orbc, ORB_R * 0.97, 28, 14))
        card(f'OrbFlare{e}', tex['core'], orbc + orbn * 0.4, 2.2, f'{c[0]:.2f},{c[1]:.2f},{c[2]:.2f},1', 2.0, (4, 4, 16, 0))
    # the light running up each tube into the orb (a thin core inside the glass, the element's colour)
    for k, path in enumerate(paths):
        e = vial_el(k, mirror); c = ELC[ELS[e]]
        L.append(f'shape TubeLight{e} tex={tex["pulse"]} color={c[0]:.2f},{c[1]:.2f},{c[2]:.2f},1 scale=2.4 blend=add vscroll=-{1.3 + 0.17 * k:.2f}')
        Pp, N, UV, F = tube(path, 0.085, 6, vrange=(0, np.linalg.norm(np.diff(path, axis=0), axis=1).sum() / 4.0))
        emit([(Pp[i], UV[i][0], UV[i][1], N[i]) for i in range(len(Pp))], F)
    # bubbles rising in each vial, and a faint glow about it
    z0, z1 = VIAL_Z
    for k in range(6):
        (cx, cy), t = vialx[k]
        e = vial_el(k, mirror); c = ELC[ELS[e]]
        L.append(f'shape VialBubbles{e} tex={tex["bubbles"]} color={c[0]:.2f},{c[1]:.2f},{c[2]:.2f},1 scale=1.6 blend=add vscroll=-{1.8 + 0.23 * k:.2f}')
        prof = [(z0 + 0.25, VIAL_R * 0.72), (z1 - 1.0, VIAL_R * 0.72)]
        Pp, N, UV, F = lathe_z(prof, cx, cy, 14, 0, 1, 0, 1.6)
        emit([(Pp[i], UV[i][0], UV[i][1], N[i]) for i in range(len(Pp))], F)
        card(f'VialGlow{e}', tex['glow'], np.array([cx, cy, (z0 + z1) / 2 - 0.3]), 1.2, f'{c[0]:.2f},{c[1]:.2f},{c[2]:.2f},0.55', 0.7)
    # the lens's swirl
    lc, ln = lens
    L.append(f'shape LensSwirl tex={tex["swirl"]} color=1,1,1,1 scale=1.4 blend=add falloff=1 uscroll=2.6 vscroll=7')
    emit(*sphere(lc + ln * 0.08, 0.34, 20, 10))
    # motes of the six colours circling the wrist (an open band round the cuff and hand, the texture sliding round it)
    for band, (r_add, za, zb, us, vs, alpha) in enumerate(((1.25, -4.9, 2.6, 5.5, 14, 0.9), (1.7, -3.8, 4.2, -8.0, 21, 0.6))):
        L.append(f'shape Motes{band} tex={tex["motes"]} color=1,1,1,{alpha} scale=1.7 blend=add uscroll={us} vscroll=-{vs}')
        V, F = [], []
        nu, nv = 48, 10
        for i in range(nv + 1):
            z = za + (zb - za) * i / nv
            for j in range(nu + 1):
                th = 2 * math.pi * j / nu
                r = cuff_base(th, min(z, 0.9)) + r_add
                p = np.array([C0[0] + r * math.cos(th), C0[1] + r * math.sin(th), z])
                fade = math.sin(math.pi * i / nv) ** 1.5
                V.append((p, j / nu * 3, i / nv * 1.5, np.array([math.cos(th), math.sin(th), 0]), fade))
        for i in range(nv):
            for j in range(nu):
                a = i * (nu + 1) + j; b = a + 1; cc = a + nu + 1; d = cc + 1
                F += [(a, cc, b), (b, cc, d)]
        for p, uu, vv, n, fade in V:
            q = P(p); L.append(f'v {q[0]:.4f} {q[1]:.4f} {q[2]:.4f} {uu:.4f} {vv:.4f} 1 1 1 {fade:.3f}')
        for a, b, c in F:
            L.append(f't {a} {c} {b}' if mirror else f't {a} {b} {c}')
    return L


# ------------------------------------------------------------------ build
FX = {}


def build_model(mirror, tex):
    M = Mesh()
    glove(M); cuff(M)
    tops = vials(M, mirror)
    straps(M); rivets(M)
    orbc, orbn, socks = orb(M)
    lens = plate(M)
    paths = tubes(M, tops, socks, orbc)
    if mirror:
        g2 = []
        for g, m, V, N, UV, F in M.groups:
            V = V * [-1, 1, 1]; N = N * [-1, 1, 1]
            g2.append((g, m, V, N, UV, np.asarray(F)[:, [0, 2, 1]]))
        M.groups = g2
    # one shape per material (fewer draw calls): the groups of each material joined
    merged = {}
    for g, m, V, N, UV, F in M.groups:
        key = g if g.startswith('Liq') else m
        if key not in merged: merged[key] = [g, m, [], [], [], [], 0]
        e = merged[key]; e[2].append(V); e[3].append(N); e[4].append(UV); e[5].append(np.asarray(F) + e[6]); e[6] += len(V)
    M.groups = [(key if key.startswith('Liq') else e[1].capitalize(), e[1], np.vstack(e[2]), np.vstack(e[3]), np.vstack(e[4]), np.vstack(e[5])) for key, e in merged.items()]
    side = 'l' if mirror else 'r'
    obj = WORK / f'gauntlet_{side}.obj'; M.write_obj(obj)
    static = WORK / f'gauntlet_{side}_static.nif'
    lD, lN = tex['leather']; bD, bN = tex['brass']; gD, gN, tD = tex['glass']; qD, qG, qN = tex['liquid']
    job = [f'in {obj}', f'out {static}', 'scale 1', 'ground 0', 'origin 1', 'offset 0 0 0',
           f'mat leather diffuse={lD} normal={lN} spec=0.55 gloss=22 alpha=0 doublesided=1',
           f'mat brass diffuse={bD} normal={bN} spec=1.9 gloss=90 alpha=0 doublesided=0',
           f'mat plate diffuse={bD} normal={bN} spec=1.9 gloss=90 alpha=0 doublesided=0',
           f'mat glass diffuse={gD} normal={gN} spec=2.4 gloss=180 alpha=blend doublesided=1',
           f'mat orbglass diffuse={gD} normal={gN} spec=2.6 gloss=200 alpha=blend doublesided=0',
           f'mat tubeglass diffuse={tD} normal={gN} spec=2.4 gloss=180 alpha=blend doublesided=1',
           f'mat liquid diffuse={qD} normal={qN} glow={qG} emit=1,1,1 emitmult=1.7 spec=1.2 gloss=90 alpha=0 doublesided=0']
    jf = WORK / f'gauntlet_{side}.job'; jf.write_text('\n'.join(job) + '\n')
    r = subprocess.run([str(TOOLS / 'modelconv'), str(jf)], capture_output=True, text=True)
    if r.returncode: print(r.stdout, r.stderr); raise SystemExit('modelconv failed')
    print(r.stdout.strip()[-300:])
    MESHDIR.mkdir(parents=True, exist_ok=True)
    nif = MESHDIR / f'gauntlet_{side}.nif'
    vialx = [vial_axis(k) for k in range(6)]
    L = fx_job(static, nif, mirror, orbc, orbn, paths, vialx, lens)
    (WORK / f'fx_{side}.job').write_text('\n'.join(L) + '\n')
    r = subprocess.run([str(TOOLS / 'fxwrite'), str(WORK / f'fx_{side}.job')], capture_output=True, text=True)
    if r.returncode or 'no node' in r.stderr: print(r.stdout, r.stderr); raise SystemExit('fxwrite failed')
    lo, hi = M.bounds()
    print(f'{nif.name}: x {lo[0]:.1f}..{hi[0]:.1f}  y {lo[1]:.1f}..{hi[1]:.1f}  z {lo[2]:.1f}..{hi[2]:.1f}; '
          f'{sum(len(g[5]) for g in M.groups)} triangles')
    return M, obj


def build_hold():
    """the weapon's own model (UN_Flask_philosopher's MODL and its first-person STAT): one tiny clear card - the glove is
    hung on the hand bone instead (src/WonderProps.inl)"""
    nif = MESHDIR / 'gauntlet_hold.nif'
    L = [f'out {nif}', f'shape Hold tex={FX["glow"]} color=0,0,0,0 scale=0 blend=add',
         'v 0 0 0 0 0', 'v 0.01 0 0 1 0', 'v 0 0.01 0 0 1', 't 0 1 2']
    (WORK / 'hold.job').write_text('\n'.join(L) + '\n')
    r = subprocess.run([str(TOOLS / 'fxwrite'), str(WORK / 'hold.job')], capture_output=True, text=True)
    if r.returncode: print(r.stdout, r.stderr); raise SystemExit('fxwrite failed (hold)')
    subprocess.run([str(TOOLS / 'addprn'), str(nif), str(nif), 'WeaponDagger'], check=True)


if __name__ == '__main__':
    WORK.mkdir(parents=True, exist_ok=True)
    tool('addprn')
    tex = {'leather': leather_tex(), 'brass': brass_tex(), 'glass': glass_tex(), 'liquid': liquid_tex()}
    FX.update(fx_tex())
    models = [build_model(False, tex), build_model(True, tex)]
    build_hold()
    build_beams()
    if '--keep' not in sys.argv:
        shutil.rmtree(WORK, ignore_errors=True)
