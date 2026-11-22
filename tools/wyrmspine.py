"""(Noah, 2026-11-21: Destruction's new wonder, in place of the Staff of Magnus - "you can obtain the staff of Magnus in base
Skyrim" - chosen from the thrown-spear ideas: "Wyrmspine Javelin") a javelin carved from a dragon's spine: a leaf-shaped
point of dragon bone, double-edged, the Dragon tongue's letters cut down its middle and glowing like banked embers; under it
a collar of bone with three small fangs curling up round the point's root; the shaft a run of the dragon's own vertebrae,
each with its ridge of spine and two stubs at its sides, strung tight one to the next; at the middle the grip bound in
black dragon scales between collars of blackened iron; at the foot a tail spike.

Held as a one-handed sword is (the grip at the origin, the point up +Y), carried on the back (Prn WeaponBack). Built along
Z as tools/priestweapons.py's parts are; its textures are the Dragon Slayer's own (the same bone, scales, iron, letters).

    python3 tools/wyrmspine.py [--keep]
      -> assets/fx/Meshes/CurseOfAlduin/wyrmspine/wyrmspine.nif"""
import math, pathlib, shutil, sys
import numpy as np

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import priestweapons as pw
from throwdagger import Mesh, tool, unit, grid_faces

g = pw.g
ROOT = g.ROOT
pw.MESHDIR = ROOT / 'assets' / 'fx' / 'Meshes' / 'CurseOfAlduin' / 'wyrmspine'
pw.WORK = g.WORK = ROOT / 'build' / 'wyrmspine'
SC = 1.6                       # (model units -> game: the Dragon Slayer's)
pw.FIT['wyrmspine'] = (4.0 / SC, SC)   # (the hand at the grip's middle, model z = 0)
TEX = 'CurseOfAlduin\\slayer\\'

GRIP = (-4.6, 4.6)             # the scale binding
SHAFT = (-27.0, 25.0)          # the vertebrae, foot to collar
HEAD = (26.6, 19.0)            # the point: from z, its length


def vertebra(M, z0, z1, k, rng):
    """one of the spine's bones along z0..z1: a waisted centrum with rims at each end, its spinous ridge raking back
    toward the foot on the throwing side (+x), two stubby transverse processes either side"""
    L = z1 - z0
    r_end, r_mid = 0.66, 0.5
    prof = []
    for t in np.linspace(0, 1, 13):
        rim = math.exp(-((t - 0.06) / 0.07) ** 2) + math.exp(-((t - 0.94) / 0.07) ** 2)
        r = r_mid + (r_end - r_mid) * (2 * t - 1) ** 2 + 0.09 * rim
        prof.append((z0 + t * L, r * (0.97 + 0.06 * rng.random())))
    M.add('Vert', 'hiltbone', *pw.lathe(prof, 20))
    # the ridge: a flattened blade of bone raked back
    a = 0.0
    base = np.array([0.45 * math.cos(a), 0.45 * math.sin(a), z0 + 0.55 * L])
    pw.talon(M, 'hiltbone', base, (1.0, 0.0, -0.35), (0.6, 0.0, -1.0), 1.25 + 0.15 * rng.random(), 0.26, seg=10)
    for s in (-1, 1):   # the side stubs
        b = np.array([0.0, 0.44 * s, z0 + 0.5 * L])
        pw.talon(M, 'hiltbone', b, (0.0, s * 1.0, -0.15), (0.0, s * 0.7, -0.7), 0.55, 0.17, seg=8)


def build():
    rng = np.random.default_rng(4401)
    M = Mesh()
    # the point: a leaf of bone, double-edged, the letters down its middle
    pw.blade(M, 'dbone', HEAD[0], HEAD[1], 3.9, 0.75, tip=0.42, seed=4402, ns=110, fuller=0.8,
             runes=('runes', 0.2, 0.12, 0.72, 3.2, 0.0))
    # (a leaf, not a paddle: widest a third of the way up, then running straight in to a keen point - blade() rounds its nose)
    def h0(t):
        body = 3.9 * 0.5 * (1.0 - 0.18 * t); pt = np.clip((1.0 - t) / 0.42, 0, 1)
        return body * np.sqrt(np.clip(pt * (2 - pt), 0, 1))
    def leaf(t):
        return 3.9 * 0.5 * np.where(t < 0.32, 0.62 + 0.38 * np.sin(np.clip(t / 0.32, 0, 1) * np.pi / 2), np.clip((1 - t) / 0.68, 0, 1) ** 0.85)
    G = []
    for gn, m, V, N, UV, F in M.groups:
        V = np.asarray(V, float).copy()
        t = np.clip((V[:, 2] - HEAD[0]) / HEAD[1], 0, 1)
        k = np.where(h0(t) > 1e-3, leaf(t) / np.maximum(h0(t), 1e-3), 0.0)
        V[:, 0] *= k
        G.append((gn, m, V, N, UV, F))
    M.groups = G
    # the collar: bone turned round the point's root, three fangs curling up along it
    M.add('Collar', 'hiltbone', *pw.lathe([(25.0, 0.62), (25.4, 0.95), (26.0, 1.05), (26.7, 0.86), (26.9, 0.6)], 24))
    for k in range(3):
        a = 2 * math.pi * k / 3 + math.pi / 2
        d = np.array([math.cos(a), math.sin(a), 0.0])
        pw.talon(M, 'hiltbone', d * 0.85 + [0, 0, 25.9], d * 0.55 + [0, 0, 1.0], -d * 0.35 + [0, 0, 1.0], 3.0, 0.28)
    # the spine: vertebrae from the foot to the collar, the grip's span bound instead
    for a, b in ((SHAFT[0], GRIP[0] - 0.5), (GRIP[1] + 0.5, SHAFT[1])):   # (each run fitted whole between its ends)
        n = max(1, round((b - a) / 2.4))
        L = (b - a) / n
        for i in range(n):
            z = a + i * L
            vertebra(M, z, z + L - 0.12, i, rng)
            # the sinew between: a dark cord, a little narrower
            M.add('Sinew', 'scales', *pw.lathe([(z + L - 0.14, 0.42), (z + L + 0.02, 0.45)], 16))
    # the grip: the scale binding between iron collars (the hand round it at z = 0)
    M.add('Iron', 'iron', *pw.lathe([(GRIP[0] - 0.5, 0.62), (GRIP[0] - 0.3, 0.8), (GRIP[0] + 0.05, 0.74)], 22))
    M.add('Grip', 'scales', *pw.lathe([(GRIP[0], 0.68)] + [(zz, 0.7 + 0.04 * math.sin(math.pi * (zz - GRIP[0]) / (GRIP[1] - GRIP[0]))) for zz in np.linspace(GRIP[0] + 0.05, GRIP[1] - 0.05, 26)] + [(GRIP[1], 0.68)], 24,
                                      uvs=(1.0, 2.0, 0.0, 0.0)))
    M.add('Iron', 'iron', *pw.lathe([(GRIP[1] - 0.05, 0.74), (GRIP[1] + 0.3, 0.8), (GRIP[1] + 0.5, 0.62)], 22))
    # the foot: the tail's last bones run down to a spike
    M.add('Spike', 'hiltbone', *pw.lathe([(SHAFT[0] + 0.1, 0.6), (SHAFT[0] - 0.4, 0.62), (SHAFT[0] - 1.6, 0.42), (SHAFT[0] - 3.4, 0.2), (SHAFT[0] - 4.6, 0.02)], 18))
    for k in range(3):
        a = 2 * math.pi * k / 3
        d = np.array([math.cos(a), math.sin(a), 0.0])
        pw.talon(M, 'hiltbone', d * 0.5 + [0, 0, SHAFT[0] - 0.9], d * 0.6 + [0, 0, -0.6], d * 0.1 + [0, 0, -1.0], 1.4, 0.16, seg=8)
    mats = {'dbone': f'diffuse={TEX}slay_bone_d.dds normal={TEX}slay_bone_n.dds spec=0.06 gloss=8 alpha=0 doublesided=0',
            'runes': f'diffuse={TEX}slay_runes_d.dds normal={TEX}slay_runes_n.dds glow={TEX}slay_runes_g.dds emit=1,0.47,0.16 emitmult=1.7 spec=0.06 gloss=8 alpha=blend doublesided=0',
            'hiltbone': f'diffuse={TEX}slay_hiltbone_d.dds normal={TEX}slay_hiltbone_n.dds spec=0.2 gloss=16 alpha=0 doublesided=0',
            'scales': f'diffuse={TEX}slay_scales_d.dds normal={TEX}slay_scales_n.dds spec=0.5 gloss=40 alpha=0 doublesided=0',
            'iron': f'diffuse={TEX}slay_iron_d.dds normal={TEX}slay_iron_n.dds spec=0.45 gloss=24 alpha=0 doublesided=0'}
    return pw.finish(M, 'wyrmspine', mats, None, 'WeaponBack')


if __name__ == '__main__':
    pw.WORK.mkdir(parents=True, exist_ok=True)
    tool('addprn')
    build()
    if '--keep' not in sys.argv:
        shutil.rmtree(pw.WORK, ignore_errors=True)
