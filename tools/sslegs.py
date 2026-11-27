"""(Noah, Nov 27 decision on the Spellsword set - his note: "the back is clipping through my legs and I don't like that my legs are
visible"; his picks: "Black Mage legs", "B: Trousers + greaves", the back flap "Cut it off")

  - legs(): the Black Mage robe's own trousers and greaves lifted out of it (the robe in his install: Black Mage Armor SE's archive,
    staged into build/vanilla/meshes/blackmage - another author's model, so what this makes is built on his PC's files and never
    committed): each of the robe's pieces is a separate part of its mesh, and the trousers (the one part wrapping both legs, front
    and back) and the greaves (every part below the shins) are kept, the rest (the coat, its tails, the belt) and the robe's own
    neck skin dropped -> build/vanilla/meshes/blackmage_cut/legs{f,m}_{0,1}.nif, which tools/armoursets.py fits to the player's
    body and gives its tiers as the Spellsword's 'boots' (the Draugr boots' place);
  - back(nif): the Draugr cuirass cut at the belt behind - everything of it below the belt and behind the hips' side seam goes
    (the skirt's back half and the back flap), so the trousers show behind and nothing hangs through the legs.

    python3 tools/sslegs.py        (then python3 tools/armoursets.py spellsword)"""
import pathlib, subprocess, sys
import numpy as np
from scipy.sparse import coo_matrix
from scipy.sparse.csgraph import connected_components

ROOT = pathlib.Path(__file__).resolve().parent.parent
TOOLS = pathlib.Path('/home/claude/tools')
VAN = ROOT / 'build' / 'vanilla'
CUT = VAN / 'meshes' / 'blackmage_cut'
WORK = ROOT / 'build' / 'sslegs'
SRC = {'f': 'blackmage_female_jmrobes_{}.nif', 'm': 'blackmage_male_jmrobes_{}.nif'}
BELT_Z, SEAM_Y = 78.0, -1.5   # (the cuirass behind: below the belt and behind the side seam - Skyrim's actors face +Y)


def shapes(nif):
    """each shape: its vertices in the skeleton's space and its triangles, in the shape's own vertex order"""
    WORK.mkdir(parents=True, exist_ok=True)
    obj = WORK / (pathlib.Path(nif).stem + '.obj')
    subprocess.run([str(TOOLS / 'shapeobj'), str(nif), str(obj)], check=True, capture_output=True)
    out, cur, V, F, base, nv = {}, None, [], [], 0, 0
    for l in open(obj):
        if l.startswith('usemtl '):
            if cur is not None: out[cur] = (np.array(V), np.array(F, int).reshape(-1, 3))
            cur = l[7:].strip(); base = nv; V, F = [], []
        elif l.startswith('v '): V.append([float(x) for x in l.split()[1:4]]); nv += 1
        elif l.startswith('f '): F.append([int(x.split('/')[0]) - 1 - base for x in l.split()[1:4]])
    if cur is not None: out[cur] = (np.array(V), np.array(F, int).reshape(-1, 3))
    return out


def parts(V, F):
    n = len(V)
    r = np.concatenate([F[:, 0], F[:, 1], F[:, 2]]); c = np.concatenate([F[:, 1], F[:, 2], F[:, 0]])
    _, lab = connected_components(coo_matrix((np.ones(len(r)), (r, c)), shape=(n, n)), directed=False)
    used = np.zeros(n, bool); used[F.ravel()] = True
    return lab, used


def legs():
    CUT.mkdir(parents=True, exist_ok=True)
    for sx, pat in SRC.items():
        for wt in '01':
            src = VAN / 'meshes' / 'blackmage' / pat.format(wt)
            S = shapes(src)
            name = next(n for n in S if 'skin' not in n.lower())
            V, F = S[name]
            lab, used = parts(V, F)
            keep, trousers = [], None
            for i in np.unique(lab[used]):
                p = V[(lab == i) & used]
                lo, hi = p.min(0), p.max(0)
                if hi[2] < 34.0: keep.append(i); continue                    # the greaves: everything below the shins
                wraps = lo[0] < -10 and hi[0] > 10 and lo[1] < -5 and hi[1] > 5   # (round both legs, front and back)
                if wraps and lo[2] < 40 and hi[2] < 80 and (trousers is None or len(p) > trousers[1]): trousers = (i, len(p))
            if trousers is None: raise SystemExit(f'{src.name}: no trousers found')
            keep.append(trousers[0])
            idx = np.where(np.isin(lab, keep) & used)[0]
            kf = WORK / f'legs{sx}_{wt}.keep'; kf.write_text('\n'.join(map(str, idx)) + '\n')
            out = CUT / f'legs{sx}_{wt}.nif'
            others = [n for n in S if n != name]
            r = subprocess.run([str(TOOLS / 'vertkeep'), str(src), str(out), name, str(kf)] + others, capture_output=True, text=True)
            if r.returncode: raise SystemExit(f'{src.name}: {r.stderr}')
            print(out.name, r.stdout.strip().replace('\n', '; '), f'(trousers + {len(keep) - 1} greave parts)')


def back(nif):
    """the cuirass cut at the belt behind (in place)"""
    S = shapes(nif)
    for name, (V, F) in S.items():
        if len(F) == 0: continue
        c = V[F].mean(1)
        gone = (c[:, 2] < BELT_Z) & (c[:, 1] < SEAM_Y)
        if not gone.any(): continue
        kept = F[~gone]
        idx = np.unique(kept.ravel())
        kf = WORK / f'{pathlib.Path(nif).stem}_{name}.keep'; kf.write_text('\n'.join(map(str, idx)) + '\n')
        r = subprocess.run([str(TOOLS / 'vertkeep'), str(nif), str(nif), name, str(kf)], capture_output=True, text=True)
        if r.returncode: raise SystemExit(f'{nif}: {r.stderr}')
        print(pathlib.Path(nif).name, name, f'{int(gone.sum())} of {len(F)} triangles behind and below the belt cut')


if __name__ == '__main__':
    legs()
