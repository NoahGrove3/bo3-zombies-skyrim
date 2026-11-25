"""Our own scythes, built from Skyrim's own weapons (Noah, 2026-11-25: "our own scythes built from Skyrim's own parts ... each
keeping its theme", "25% larger", shown in the workshop for his OK before they go in the game).

Each scythe: a haft - the long pole of one of Skyrim's battleaxes (its head taken off), the staff of a Dragon Priest, or a spine
- and a blade - the blade of one of Skyrim's swords of the same make, bent into a reaping crescent and set across the top of the
haft, its own guard kept as the collar where the two meet. Skyrim's own meshes and textures, read from Noah's game (staged to
/mnt/user-data/uploads/un_tmp/scy - never committed as they are).

In the game's frame (the Battle Scythe models they replace): +Y up the haft, the grip at the origin, the blade reaching out along
-X from the top and curving down; 25% larger than those.

    python3 tools/scythes.py preview   -> build/scythes/<name>.json (the workshop's preview) + build/scythes/sheet.png
"""
import json, math, pathlib, sys
import numpy as np
ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / 'tools' / 'preview'))
SRC = pathlib.Path('/mnt/user-data/uploads/un_tmp/scy/meshes')
OUT = ROOT / 'build' / 'scythes'

YB, YT = -50.0, 84.0          # (the haft: its foot and its head - the Battle Scythe's -35..65, 25% larger and a touch more)
BLADE_LEN = 68.0              # (the blade's length along its curve)

# name: (the game's model it replaces, its title, the haft (model, how), the blade (model), the blade's width, its bend, extras)
SCYTHES = {
 'daedric':      ("Nightcaller's Scythe (Vokun)", ('weapons/daedric/daedricbattleaxe.nif', 'axe'), 'weapons/daedric/daedricgreatsword.nif', 1.55, 78, []),
 'silver':       ('Silver Reaping', ('weapons/silver/silvergreatsword.nif', 'grip'), 'weapons/silver/silvergreatsword.nif', 1.6, 70, []),
 'Skeleton':     ('Bonereaper', ('clutter/bones/humanspine.nif', 'spine'), 'weapons/draugr/draugrgreatsword.nif', 1.55, 82, ['skull']),
 'dragonTeeth':  ('Wyrmtooth', ('dlc01/weapons/dragonbone/battleaxe.nif', 'axe'), 'dlc01/weapons/dragonbone/greatsword.nif', 1.35, 74, []),
 'vampire':      ('Bloodmoon Scythe', ('weapons/ebony/ebonybattleaxe.nif', 'axe'), 'dlc01/weapons/vampire/sword.nif', 2.6, 76, []),
 'forsworn':     ('Shadowflame Scythe - Forsworn (Tier I)', ('weapons/forsworn/forswornaxe.nif', 'axe'), 'weapons/forsworn/forswornsword.nif', 1.6, 74, []),
 'falmer':       ('Shadowflame Scythe - Falmer (Tier II)', ('weapons/falmer/falmeraxe.nif', 'axe'), 'weapons/falmer/falmerlongsword.nif', 1.9, 80, []),
 'dwarven':      ('Shadowflame Scythe - Dwarven (Tier III)', ('weapons/dwarven/dwarvenbattleaxe.nif', 'axe'), 'weapons/dwarven/dwarvengreatsword.nif', 1.5, 66, []),
 'dragonpriest': ('Shadowflame Scythe - Dragon Priest (Tier IV)', ('weapons/dragonpriest/dragonprieststaff.nif', 'staff'), 'weapons/dragonpriest/dragonpriestdagger.nif', 2.1, 84, []),
}


def load(rel):
    import export_live as E
    out = []
    for s in E.read_model(SRC / rel):
        t = s['tex'].lower()
        if s['kind'] != 'lit' or 'blood' in t or 'scabbard' in t or not s['t']: continue
        out.append(dict(name=s['name'], tex=s['tex'], V=np.array(s['v'], float), UV=np.array(s['uv'], float), T=np.array(s['t'], int)))
    return out


def subdivide(sh, maxlen=2.5):
    """long triangles split until no edge is longer than maxlen (so a bend bends them, not just their corners)"""
    V, UV, T = list(sh['V']), list(sh['UV']), [tuple(t) for t in sh['T']]
    mids = {}
    def mid(a, b):
        k = (min(a, b), max(a, b))
        if k not in mids:
            V.append((np.asarray(V[a]) + np.asarray(V[b])) / 2); UV.append((np.asarray(UV[a]) + np.asarray(UV[b])) / 2); mids[k] = len(V) - 1
        return mids[k]
    for _ in range(8):
        out, split = [], False
        for a, b, c in T:
            L = [np.linalg.norm(np.asarray(V[x]) - np.asarray(V[y])) for x, y in ((a, b), (b, c), (c, a))]
            i = int(np.argmax(L))
            if L[i] <= maxlen: out.append((a, b, c)); continue
            split = True
            a_, b_, c_ = [(a, b, c), (b, c, a), (c, a, b)][i]
            m = mid(a_, b_)
            out += [(a_, m, c_), (m, b_, c_)]
        T = out
        if not split: break
    return dict(sh, V=np.array(V, float), UV=np.array(UV, float), T=np.array(T, int))


def keep(sh, ok):
    """the shape's triangles whose three corners all pass"""
    m = ok[sh['T']].all(1)
    T = sh['T'][m]
    if not len(T): return None
    idx = np.unique(T); re = -np.ones(len(sh['V']), int); re[idx] = np.arange(len(idx))
    return dict(sh, V=sh['V'][idx], UV=sh['UV'][idx], T=re[T])


def haft(rel, how):
    parts = []
    if how == 'spine':   # (two of Skyrim's human spines end to end: the Bonereaper's pole of bone)
        sp = load(rel)[0]
        h = sp['V'][:, 1].max() - sp['V'][:, 1].min()
        k = (YT - YB) / (2 * h)
        for i in range(2):
            V = sp['V'].copy(); V[:, 1] = (V[:, 1] - V[:, 1].min()) * k + YB + i * h * k; V[:, [0, 2]] *= 0.75
            parts.append(dict(sp, V=V))
        return parts
    for sh in load(rel):
        V = sh['V']
        if how in ('axe', 'staff'):
            # (the head taken off: the pole is what stays within its own width all the way up)
            r0 = np.percentile(np.abs(V[V[:, 1] < 0][:, 0]), 96) if (V[:, 1] < 0).any() else 2.0
            sh = keep(sh, np.abs(V[:, 0]) <= r0 * 1.35 + 0.4) if how == 'axe' else sh
            if sh is None: continue
            sh = subdivide(sh, 6.0)
            V = sh['V'].copy()
            y0, y1 = V[:, 1].min(), V[:, 1].max()
            # (its foot and its top as Skyrim made them, a touch larger; the plain pole between drawn out to the scythe's length)
            k = 1.12
            a0, a1 = y0 + 0.22 * (y1 - y0), y1 - 0.22 * (y1 - y0)
            b0, b1 = YB + (a0 - y0) * k, (YT - 6) - (y1 - a1) * k
            y = V[:, 1]
            V[:, 1] = np.where(y < a0, b0 + (y - a0) * k, np.where(y > a1, b1 + (y - a1) * k, b0 + (y - a0) * (b1 - b0) / (a1 - a0)))
            V[:, [0, 2]] *= 1.12
            parts.append(dict(sh, V=V))
        elif how == 'grip':   # (the sword's own bound grip, repeated up a pole, its pommel at the foot)
            g = keep(sh, V[:, 1] < -1.0)
            if g is None: continue
            gy0, gy1 = g['V'][:, 1].min(), g['V'][:, 1].max()
            core = keep(g, g['V'][:, 1] > gy0 + 4.5)   # (the wrap without its pommel)
            pom = keep(g, g['V'][:, 1] <= gy0 + 4.5)
            seg = gy1 - (gy0 + 4.5)
            V = pom['V'].copy(); V[:, 1] += YB - gy0; parts.append(dict(pom, V=V))
            y = YB + 4.5; n = int(math.ceil((YT - 4 - y) / seg))
            for i in range(n):
                V = core['V'].copy(); V[:, 1] += y - (gy0 + 4.5); parts.append(dict(core, V=V)); y += seg
    return parts


def blade(rel, width, bend, top):
    """the sword's blade (and its guard) bent into the scythe's crescent: out along -X from the head, curving down"""
    out = []
    for sh in load(rel):
        V = sh['V']
        # where the guard ends and the blade begins: the widest slice above the grip
        ys = np.linspace(0, V[:, 1].max(), 60)
        wid = [np.abs(V[(V[:, 1] > a) & (V[:, 1] < a + 2.5)][:, 0]).max() if ((V[:, 1] > a) & (V[:, 1] < a + 2.5)).any() else 0 for a in ys]
        gy = ys[int(np.argmax(wid[:30]))]
        b = keep(sh, V[:, 1] > gy - 3.0)
        if b is None: continue
        b = subdivide(b)
        P = b['V'].copy()
        y0 = gy + 3.0; L0 = P[:, 1].max() - y0
        s = P[:, 1] - y0
        s = np.where(s > 0, s * (BLADE_LEN / L0), s * 1.2)   # (the guard as it is; the blade drawn to the scythe's length)
        # the curve: heading -X, a little up, bending down through 'bend' degrees over its length
        k = math.radians(bend) / BLADE_LEN
        phi0 = math.radians(180 - 14)
        phi = phi0 + np.clip(s, 0, None) * k            # (turning counter-clockwise from -X: the tip sweeps down)
        # centre line: integrate (cos phi, sin phi) ds (closed form for a circular arc)
        cx = np.where(s > 0, (np.sin(phi) - math.sin(phi0)) / k, s * math.cos(phi0))
        cy = np.where(s > 0, (-np.cos(phi) + math.cos(phi0)) / k, s * math.sin(phi0))
        nx, ny = -np.sin(phi), np.cos(phi)               # (across the blade)
        taper = np.where(s > 0, 1.0, 0.85)
        x = P[:, 0] * width * taper
        X = -1.0 + cx + nx * x
        Y = top + cy + ny * x
        Z = P[:, 2] * 1.2
        out.append(dict(b, V=np.stack([X, Y, Z], 1)))
    return out


def skull(at):
    sk = load('clutter/bones/humanskull.nif')[0]
    V = sk['V'].copy()   # (its crown up +Z, face along -Y: turned to look out along the blade, -X)
    V = np.stack([V[:, 1], V[:, 2], -V[:, 0]], 1) * 0.62
    V += np.array(at) - V.mean(0)
    return [dict(sk, V=V)]


def build(name):
    title, (hrel, how), brel, width, bend, extra = SCYTHES[name]
    parts = haft(hrel, how)
    top = YT - 10
    parts += blade(brel, width, bend, top)
    if 'skull' in extra: parts += skull((0.5, top + 2, 0))
    return title, parts


def sheet():
    import matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt
    fig, axs = plt.subplots(1, len(SCYTHES), figsize=(4 * len(SCYTHES), 6))
    for ax, n in zip(axs, SCYTHES):
        title, parts = build(n)
        for p in parts: ax.triplot(p['V'][:, 0], p['V'][:, 1], p['T'], lw=0.15)
        ax.set_aspect('equal'); ax.set_title(n); ax.axhline(0, c='k', lw=.4); ax.axvline(0, c='k', lw=.4)
    OUT.mkdir(parents=True, exist_ok=True); plt.savefig(OUT / 'sheet.png', dpi=55)


HOW = {'axe': "Skyrim's {w}, its head taken off", 'staff': "Skyrim's {w}", 'spine': "two of Skyrim's human spines, end to end",
       'grip': "the {w}'s own bound grip, carried up a pole, its pommel at the foot"}


def label(rel):
    n = rel.split('/')[-1][:-4]
    nice = {'daedricbattleaxe': 'Daedric Battleaxe', 'daedricgreatsword': 'Daedric Greatsword', 'silvergreatsword': 'Silver Greatsword',
            'humanspine': 'human spine', 'draugrgreatsword': 'Ancient Nord Greatsword', 'battleaxe': 'Dragonbone Battleaxe',
            'greatsword': 'Dragonbone Greatsword', 'ebonybattleaxe': 'Ebony Battleaxe', 'sword': "Dawnguard vampire sword",
            'forswornaxe': 'Forsworn Axe', 'forswornsword': 'Forsworn Sword', 'falmeraxe': 'Falmer War Axe', 'falmerlongsword': 'Falmer Sword',
            'dwarvenbattleaxe': 'Dwarven Battleaxe', 'dwarvengreatsword': 'Dwarven Greatsword', 'dragonprieststaff': 'Dragon Priest staff',
            'dragonpriestdagger': 'Dragon Priest dagger'}
    return nice.get(n, n)


def info():
    out = []
    for k, (title, (hrel, how), brel, width, bend, extra) in SCYTHES.items():
        built = 'Pole: ' + HOW[how].format(w=label(hrel)) + '. Blade: the ' + label(brel) + "'s blade, bent into a crescent, its own guard as the collar."
        if 'skull' in extra: built += " A human skull at the head."
        theme = {'daedric': 'Daedric', 'silver': 'Silver', 'Skeleton': 'Bone', 'dragonTeeth': 'Dragon bone and teeth', 'vampire': 'Vampire',
                 'forsworn': 'Forsworn', 'falmer': 'Falmer', 'dwarven': 'Dwarven', 'dragonpriest': 'Dragon Priest'}[k]
        out.append(dict(id=k, title=title, theme=theme, built=built))
    return out


def preview():
    """the workshop's copy of each: the page's live/ format (tools/preview/export_live.py), its textures small"""
    import base64, gzip, io
    TEX = OUT / 'tex'
    LIVE = ROOT / 'build' / 'preview' / 'live'; LIVE.mkdir(parents=True, exist_ok=True)
    for k in SCYTHES:
        title, parts = build(k)
        merged = {}
        for p in parts:   # (one shape per texture)
            m = merged.get(p['tex'])
            if m is None: merged[p['tex']] = dict(p)
            else: m['T'] = np.concatenate([m['T'], p['T'] + len(m['V'])]); m['V'] = np.concatenate([m['V'], p['V']]); m['UV'] = np.concatenate([m['UV'], p['UV']])
        parts = list(merged.values())
        allp = np.concatenate([p['V'] for p in parts])
        lo, hi = allp.min(0) - 1, allp.max(0) + 1; span = np.maximum(hi - lo, 1e-3)
        buf = io.BytesIO(); head = []; cache = {}
        for p in parts:
            t = p['tex'].lower().replace('textures\\', '', 1).replace('\\', '__').replace(' ', '_')[:-4] + '.jpg'
            if t not in cache:
                f = TEX / t
                cache[t] = 'data:image/jpeg;base64,' + base64.b64encode(f.read_bytes()).decode() if f.exists() else None
            m = {'k': 'm', 'n': len(p['V']), 'nt': len(p['T']), 'i32': len(p['V']) >= 65536}
            if cache[t]: m['map'] = cache[t]
            else: m['color'] = '#8a8a8a'
            head.append(m)
            buf.write(np.round((p['V'] - lo) / span * 65535 - 32768).clip(-32768, 32767).astype('<i2').tobytes())
            buf.write(np.round(p['UV'] * 2048).clip(-32768, 32767).astype('<i2').tobytes())
            buf.write(np.asarray(p['T'], dtype='<u2').tobytes())
        rec = {'key': 'scy_' + k, 'c1': 'white', 'scale': 1.0, 'lo': lo.round(4).tolist(), 'span': span.round(4).tolist(), 'shapes': head,
               'looks': {'o1': []}, 'gz': base64.b64encode(gzip.compress(buf.getvalue(), 9)).decode()}
        (LIVE / f'scy_{k}.json').write_text(json.dumps(rec, separators=(',', ':')))
        print(k, (LIVE / f'scy_{k}.json').stat().st_size // 1024, 'KB', title)
    (OUT / 'info.json').write_text(json.dumps(info(), indent=1))


if __name__ == '__main__':
    if sys.argv[1:] == ['preview']: preview()
    sheet()
