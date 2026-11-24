"""(Noah, 2026-11-21: the class sets "hyper realistic", on Skyrim's own armour - and ours, not another author's conversion) one of
Skyrim's armour pieces carried off Skyrim's own body onto the body the player wears (3BA for women, HIMBO for men), as
BodySlide's conversions are made:

  - the displacement from Skyrim's body to the new one is worked out at every vertex of Skyrim's body (where on the new body
    its nearest point lies), smoothed over the body so it moves as one surface, and each vertex of the armour moved by its
    nearest skin's displacement (its 12 nearest, the nearest counting most) - a robe's skirt far from any skin moves with the
    hips and thighs it hangs from;
  - then nothing of it is left inside the new body: any part nearer the skin than a hair is pushed out along the skin's normal;
  - Skyrim's own skin pieces in a robe or cuirass are dropped: the armour record wears Skyrim's NakedTorso addon beside it, so
    what shows under it is the player's own installed body (3BA, HIMBO - never copied into ours);
  - the body's RaceMenu/BodySlide sliders carried onto it (the body's own shape keeps its morphs whole) - tools/underwear.py's
    transfer - in a .tri of its own, named on the root.

Skyrim's meshes and textures are read from the player's game (staged into build/vanilla, never committed); what this writes is
derived from them, as every Skyrim armour conversion is.

    python3 tools/armourfit.py <armour.nif> <female|male> <out.nif> [shape tex0 tex1 ...]"""
import pathlib, struct, subprocess, sys
import numpy as np
from scipy.spatial import cKDTree

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import underwear as U

TOOLS = pathlib.Path('/home/claude/tools')
ROOT = pathlib.Path(__file__).resolve().parent.parent
VAN = ROOT / 'build' / 'vanilla'          # Skyrim's own files, staged from the archives
GAME = U.GAME                              # the player's character assets (3BA, HIMBO)
WORK = ROOT / 'build' / 'armourfit'
DEBUG = False
BODY = {'female': ('femalebody_1.nif', '3BA', 'femalebody.tri'), 'male': ('malebody_1.nif', 'HIMBO - Body', 'malebody.tri')}


def shapes_global(nif):
    """each shape's vertices in the skeleton's space (skinned shapes out of their skin space), its triangles, its first texture"""
    WORK.mkdir(parents=True, exist_ok=True)
    import hashlib, os   # (named by the whole path and the process: the sets are fitted four at a time)
    obj = WORK / (pathlib.Path(nif).stem + '_' + hashlib.md5(str(nif).encode()).hexdigest()[:8] + f'_{os.getpid()}_g.obj')
    subprocess.run([str(TOOLS / 'shapeobj'), str(nif), str(obj)], check=True, capture_output=True)
    dump = subprocess.run([str(TOOLS / 'nifdump'), str(nif)], capture_output=True, text=True).stdout
    tex, cur = {}, None
    for l in dump.splitlines():
        if l.startswith("shape '"): cur = l.split("'")[1]; tex[cur] = ''
        elif l.strip().startswith('tex0 ') and cur and not tex[cur]: tex[cur] = l.strip()[5:]
    out, cur, V, F, base = {}, None, [], [], 0
    def flush():
        if cur is not None: out[cur] = (np.array(V, float), np.array(F, int).reshape(-1, 3), tex.get(cur, ''))
    for l in open(obj):
        if l.startswith('usemtl '):
            flush(); cur = l[7:].strip(); base += len(V); V, F = [], []
        elif l.startswith('v '): V.append([float(x) for x in l.split()[1:4]])
        elif l.startswith('f '): F.append([int(x.split('/')[0]) - 1 - base for x in l.split()[1:4]])
    flush()
    return out


def vertex_normals(V, F):
    return U.normals(V, F)


def closest_tri(X, P, F, k=6):
    """for each point of X, the nearest point on the mesh (P, F) - among the triangles round its k nearest vertices - its
    triangle and barycentrics, and the distance"""
    vt = [[] for _ in range(len(P))]
    for i, f in enumerate(F):
        for x in f: vt[x].append(i)
    _, j = cKDTree(P).query(X, k=k)
    cands = [sorted({t for jj in row for t in vt[jj]}) or [0] for row in j]
    L = max(map(len, cands)); C = np.array([c + [c[0]] * (L - len(c)) for c in cands])
    a, b, c = P[F[C, 0]], P[F[C, 1]], P[F[C, 2]]
    x = X[:, None, :]
    ab, ac, ax = b - a, c - a, x - a
    d00, d01, d11 = (ab * ab).sum(-1), (ab * ac).sum(-1), (ac * ac).sum(-1)
    d20, d21 = (ax * ab).sum(-1), (ax * ac).sum(-1)
    den = np.maximum(d00 * d11 - d01 * d01, 1e-12)
    v = (d11 * d20 - d01 * d21) / den; w = (d00 * d21 - d01 * d20) / den
    raw = np.stack([1 - v - w, v, w], -1)
    bc = np.clip(raw, 0, 1); bc /= bc.sum(-1, keepdims=True)   # (clamped: near enough at edges)
    q = a * bc[..., :1] + b * bc[..., 1:2] + c * bc[..., 2:]
    dd = np.linalg.norm(q - x, axis=-1); m = dd.argmin(1); r = np.arange(len(X))
    within = (raw[r, m] > -0.02).all(-1)    # (the point lies over the triangle, not off its edge - not at an opening)
    return q[r, m], C[r, m], bc[r, m], dd[r, m], within


def fit(armour, sex, out, role='body', tri=None, textures=None, glows=None, clearance=0.6, keep=None, hug=None):
    """role 'body': a robe/cuirass - Skyrim's skin pieces in it dropped: the player's own body is worn under it by the armour
    record itself (Skyrim's NakedTorso addon beside ours, so whatever body they have shows, none of it shipped here); anything else (boots, gauntlets, a hood) keeps its own feet and hands, moved with it. tri = (its path as the game
    names it, under meshes\\; the file to write) - the body's sliders carried onto it, written from the _1 fit; weight from
    the armour's own _0/_1."""
    textures = textures or {}; glows = glows or {}; trispec = tri
    wt = '0' if pathlib.Path(armour).stem.endswith('_0') else '1'
    body = BODY[sex][0].replace('_1.', f'_{wt}.')
    src_nif = VAN / 'meshes' / 'actors' / 'character' / 'character assets' / body
    tgt_nif = GAME / body
    S = shapes_global(src_nif)
    SV = np.vstack([v for v, f, t in S.values()]); SF = np.vstack([f + sum(len(x[0]) for x in list(S.values())[:i]) for i, (v, f, t) in enumerate(S.values())])
    T = shapes_global(tgt_nif)
    TV, TF, _ = T[BODY[sex][1]]
    TN = vertex_normals(TV, TF)
    # the displacement at Skyrim's body, smoothed over it
    tt = cKDTree(TV)
    _, ti = tt.query(SV)
    D = TV[ti] - SV
    avg = U.graph(SF, len(SV), SV)
    for _ in range(25): D = 0.5 * D + 0.5 * avg(D)
    st = cKDTree(SV)
    SN = vertex_normals(SV, SF)
    bavg = U.graph(TF, len(TV), TV)
    db, jb = tt.query(TV, k=96, distance_upper_bound=6.0)       # (the swelling's reach over the body)
    fb = np.clip((6.0 - db) / 3.0, 0, 1); jb = np.minimum(jb, len(TV) - 1)
    A = shapes_global(armour)
    job = [f'armour {armour}', f'out {out}']
    WORK.mkdir(parents=True, exist_ok=True)
    moved, faces, dropped = {}, {}, False
    for name, (V, F, tex) in A.items():
        if keep is not None:   # (one piece lifted out of a mesh - Skyrim's own underwear out of its body: the rest dropped)
            if name not in keep:
                job.append(f'drop {name}'); continue
            skin = False
        else:
            skin = 'actors\\character' in tex.lower()
        if skin and role == 'body':   # (Skyrim's skin pieces: the new body stands in for them)
            job.append(f'drop {name}'); dropped = True; continue
        dd, ii = st.query(V, k=12)
        w = 1 / np.maximum(dd, 0.3) ** 2; w /= w.sum(1, keepdims=True)
        P = V + (D[ii] * w[..., None]).sum(1)
        # (how far each vertex stood off Skyrim's own body: a part tucked inside it there - a sleeve's inner rim, a hidden
        # lining - may stay tucked in on the new body, and the skin showing through it does not count)
        d0, j0 = st.query(V, k=4)
        w0 = 1 / np.maximum(d0, 0.2); w0 /= w0.sum(1, keepdims=True)
        sn0 = (SN[j0] * w0[..., None]).sum(1); sn0 /= np.linalg.norm(sn0, axis=1, keepdims=True) + 1e-9
        h0 = (((V - (SV[j0] * w0[..., None]).sum(1)) * sn0).sum(1))
        cl = np.where(d0[:, 0] < 10.0, np.minimum(clearance, h0), clearance)
        hidden = (h0[F] < -0.1).any(1)
        if skin:   # (a boot's foot, a gauntlet's hand: moved with it, nothing pushed)
            f = WORK / f'{pathlib.Path(out).stem}_{len(moved)}s.f32'; f.write_bytes(P.astype('<f4').tobytes())
            job.append(f'pos {name} {f}'); continue
        # nothing of the body through it, and nothing of it under the body - both ways: a cloth vertex under the skin, and the
        # skin bulging up between the cloth's vertices (a low-poly trouser leg's flat triangles over HIMBO's round thighs).
        # Either swells the body there (the swelling spread a few units round, smoothly, far enough to reach the corners of the
        # triangle over it), and every layer of cloth over a spot of skin moves out by that spot's swelling - a tabard over a
        # tunic as far as the tunic, so their order holds
        # (2026-11-23, Test Night: "the nipple pokes through the bra at Weight 0") the hug comes first and the
        # nothing-through-it rounds after it, so the skin's sharper spots (3BA's nipples) still lift the cloth over them
        if hug is not None:
            # (Noah, 2026-11-23: "the bra looks strange, like there's too much of a gap from the skin" - and its size followed
            # the weight slider while his body doesn't) a skin-tight piece laid back onto the new body: each vertex near the
            # skin set at the offset it had on Skyrim's own body (h0), held between hug[0] and hug[1] - so it follows the
            # body the player has at each weight, never floating off it
            Qh, trh, bch, dh, _ = closest_tri(P, TV, TF)
            nh = (TN[TF[trh]] * bch[..., None]).sum(1); nh /= np.linalg.norm(nh, axis=1, keepdims=True) + 1e-9
            near = dh < 6.0
            want = np.clip(h0, hug[0], hug[1])
            P = np.where(near[:, None], Qh + nh * want[:, None], P)
            print(f'  {name}: hugged {int(near.sum())} of {len(P)} to the skin ({hug[0]}..{hug[1]})', file=sys.stderr)
        grown = np.zeros(len(TV))
        for it in range(8):
            Q, tri, bc, dist, within = closest_tri(TV, P, F)
            off = Q - TV; along = (off * TN).sum(1)
            lat = np.linalg.norm(off - TN * along[:, None], axis=1)
            need = np.where(within & ~hidden[tri] & (lat < 0.8) & (dist < 6) & (along > -2.0), np.maximum(clearance - along, 0), 0)
            vec = TN * need[:, None]          # (the swelling as a movement: along the skin's normal, or the bulge's for a corner)
            d2, j = tt.query(P, k=4)
            w2 = 1 / np.maximum(d2, 0.2); w2 /= w2.sum(1, keepdims=True)
            sp = (TV[j] * w2[..., None]).sum(1); sn = (TN[j] * w2[..., None]).sum(1)
            sn /= np.linalg.norm(sn, axis=1, keepdims=True) + 1e-9
            short = np.where(d2[:, 0] < 10.0, np.maximum(cl - ((P - sp) * sn).sum(1), 0), 0)
            def put(at, mag, dirn):
                o = np.argsort(mag)    # (larger last, so they win)
                at, mag, dirn = at[o], mag[o], dirn[o]
                keep = mag > need[at]
                vec[at[keep]] = dirn[keep] * mag[keep, None]; need[at[keep]] = mag[keep]
            pk = np.where(need > 0)[0]; pn = need[pk].copy()
            put(j[:, 0], short, sn)
            for c in range(3): put(j[F[tri[pk], c], 0], pn, TN[pk])   # (a flat triangle the skin bulges through lifts whole)
            if need.max() < 0.03: break
            if DEBUG: k = np.argsort(-need)[:4]; print(it, [(tuple(np.round(TV[x], 0)), round(float(need[x]), 2)) for x in k], file=sys.stderr)
            src = jb[np.arange(len(TV)), (need[jb] * fb).argmax(1)]; f = (need[jb] * fb).max(1) / np.maximum(need[src], 1e-9)
            vec = vec[src] * f[:, None]
            for _ in range(2): vec = 0.5 * vec + 0.5 * bavg(vec)
            mag = np.linalg.norm(vec, axis=1); cap = np.maximum(3.0 - grown, 0)
            vec *= (np.minimum(mag, cap) / np.maximum(mag, 1e-9))[:, None]; grown += np.minimum(mag, cap); need = np.minimum(mag, cap)
            dk, jk = tt.query(P, k=8)
            wk = 1 / np.maximum(dk, 1.0); wk /= wk.sum(1, keepdims=True)
            P = P + (vec[jk] * wk[..., None]).sum(1)
        print(f'  {name}: {it + 1} rounds, last need {need.max():.2f}', file=sys.stderr)
        f = WORK / f'{pathlib.Path(out).stem}_{len(moved)}.f32'
        f.write_bytes(P.astype('<f4').tobytes())
        job.append(f'pos {name} {f}')
        moved[name] = P; faces[name] = F
        for slot, path in textures.get(name, {}).items(): job.append(f'tex {name} {slot} {path}')
        if name in glows: job.append(f'glow {name} ' + ' '.join(str(x) for x in glows[name]))
    if trispec: job.append(f'bodytri {trispec[0]}')
    jf = WORK / (pathlib.Path(out).stem + '.job'); jf.write_text('\n'.join(job) + '\n')
    pathlib.Path(out).parent.mkdir(parents=True, exist_ok=True)
    r = subprocess.run([str(TOOLS / 'armourfit'), str(jf)], capture_output=True, text=True)
    if r.returncode: print(r.stdout, r.stderr); raise SystemExit('armourfit failed')
    print(pathlib.Path(out).name, ':', ' | '.join(l for l in r.stdout.splitlines() if not l.startswith('xf '))[-300:])
    if trispec and wt == '1':
        xf = {}
        for l in r.stdout.splitlines():
            if l.startswith('xf '):
                n, v = l[3:].split('|'); v = [float(x) for x in v.split()]
                xf[n] = np.array(v[:9]).reshape(3, 3) * v[9]
        globals()['_TF'] = TF
        sliders(sex, TV, TN, moved, faces, xf, False, pathlib.Path(trispec[1]))
    return moved


TRI = {'female': 'femalebody.tri', 'male': 'malebody.tri'}


def sliders(sex, TV, TN, moved, faces, xf, body, path):
    """the body's RaceMenu/BodySlide sliders onto the refitted armour: each cloth shape by tools/underwear.py's transfer
    (every corner moved as the skin round it moves, never in under it), the cloned body with its own morphs whole; deltas
    turned from the skeleton's space into each shape's own"""
    import types
    morphs = U.read_tri(GAME / TRI[sex], BODY[sex][1])
    b = types.SimpleNamespace(V=TV, N=TN)
    out = {}
    def depth(P, V, N):
        d, j = cKDTree(V).query(P, k=4); w = 1 / np.maximum(d, 0.2); w /= w.sum(1, keepdims=True)
        n = (N[j] * w[..., None]).sum(1); n /= np.linalg.norm(n, axis=1, keepdims=True) + 1e-9
        return ((P - (V[j] * w[..., None]).sum(1)) * n).sum(1), n, d[:, 0]
    TF = getattr(sys.modules[__name__], '_TF')
    for name, P in moved.items():
        C = U.cloth_morphs(b, P, faces[name], morphs)
        # (then each slider checked at full: a corner it would take under the moved skin is carried out again, to where
        # it stood before - or a hair off the skin - by that slider's own morph)
        d0, _, near = depth(P, TV, TN); near = near < 4
        for mn, (ids, dl) in morphs.items():
            T2 = TV.copy(); T2[ids] += dl
            P2 = P + C.get(mn, 0)
            d1, n1, _ = depth(P2, T2, U.normals(T2, TF))
            fix = np.where(near, np.maximum(np.minimum(d0, 0.2) - d1, 0), 0)
            if fix.max() > 1e-3: C[mn] = C.get(mn, np.zeros_like(P)) + n1 * fix[:, None]
        R = xf.get(name, np.eye(3))
        out[name] = {mn: d @ R.T for mn, d in C.items()}
    if body:
        dense = {}
        for mn, (ids, dl) in morphs.items():
            D = np.zeros_like(TV); D[ids] = dl; dense[mn] = D @ xf.get(BODY[sex][1], np.eye(3)).T
        out[BODY[sex][1]] = dense
    U.write_tri(path, out)
    print('  sliders:', path.name, {k: len(v) for k, v in out.items()}, f'{path.stat().st_size // 1024} KB', file=sys.stderr)


if __name__ == '__main__':
    fit(sys.argv[1], sys.argv[2], sys.argv[3], *(sys.argv[4:5] or []))
