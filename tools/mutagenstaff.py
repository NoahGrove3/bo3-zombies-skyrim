"""(Noah: the Mutagen Gauntlet made a staff - "an alchemist style staff with 2k textures and very hyper realistic" - "Orb in a
cage of vials", "Hold attack, staff thrust") the Alchemist's wonder, modelled here from scratch (no one else's mesh): a dark
wood staff with a brass-shod foot, brass bands and a leather-wrapped grip; at its head a brass collar ringed with six glass
vials of the elements (fire, frost, shock, acid, holy, void), each feeding a glass tube up into a glass orb held in a cage
of six curving brass ribs under a spired finial.

Passive animation (looping from load): the orb's two swirling shells of the six colours and its pulsing core and halo;
bubbles rising in every vial; motes of the six colours circling the head. Driven by src/WonderProps.inl while the beam
pours (by name, as the glove's were): each vial's liquid surface (Liq<e>_T, its column skinned to it) lowered and raised
smoothly; the pouring vial's tube lit (TubeLight<e>), the orb flooded with its colour (OrbTint<e>, OrbFlare<e>); the beam
leaves from the orb (OrbCore_bb).

A staff as Skyrim's are: on the WEAPON node, the shaft up +Y, the grip at the origin; Prn WeaponStaff (its place on your
back). The same model is the first-person one.

    python3 tools/mutagenstaff.py [--preview <png>] [--keep]
      -> assets/fx/Meshes/CurseOfAlduin/alchemist/mutagen_staff.nif (+ the beam's gauntlet_beam_<el>, gauntlet_flare_<el>)
         assets/fx/Textures/CurseOfAlduin/alchemist/mutagen_wood_{d,n}.dds, mutagen_grip_{d,n}.dds (2048), and the shared
         gauntlet_* brass, glass and effect textures (tools/gauntlet.py's makers)

Built along Z (the head up) and turned at the end so the shaft runs up +Y."""
import math, pathlib, shutil, subprocess, sys
import numpy as np
from scipy.ndimage import gaussian_filter

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import gauntlet as g
from throwdagger import Mesh, unit, grid_faces, tileable, height_to_normal, tool, rename_root

ROOT = g.ROOT
MESHDIR, TEXDIR, TEXREL = g.MESHDIR, g.TEXDIR, g.TEXREL
WORK = ROOT / 'build' / 'mutagenstaff'
g.WORK = WORK
TOOLS = g.TOOLS
ELS, ELC = g.ELS, g.ELC

# the staff's stations along its axis (Z here, +Y in the game): the foot, the grip at 0, the head
FOOT, SHAFT_TOP = -34.0, 57.0
COLLAR = (56.0, 60.5)
VIAL_RING = 3.05            # the vials' axis, out from the staff's
VIAL_R, VIAL_Z = 0.62, (58.6, 64.4)
ORB_C, ORB_R = 68.6, 2.75
CAGE_R = 3.9
TOP = 79.0


def shaft_r(z):
    """the shaft's radius: fuller at the foot, slimming toward the head, a gentle irregular swell (hand-shaped wood)"""
    z = np.asarray(z, float)
    base = 1.08 - 0.20 * (z - FOOT) / (SHAFT_TOP - FOOT)
    wob = 0.035 * np.sin(z * 0.37) + 0.025 * np.sin(z * 1.13 + 1.0)
    return base + wob


def lathe(prof, nc=40, u0=0.0, u1=1.0, v0=0.0, v1=1.0, cx=0.0, cy=0.0):
    return g.lathe_z(prof, cx, cy, nc, u0, u1, v0, v1)


def shaft(M):
    """the wood from the foot's shoe to the collar; UV u round, v up the wood sheet (one sheet over its length)"""
    zs = np.linspace(FOOT + 2.5, SHAFT_TOP, 90)
    prof = [(z, float(shaft_r(z))) for z in zs]
    M.add('Wood', 'wood', *lathe(prof, 36, 0, 1, 0, 1))


def bands(M):
    """the brass: the foot's pointed shoe, three bands, and the collars either side of the grip"""
    f0 = FOOT
    shoe = [(f0 - 1.2, 0.0), (f0 - 1.0, 0.25), (f0, 0.7), (f0 + 1.2, 1.12), (f0 + 2.4, 1.18), (f0 + 2.7, 1.08), (f0 + 2.8, 0.9)]
    M.add('Brass', 'brass', *lathe(shoe, 32, 0.02, 0.3, 0.02, 0.05))
    for k, zc in enumerate((-20.0, -10.5, 11.5, 28.0, 46.0)):
        r = float(shaft_r(zc))
        w = 0.6 if k in (1, 2) else 0.45
        prof = [(zc - w - 0.1, r - 0.02), (zc - w, r + 0.12), (zc - w * 0.4, r + 0.2), (zc, r + 0.16), (zc + w * 0.4, r + 0.2), (zc + w, r + 0.12), (zc + w + 0.1, r - 0.02)]
        M.add('Brass', 'brass', *lathe(prof, 36, 0.02, 0.3, 0.06 + 0.04 * k, 0.09 + 0.04 * k))


PITCH = 1.15


def grip(M):
    """the leather wrap: a strap wound up the grip in a helix, its edges raised (the grip's sheet: u round, v along)"""
    g0, g1 = -10.0, 11.0
    nr, nc = 120, 32
    zz = np.linspace(g0, g1, nr)
    th = np.linspace(0, 2 * np.pi, nc + 1)
    Z, T = np.meshgrid(zz, th, indexing='ij')
    ph = T / (2 * np.pi) + (Z - g0) / PITCH
    ridge = np.abs(np.sin(np.pi * ph)) ** 0.35
    r = shaft_r(Z) + 0.12 + 0.07 * ridge
    V = np.stack([r * np.cos(T), r * np.sin(T), Z], -1)
    N = g.outward(g.grid_n(V[:, :-1]), V[:, :-1], (0, 0))
    N = np.concatenate([N, N[:, :1]], 1)
    UV = np.stack([T / (2 * np.pi), (Z - g0) / (g1 - g0)], -1)
    M.add('Grip', 'grip', V.reshape(-1, 3), N.reshape(-1, 3), UV.reshape(-1, 2), grid_faces(nr, nc + 1))


VIAL_TH = np.radians(np.arange(6) * 60.0 + 90.0)   # (the fire vial facing ahead of the staff's face: the thumb's side is free)


def vial_xy(k):
    t = VIAL_TH[k]
    return VIAL_RING * math.cos(t), VIAL_RING * math.sin(t)


def collar(M):
    """the collar the vials stand on: a flared brass cup on the shaft's top, a ring of six sockets, a fluted rim"""
    c0, c1 = COLLAR
    prof = [(c0 - 0.4, float(shaft_r(c0)) - 0.02), (c0, float(shaft_r(c0)) + 0.18), (c0 + 0.6, 1.25), (c0 + 1.4, 2.1), (c0 + 2.2, 3.6),
            (c0 + 2.5, 3.9), (c0 + 2.75, 3.95), (c0 + 2.95, 3.75), (c0 + 3.0, 3.2), (c0 + 3.05, 2.0), (c0 + 3.4, 1.6), (c0 + 4.5, 1.25), (c1, 1.1), (c1 + 0.05, 0.0)]
    M.add('Brass', 'brass', *lathe(prof, 60, 0.02, 0.3, 0.3, 0.38))
    # the vials' cups and clasps
    z0, z1 = VIAL_Z
    for k in range(6):
        cx, cy = vial_xy(k)
        cup = [(z0 - 0.5, 0.0), (z0 - 0.5, VIAL_R * 0.9), (z0 - 0.3, VIAL_R * 1.18), (z0 + 0.5, VIAL_R * 1.2), (z0 + 0.6, VIAL_R * 1.05)]
        M.add('Brass', 'brass', *lathe(cup, 24, 0.02, 0.3, 0.42 + 0.03 * k, 0.44 + 0.03 * k, cx, cy))
        clasp = [(z0 + 2.4, VIAL_R * 1.02), (z0 + 2.5, VIAL_R * 1.14), (z0 + 2.9, VIAL_R * 1.14), (z0 + 3.0, VIAL_R * 1.02)]
        M.add('Brass', 'brass', *lathe(clasp, 24, 0.02, 0.3, 0.6 + 0.02 * k, 0.61 + 0.02 * k, cx, cy))


def vials(M):
    """six glass vials (the liquid is an effect, skinned: fx below); a brass cap and spigot on each. Returns the spigots"""
    tops = []
    z0, z1 = VIAL_Z
    R = VIAL_R
    for k in range(6):
        cx, cy = vial_xy(k)
        prof = [(z0 + 0.02, 0.0), (z0 + 0.06, R * 0.55), (z0 + 0.2, R * 0.88), (z0 + 0.45, R)] + \
               [(z, R) for z in np.linspace(z0 + 0.7, z1 - 1.0, 6)] + \
               [(z1 - 0.75, R * 0.92), (z1 - 0.55, R * 0.62), (z1 - 0.35, R * 0.58), (z1 - 0.25, R * 0.7), (z1 - 0.12, R * 0.7), (z1 - 0.06, R * 0.55)]
        M.add('Glass', 'glass', *lathe(prof, 28, cx=cx, cy=cy))
        cp = [(z1 - 0.28, R * 0.74), (z1 - 0.28, R * 0.82), (z1 + 0.07, R * 0.82), (z1 + 0.1, R * 0.78), (z1 + 0.28, R * 0.6),
              (z1 + 0.4, R * 0.3), (z1 + 0.65, R * 0.22), (z1 + 0.85, R * 0.25), (z1 + 0.88, 0.0)]
        M.add('Brass', 'brass', *lathe(cp, 24, 0.02, 0.3, 0.05 + 0.05 * k, 0.08 + 0.05 * k, cx, cy))
        tops.append(np.array([cx, cy, z1 + 0.84]))
    return tops


def cage(M):
    """six brass ribs from the collar's rim up round the orb (between the vials) to the finial; a ring at the orb's waist;
    the finial: a spire with a bead"""
    base_z = COLLAR[0] + 2.9
    for k in range(6):
        t = VIAL_TH[k] + math.radians(30)
        pts = []
        for s in np.linspace(0, 1, 26):
            z = base_z + (TOP - 4.2 - base_z) * s
            rr = 3.75 * (1 - s) ** 0.35 * (0.35 + 0.65 * math.sin(math.pi * min(1.0, s * 1.05)) ** 0.7) + 0.0
            rr = max(rr, 0.35) if s < 0.98 else 0.3
            rr = rr + 1.0 * math.sin(math.pi * s) ** 1.5 * (CAGE_R - 3.0)
            pts.append((rr * math.cos(t), rr * math.sin(t), z))
        pts = g.spline(np.array(pts), 60)
        M.add('Brass', 'brass', *g.tube(pts, np.linspace(0.26, 0.17, 60), 10, vrange=(0.7, 0.74), urange=(0.05, 0.12)))
    # the ring at the orb's waist, and a smaller one above
    for zc, rr in ((ORB_C, ORB_R + 0.35), (ORB_C + 2.6, 1.9)):
        a = np.linspace(0, 2 * np.pi, 97)
        ring = np.stack([rr * np.cos(a), rr * np.sin(a), np.full_like(a, zc)], -1)
        M.add('Brass', 'brass', *g.tube(ring, 0.14, 8, vrange=(0.76, 0.78), urange=(0.05, 0.4)))
    fz = TOP - 4.4
    spire = [(fz, 0.0), (fz, 0.9), (fz + 0.5, 1.05), (fz + 0.9, 0.8), (fz + 1.6, 0.42), (fz + 2.0, 0.55), (fz + 2.4, 0.42), (fz + 3.4, 0.18), (TOP, 0.0)]
    M.add('Brass', 'brass', *lathe(spire, 28, 0.02, 0.3, 0.8, 0.84))
    bead = [(fz + 2.2 + 0.45 * math.cos(b), 0.45 * math.sin(b)) for b in np.linspace(np.pi, 0, 12)]
    M.add('Brass', 'brass', *lathe(bead, 20, 0.02, 0.3, 0.85, 0.86))


def orb(M):
    """the glass orb, seated on a cup at the collar's top"""
    prof = [(ORB_C - ORB_R * math.cos(a), ORB_R * math.sin(a)) for a in np.linspace(0, np.pi, 26)]
    M.add('Glass', 'orbglass', *lathe(prof, 48))
    zb = ORB_C - ORB_R
    seat = [(COLLAR[1] - 0.2, 1.0), (COLLAR[1] + 0.4, 0.62), (COLLAR[1] + 1.4, 0.5), (COLLAR[1] + 2.2, 0.62), (COLLAR[1] + 2.5, 0.5),
            (zb - 0.9, 0.55), (zb - 0.3, 1.05), (zb + 0.25, 1.5), (zb + 0.55, 1.62), (zb + 0.62, 1.45)]   # (a slender stem, a ring knop, the orb's cup)
    M.add('Brass', 'brass', *lathe(seat, 36, 0.02, 0.3, 0.88, 0.9))


def tubes(M, tops):
    """six glass tubes, each from its vial's spigot curving in and up into the orb's lower half; brass ferrules at both ends"""
    paths = []
    for k in range(6):
        p0 = tops[k]
        t = VIAL_TH[k]
        d = np.array([math.cos(t), math.sin(t), 0.0])
        entry_dir = unit(np.array([d[0] * 0.75, d[1] * 0.75, -0.66]))
        p3 = np.array([0, 0, ORB_C]) + entry_dir * (ORB_R + 0.05)
        way = [p0, p0 + np.array([0, 0, 0.7]), p0 + d * 0.3 + np.array([0, 0, 1.6]), p3 + entry_dir * 1.0, p3]
        path = g.spline(np.array(way), 40)
        M.add('Tube', 'tubeglass', *g.tube(path, 0.2, 10))
        for q in (2, len(path) - 3):
            seg = path[max(0, q - 2):q + 3]
            M.add('Brass', 'brass', *g.tube(seg, 0.27, 10, vrange=(0.97, 0.99), urange=(0.05, 0.12)))
        paths.append(path)
    return paths


# ------------------------------------------------------------------ textures
def wood_tex():
    """2048: dark weathered wood, its grain running up the staff (v), slow wavering streaks, a few knots, rubbed paler
    and polished where hands have worn it (mid-shaft); a fine pore detail in the normal map"""
    S = 2048
    v, u = (np.mgrid[0:S, 0:S].astype(np.float32) + 0.5) / S
    streak = tileable(S, S, 1.2, 81, aniso=(1.0, 0.04))
    fine = tileable(S, S, 0.5, 82, aniso=(1.0, 0.25))
    warp = tileable(S, S, 2.0, 83)
    grain = np.sin((u * 18 + 1.6 * warp + 0.6 * streak) * 2 * np.pi) * 0.5 + 0.5
    rng = np.random.default_rng(84)
    knot = np.zeros((S, S), np.float32)
    for _ in range(7):
        cu, cv, r = rng.random(), rng.random(), 0.012 + 0.02 * rng.random()
        du = np.minimum(np.abs(u - cu), 1 - np.abs(u - cu)) / r; dv = (v - cv) / (r * 2.2)
        d = np.hypot(du, dv)
        knot = np.maximum(knot, np.exp(-d ** 2) + 0.4 * np.exp(-((d - 1.4) / 0.4) ** 2) * np.cos(d * 9) ** 2)
    lum = 0.55 + 0.25 * (grain - 0.5) + 0.3 * (streak - 0.5) + 0.12 * (fine - 0.5) - 0.45 * knot
    worn = np.exp(-((v - 0.36) / 0.14) ** 2)                   # (the hand's place on the shaft, rubbed and polished)
    lum = lum * (1 + 0.18 * worn)
    base = np.array([62, 40, 27], np.float32)
    col = base * np.clip(lum, 0.15, 1.6)[..., None]
    col = col * (1 - 0.25 * knot[..., None]) + np.array([30, 18, 12]) * 0.25 * knot[..., None]
    D = g.save(col, 'mutagen_wood_d.dds')
    h = 0.5 * grain + 0.35 * streak + 0.25 * fine - 0.8 * knot
    nrm = height_to_normal(gaussian_filter(h, 0.7), 4.0)
    spec = np.clip(55 + 60 * worn + 20 * (fine - 0.5), 0, 255)
    N = g.save(np.dstack([nrm, spec]), 'mutagen_wood_n.dds', True)
    return D, N


def grip_tex():
    """2048: the grip's dark oxblood leather strap - the helix's bands (phase = u + along / PITCH), pebbled grain, stitched
    edges, darkened and shined where held"""
    S = 2048
    v, u = (np.mgrid[0:S, 0:S].astype(np.float32) + 0.5) / S
    along = v * 21.0
    ph = u + along / PITCH
    band = np.abs(np.sin(np.pi * ph))
    edge = np.exp(-((band - 0.25) / 0.05) ** 2)
    d1, d2 = g.cells(512, 2600, 91)
    peb = np.tile(np.clip((d2 - d1) / 6.0, 0, 1), (4, 4))
    fine = tileable(S, S, 0.6, 92)
    stitch = (np.sin(ph * 2 * np.pi * 0.5 + 0.0) * 0) + (((ph * 40) % 1.0) < 0.55) * np.exp(-((band - 0.33) / 0.025) ** 2)
    lum = (0.5 + 0.5 * band ** 0.4) * (0.82 + 0.25 * peb) * (0.9 + 0.2 * fine)
    col = np.array([70, 34, 24], np.float32) * lum[..., None]
    col = col * (1 - 0.5 * edge[..., None])
    col = col * (1 - stitch[..., None]) + np.array([190, 170, 135]) * stitch[..., None]
    D = g.save(col, 'mutagen_grip_d.dds')
    nrm = height_to_normal(gaussian_filter(band ** 0.4 * 1.2 + peb * 0.35 + stitch * 0.5, 0.6), 6.0)
    spec = np.clip(40 + 60 * band * peb + 40 * stitch, 0, 255)
    N = g.save(np.dstack([nrm, spec]), 'mutagen_grip_n.dds', True)
    return D, N


# ------------------------------------------------------------------ effects (fxwrite)
def fx_job(base, out, paths):
    L = [f'base {base}', f'out {out}']
    tex = g.FX

    def emit(V, F):
        for p, uu, vv, n in V:
            L.append(f'v {p[0]:.4f} {p[1]:.4f} {p[2]:.4f} {uu:.4f} {vv:.4f} 1 1 1 1')
            L.append(f'n {n[0]:.4f} {n[1]:.4f} {n[2]:.4f}')
        for a, b, c in F:
            L.append(f't {a} {b} {c}')

    def card(name, t, at, hs, col, sc, flip=None):
        extra = f' flip={flip[0]}x{flip[1]} fps={flip[2]} phase={flip[3]}' if flip else ''
        L.append(f'shape {name} tex={t} color={col} scale={sc} blend=add billboard=1 at={at[0]:.3f},{at[1]:.3f},{at[2]:.3f}{extra}')
        cu, cv = (1 / flip[0], 1 / flip[1]) if flip else (1, 1)
        for X, Y, uu, vv in ((-hs, hs, 0, 0), (hs, hs, cu, 0), (hs, -hs, cu, cv), (-hs, -hs, 0, cv)):
            L.append(f'v {X:.3f} {Y:.3f} 0 {uu} {vv} 1 1 1 1')
        L.extend(['t 0 1 2', 't 0 2 3'])

    oc = np.array([0, 0, ORB_C])
    L.append(f'shape OrbSwirlA tex={tex["swirl"]} color=1,1,1,1 scale=1.8 blend=add falloff=1 uscroll=3.2 vscroll=9')
    emit(*g.sphere(oc, ORB_R * 0.9, 36, 18))
    L.append(f'shape OrbSwirlB tex={tex["swirl"]} color=1,1,1,0.9 scale=1.5 blend=add falloff=1 uscroll=-4.6 vscroll=-13')
    V, F = g.sphere(oc, ORB_R * 0.66, 30, 15)
    emit([(p, uu * 2 + 0.37, vv + 0.21, n) for p, uu, vv, n in V], F)
    L.append(f'shape OrbRim tex={tex["glow"]} color=0.85,0.8,1,0.6 scale=1.2 blend=add rim=1')
    V, F = g.sphere(oc, ORB_R * 1.02, 36, 18)
    emit([(p, 0.5, 0.5, n) for p, uu, vv, n in V], F)
    card('OrbCore', tex['core'], oc, ORB_R * 0.95, '1,1,1,1', 2.2, (4, 4, 10, 0))
    card('OrbHalo', tex['glow'], oc, ORB_R * 2.6, '0.8,0.75,1,0.5', 0.9)
    for e in range(6):
        c = ELC[ELS[e]]
        L.append(f'shape OrbTint{e} tex={tex["swirl"]} color={c[0]:.2f},{c[1]:.2f},{c[2]:.2f},1 scale=3.0 blend=add falloff=1 uscroll=1.6 vscroll=4')
        emit(*g.sphere(oc, ORB_R * 0.97, 30, 15))
        card(f'OrbFlare{e}', tex['core'], oc, ORB_R * 2.2, f'{c[0]:.2f},{c[1]:.2f},{c[2]:.2f},1', 2.0, (4, 4, 16, 0))
    for k, path in enumerate(paths):
        c = ELC[ELS[k]]
        L.append(f'shape TubeLight{k} tex={tex["pulse"]} color={c[0]:.2f},{c[1]:.2f},{c[2]:.2f},1 scale=2.4 blend=add vscroll=-{1.0 + 0.13 * k:.2f}')
        Pp, N, UV, F = g.tube(path, 0.11, 6, vrange=(0, np.linalg.norm(np.diff(path, axis=0), axis=1).sum() / 3.0))
        emit([(Pp[i], UV[i][0], UV[i][1], N[i]) for i in range(len(Pp))], F)
    # the vials' liquid: skinned to Liq<e>_B (its foot) and Liq<e>_T (its surface), as the glove's
    z0, z1 = VIAL_Z
    zb, zt = z0 + 0.45, z0 + 0.15 + (z1 - z0 - 1.25) * 0.86
    R = VIAL_R
    for k in range(6):
        cx, cy = vial_xy(k)
        e = k; c = ELC[ELS[e]]
        L.append(f'node LiqN{e} MutagenStaff 0 0 0')
        L.append(f'node Liq{e}_B LiqN{e} {cx:.4f} {cy:.4f} {zb:.4f}')
        L.append(f'node Liq{e}_T LiqN{e} {cx:.4f} {cy:.4f} {zt:.4f}')

        def skinned(name, prof, ru, opts):
            L.append(f'shape {name} {opts} parent=LiqN{e} skin=Liq{e}_B,Liq{e}_T')
            Pp, N, UV, F = g.lathe_z(prof, cx, cy, 20, 0, ru, 0, 1)
            for i2 in range(len(Pp)):
                q = Pp[i2]; n = N[i2]
                wt = float(np.clip((q[2] - zb) / (zt - zb), 0, 1))
                L.append(f'v {q[0]:.4f} {q[1]:.4f} {q[2]:.4f} {UV[i2][0]:.4f} {UV[i2][1] * (zt - z0) / 1.8:.4f} 1 1 1 1')
                L.append(f'n {n[0]:.4f} {n[1]:.4f} {n[2]:.4f}')
                L.append(f'w 0 {1 - wt:.4f} 1 {wt:.4f}' if 0 < wt < 1 else ('w 1 1' if wt >= 1 else 'w 0 1'))
            for a, b, cc in F:
                L.append(f't {a} {b} {cc}')
        body = [(z0 + 0.09, 0.0), (z0 + 0.13, R * 0.5), (z0 + 0.26, R * 0.8), (zb, R * 0.9)] + [(z, R * 0.9) for z in np.linspace(zb + 0.25, zt - 0.04, 6)] + [(zt, R * 0.86), (zt + 0.002, 0.0)]
        skinned(f'LiqBody{e}', body, 1, f'tex={tex["liquid"]} color={c[0]:.2f},{c[1]:.2f},{c[2]:.2f},0.9 scale=1.1 blend=alpha uscroll={6.0 + 0.7 * k:.2f}')
        inner = [(z0 + 0.15, 0.0), (z0 + 0.26, R * 0.55), (zb, R * 0.7)] + [(z, R * 0.72) for z in np.linspace(zb + 0.25, zt - 0.08, 5)] + [(zt - 0.05, 0.0)]
        skinned(f'LiqGlow{e}', inner, 2, f'tex={tex["swirl"]} color={c[0]:.2f},{c[1]:.2f},{c[2]:.2f},1 scale=1.5 blend=add falloff=1 uscroll={3.5 + 0.4 * k:.2f} vscroll={5.0 + 0.5 * k:.2f}')
        skinned(f'VialBubbles{e}', [(zb, R * 0.8), (zt - 0.06, R * 0.8)], 1, f'tex={tex["bubbles"]} color={c[0]:.2f},{c[1]:.2f},{c[2]:.2f},1 scale=1.6 blend=add vscroll=-{1.8 + 0.23 * k:.2f}')
        card(f'VialGlow{e}', tex['glow'], np.array([cx, cy, (z0 + z1) / 2]), 1.7, f'{c[0]:.2f},{c[1]:.2f},{c[2]:.2f},0.5', 0.7)
    # motes of the six colours circling the head
    for band, (rad, za, zb2, us, vs, alpha) in enumerate(((5.2, COLLAR[0] - 1.0, TOP - 3.0, 5.5, 14, 0.9), (6.6, COLLAR[0] + 1.0, TOP, -8.0, 21, 0.6))):
        L.append(f'shape Motes{band} tex={tex["motes"]} color=1,1,1,{alpha} scale=1.7 blend=add uscroll={us} vscroll=-{vs}')
        nu, nv = 48, 10
        for i in range(nv + 1):
            z = za + (zb2 - za) * i / nv
            for j in range(nu + 1):
                th = 2 * math.pi * j / nu
                fade = math.sin(math.pi * i / nv) ** 1.5
                L.append(f'v {rad * math.cos(th):.4f} {rad * math.sin(th):.4f} {z:.4f} {j / nu * 3:.4f} {i / nv * 1.5:.4f} 1 1 1 {fade:.3f}')
        for i in range(nv):
            for j in range(nu):
                a = i * (nu + 1) + j; b = a + 1; cc = a + nu + 1; d = cc + 1
                L += [f't {a} {cc} {b}', f't {b} {cc} {d}']
    return L


def turn_y(L):
    """everything built along Z turned so it runs up +Y: (x, y, z) -> (x, z, -y), points and normals (and nodes)"""
    out = []
    bb = False
    for l in L:
        p = l.split(' ')
        if p[0] in ('v', 'n') and len(p) >= 4 and not bb:
            x, y, z = float(p[1]), float(p[2]), float(p[3])
            p[1], p[2], p[3] = f'{x:.4f}', f'{z:.4f}', f'{-y:.4f}'
            out.append(' '.join(p)); continue
        if p[0] == 'node':
            x, y, z = float(p[3]), float(p[4]), float(p[5])
            p[3], p[4], p[5] = f'{x:.4f}', f'{z:.4f}', f'{-y:.4f}'
            out.append(' '.join(p)); continue
        if p[0] == 'shape':
            bb = any(w.startswith('billboard=') and w != 'billboard=0' for w in p)   # (a card: its corners are its own, facing you)
            for i, w in enumerate(p):
                if w.startswith('at='):
                    x, y, z = (float(q) for q in w[3:].split(','))
                    p[i] = f'at={x:.3f},{z:.3f},{-y:.3f}'
            out.append(' '.join(p)); continue
        out.append(l)
    return out


def build(tex):
    M = Mesh()
    shaft(M); bands(M); grip(M); collar(M)
    tops = vials(M)
    cage(M); orb(M)
    paths = tubes(M, tops)
    merged = {}
    for gname, m, V, N, UV, F in M.groups:
        if m not in merged: merged[m] = [gname, m, [], [], [], [], 0]
        e = merged[m]; e[2].append(V); e[3].append(N); e[4].append(UV); e[5].append(np.asarray(F) + e[6]); e[6] += len(V)
    R = np.array([[1, 0, 0], [0, 0, 1], [0, -1, 0]], float)   # (Z up -> +Y)
    M.groups = [(m.capitalize(), m, np.vstack(e[2]) @ R.T, np.vstack(e[3]) @ R.T, np.vstack(e[4]), np.vstack(e[5])) for m, e in merged.items()]
    WORK.mkdir(parents=True, exist_ok=True)
    obj = WORK / 'mutagen_staff.obj'; M.write_obj(obj)
    static = WORK / 'mutagen_staff_static.nif'
    bD, bN = tex['brass']; gD, gN, tD = tex['glass']; wD, wN = tex['wood']; rD, rN = tex['grip']
    job = [f'in {obj}', f'out {static}', 'scale 1', 'ground 0', 'origin 1', 'offset 0 0 0',
           f'mat wood diffuse={wD} normal={wN} spec=0.6 gloss=30 alpha=0 doublesided=0',
           f'mat grip diffuse={rD} normal={rN} spec=0.5 gloss=22 alpha=0 doublesided=0',
           f'mat brass diffuse={bD} normal={bN} spec=1.9 gloss=90 alpha=0 doublesided=0',
           f'mat glass diffuse={gD} normal={gN} spec=2.4 gloss=180 alpha=blend doublesided=1',
           f'mat orbglass diffuse={gD} normal={gN} spec=2.6 gloss=200 alpha=blend doublesided=0',
           f'mat tubeglass diffuse={tD} normal={gN} spec=2.4 gloss=180 alpha=blend doublesided=1']
    jf = WORK / 'staff.job'; jf.write_text('\n'.join(job) + '\n')
    r = subprocess.run([str(TOOLS / 'modelconv'), str(jf)], capture_output=True, text=True)
    if r.returncode: print(r.stdout, r.stderr); raise SystemExit('modelconv failed')
    import throwdagger; throwdagger.WORK.mkdir(parents=True, exist_ok=True)
    rename_root(static, 'MutagenStaff')
    nif = MESHDIR / 'mutagen_staff.nif'
    L = fx_job(static, nif, paths)
    L = [L[0], L[1]] + turn_y(L[2:])
    (WORK / 'fx.job').write_text('\n'.join(g.fx_fix(L)) + '\n')
    r = subprocess.run([str(TOOLS / 'fxwrite'), str(WORK / 'fx.job')], capture_output=True, text=True)
    if r.returncode or 'no node' in r.stderr: print(r.stdout, r.stderr); raise SystemExit('fxwrite failed')
    subprocess.run([str(TOOLS / 'addprn'), str(nif), str(nif), 'WeaponStaff'], check=True)
    lo, hi = M.bounds()
    print(f'{nif.name}: x {lo[0]:.1f}..{hi[0]:.1f}  y {lo[1]:.1f}..{hi[1]:.1f}  z {lo[2]:.1f}..{hi[2]:.1f}; {sum(len(gg[5]) for gg in M.groups)} triangles')
    return M, obj


if __name__ == '__main__':
    WORK.mkdir(parents=True, exist_ok=True)
    tool('addprn')
    tex = {'brass': g.brass_tex(), 'glass': g.glass_tex(), 'wood': wood_tex(), 'grip': grip_tex()}
    g.FX.update(g.fx_tex())
    build(tex)
    g.build_beams()
    if '--keep' not in sys.argv:
        shutil.rmtree(WORK, ignore_errors=True)
