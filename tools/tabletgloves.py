"""(Noah: the Tablet's fist weapons - "they look like paper thin rings instead of real hand wraps or elemental brass
knuckles") the Stormbound Knuckles and the Emberclaw Wraps made as gloves worn on the hands (an armour piece in the hands'
slot, put on with the weapon - src/Tablet.cpp), skinned to the hand's own bones so they close with the fist:

  - wraps: thick strips wound round the palm and wrist and between the knuckles - a solid shell (an outside, an inside
    and its edges) laid over the hand, its turns standing proud, the strip's stitched edges along each turn
  - the knuckles: a heavy brass ring round each finger's first joint with a raised plate over the knuckle and a storm
    gem set in it, each riding its own finger bone; a brass grip bar across the palm on the hand bone

Fitted over Skyrim's own hands (read at build time from your install's 1stpersonmalehands_1.nif and malehands_1.nif - their
shape is not copied, only stood upon), first person and third, both hands.

    python3 tools/tabletgloves.py [stormknuckles] [emberclaw] [--keep]
      -> assets/fx/Meshes/CurseOfAlduin/tablet/<id>_gloves_1st.nif, <id>_gloves.nif (+ textures beside the weapons')"""
import math, pathlib, shutil, subprocess, sys
import numpy as np
from scipy.ndimage import gaussian_filter
from PIL import Image

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import tabletweapons as tw
from tabletweapons import pw, g, unit, tileable, height_to_normal, save, metal_tex, leather_tex, cloth_tex, gem_tex, superbox, torus

ROOT = pw.ROOT
GAME = pathlib.Path('/mnt/user-data/uploads/common--Skyrim Special Edition/Data/Meshes/actors/Character/character assets')
HANDS = {'1st': GAME / '1stpersonmalehands_1.nif', '3rd': GAME / 'malehands_1.nif'}
WORK = ROOT / 'build' / 'tabletgloves'
S = 4096


# ================================================================== the hands, read
class Hands:
    def __init__(self, nif):
        out = WORK / ('hands_' + nif.stem)
        out.mkdir(parents=True, exist_ok=True)
        subprocess.run([str(pw.TOOLS / 'nifexport'), str(nif), str(out)], check=True, capture_output=True)
        self.order, self.s2b, V, B, Wt, F = [], {}, [], [], [], []
        for l in open(out / 'shape_0.txt'):
            p = l.rstrip('\n').split('\t')
            if p[0] == 'bone':
                self.order.append(p[1])
                self.s2b[p[1]] = (np.array(p[3].split(), float).reshape(3, 3), np.array(p[2].split(), float))
            elif l.startswith('v '):
                q = l.split(); V.append([float(x) for x in q[1:4]]); B.append([int(x) for x in q[6:10]]); Wt.append([float(x) for x in q[10:14]])
            elif l.startswith('f '):
                F.append([int(x) for x in l.split()[1:4]])
        self.V, self.B, self.W, self.F = np.array(V), np.array(B), np.array(Wt), np.array(F)
        self.N = vnormals(self.V, self.F)

    def idx(self, name): return self.order.index(name)

    def local(self, bone, P):
        R, t = self.s2b[bone]; return np.asarray(P) @ R.T + t

    def skin(self, bone, L):
        R, t = self.s2b[bone]; return (np.asarray(L) - t) @ R            # (R orthonormal)

    def dirskin(self, bone, d):
        R, _ = self.s2b[bone]; return np.asarray(d) @ R

    def origin(self, bone): return self.skin(bone, np.zeros(3))


def vnormals(V, F):
    fn = np.cross(V[F[:, 1]] - V[F[:, 0]], V[F[:, 2]] - V[F[:, 0]])
    N = np.zeros_like(V)
    for k in range(3): np.add.at(N, F[:, k], fn)
    return unit(N + 1e-12)


def subdivide(V, F, attrs):
    """each triangle into four (shared midpoints); per-vertex attributes (arrays) averaged onto the midpoints"""
    edges = {}
    V = list(V); attrs = [list(a) for a in attrs]
    def mid(a, b):
        k = (min(a, b), max(a, b))
        if k not in edges:
            edges[k] = len(V); V.append((np.asarray(V[a]) + np.asarray(V[b])) / 2)
            for A in attrs: A.append((np.asarray(A[a], float) + np.asarray(A[b], float)) / 2)
        return edges[k]
    F2 = []
    for a, b, c in F:
        ab, bc, ca = mid(a, b), mid(b, c), mid(c, a)
        F2 += [(a, ab, ca), (ab, b, bc), (ca, bc, c), (ab, bc, ca)]
    return np.array(V, float), np.array(F2), [np.array(A, float) for A in attrs]


def dense_weights(H, B, W):
    """(n, bones) weights from the 4-slot lists"""
    D = np.zeros((len(B), len(H.order)))
    for k in range(4): np.add.at(D, (np.arange(len(B)), B[:, k]), W[:, k])
    return D


def top4(D):
    o = np.argsort(-D, 1)[:, :4]
    w = np.take_along_axis(D, o, 1); w = w / np.maximum(w.sum(1, keepdims=True), 1e-9)
    return o, w


# ================================================================== the pieces
class Piece:
    """one material's mesh in skin space, weighted"""
    def __init__(self): self.V, self.N, self.UV, self.Wd, self.F = [], [], [], [], []

    def add(self, V, N, UV, Wd, F):
        b = sum(len(x) for x in self.V)
        self.V.append(np.asarray(V, float)); self.N.append(np.asarray(N, float)); self.UV.append(np.asarray(UV, float))
        self.Wd.append(np.asarray(Wd, float)); self.F.append(np.asarray(F) + b)

    def arrays(self):
        return np.vstack(self.V), np.vstack(self.N), np.vstack(self.UV), np.vstack(self.Wd), np.vstack(self.F)


def shell(H, side, keep, P, Wd, F, lift_in, lift_out, ridge, pitch, piece, seed=0, zcut=None):
    """a solid wrapping over the faces in `keep`: an outside lift_out off the skin (plus the turns' ridge), an inside
    lift_in off it, and the edges joining them where the wrapping ends. UV: u along the strip, v across it"""
    Fk = F[keep]
    used = np.unique(Fk)
    remap = -np.ones(len(P), int); remap[used] = np.arange(len(used))
    Vu, Wu, Fu = P[used], Wd[used], remap[Fk]
    hb = f'NPC {side} Hand [{side}Hnd]'
    if zcut is not None:   # (the wrapping's ends cut straight, not stepped along the faces)
        from collections import Counter
        ec = Counter((min(e), max(e)) for a, b, c in Fu for e in ((a, b), (b, c), (c, a)))
        rimv = np.unique([v for k, n in ec.items() if n == 1 for v in k])
        Lv = H.local(hb, Vu)
        fing = np.zeros(len(Vu), bool)
        for fi in range(5):
            for k in range(3):
                nm = f'NPC {side} Finger{fi}{k} [{side}F{fi}{k}]'
                if nm in H.order: fing |= Wu[:, H.idx(nm)] > 0.7
        top = rimv[(Lv[rimv, 2] > zcut - 1.3) & ~fing[rimv]]
        bot = rimv[Lv[rimv, 2] < -3.4]
        Lv[top, 2] = zcut
        Lv[bot, 2] = -4.0
        Vu = Vu.copy(); Vu[top] = H.skin(hb, Lv[top]); Vu[bot] = H.skin(hb, Lv[bot])
    Nu = vnormals(Vu, Fu)
    L = H.local(hb, Vu)
    if side == 'L': L = L * [-1, 1, 1]
    th = np.arctan2(L[:, 1], L[:, 0])
    ph = th / (2 * np.pi) + L[:, 2] / pitch
    turn = np.abs(np.sin(np.pi * ph)) ** 0.35
    wob = 0.03 * np.sin(L[:, 2] * 2.7 + seed) + 0.02 * np.sin(th * 5 + seed)
    out = Vu + Nu * (lift_out + ridge * turn + wob)[:, None]
    inn = Vu + Nu * lift_in
    u = th / (2 * np.pi) * 3.0
    v = ph % 1.0
    # (where a turn's edge crosses inside a triangle the v wraps: those triangles use v unwrapped)
    UV = np.stack([u, ph], -1)
    piece.add(out, vnormals(out, Fu), UV, Wu, Fu)
    piece.add(inn, -vnormals(inn, Fu), UV, Wu, Fu[:, [0, 2, 1]])
    # the edges: each boundary edge (used by one face) closed with a quad from the inside to the outside
    from collections import Counter
    ec = Counter()
    for a, b, c in Fu:
        for e in ((a, b), (b, c), (c, a)): ec[(min(e), max(e))] += 1
    owner = {}
    for a, b, c in Fu:
        for e in ((a, b), (b, c), (c, a)): owner[(min(e), max(e))] = e
    rim = [owner[k] for k, n in ec.items() if n == 1]
    if rim:
        n0 = len(Vu)
        Vr = np.vstack([out, inn]); Wr = np.vstack([Wu, Wu]); UVr = np.vstack([UV, UV + [0, 0.05]])
        Fr = []
        for a, b in rim:
            Fr += [(a, b + n0, b), (a, a + n0, b + n0)]
        Fr = np.array(Fr)
        piece.add(Vr, vnormals(Vr, Fr), UVr, Wr, Fr)


def wraps(H, side, P, Wd, F, region, lift_in, lift_out, ridge, pitch, piece, seed=0):
    keep = region(H, side, P, Wd, F)
    shell(H, side, keep, P, Wd, F, lift_in, lift_out, ridge, pitch, piece, seed, getattr(region, 'zmax', None))


def hand_region(zmax, thumb=False, fingers=0.0):
    """faces on the hand (its bones dominating), from the wrist up to local height zmax; the thumb's own bones left bare
    unless thumb; fingers (0..1): how far up each finger's first joint the wrapping goes"""
    def f(H, side, P, Wd, F):
        hb = f'NPC {side} Hand [{side}Hnd]'
        L = H.local(hb, P)
        sideb = [i for i, n in enumerate(H.order) if f' {side} ' in n]
        dom = Wd.argmax(1)
        ok = np.isin(dom, sideb)
        thumbb = [H.idx(f'NPC {side} Finger0{k} [{side}F0{k}]') for k in range(3)]
        isthumb = np.isin(dom, thumbb) | (Wd[:, thumbb].sum(1) > 0.5)
        finger = np.zeros(len(P), bool)
        for fi in range(1, 5):
            b0 = f'NPC {side} Finger{fi}0 [{side}F{fi}0]'; b1 = f'NPC {side} Finger{fi}1 [{side}F{fi}1]'
            o0, o1 = H.origin(b0), H.origin(b1)
            ax = o1 - o0; s = ((P - o0) @ ax) / (ax @ ax)
            finger |= (Wd[:, [H.idx(b0), H.idx(b1), H.idx(b1.replace('1 [', '2 [').replace(f'F{fi}1]', f'F{fi}2]'))]].sum(1) > 0.5) & (s > fingers)
        okv = ok & (L[:, 2] < zmax) & (L[:, 2] > -4.5) & ~finger & (thumb | ~isthumb)
        return okv[F].all(1)
    f.zmax = zmax
    return f


def finger_rings(H, side, plate_mat, gem_mat, brass, gems, gem_at):
    """a heavy brass ring round each finger's first joint, a raised plate over its knuckle, a gem set in the plate"""
    hb = f'NPC {side} Hand [{side}Hnd]'
    back = unit(H.dirskin(hb, [0, 1.0, 0]))
    for fi in range(1, 5):
        b0 = f'NPC {side} Finger{fi}0 [{side}F{fi}0]'; b1 = f'NPC {side} Finger{fi}1 [{side}F{fi}1]'
        o0, o1 = H.origin(b0), H.origin(b1)
        ax = unit(o1 - o0); ln = np.linalg.norm(o1 - o0)
        c = o0 + ax * ln * 0.38
        dom = H.W.argmax(1); bi = H.idx(b0)
        mine = H.V[(H.B[np.arange(len(H.B)), dom] == bi)]
        rel = mine - c; along = rel @ ax
        near = rel[np.abs(along) < ln * 0.2] - np.outer(along[np.abs(along) < ln * 0.2], ax)
        R0 = (np.linalg.norm(near, axis=1).max() if len(near) else 0.9) + 0.12
        wd = np.zeros(len(H.order)); wd[bi] = 1.0
        P_, N_, UV_, F_ = torus(c, R0 + 0.34, 0.36, ax, 40, 12, stretch=1.25, long=ax)
        brass.add(P_, N_, UV_, np.tile(wd, (len(P_), 1)), F_)
        bk = unit(back - ax * (back @ ax))
        side_ax = unit(np.cross(ax, bk))
        Rm = np.column_stack([side_ax, bk, ax])
        pc = c + bk * (R0 + 0.55)
        P_, N_, UV_, F_ = superbox(pc, 1.05, 0.42, 0.95, 3.4, 20, 12, R=Rm)       # (the plate over the knuckle)
        brass.add(P_, N_, UV_, np.tile(wd, (len(P_), 1)), F_)
        for s_ in (-1, 1):   # (its flanges down the sides, where the next finger's plate meets it)
            P_, N_, UV_, F_ = superbox(c + side_ax * s_ * (R0 + 0.25) + bk * 0.25, 0.22, 0.55, 0.85, 3.0, 10, 8, R=Rm)
            brass.add(P_, N_, UV_, np.tile(wd, (len(P_), 1)), F_)
        P_, N_, UV_, F_ = superbox(pc + bk * 0.42, 0.62, 0.26, 0.55, 1.7, 16, 10, R=Rm)   # (the gem)
        gems.add(P_, N_, UV_ * 0.25 + fi * 0.2, np.tile(wd, (len(P_), 1)), F_)
        gem_at.append((b0, pc + bk * 0.7))


def palm_bar(H, side, brass):
    """the grip bar across the palm (on the hand bone): the dusters' brass, held in the fist"""
    hb = f'NPC {side} Hand [{side}Hnd]'
    sg = -1 if side == 'L' else 1
    Lr = H.local(hb, H.V)
    pts = []
    for x in np.linspace(-2.6, 1.6, 24):
        m = (np.abs(Lr[:, 0] - sg * x) < 0.6) & (np.abs(Lr[:, 2] - 6.6) < 0.6) & (Lr[:, 1] < 0)
        y = (Lr[m, 1].min() if m.any() else -3.6) - 0.55
        pts.append([sg * x, y, 6.6 - 0.12 * x])
    pts = np.array(pts); pts[:, 1] = gaussian_filter(pts[:, 1], 2.0, mode='nearest')
    P = H.skin(hb, np.array(pts))
    wd = np.zeros(len(H.order)); wd[H.idx(hb)] = 1.0
    P_, N_, UV_, F_ = pw.rod(P, 0.55, 14)
    brass.add(P_, N_, UV_, np.tile(wd, (len(P_), 1)), F_)


def coals(H, side, coal, iron, at):
    """live coals sewn into iron-rimmed pockets over the back of the hand and the knuckles (on the hand bone)"""
    hb = f'NPC {side} Hand [{side}Hnd]'
    sg = -1 if side == 'L' else 1
    wd = np.zeros(len(H.order)); wd[H.idx(hb)] = 1.0
    back = unit(H.dirskin(hb, [0, 1.0, 0]))
    rr = np.random.default_rng(91 if side == 'R' else 92)
    for x, z in ((-1.9, 4.2), (0.1, 4.7), (2.0, 4.0), (-0.9, 2.3), (1.1, 2.1), (-2.5, 7.1), (-0.8, 7.4), (0.9, 7.4), (2.5, 7.0)):
        # (set on the back of the hand's skin there, over the wrapping)
        Lr = H.local(hb, H.V)
        m = (np.abs(Lr[:, 0] - sg * x) < 0.7) & (np.abs(Lr[:, 2] - z) < 0.7)
        skin_y = Lr[m, 1].max() if m.any() else 1.8
        c = H.skin(hb, [sg * x, skin_y + 0.72, z])
        P_, N_, UV_, F_ = superbox(c, 0.5 + 0.12 * rr.random(), 0.36, 0.48 + 0.12 * rr.random(), 2.0, 14, 8)
        coal.add(P_, N_, UV_, np.tile(wd, (len(P_), 1)), F_)
        P_, N_, UV_, F_ = torus(c - back * 0.15, 0.68, 0.11, back, 24, 6)
        iron.add(P_, N_, UV_, np.tile(wd, (len(P_), 1)), F_)
        at.append(c + back * 0.5)


# ================================================================== textures (4K)
def strap_tex(name, base_rgb, seed=0, char=0.0):
    """4096: a leather (or cloth) strip wound round: v across the strip (its edges at 0 and 1 rolled and stitched), u
    along it - grain, creases, sweat-dark where gripped; char (0..1): scorched black in patches"""
    v, u = (np.mgrid[0:S, 0:S].astype(np.float32) + 0.5) / S
    vv = v % 1.0
    edge = np.clip(1 - np.minimum(vv, 1 - vv) / 0.07, 0, 1)
    stitch = ((np.abs(vv - 0.1) < 0.008) | (np.abs(vv - 0.9) < 0.008)) & ((u * 140) % 1.0 < 0.55)
    d1, d2 = g.cells(512, 2400, 950 + seed)
    peb = np.tile(np.clip((d2 - d1) / 6.0, 0, 1), (S // 512, S // 512))
    fine = tileable(S, S, 0.5, 951 + seed); big = tileable(S, S, 2.0, 952 + seed)
    crease = np.exp(-((tileable(S, S, 1.0, 953 + seed, aniso=(0.25, 1.0)) - 0.5) / 0.01) ** 2)
    lum = 0.8 + 0.25 * (big - 0.5) + 0.1 * (fine - 0.5) + 0.1 * peb - 0.3 * crease - 0.35 * edge
    col = np.array(base_rgb, np.float32) * lum[..., None]
    col = np.where(stitch[..., None], np.array([150, 130, 100], np.float32) * (0.8 + 0.2 * fine[..., None]), col)
    if char:
        sc = np.clip((tileable(S, S, 1.5, 954 + seed) - 0.5) * 3, 0, 1) * char
        col = col * (1 - 0.85 * sc[..., None]) + np.array([14, 11, 9]) * 0.85 * sc[..., None]
    D = save(col, name + '_d.dds')
    h = 0.5 * peb + 0.3 * fine - 0.8 * crease - 1.2 * edge ** 2 + 0.6 * stitch
    N = save(np.dstack([height_to_normal(gaussian_filter(h, 0.7 * 2), 9.0 * 2), np.clip(26 + 22 * peb - 20 * crease, 0, 255)]), name + '_n.dds', True)
    return D, N


# ================================================================== the gloves
def write(name, view, H, pieces, mats):
    """the job for tools/modelconv/skinglove.cpp: our pieces on the template's bones"""
    W = WORK; W.mkdir(parents=True, exist_ok=True)
    lines = [f'template {HANDS[view]}', f'out {pw.MESHDIR / (name + ("_gloves_1st" if view == "1st" else "_gloves") + ".nif")}']
    tri = 0
    for m, pc in pieces.items():
        if not pc.V: continue
        V, N, UV, Wd, F = pc.arrays()
        o, w = top4(Wd)
        lines.append(f'shape {name}_{m} {mats[m]}')
        for i in range(len(V)):
            lines.append(f'v {V[i][0]:.4f} {V[i][1]:.4f} {V[i][2]:.4f} {UV[i][0]:.5f} {UV[i][1]:.5f} {N[i][0]:.4f} {N[i][1]:.4f} {N[i][2]:.4f} ' +
                         ' '.join(f'{o[i][k]} {w[i][k]:.4f}' for k in range(4) if w[i][k] > 0.001))
        lines += [f't {a} {b} {c}' for a, b, c in F]; tri += len(F)
    jf = W / f'{name}_{view}.job'; jf.write_text('\n'.join(lines) + '\n')
    r = subprocess.run([str(pw.TOOLS / 'skinglove'), str(jf)], capture_output=True, text=True)
    if r.returncode: print(r.stdout, r.stderr); raise SystemExit('skinglove failed')
    print(f'{name} ({view}): {tri} triangles')
    # (a preview OBJ in skin space, with the hand)
    with open(W / f'{name}_{view}.obj', 'w') as f:
        vi = 1
        for m, pc in pieces.items():
            if not pc.V: continue
            V, N, UV, Wd, F = pc.arrays()
            for p in V: f.write(f'v {p[0]:.4f} {p[1]:.4f} {p[2]:.4f}\n')
            for q in UV: f.write(f'vt {q[0]:.4f} {q[1]:.4f}\n')
            for n in N: f.write(f'vn {n[0]:.4f} {n[1]:.4f} {n[2]:.4f}\n')
            f.write(f'usemtl {m}\n')
            for a, b, c in F + vi: f.write(f'f {a}/{a}/{a} {b}/{b}/{b} {c}/{c}/{c}\n')
            vi += len(V)
        hv = len(H.V)
        for p in H.V: f.write(f'v {p[0]:.4f} {p[1]:.4f} {p[2]:.4f}\n')
        for n in H.N: f.write(f'vn {n[0]:.4f} {n[1]:.4f} {n[2]:.4f}\n')
        f.write('usemtl skin\n')
        for a, b, c in H.F + vi: f.write(f'f {a}//{a} {b}//{b} {c}//{c}\n')


def write_show(name, H, pieces, mats, fx=None):
    """(the glove rising out of the tablet's pool: tabshow_<id>) the right glove alone, still, in the right hand's own frame
    (the fingers up) - a plain lit model"""
    hb = 'NPC R Hand [RHnd]'
    rset = [i for i, n in enumerate(H.order) if ' R ' in n]
    M = pw.Mesh()
    for m, pc in pieces.items():
        if not pc.V: continue
        V, N, UV, Wd, F = pc.arrays()
        right = np.isin(Wd.argmax(1), rset)
        keepf = right[F].all(1)
        used = np.unique(F[keepf]); rm = -np.ones(len(V), int); rm[used] = np.arange(len(used))
        R, t = H.s2b[hb]
        M.add(m.capitalize(), m, V[used] @ R.T + t, N[used] @ R.T, UV[used], rm[F[keepf]])
    W = WORK; obj = W / f'{name}_show.obj'; M.write_obj(obj)
    static = W / f'{name}_show_static.nif'
    job = [f'in {obj}', f'out {static}', 'scale 1.6', 'ground 0', 'origin 1', 'offset 0 0 0'] + [f'mat {k} {v} alpha=0 doublesided=1' for k, v in mats.items()]
    (W / f'{name}_show.job').write_text('\n'.join(job) + '\n')
    r = subprocess.run([str(pw.TOOLS / 'modelconv'), str(W / f'{name}_show.job')], capture_output=True, text=True)
    if r.returncode: print(r.stdout, r.stderr); raise SystemExit('modelconv failed (show)')
    shutil.copy(static, pw.MESHDIR / f'{name}_show.nif')
    print(f'{name}_show.nif')


def dense_mesh(H):
    D = dense_weights(H, H.B, H.W)
    P, F, (Dw,) = subdivide(H.V, H.F, [D])
    P, F, (Dw,) = subdivide(P, F, [Dw])
    return P, Dw, F


def stormknuckles(tex):
    lD, lN = tex['strap']; bD, bN = tex['brass']; gD, gN, gG = tex['gem']
    mats = {'strap': f'diffuse={lD} normal={lN} spec=0.35 gloss=16', 'brass': f'diffuse={bD} normal={bN} spec=0.5 gloss=24',
            'gem': f'diffuse={gD} normal={gN} glow={gG} emit=0.6,0.8,1 emitmult=2.6 spec=1.2 gloss=90'}
    for view, nif in HANDS.items():
        H = Hands(nif)
        P, Dw, F = dense_mesh(H)
        pieces = {'strap': Piece(), 'brass': Piece(), 'gem': Piece()}
        at = []
        for side in ('R', 'L'):
            wraps(H, side, P, Dw, F, hand_region(6.9, thumb=False), 0.08, 0.34, 0.16, 1.15, pieces['strap'], seed=1)
            finger_rings(H, side, 'brass', 'gem', pieces['brass'], pieces['gem'], at)
            palm_bar(H, side, pieces['brass'])
        write('stormknuckles', view, H, pieces, mats)
        if view == '3rd': write_show('stormknuckles', H, pieces, mats)


def emberclaw(tex):
    cD, cN = tex['wrap']; iD, iN = tex['iron']; kD, kN, kG = tex['coal']
    mats = {'wrap': f'diffuse={cD} normal={cN} spec=0.15 gloss=10', 'iron': f'diffuse={iD} normal={iN} spec=0.5 gloss=22',
            'coal': f'diffuse={kD} normal={kN} glow={kG} emit=1,0.5,0.15 emitmult=2.2 spec=0.3 gloss=20'}
    for view, nif in HANDS.items():
        H = Hands(nif)
        P, Dw, F = dense_mesh(H)
        pieces = {'wrap': Piece(), 'iron': Piece(), 'coal': Piece()}
        at = []
        for side in ('R', 'L'):
            wraps(H, side, P, Dw, F, hand_region(8.4, thumb=True, fingers=0.45), 0.08, 0.42, 0.22, 0.95, pieces['wrap'], seed=2)
            coals(H, side, pieces['coal'], pieces['iron'], at)
        write('emberclaw', view, H, pieces, mats)
        if view == '3rd': write_show('emberclaw', H, pieces, mats)


if __name__ == '__main__':
    WORK.mkdir(parents=True, exist_ok=True)
    pw.WORK = g.WORK = WORK
    tw.tool('nifexport'); tw.tool('skinglove')
    names = [a for a in sys.argv[1:] if not a.startswith('--')] or ['stormknuckles', 'emberclaw']
    if 'stormknuckles' in names:
        tex = {'strap': strap_tex('w_stormknuckles_strap', (64, 46, 34), seed=1),
               'brass': metal_tex('w_stormknuckles_brass', (150, 116, 62), dark=0.7, etch=pw.knotwork(2048, 6, 0.1, 3), seed=810),
               'gem': gem_tex('w_stormknuckles_gem', (90, 150, 255), seed=812)}
        stormknuckles(tex)
    if 'emberclaw' in names:
        W2 = 1024
        cl = tileable(W2, W2, 1.2, 822); cr = np.clip(1 - np.abs(tileable(W2, W2, 0.9, 823) - 0.5) / 0.05, 0, 1)
        coal = (save(np.array([30, 22, 18], np.float32) * (0.6 + 0.8 * cl[..., None]) + np.array([255, 110, 30]) * cr[..., None] * 0.9, 'w_emberclaw_coal_d.dds'),
                save(np.dstack([height_to_normal(gaussian_filter(cl - cr, 1.0 * 2), 6.0 * 2), np.full((W2, W2), 30.0)]), 'w_emberclaw_coal_n.dds', True),
                save(np.array([255, 120, 30], np.float32) * (0.2 + 0.6 * cl[..., None] + cr[..., None]), 'w_emberclaw_coal_g.dds'))
        tex = {'wrap': strap_tex('w_emberclaw_wrapstrip', (96, 78, 60), seed=2, char=0.7),
               'iron': metal_tex('w_emberclaw_iron', (66, 64, 62), rust=0.5, dark=0.85, seed=825),
               'coal': coal}
        emberclaw(tex)
    if '--keep' not in sys.argv:
        shutil.rmtree(WORK, ignore_errors=True)
