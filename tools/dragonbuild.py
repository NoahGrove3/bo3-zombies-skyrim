"""Our own glider dragon (Noah: not the glider's dragon skin - a dragon of our own, alive, not stiff, its talons closed
round your hands): Skyrim's dragon mesh (both its shapes, joined) on a skeleton made from Skyrim's dragon skeleton -
every joint of it: five neck bones, eight tail bones, the wing's arm and four fingers, three toes of two joints on each
foot - with Skyrim's own skin weights, so it bends like a living thing.

Every bone is set square to the dragon's own space (head +Y, up +Z, the left wing -X) at its joint, so a turn about X
pitches it, about Y rolls it, about Z turns it - the game (src/Movement.cpp) poses the wings, legs, talons, head and jaw
that way each frame; the nif's own loop (written here as keys) breathes the body, the neck and the tail.

    python3 tools/dragonbuild.py job <dragon.txt> <skel.txt> <out_job.txt> [--subdiv]
    python3 tools/dragonbuild.py preview <dragon.txt> <skel.txt> <out.png> <texture.dds> [pose]
(dragon.txt / skel.txt: tools/modelconv/dragonexport of meshes\\actors\\dragon\\character assets\\dragon.nif and skeleton.nif;
the job is for tools/modelconv/dragonbuild, which puts it in the glider's nif.)"""
import sys, math, json, pathlib
import numpy as np
sys.path.insert(0, str(pathlib.Path(__file__).parent))
import dragonmesh as dm

D2R = math.pi / 180


def Rot(pitch, roll, yaw):
    """src/Movement.cpp Rot: radians about X (nose up), Y (roll), Z (turn), as Z * Y * X"""
    cx, sx, cy, sy, cz, sz = math.cos(pitch), math.sin(pitch), math.cos(roll), math.sin(roll), math.cos(yaw), math.sin(yaw)
    X = np.array([[1, 0, 0], [0, cx, -sx], [0, sx, cx]])
    Y = np.array([[cy, 0, sy], [0, 1, 0], [-sy, 0, cy]])
    Z = np.array([[cz, -sz, 0], [sz, cz, 0], [0, 0, 1]])
    return Z @ Y @ X


def AxisAngle(axis, a):
    axis = np.asarray(axis, float); axis = axis / (np.linalg.norm(axis) + 1e-12)
    x, y, z = axis; c, s = math.cos(a), math.sin(a); C = 1 - c
    return np.array([[c + x * x * C, x * y * C - z * s, x * z * C + y * s], [y * x * C + z * s, c + y * y * C, y * z * C - x * s], [z * x * C - y * s, z * y * C + x * s, c + z * z * C]])


def rename(b):
    b = b.replace('NPC_', '')
    fixed = {'Pelvis': 'DG_Body', 'Spine1': 'DG_Spine1', 'Spine2': 'DG_Spine2', 'Spine3': 'DG_Chest', 'Head': 'DG_Head', 'Jaw': 'DG_Jaw',
             'Tongue1': 'DG_Tongue1', 'Tongue2': 'DG_Tongue2', 'LUpLip': 'DG_LipL', 'RUpLip': 'DG_LipR',
             'LLBrow': 'DG_BrowLL', 'LUBrow': 'DG_BrowLU', 'RLBrow': 'DG_BrowRL', 'RUBrow': 'DG_BrowRU'}
    if b in fixed:
        return fixed[b]
    if b.startswith('Neck') and b[4:].isdigit():
        return 'DG_Neck' + b[4:]
    if b.startswith('Tail') and b[4:].isdigit():
        return 'DG_Tail' + b[4:]
    S = b[0]
    r = b[1:]
    leg = {'LegThigh': '1', 'LegCalf': '2', 'LegFoot': '3'}
    if r in leg:
        return f'DG_Leg{S}{leg[r]}'
    if r.startswith('LegToe'):
        return f'DG_Toe{S}{r[6:]}'
    wing = {'Collarbone': '0', 'UpArm1': '1', 'UpArm2': '1t', 'Forearm1': '2', 'Forearm2': '2t', 'Hand': '3', 'Thumb1': 'Th', 'Elbow': 'El'}
    if r in wing:
        return f'DG_Wing{S}{wing[r]}'
    if r.startswith('Finger'):
        return f'DG_Wing{S}F{r[6:]}'
    raise KeyError(b)


class Rig:
    """the bones (name -> parent, rest position in the dragon's space) and the mesh skinned to them"""

    def __init__(self, dragon_txt, skel_txt, subdiv=False):
        shapes = dm.load(dragon_txt)
        skel = dm.load_skeleton(skel_txt)
        # every skin bone's place (the same in both shapes)
        pos = {}
        for s in shapes:
            for b, B in zip(s.bones, s.bind):
                pos[b] = B[:3, 3]
        # the skeleton's parents, skipping nodes that carry no skin (hubs)
        def skin_parent(b):
            p = skel[b]['parent']
            while p is not None and p not in pos:
                p = skel[p]['parent'] if p in skel else None
            return p
        self.bones = {}   # name -> [parent, pos]
        self.order = []
        for b in skel:   # (the skeleton's order: parents first)
            if b in pos:
                p = skin_parent(b)
                self.bones[rename(b)] = [rename(p) if p else None, np.array(pos[b], float)]
                self.order.append(rename(b))
        # the game's own handles
        self.add('DG_Bank', None, (0, -1450, 180), first=True)
        self.bones['DG_Body'][0] = 'DG_Bank'
        head = self.bones['DG_Head']
        self.add('DG_Look', head[0], head[1], before='DG_Head')
        head[0] = 'DG_Look'
        lip = (self.bones['DG_LipL'][1] + self.bones['DG_LipR'][1]) / 2
        self.add('DG_Mouth', 'DG_Head', lip + np.array([0, 18, -14]))
        # the mesh: both shapes as one (one material)
        V, N, U, C, T, Wt = [], [], [], [], [], []
        skin = [rename(b) for b in shapes[0].bones]
        for s in shapes:
            for b in s.bones:
                if rename(b) not in skin:
                    skin.append(rename(b))
        off = 0
        for s in shapes:
            W = np.zeros((len(s.pos), len(skin)))
            for j, b in enumerate(s.bones):
                W[:, skin.index(rename(b))] = s.weights[:, j]
            V.append(s.pos); N.append(s.nrm); U.append(s.uv); C.append(s.col); T.append(s.tri + off); Wt.append(W)
            off += len(s.pos)
        self.pos, self.nrm, self.uv, self.col = np.concatenate(V), np.concatenate(N), np.concatenate(U), np.concatenate(C)
        self.tri, self.W, self.skin = np.concatenate(T), np.concatenate(Wt), skin
        self.tex = shapes[0].tex
        if subdiv:
            self.subdivide()
        # (Noah: a saddle not to sit on - to hang from) the harness, and the bar you hang from on DG_Trapeze (the game
        # swings it), its grips DG_HandleL / R where your hands hold it
        import dragonharness as dh
        self.harness, pivot = dh.build(self)
        self.add('DG_Trapeze', 'DG_Spine1', pivot)
        for S, sx in (('L', -1), ('R', 1)):
            self.add(f'DG_Handle{S}', 'DG_Trapeze', (sx * dh.HANDS_X, dh.BAR_Y, dh.BAR_Z))
        self.skin.append('DG_Trapeze')
        self.W = np.concatenate([self.W, np.zeros((len(self.W), 1))], 1)
        self.hW = dh.weights(self, self.harness)

    def add(self, name, parent, p, first=False, before=None):
        self.bones[name] = [parent, np.array(p, float)]
        if first:
            self.order.insert(0, name)
        elif before:
            self.order.insert(self.order.index(before), name)
        else:
            self.order.append(name)

    def subdivide(self):
        """one level of curved (Phong) tessellation: every triangle into four, the new points on the surface the vertex
        normals describe, so the silhouette rounds out - the original vertices stay where they were"""
        P, N, U, C, W, T = self.pos, self.nrm, self.uv, self.col, self.W, self.tri
        # normals averaged over vertices at the same place (the uv seams), so a seam stays closed
        key = np.round(P, 3)
        _, inv = np.unique(key, axis=0, return_inverse=True)
        inv = inv.reshape(-1)
        Ns = np.zeros((inv.max() + 1, 3))
        np.add.at(Ns, inv, N)
        Ns = Ns[inv]; Ns /= np.linalg.norm(Ns, axis=1)[:, None] + 1e-9
        edges = {}
        newP, newN, newU, newC, newW = [], [], [], [], []
        base = len(P)
        def mid(a, b):
            k = (min(a, b), max(a, b))
            if k in edges:
                return edges[k]
            m = (P[a] + P[b]) / 2
            # project the midpoint onto each end's tangent plane, and take the average (alpha 0.6)
            pa = m - np.dot(m - P[a], Ns[a]) * Ns[a]
            pb = m - np.dot(m - P[b], Ns[b]) * Ns[b]
            q = 0.4 * m + 0.6 * (pa + pb) / 2
            n = N[a] + N[b]; n /= np.linalg.norm(n) + 1e-9
            newP.append(q); newN.append(n); newU.append((U[a] + U[b]) / 2); newC.append((C[a] + C[b]) / 2); newW.append((W[a] + W[b]) / 2)
            edges[k] = base + len(newP) - 1
            return edges[k]
        out = []
        for a, b, c in T:
            ab, bc, ca = mid(a, b), mid(b, c), mid(c, a)
            out += [(a, ab, ca), (ab, b, bc), (ca, bc, c), (ab, bc, ca)]
        self.pos = np.concatenate([P, np.array(newP)]); self.nrm = np.concatenate([N, np.array(newN)])
        self.uv = np.concatenate([U, np.array(newU)]); self.col = np.concatenate([C, np.array(newC)])
        self.W = np.concatenate([W, np.array(newW)]); self.tri = np.array(out)

    # ---- posing (as the game does it: each bone square to its parent at rest, turned by its local rotation)
    def world(self, rot=None, trans=None):
        rot, trans = rot or {}, trans or {}
        out = {}
        for b in self.order:
            p, pos = self.bones[b]
            off = pos - (self.bones[p][1] if p else 0)
            off = off + trans.get(b, 0)
            R = rot.get(b, np.eye(3))
            if p:
                PR, PT = out[p]
                out[b] = (PR @ R, PT + PR @ off)
            else:
                out[b] = (R, off)
        return out

    def skinned(self, rot=None, trans=None):
        Wd = self.world(rot, trans)
        P = np.zeros_like(self.pos); N = np.zeros_like(self.nrm)
        for j, b in enumerate(self.skin):
            w = self.W[:, j]
            m = w > 0
            if not m.any():
                continue
            R, T = Wd[b]
            rest = self.bones[b][1]
            P[m] += w[m, None] * ((self.pos[m] - rest) @ R.T + T)
            N[m] += w[m, None] * (self.nrm[m] @ R.T)
        N /= np.linalg.norm(N, axis=1)[:, None] + 1e-9
        return P, N, Wd

    def skinned_all(self, rot=None, trans=None):
        """the dragon and its harness together (for previews): positions, normals, uvs, triangles, bone places"""
        P, N, Wd = self.skinned(rot, trans)
        h = self.harness
        HP, HN = np.array(h.P), np.array(h.N)
        QP = np.zeros_like(HP); QN = np.zeros_like(HN)
        for j, b in enumerate(self.skin):
            w = self.hW[:, j]; m = w > 0
            if not m.any():
                continue
            R, T = Wd[b]
            QP[m] += w[m, None] * ((HP[m] - self.bones[b][1]) @ R.T + T)
            QN[m] += w[m, None] * (HN[m] @ R.T)
        # (previews: one atlas - the dragon's texture on the left half, the harness's on the right)
        u = self.uv.copy(); u[:, 0] = (u[:, 0] % 1.0) * 0.5; u[:, 1] = u[:, 1] % 1.0
        hu = np.array(h.U, float); hu[:, 0] = 0.5 + (hu[:, 0] % 1.0) * 0.5; hu[:, 1] = hu[:, 1] % 1.0
        UV = np.concatenate([u, hu])
        return np.concatenate([P, QP]), np.concatenate([N, QN]), UV, np.concatenate([self.tri, np.array(h.T) + len(P)]), Wd


# ---- the poses the game gives it (src/Movement.cpp), for previews
def glide_pose(rig, t=0.0, fold=0.0, beat=0.0, grip=1.0, toes=None):
    r = {}
    loop = idle_loop(t)
    r.update(loop)
    for S, sg in (('L', 1), ('R', -1)):
        wing_pose(r, S, sg, beat, fold, t)
        leg_pose(r, S, grip, fold, t)
        toe_pose(rig, r, S, (TOES['glide'] * (1 - fold) + TOES['dive'] * fold) if toes is None else toes)
    return r


# The wings: (degrees) the arm's three joints - shoulder, elbow, wrist - lifted (roll), swept back (turn), twisted
# (pitch); at rest straight out, a little sway; the fingers spread the membrane, and fold in along the arm in a dive
WING_REST = [(0, 0, 0), (0, 0, 0), (0, 0, 0)]
def wing_pose(r, S, sg, beat, fold, t):
    amps = [34, 18, 12] if beat > 0 else [24, 10, 6]   # (up high, the stroke down shallower: the new arm bends at every joint)
    foldBack = [62, -150, 120]   # (the dive: the arm swept back, the forearm folded forward against it, the hand back along it)
    foldRoll = [8, -10, -6]
    for j, n in enumerate(['1', '2', '3']):
        sway = (2.5 if j == 0 else 1.5) * math.sin(2 * math.pi * t / 3 + j * 0.6)
        roll = WING_REST[j][0] * (1 - fold) + sway * (1 - 0.6 * fold) + amps[j] * beat + foldRoll[j] * fold
        back = WING_REST[j][1] + foldBack[j] * fold
        r[f'DG_Wing{S}{n}'] = Rot(0, sg * roll * D2R, sg * back * D2R)
    # the fingers: closed up against each other along the forearm in the dive
    for f, k in (('F11', 0.2), ('F21', 0.45), ('F31', 0.75), ('F41', 1.0)):
        r[f'DG_Wing{S}{f}'] = Rot(0, 0, sg * (-70 * k) * D2R * fold)


# (Noah: the legs stretched out behind it, toes pointing to its tail - a different set for the glide, the dive, a beat)
LEG_TUCK = [-1.25, 0.5, -1.9]     # gliding: trailing back below the tail
LEG_DIVE = [-1.45, 0.25, -1.6]    # diving: pressed tight along the tail
LEG_FLAP = [-0.75, 0.7, -1.3]     # a downstroke: swung forward, then back
TOES = {'glide': 0.05, 'dive': 0.3, 'flap': -0.2}
def leg_pose(r, S, grip, fold, t):
    ph = 0 if S == 'L' else 1.3
    # (Noah: you hang from the harness now - its legs are free: drawn up and back under it, swaying)
    k = [LEG_TUCK[i] * (1 - fold) + LEG_DIVE[i] * fold for i in range(3)]
    hip = k[0] + 0.06 * math.sin(t * 1.5 + ph) * (1 - fold)
    knee = k[1] + 0.08 * math.sin(t * 1.5 + 0.7 + ph) * (1 - fold)
    foot = k[2] + 0.08 * math.sin(t * 2.1 + 1.4 + ph) * (1 - fold)
    r[f'DG_Leg{S}1'] = Rot(hip, 0, 0)
    r[f'DG_Leg{S}2'] = Rot(knee, 0, 0)
    r[f'DG_Leg{S}3'] = Rot(foot, 0, 0)


def toe_pose(rig, r, S, curl):
    """each talon curls about its own sideways axis (across its length): the root joint and the tip both, the tips more -
    closed, they wrap round the hollow under the foot (DG_Grip)"""
    for a, b in ((1, 2), (3, 4), (5, 6)):
        for k, (bone, child, amt) in enumerate(((f'DG_Toe{S}{a}', f'DG_Toe{S}{b}', 0.95), (f'DG_Toe{S}{b}', None, 1.25))):
            p = rig.bones[bone][1]
            d = (rig.bones[child][1] - p) if child else (p - rig.bones[f'DG_Toe{S}{a}'][1])
            axis = np.cross(d, [0, 0, 1.0])
            r[bone] = AxisAngle(axis, -curl * amt)


def idle_loop(t):
    """the nif's own 6 s loop: the body breathing, the neck answering it, the tail swimming in a travelling wave"""
    g = lambda ph, P=3.0: math.sin(2 * math.pi * t / P + ph)
    r = {'DG_Body': Rot(1.0 * D2R * g(1.4), 0, 0)}
    for k in range(1, 6):
        r[f'DG_Neck{k}'] = Rot((1.2 - 0.15 * k) * D2R * g(2.0 + 0.35 * k), 0, (1.0 + 0.3 * k) * D2R * math.sin(2 * math.pi * t / 6 - 0.4 * k))
    for k in range(1, 9):
        ph = 0.45 * k
        r[f'DG_Tail{k}'] = Rot((0.8 + 0.25 * k) * D2R * math.sin(2 * math.pi * t / 6 - ph), 0, (1.6 + 0.55 * k) * D2R * math.sin(2 * math.pi * t / 3 - ph))
    return r


def idle_trans(t):
    return {'DG_Body': np.array([0, 0, 3.5 * math.sin(2 * math.pi * t / 3 + 0.3)])}


def write_job(rig, out):
    L = []
    for b in rig.order:
        p, pos = rig.bones[b]
        L.append(f'bone {b} {p or "-"} {pos[0]:.3f} {pos[1]:.3f} {pos[2]:.3f}')
    L.append('skin ' + ' '.join(rig.skin))
    h = rig.harness
    meshes = [('DG_Dragon', '-', '-', rig.pos, rig.nrm, rig.uv, rig.col, rig.tri, rig.W),
              ('DG_Harness', r'textures\CurseOfAlduin\Dragon\harness.dds', r'textures\CurseOfAlduin\Dragon\harness_n.dds',
               np.array(h.P), np.array(h.N), np.array(h.U), np.ones((len(h.P), 4)), np.array(h.T), rig.hW)]
    for name, d, n_, P, Nn, UV, Cc, Tt, Ww in meshes:
        L.append(f'mesh {name} {d} {n_}')
        for i in range(len(P)):
            p, n, u, c = P[i], Nn[i], UV[i], Cc[i]
            L.append(f'v {p[0]:.4f} {p[1]:.4f} {p[2]:.4f} {n[0]:.5f} {n[1]:.5f} {n[2]:.5f} {u[0]:.6f} {u[1]:.6f} {c[0]:.3f} {c[1]:.3f} {c[2]:.3f} {c[3]:.3f}')
        for a, b, c in Tt:
            L.append(f't {a} {b} {c}')
        for i in range(len(P)):
            w = Ww[i]
            idx = np.argsort(-w)[:4]
            idx = [j for j in idx if w[j] > 1e-4]
            tot = sum(w[j] for j in idx)
            L.append('vw ' + ' '.join(f'{j} {w[j] / tot:.5f}' for j in idx))
    # the loop: 6 s, 30 keys a second, on the bones the game leaves to the nif
    T, N = 6.0, 180
    names = list(idle_loop(0).keys())
    for b in names:
        L.append(f'anim {b} {T} {N + 1}')
        base = rig.bones[b][1] - (rig.bones[rig.bones[b][0]][1] if rig.bones[b][0] else 0)
        for i in range(N + 1):
            t = T * i / N
            R = idle_loop(t)[b]
            tr = base + idle_trans(t).get(b, 0)
            q = quat(R)
            L.append(f'k {t:.4f} {tr[0]:.4f} {tr[1]:.4f} {tr[2]:.4f} {q[0]:.6f} {q[1]:.6f} {q[2]:.6f} {q[3]:.6f}')
    pathlib.Path(out).write_text('\n'.join(L) + '\n')
    # the places other tools hang things on (tools/dragonfx.py)
    pts = {b: rig.bones[b][1].tolist() for b in rig.order}
    tipL = rig.pos[np.argmin(rig.pos[:, 0])]
    pts['tipL'] = tipL.tolist(); pts['tipR'] = [-tipL[0], tipL[1], tipL[2]]
    pathlib.Path(out).with_suffix('.points.json').write_text(json.dumps(pts, indent=1))
    print(f'job: {len(rig.order)} bones, {len(rig.skin)} skinned, {len(rig.pos)} verts, {len(rig.tri)} tris, {len(names)} animated')


def quat(R):
    w = math.sqrt(max(0, 1 + R[0, 0] + R[1, 1] + R[2, 2])) / 2
    x = math.copysign(math.sqrt(max(0, 1 + R[0, 0] - R[1, 1] - R[2, 2])) / 2, R[2, 1] - R[1, 2])
    y = math.copysign(math.sqrt(max(0, 1 - R[0, 0] + R[1, 1] - R[2, 2])) / 2, R[0, 2] - R[2, 0])
    z = math.copysign(math.sqrt(max(0, 1 - R[0, 0] - R[1, 1] + R[2, 2])) / 2, R[1, 0] - R[0, 1])
    return (w, x, y, z)


VIEWS = {   # (eye offset from the look point, look point)
    'side': ((2600, 0, 80), (0, -1600, 120)),
    'top': ((0, -1, 3000), (0, -1600, 120)),
    'back': ((900, -2400, 900), (0, -1500, 120)),
    'front': ((-900, 2200, -300), (0, -1500, 120)),
    'footL': ((-350, 420, -120), (-80, -1630, -60)),
    'footS': ((-520, 0, 0), (-80, -1630, -60)),
    'harness': ((-700, 420, -260), (0, -1540, 60)),
    'under': ((-150, -250, -800), (0, -1540, 60)),
    'hside': ((-800, 0, 40), (0, -1540, 60)),
}


def main():
    cmd = sys.argv[1]
    rig = Rig(sys.argv[2], sys.argv[3], subdiv='--subdiv' in sys.argv)
    if cmd == 'job':
        write_job(rig, sys.argv[4])
        return
    if cmd == 'poses':
        write_poses(rig, sys.argv[4])
        return
    if cmd == 'preview':
        out, tex = sys.argv[4], sys.argv[5]
        pose = sys.argv[6] if len(sys.argv) > 6 and not sys.argv[6].startswith('--') else 'glide'
        views = sys.argv[7].split(',') if len(sys.argv) > 7 and not sys.argv[7].startswith('--') else ['back', 'side', 'top', 'front']
        kw = dict(glide={}, dive=dict(fold=1.0), up=dict(beat=1.0), down=dict(beat=-1.3), open=dict(grip=0.0), rest=None)[pose]
        rot = glide_pose(rig, **kw) if kw is not None else {}
        P, N, _ = rig.skinned(rot, idle_trans(0) if kw is not None else None)
        ps = []
        for i, v in enumerate(views):
            e, l = VIEWS[v]
            ps.append(dm.render(P, N, rig.uv, rig.tri, f'/tmp/claude-0/pv{i}.png', np.array(l) + np.array(e), np.array(l), tex, fov=40 if not v.startswith('foot') else 30))
        dm.sheet(ps, out)



# ---- solved poses (the game blends to them: src/DragonPoses.h, written by 'poses')
def aim(rig, rot, bone, child, target_dir, Wd=None):
    """turn `bone` (on top of what rot already gives it) so its child lies along target_dir (dragon space)"""
    Wd = rig.world(rot)
    R, T = Wd[bone]
    cur = Wd[child][1] - T
    t = np.asarray(target_dir, float)
    cur, t = cur / np.linalg.norm(cur), t / np.linalg.norm(t)
    ax = np.cross(cur, t)
    s = np.linalg.norm(ax)
    if s < 1e-6:
        return
    G = AxisAngle(ax, math.atan2(s, np.dot(cur, t)))   # the turn in the dragon's space
    # local' so that parentR @ local' = G @ (parentR @ local)
    p = rig.bones[bone][0]
    PR = Wd[p][0] if p else np.eye(3)
    rot[bone] = PR.T @ G @ PR @ rot.get(bone, np.eye(3))


DIVE = {   # where each part of the left wing points, folded for a dive (Noah: tight, like a stooping bird's)
    ('DG_WingL1', 'DG_WingL1t'): (-0.45, -0.85, 0.15),    # the arm swept back along its side
    ('DG_WingL2', 'DG_WingL2t'): (-0.55, 0.78, 0.12),     # the forearm folded forward against it
    ('DG_WingLF11', 'DG_WingLF12'): (-0.22, -0.96, 0.0),  # the fingers all laid back along its flank
    ('DG_WingLF21', 'DG_WingLF22'): (-0.20, -0.97, -0.03),
    ('DG_WingLF31', 'DG_WingLF32'): (-0.17, -0.98, -0.05),
    ('DG_WingLF41', 'DG_WingLF42'): (-0.10, -0.99, -0.06),
}


def dive_pose(rig):
    rot = {}
    for (b, c), d in DIVE.items():
        aim(rig, rot, b, c, d)
        aim(rig, rot, b.replace('L', 'R', 1) if b.startswith('DG_WingL') else b, c.replace('DG_WingL', 'DG_WingR'), (-d[0], d[1], d[2])) if False else None
    # the right wing: the mirror
    out = dict(rot)
    for b, R in rot.items():
        M = np.diag([-1, 1, 1])
        out[b.replace('DG_WingL', 'DG_WingR')] = M @ R @ M
    return out


def hand_mesh(at, r=20.0, up=(0, 0.25, 1)):
    """a stand-in for your hand and forearm (a fist and a cylinder up from it), for the grip previews"""
    up = np.asarray(up, float); up /= np.linalg.norm(up)
    side = np.cross(up, [1, 0, 0]); side /= np.linalg.norm(side)
    fwd = np.cross(side, up)
    P, N, T = [], [], []
    # fist: a sphere
    n = 12
    for i in range(n + 1):
        th = math.pi * i / n
        for j in range(2 * n):
            ph = math.pi * j / n
            d = np.array([math.sin(th) * math.cos(ph), math.sin(th) * math.sin(ph), math.cos(th)])
            P.append(np.asarray(at) + d * r); N.append(d)
    for i in range(n):
        for j in range(2 * n):
            a = i * 2 * n + j; b = i * 2 * n + (j + 1) % (2 * n); c = a + 2 * n; e = b + 2 * n
            T += [(a, b, c), (b, e, c)]
    base = len(P)
    for k in range(2):
        for j in range(2 * n):
            ph = math.pi * j / n
            d = side * math.cos(ph) + fwd * math.sin(ph)
            P.append(np.asarray(at) + up * (k * 160) + d * r * 0.8); N.append(d)
    for j in range(2 * n):
        a, b = base + j, base + (j + 1) % (2 * n)
        T += [(a, b, a + 2 * n), (b, b + 2 * n, a + 2 * n)]
    return np.array(P), np.array(N), np.array(T)


def preview_grip(rig, out, tex, curl=1.0, extra=None):
    rot = glide_pose(rig, toes=curl)
    if extra:
        rot.update(extra)
    P, N, Wd = rig.skinned(rot)
    UV = rig.uv
    T = rig.tri
    for S in 'LR':
        hp, hn, ht = hand_mesh(Wd[f'DG_Grip{S}'][1])
        T = np.concatenate([T, ht + len(P)]); P = np.concatenate([P, hp]); N = np.concatenate([N, hn]); UV = np.concatenate([UV, np.full((len(hp), 2), 0.999)])
    g = Wd['DG_GripL'][1]
    ps = []
    for i, e in enumerate([(-420, 260, 60), (-480, -60, -40), (-200, -380, -200), (0, 30, -480)]):
        ps.append(dm.render(P, N, UV, T, f'/tmp/claude-0/pg{i}.png', g + np.array(e), g, tex, fov=35))
    dm.sheet(ps, out)


def atlas(tex, htex):
    from PIL import Image
    a = Image.new('RGB', (2048, 1024))
    a.paste(Image.open(tex).convert('RGB').resize((1024, 1024)), (0, 0))
    a.paste(Image.open(htex).convert('RGB').resize((1024, 1024)), (1024, 0))
    a.save('/tmp/claude-0/atlas.png')
    return '/tmp/claude-0/atlas.png'


def preview_all(rig, out, tex, rot, views=('back', 'side', 'top', 'front'), hands=True):
    P, N, UV, T, Wd = rig.skinned_all(rot)
    if hands:
        for S in 'LR':
            hp, hn, ht = hand_mesh(Wd[f'DG_Handle{S}'][1], r=14, up=(0, 0.1, -1))
            T = np.concatenate([T, ht + len(P)]); P = np.concatenate([P, hp]); N = np.concatenate([N, hn]); UV = np.concatenate([UV, np.full((len(hp), 2), 0.02)])
    ps = []
    for i, v in enumerate(views):
        e, l = VIEWS[v]
        ps.append(dm.render(P, N, UV, T, f'/tmp/claude-0/pa{i}.png', np.array(l) + np.array(e), np.array(l), tex, fov=40 if not v.startswith('foot') else 30))
    return dm.sheet(ps, out)


def write_poses(rig, out):
    """src/DragonPoses.h: what the game blends the new dragon's bones to - its wings folded for a dive, each talon's
    curl axis, the legs' tuck"""
    dive = dive_pose(rig)
    L = ['// Written by tools/dragonbuild.py poses - do not edit: the glider dragon\'s solved poses (tools/dragonbuild.py).',
         '#pragma once', 'namespace DragonPoses', '{',
         '\tstruct BoneQuat { const char* bone; float w, x, y, z; };',
         '\t// the wings folded tight for a dive (local rotations, on top of the rest pose)',
         '\tinline constexpr BoneQuat kDive[] = {']
    for b in sorted(dive):
        q = quat(dive[b])
        L.append(f'\t\t{{ "{b}", {q[0]:.6f}f, {q[1]:.6f}f, {q[2]:.6f}f, {q[3]:.6f}f }},')
    L.append('\t};')
    L.append('\tstruct Axis { const char* bone; float x, y, z; float amount; };')
    L.append('\t// each talon joint: the axis it curls about (its parent\'s space = the dragon\'s), and how far of a full curl')
    L.append('\tinline constexpr Axis kToes[] = {')
    for S in 'LR':
        for a, b in ((1, 2), (3, 4), (5, 6)):
            for bone, child, amt in ((f'DG_Toe{S}{a}', f'DG_Toe{S}{b}', 0.95), (f'DG_Toe{S}{b}', None, 1.25)):
                p = rig.bones[bone][1]
                d = (rig.bones[child][1] - p) if child else (p - rig.bones[f'DG_Toe{S}{a}'][1])
                ax = np.cross(d, [0, 0, 1.0]); ax /= np.linalg.norm(ax)
                L.append(f'\t\t{{ "{bone}", {ax[0]:.5f}f, {ax[1]:.5f}f, {ax[2]:.5f}f, {amt}f }},')
    L.append('\t};')
    for nm, v in (('kLegGlide', LEG_TUCK), ('kLegDive', LEG_DIVE), ('kLegFlap', LEG_FLAP)):
        L.append(f'\tinline constexpr float {nm}[3] = {{ {v[0]}f, {v[1]}f, {v[2]}f }};  // hip, knee, foot (rad, about X)')
    L.append(f"\tinline constexpr float kToesGlide = {TOES['glide']}f, kToesDive = {TOES['dive']}f, kToesFlap = {TOES['flap']}f;  // talon curl")
    L.append('}')
    pathlib.Path(out).write_text('\n'.join(L) + '\n')
    print('wrote', out)


if __name__ == '__main__':
    main()
