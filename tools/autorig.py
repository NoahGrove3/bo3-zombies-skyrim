"""Auto-rig an unrigged character mesh onto a Skyrim skeleton (draugr or human) and write a skinned NIF.

The mesh is turned into Skyrim space (Y forward, Z up), scaled to the skeleton's height and stood on the ground.
Joints default to the skeleton's own (relaxed) pose; per-model landmarks move the joints that differ (arms out,
an arm reaching forward). Limb bones are re-aimed at their moved children, so the bind pose matches the mesh and
Skyrim's animations bend the mesh from there. Weights: distance to each bone's segment, smooth falloff, top 4,
props (a scythe, a staff) locked to a hand.

    python tools/autorig.py <job name>      (jobs are defined in JOBS below)"""
import math, pathlib, re, subprocess, sys
import numpy as np
from PIL import Image, ImageDraw

sys.path.insert(0, str(pathlib.Path(__file__).parent))
import meshlib as M

WORK = pathlib.Path('/home/claude/work')
WRITER = '/home/claude/tools/rigwrite'

def read_nodes(path):
    """nifnodes dump -> {name: (world translation (3,), world rotation (3,3))}"""
    lines = open(path).read().split('\n'); out = {}
    for i, l in enumerate(lines):
        m = re.match(r'\s*\d+ (.+?)\s+local t\(.*?\) world t\(([-\d. ]+)\)', l)
        if m and i + 1 < len(lines):
            r = re.findall(r'-?\d+\.\d+', lines[i + 1])
            if len(r) == 9:
                out[m.group(1).strip()] = (np.array([float(x) for x in m.group(2).split()]), np.array([float(x) for x in r]).reshape(3, 3))
    return out

def L(s): return s  # readability in tables

TRUNK = ['NPC Pelvis [Pelv]', 'NPC Spine [Spn0]', 'NPC Spine1 [Spn1]', 'NPC Spine2 [Spn2]', 'NPC Neck [Neck]', 'NPC Head [Head]']
FOOT = {}
def limbs(S):
    foot = FOOT.get(S, 'NPC L Foot [LLft ]' if S == 'L' else 'NPC R Foot [Rft ]')
    return {'arm': [f'NPC {S} Clavicle [{S}Clv]', f'NPC {S} UpperArm [{S}Uar]', f'NPC {S} Forearm [{S}Lar]', f'NPC {S} Hand [{S}Hnd]'],
            'leg': [f'NPC {S} Thigh [{S}Thg]', f'NPC {S} Calf [{S}Clf]', foot, f'NPC {S} Toe0 [{S}Toe]']}

def rot_between(a, b):
    a = a / np.linalg.norm(a); b = b / np.linalg.norm(b)
    v = np.cross(a, b); c = float(np.dot(a, b))
    if np.linalg.norm(v) < 1e-8:
        return np.eye(3) if c > 0 else -np.eye(3)
    vx = np.array([[0, -v[2], v[1]], [v[2], 0, -v[0]], [-v[1], v[0], 0]])
    return np.eye(3) + vx + vx @ vx * (1 / (1 + c))

def seg_dist(P, a, b):
    ab = b - a; t = np.clip(((P - a) @ ab) / max(1e-9, ab @ ab), 0, 1)
    return np.linalg.norm(P - (a + t[:, None] * ab), axis=1)

R_ARM = ['NPC R Clavicle [RClv]', 'NPC R UpperArm [RUar]', 'NPC R Forearm [RLar]', 'NPC R Hand [RHnd]']
L_ARM = ['NPC L Clavicle [LClv]', 'NPC L UpperArm [LUar]', 'NPC L Forearm [LLar]', 'NPC L Hand [LHnd]']
JOBS = {
    # Iron Reaper: draugr skeleton, bulky plate, scythe in the right hand (model's right = Skyrim +X)
    'reaper': dict(obj=WORK / 'enemies/reaper_s2.obj', skel=WORK / 'skel/draugr_nodes.txt', top=152.0, out='reaper/reaper.nif',
                   joints={R_ARM[0]: (6, -5, 122), R_ARM[1]: (24, -3, 117), R_ARM[2]: (30, 0, 99), R_ARM[3]: (40, 14, 88),
                           L_ARM[0]: (-6, -5, 122), L_ARM[1]: (-24, -3, 117), L_ARM[2]: (-31, -3, 96), L_ARM[3]: (-35, 0, 77),
                           'NPC R Thigh [RThg]': (9, -2, 80), 'NPC R Calf [RClf]': (14, -1, 45), 'NPC R Foot [Rft ]': (17, -4, 9), 'NPC R Toe0 [RToe]': (19, 8, 1),
                           'NPC L Thigh [LThg]': (-9, -2, 80), 'NPC L Calf [LClf]': (-15, -1, 45), 'NPC L Foot [LLft ]': (-20, -4, 9), 'NPC L Toe0 [LToe]': (-22, 8, 1)},
                   props=[((44, -200, 0), (300, 200, 300), R_ARM[3]), ((26, 12, 55), (44, 200, 130), R_ARM[3])],
                   tex=dict(base=pathlib.Path('/home/claude/models_in/reaper/textures/Zyklus5_Zeile6_basecolor.jpeg'),
                            normal=pathlib.Path('/home/claude/models_in/reaper/textures/Zyklus5_Zeile6_normal.jpeg'),
                            rough=pathlib.Path('/home/claude/models_in/reaper/textures/Zyklus5_Zeile6_roughness.jpeg')),
                   glow=None, spec=0.6, gloss=40),
    # Voidbound Necromancer: human skeleton, skull staff in the right hand, left hand reaching forward
    'necro': dict(obj=WORK / 'enemies/necro_s2.obj', skel=WORK / 'skel/human_nodes.txt', top=144.0, shift=(30.0, 0.0), out='necro/necro.nif',
                  joints={R_ARM[1]: (13, -4, 108), R_ARM[2]: (23, 0, 86), R_ARM[3]: (28, 4, 96),
                          L_ARM[1]: (-13, -4, 106), L_ARM[2]: (-22, 20, 104), L_ARM[3]: (-28, 45, 104)},
                  props=[((20, -200, 98), (300, 200, 300), R_ARM[3])],
                  tex=dict(base=pathlib.Path('/home/claude/work/enemies/necro/tex0.jpg'), normal=None, rough=None),
                  glow=(0.55, 0.25, 1.0), spec=0.25, gloss=20),
    # 2026-10-11 pass (prepared by tools in work/enemies2: objexp -> weld -> seamsimp 28k)
    'abom': dict(obj=WORK / 'enemies2/abom_s2.obj', skel=WORK / 'skel/draugr_nodes.txt', top=160.0, out='abom/abom.nif',
                 joints={'NPC Spine2 [Spn2]': (0, -4, 108), 'NPC Neck [Neck]': (0, 14, 128), 'NPC Head [Head]': (0, 34, 136),
                         R_ARM[0]: (12, -4, 124), R_ARM[1]: (42, -8, 116), R_ARM[2]: (57, -10, 95), R_ARM[3]: (64, 12, 55),
                         L_ARM[0]: (-12, -4, 124), L_ARM[1]: (-42, -8, 116), L_ARM[2]: (-59, -27, 97), L_ARM[3]: (-67, -5, 55),
                         'NPC R Thigh [RThg]': (16, -10, 72), 'NPC R Calf [RClf]': (25, 8, 46), 'NPC R Foot [Rft ]': (23, -22, 13), 'NPC R Toe0 [RToe]': (29, 4, 2),
                         'NPC L Thigh [LThg]': (-16, -10, 72), 'NPC L Calf [LClf]': (-25, 8, 46), 'NPC L Foot [LLft ]': (-22, -22, 13), 'NPC L Toe0 [LToe]': (-31, 4, 2)},
                 props=[],
                 tex=dict(base=pathlib.Path('/home/claude/work/enemies2/abom/textures/Zyklus16_Zeile1_fbx_basecolor.jpeg'),
                          normal=pathlib.Path('/home/claude/work/enemies2/abom/textures/Zyklus16_Zeile1_fbx_normal.jpeg'),
                          rough=pathlib.Path('/home/claude/work/enemies2/abom/textures/Zyklus16_Zeile1_fbx_roughness.jpeg')),
                 glow=None, spec=0.3, gloss=20),
    'king': dict(obj=WORK / 'enemies2/king_s2.obj', skel=WORK / 'skel/draugr_nodes.txt', top=152.0, out='king/king.nif',
                 joints={R_ARM[1]: (24, -2, 119), R_ARM[2]: (31, -6, 95), R_ARM[3]: (37, 4, 76),
                         L_ARM[1]: (-24, -2, 119), L_ARM[2]: (-31, -1, 95), L_ARM[3]: (-33, 4, 76),
                         'NPC R Thigh [RThg]': (10, 0, 78), 'NPC R Calf [RClf]': (16, 0, 45), 'NPC R Foot [Rft ]': (22, -3, 9), 'NPC R Toe0 [RToe]': (26, 12, 1),
                         'NPC L Thigh [LThg]': (-10, 0, 78), 'NPC L Calf [LClf]': (-16, 0, 45), 'NPC L Foot [LLft ]': (-22, -3, 9), 'NPC L Toe0 [LToe]': (-26, 12, 1)},
                 props=[((62, 33, 0), (80, 52, 22), R_ARM[3]), ((51, 19, 22), (72, 45, 40), R_ARM[3]),
                        ((38, 4, 40), (60, 30, 56), R_ARM[3]), ((37, 1, 56), (50, 18, 70), R_ARM[3]),
                        ((-70, -70, 0), (70, -16, 70), 'NPC Pelvis [Pelv]')],
                 tex=dict(base=pathlib.Path('/home/claude/work/enemies2/king/textures/Zyklus2_Zeile3_basecolor.jpeg'),
                          normal=pathlib.Path('/home/claude/work/enemies2/king/textures/Zyklus2_Zeile3_normal.jpeg'),
                          rough=pathlib.Path('/home/claude/work/enemies2/king/textures/Zyklus2_Zeile3_roughness.jpeg')),
                 glow=None, spec=0.6, gloss=40),
    'knight': dict(obj=WORK / 'enemies2/knight_s2.obj', skel=WORK / 'skel/draugr_nodes.txt', top=155.0, out='knight/knight.nif',
                   joints={R_ARM[1]: (22, 0, 119), R_ARM[2]: (30, -4, 97), R_ARM[3]: (38, 4, 79),
                           L_ARM[1]: (-24, 0, 119), L_ARM[2]: (-30, 0, 97), L_ARM[3]: (-34, 8, 79),
                           'NPC R Thigh [RThg]': (11, 0, 78), 'NPC R Calf [RClf]': (19, -5, 45), 'NPC R Foot [Rft ]': (22, -12, 9), 'NPC R Toe0 [RToe]': (25, -1, 1),
                           'NPC L Thigh [LThg]': (-11, 0, 78), 'NPC L Calf [LClf]': (-16, 8, 45), 'NPC L Foot [LLft ]': (-20, 8, 9), 'NPC L Toe0 [LToe]': (-24, 20, 1)},
                   props=[((36, -10, -1), (60, 16, 20), R_ARM[3]), ((30, -10, 20), (60, 16, 58), R_ARM[3]), ((27, -10, 58), (60, 16, 70), R_ARM[3])],
                   tex=dict(base=pathlib.Path('/home/claude/work/enemies2/knight/textures/Zyklus4_Zeile6_basecolor.jpeg'),
                            normal=pathlib.Path('/home/claude/work/enemies2/knight/textures/Zyklus4_Zeile6_normal.jpeg'),
                            rough=pathlib.Path('/home/claude/work/enemies2/knight/textures/Zyklus4_Zeile6_roughness.jpeg')),
                   glow=None, spec=0.5, gloss=35),
    'lich': dict(obj=WORK / 'enemies2/lich_s2.obj', skel=WORK / 'skel/draugr_nodes.txt', top=150.0, out='lich/lich.nif',
                 joints={R_ARM[1]: (22, -3, 119), R_ARM[2]: (36, -2, 96), R_ARM[3]: (48, 8, 74),
                         L_ARM[1]: (-22, -3, 119), L_ARM[2]: (-29, 2, 97), L_ARM[3]: (-32, 27, 92),
                         'NPC R Thigh [RThg]': (11, 0, 78), 'NPC R Calf [RClf]': (24, 6, 45), 'NPC R Foot [Rft ]': (28, 0, 9), 'NPC R Toe0 [RToe]': (32, 16, 1),
                         'NPC L Thigh [LThg]': (-11, 0, 78), 'NPC L Calf [LClf]': (-24, 6, 45), 'NPC L Foot [LLft ]': (-27, 0, 9), 'NPC L Toe0 [LToe]': (-34, 16, 1)},
                 props=[((-48, 29, 100), (-22, 53, 151), L_ARM[3]), ((-37, 19, 70), (-24, 38, 100), L_ARM[3]),
                        ((-34, 18, 44), (-22, 36, 70), L_ARM[3]), ((-33, 7, 12), (-23, 20, 28), L_ARM[3]),
                        ((-70, -70, 12), (70, -16, 75), 'NPC Pelvis [Pelv]')],
                 tex=dict(base=pathlib.Path('/home/claude/work/enemies2/lich/textures/Zyklus2_Zeile6_basecolor.jpeg'),
                          normal=pathlib.Path('/home/claude/work/enemies2/lich/textures/Zyklus2_Zeile6_normal.jpeg'),
                          rough=pathlib.Path('/home/claude/work/enemies2/lich/textures/Zyklus2_Zeile6_roughness.jpeg')),
                 glow=None, spec=0.35, gloss=25),
    'hag': dict(obj=WORK / 'enemies2/hag_s2.obj', skel=WORK / 'skel/draugr_nodes.txt', top=132.0, out='hag/hag.nif',
                joints={'NPC Pelvis [Pelv]': (-2, -9, 74), 'NPC Spine [Spn0]': (-2, -9, 79), 'NPC Spine1 [Spn1]': (-1, -6, 88), 'NPC Spine2 [Spn2]': (0, -1, 98),
                        'NPC Neck [Neck]': (2, 10, 111), 'NPC Head [Head]': (3, 20, 121),
                        R_ARM[0]: (4, 0, 108), R_ARM[1]: (15, 1, 105), R_ARM[2]: (27, -12, 86), R_ARM[3]: (35, 0, 66),
                        L_ARM[0]: (-4, 0, 108), L_ARM[1]: (-19, 1, 105), L_ARM[2]: (-35, -9, 86), L_ARM[3]: (-41, 0, 66),
                        'NPC R Thigh [RThg]': (7, -8, 70), 'NPC R Calf [RClf]': (12, 5, 45), 'NPC R Foot [Rft ]': (13, -7, 8), 'NPC R Toe0 [RToe]': (18, 5, 1),
                        'NPC L Thigh [LThg]': (-10, -8, 70), 'NPC L Calf [LClf]': (-19, 5, 45), 'NPC L Foot [LLft ]': (-20, -7, 8), 'NPC L Toe0 [LToe]': (-25, 5, 1)},
                props=[((-26, 14, 44), (26, 40, 116), 'NPC Head [Head]')],
                tex=dict(base=pathlib.Path('/home/claude/work/enemies2/hag/textures/Zyklus8_Zeile3_basecolor.jpeg'),
                         normal=pathlib.Path('/home/claude/work/enemies2/hag/textures/Zyklus8_Zeile3_normal.jpeg'),
                         rough=pathlib.Path('/home/claude/work/enemies2/hag/textures/Zyklus8_Zeile3_roughness.jpeg')),
                glow=None, spec=0.5, gloss=45),
    'hooded': dict(obj=WORK / 'enemies2/hooded_s2.obj', skel=WORK / 'skel/human_nodes.txt', top=140.0, out='hooded/hooded.nif',
                   joints={'NPC Pelvis [Pelv]': (-2, 3, 70), 'NPC Spine [Spn0]': (-2, 2, 77), 'NPC Spine1 [Spn1]': (-2, 2, 86), 'NPC Spine2 [Spn2]': (-2, 2, 97),
                           'NPC Neck [Neck]': (-1, 3, 115), 'NPC Head [Head]': (-1, 6, 122),
                           R_ARM[1]: (11, 2, 106), R_ARM[2]: (15, 2, 82), R_ARM[3]: (18, 10, 64),
                           L_ARM[1]: (-15, -2, 106), L_ARM[2]: (-20, 2, 82), L_ARM[3]: (-23, 10, 64),
                           'NPC R Thigh [RThg]': (5, 3, 70), 'NPC R Calf [RClf]': (8, 2, 36), 'NPC R Foot [Rft ]': (9, -3, 7), 'NPC R Toe0 [RToe]': (11, 7, 1),
                           'NPC L Thigh [LThg]': (-9, 3, 70), 'NPC L Calf [LClf]': (-11, 2, 36), 'NPC L Foot [Lft ]': (-12, -3, 7), 'NPC L Toe0 [LToe]': (-14, 7, 1)},
                   props=[],
                   tex=dict(base=pathlib.Path('/home/claude/work/enemies2/hooded/exp/tex0.png'), normal=None, rough=None),
                   skirt=dict(hip=68.0, legMax=0.55, blend=7.0, footTop=10.0, footR=7.0, power=1.4), armRadius=3.2,
                   glow=None, spec=0.2, gloss=15),
}

# (Noah: the Volatile) an infected creep - a vertex-coloured sculpt, decimated and its colours baked into an atlas
# (work/creep: simplify -> bake.py); arms out and down, tall and gaunt
JOBS['volatile'] = dict(obj=WORK / 'creep/creep_s2.obj', skel=WORK / 'skel/draugr_nodes.txt', top=170.0, out='volatile/volatile.nif', vn=True,
                        joints={'NPC Pelvis [Pelv]': (0, 0, 80), 'NPC Spine [Spn0]': (0, 0, 86), 'NPC Spine1 [Spn1]': (0, 0, 97), 'NPC Spine2 [Spn2]': (0, -1, 110),
                                'NPC Neck [Neck]': (0, 0, 134), 'NPC Head [Head]': (0, 2, 145),
                                R_ARM[0]: (6, -2, 130), R_ARM[1]: (20, 0, 128), R_ARM[2]: (39, 1, 115), R_ARM[3]: (57, 2, 104),
                                L_ARM[0]: (-6, -2, 130), L_ARM[1]: (-20, 0, 128), L_ARM[2]: (-39, 1, 115), L_ARM[3]: (-57, 2, 104),
                                'NPC R Thigh [RThg]': (10, 0, 75), 'NPC R Calf [RClf]': (16, 0, 41), 'NPC R Foot [Rft ]': (20, -3, 8), 'NPC R Toe0 [RToe]': (22, 9, 1),
                                'NPC L Thigh [LThg]': (-10, 0, 75), 'NPC L Calf [LClf]': (-16, 0, 41), 'NPC L Foot [LLft ]': (-20, -3, 8), 'NPC L Toe0 [LToe]': (-22, 9, 1)},
                        props=[], tex=dict(base=WORK / 'creep/exp/tex0.png', normal=None, rough=None), glow=None, spec=0.35, gloss=25)

def to_skyrim(V, top, shift=(0, 0)):
    P = np.stack([-V[:, 0], V[:, 2], V[:, 1]], 1).astype(np.float64)
    P *= top / np.ptp(P[:, 2])
    P[:, 2] -= P[:, 2].min()
    head = P[P[:, 2] > 0.86 * top]
    torso = P[(P[:, 2] > 0.55 * top) & (P[:, 2] < 0.8 * top)]
    P[:, 0] -= np.median(head[:, 0]) - shift[0]; P[:, 1] -= np.median(torso[:, 1]) - shift[1]
    return P

def debug_view(P, F, joints, path, size=800):
    out = Image.new('RGB', (size * 2, size), (28, 28, 34))
    for k, (ax, flip) in enumerate((((0, 2), 1), ((1, 2), 1))):
        im = Image.new('RGB', (size, size), (28, 28, 34)); d = ImageDraw.Draw(im)
        Q = P[:, ax]; sc = size / 170.0; ox = size / 2
        to = lambda p: (ox + p[0] * sc, size - 20 - p[1] * sc)
        depth = P[:, 1 - ax[0] if ax[0] < 2 else 0]
        tri = P[F]; n = np.cross(tri[:, 1] - tri[:, 0], tri[:, 2] - tri[:, 0]); n /= np.linalg.norm(n, axis=1, keepdims=True) + 1e-9
        l = np.clip(np.abs(n @ np.array([0.3, 0.5, 0.8])), 0.25, 1)
        dcol = [i for i in range(3) if i not in ax][0]
        for i in np.argsort(tri[:, :, dcol].mean(1) * (1 if k == 0 else -1)):
            c = int(150 * l[i]); d.polygon([to((p[ax[0]], p[ax[1]])) for p in tri[i]], fill=(c, c, c))
        for g in range(-80, 90, 10):
            x, _ = to((g, 0)); d.line([(x, 0), (x, size)], fill=(60, 60, 90) if g else (120, 60, 60))
            d.text((x + 2, 2), str(g), fill=(140, 140, 200))
        for g in range(0, 170, 10):
            _, y = to((0, g)); d.line([(0, y), (size, y)], fill=(60, 60, 90)); d.text((2, y - 10), str(g), fill=(140, 140, 200))
        for name, (pos, _) in joints.items():
            x, y = to((pos[ax[0]], pos[ax[1]]))
            col = (255, 80, 80) if ' R ' in name else (80, 160, 255) if ' L ' in name else (255, 220, 60)
            d.ellipse([x - 4, y - 4, x + 4, y + 4], fill=col)
        out.paste(im, (k * size, 0))
    out.save(path)

def build(name, preview_only=False):
    J = JOBS[name]
    V, T, F = M.load_obj(J['obj'])
    P = to_skyrim(V, J['top'], J.get('shift', (0, 0)))
    VN = None
    if J.get('vn'):  # (an atlas of separate triangles: its smooth normals come with it, the welded ones)
        vn = [l[3:] for l in open(J['obj']) if l.startswith('vn ')]
        VN = np.loadtxt(vn, dtype=np.float64)
        VN = np.stack([-VN[:, 0], VN[:, 2], VN[:, 1]], 1)
    skel = read_nodes(J['skel'])
    for S in 'LR':
        FOOT[S] = next(k for k in skel if k.startswith(f'NPC {S} Foot ['))
    joints = {k: (v[0].copy(), v[1].copy()) for k, v in skel.items() if k.startswith('NPC ')}
    for k, pos in J['joints'].items():
        joints[k] = (np.array(pos, float), joints[k][1])
    if preview_only:
        debug_view(P, F, {k: joints[k] for k in TRUNK + sum([sum(limbs(S).values(), []) for S in 'LR'], [])}, WORK / f'prev/rig_{name}.png')
        return
    # bind rotations: limbs re-aimed at their moved child joints; everything else keeps the skeleton's rotation
    bind = {}
    chains = [TRUNK] + [c for S in 'LR' for c in limbs(S).values()]
    for chain in chains:
        for i, b in enumerate(chain):
            pos, rot = joints[b]
            if i + 1 < len(chain) and b in J['joints'] or (i + 1 < len(chain) and chain[i + 1] in J['joints']):
                c = chain[i + 1]
                d0 = skel[c][0] - skel[b][0]; d1 = joints[c][0] - pos
                rot = rot_between(d0, d1) @ skel[b][1]
            bind[b] = (pos, rot)
    # hands follow their forearm's re-aim
    for S in 'LR':
        arm = limbs(S)['arm']
        if arm[3] not in J['joints'] and arm[2] in bind:
            bind[arm[3]] = (joints[arm[3]][0], bind[arm[2]][1] @ skel[arm[2]][1].T @ skel[arm[3]][1])
    # segments for weighting
    segs = []
    for chain in chains:
        for i, b in enumerate(chain):
            a = bind[b][0]
            e = bind[chain[i + 1]][0] if i + 1 < len(chain) else a + (a - bind[chain[i - 1]][0]) * 0.5
            segs.append((b, a, e))
    D = np.stack([seg_dist(P, a, e) for _, a, e in segs], 1)
    names = [s[0] for s in segs]
    # keep arms off the legs and torso-hugging robe off the arms: penalise far-side bones
    W = 1.0 / np.maximum(D, 0.5) ** 4
    for lo, hi, bone in J['props']:
        inside = np.all((P >= np.array(lo)) & (P <= np.array(hi)), 1)
        W[inside] = 0; W[inside, names.index(bone)] = 1.0
    if J.get('armRadius'):
        # (Noah: "the Bride's dress is still attached to her arms") her arms are fused to the dress in one mesh, hanging
        # against it: an arm bone may pull a point only if the point is reached from that arm along the surface about as
        # directly as through the air - the arm itself; the cloth beside it is reached only the long way round (up the
        # arm, over the shoulder, down the dress) and keeps to the body
        from scipy.sparse import coo_matrix
        from scipy.sparse.csgraph import dijkstra
        key = np.round(P * 50).astype(np.int64); _, inv = np.unique(key, axis=0, return_inverse=True); inv = inv.ravel()
        Fw = inv[F]; nW = inv.max() + 1
        Pw = np.zeros((nW, 3)); Pw[inv] = P
        e0 = np.concatenate([Fw[:, 0], Fw[:, 1], Fw[:, 2]]); e1 = np.concatenate([Fw[:, 1], Fw[:, 2], Fw[:, 0]])
        wl = np.linalg.norm(Pw[e0] - Pw[e1], axis=1) + 1e-6
        G = coo_matrix((wl, (e0, e1)), shape=(nW, nW)).tocsr()
        for k, n in enumerate(names):
            if not any(x in n for x in ('UpperArm', 'Forearm', 'Hand', 'Finger')):
                continue
            dE = D[:, k]  # (per original vertex) distance through the air to this bone
            dEw = np.full(nW, 1e9); np.minimum.at(dEw, inv, dE)
            seeds = np.nonzero(dEw < J['armRadius'])[0]
            if len(seeds) == 0:
                W[:, k] = 0
                continue
            dg = dijkstra(G, directed=False, indices=seeds, min_only=True)
            dGw = dg + J['armRadius']
            okw = (dGw <= dEw * 1.4 + 3.0).astype(np.float64)
            # (softened across the surface: an arm's edge fades out over a few rings, nothing tears)
            A = coo_matrix((np.ones(len(e0)), (e0, e1)), shape=(nW, nW)).tocsr(); A = A + A.T
            deg = np.asarray(A.sum(1)).ravel() + 1
            for _ in range(4):
                okw = (A @ okw + okw) / deg
            W[:, k] *= np.clip(okw[inv] * 1.6 - 0.3, 0, 1)
        W[W.sum(1) <= 0, names.index('NPC Spine2 [Spn2]')] = 1.0
    top = np.argsort(-W, 1)[:, :4]
    tw = np.take_along_axis(W, top, 1); tw /= tw.sum(1, keepdims=True)
    if J.get('skirt'):
        # (Noah: "fix the Draugr Bride's dress") a long dress is not two trouser legs: below the hips it hangs from the
        # pelvis and is only drawn after the thighs, more toward the hem, each side by its own leg and the middle by
        # both - so it sways and parts as she runs instead of splitting and stretching with the legs; it never bends
        # at the knees
        sk = J['skirt']
        hip = sk['hip']; pel = names.index('NPC Pelvis [Pelv]')
        lt = names.index('NPC L Thigh [LThg]'); rt = names.index('NPC R Thigh [RThg]')
        feet = [bind[FOOT[S]][0] for S in 'LR']
        for v in np.nonzero(P[:, 2] < hip)[0]:
            z, x = P[v, 2], P[v, 0]
            if z < sk.get('footTop', 0) and min(np.linalg.norm(P[v, :2] - f[:2]) for f in feet) < sk.get('footR', 0):
                continue  # (a foot peeking out: its own weights)
            if any(k in names[top[v, 0]] for k in ('Hand', 'Forearm', 'UpperArm', 'Finger', 'Clavicle')):
                continue  # (her hands hang low beside the dress: they stay the arms')
            t = np.clip((hip - z) / hip, 0, 1)
            leg = sk['legMax'] * t ** sk.get('power', 0.8)
            sL = np.clip(0.5 - x / (2 * sk['blend']), 0, 1)  # left is -X
            top[v, :] = [pel, lt, rt, pel]
            tw[v, :] = [1 - leg, leg * sL, leg * (1 - sL), 0]
    used = sorted(set(top[tw > 0.01].ravel()))
    idx = {b: k for k, b in enumerate(used)}
    N = VN if VN is not None else M_normals(P, F)
    lines = ['bones ' + str(len(used))]
    for b in used:
        pos, rot = bind[names[b]]
        lines.append('bone ' + names[b] + '|' + ' '.join(f'{x:.5f}' for x in list(pos) + list(rot.ravel())))
    lines.append(f'shape {name} ' + J['matline'])
    for v in range(len(P)):
        ws = [(idx[top[v, k]], tw[v, k]) for k in range(4) if tw[v, k] > 0.01]
        s = sum(w for _, w in ws)
        lines.append(f'v {P[v,0]:.4f} {P[v,1]:.4f} {P[v,2]:.4f} {T[v,0]:.5f} {1.0 - T[v,1]:.5f} {N[v,0]:.4f} {N[v,1]:.4f} {N[v,2]:.4f} ' + ' '.join(f'{b} {w / s:.4f}' for b, w in ws))
    lines += [f't {a} {b} {c}' for a, b, c in F]
    return lines

def M_normals(P, F):
    N = np.zeros_like(P); fn = np.cross(P[F[:, 1]] - P[F[:, 0]], P[F[:, 2]] - P[F[:, 0]])
    for k in range(3): np.add.at(N, F[:, k], fn)
    return N / (np.linalg.norm(N, axis=1, keepdims=True) + 1e-9)

def textures(name, J, out):
    import assets as A
    A.OUT = out
    t = J['tex']
    d = A.save_dds(A.load(t['base'], 2048), f'{name}/{name}_d.dds')
    if t['normal'] is not None:
        n = A.save_dds(A.normal_with_spec(t['normal'], t['rough'], 2048), f'{name}/{name}_n.dds', alpha=True)
    else:  # no normal map: derive one from the albedo's light/dark detail
        g = np.asarray(A.load(t['base'], 2048, 'L'), np.float32) / 255
        gx = np.gradient(g, axis=1); gy = np.gradient(g, axis=0)
        nm = np.dstack([np.clip(128 - gx * 900, 0, 255), np.clip(128 + gy * 900, 0, 255), np.full_like(g, 255), np.full_like(g, 60)]).astype(np.uint8)
        n = A.save_dds(Image.fromarray(nm, 'RGBA'), f'{name}/{name}_n.dds', alpha=True)
    line = f'diffuse={d} normal={n} spec={J["spec"]} gloss={J["gloss"]}'
    if J['glow']:
        a = np.asarray(A.load(t['base'], 1024, 'L'), np.float32)
        g = np.clip((a - 150) * 4, 0, 255)  # only the brightest bits glow (eyes, runes)
        gl = A.save_dds(Image.fromarray(np.dstack([g * J['glow'][0], g * J['glow'][1], g * J['glow'][2]]).astype(np.uint8), 'RGB'), f'{name}/{name}_g.dds')
        line += f' glow={gl} emit={",".join(str(c) for c in J["glow"])} emitmult=2.0'
    return line

if __name__ == '__main__':
    name = sys.argv[1]
    if len(sys.argv) > 2 and sys.argv[2] == 'preview':
        build(name, preview_only=True); sys.exit()
    out = pathlib.Path(sys.argv[2] if len(sys.argv) > 2 else '/home/claude/work/out/Data')
    J = JOBS[name]
    J['matline'] = textures(name, J, out)
    lines = build(name)
    nif = out / 'Meshes' / 'CurseOfAlduin' / J['out']; nif.parent.mkdir(parents=True, exist_ok=True)
    job = WORK / f'enemies/{name}.rig'; job.write_text('\n'.join([f'out {nif}'] + lines) + '\n')
    r = subprocess.run([WRITER, str(job)], capture_output=True, text=True)
    print(r.stdout.strip()[-300:], r.stderr.strip()[-500:])
    if r.returncode: raise SystemExit('rigwrite failed')
