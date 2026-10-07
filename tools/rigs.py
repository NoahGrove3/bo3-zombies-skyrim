"""Re-rig the supplied zombie and dragon-boss characters onto Skyrim's draugr skeleton (skinconv) and build
their textures. Output goes into the same Data tree as tools/assets.py.
    python tools/rigs.py <out Data dir> <draugr skeleton.nif> [zombie|boss]"""
import pathlib, subprocess, sys
import numpy as np
from PIL import Image
sys.path.insert(0, str(pathlib.Path(__file__).parent))
import assets as A

OUT = pathlib.Path(sys.argv[1]); SKEL = sys.argv[2]; ONLY = set(sys.argv[3:])
A.OUT = OUT
CONV = '/home/claude/tools/skinconv'

def finger_maps(side_src, side_tgt, names):
    """names: {(finger 0-4, joint 0-2): source bone}"""
    S = 'L' if side_tgt == 'L' else 'R'
    return [(src, f'NPC {S} Finger{f}{j} [{S}F{f}{j}]') for (f, j), src in names.items()]

def side(s): return 'L' if s == 'L' else 'R'

def humanoid_aims():
    aims = []  # the trunk (pelvis..head) is remapped as one piece by skinconv: no per-bone turning there
    for S in 'LR':
        foot = 'NPC L Foot [LLft ]' if S == 'L' else 'NPC R Foot [Rft ]'
        aims += [(f'NPC {S} Clavicle [{S}Clv]', f'NPC {S} UpperArm [{S}Uar]'), (f'NPC {S} UpperArm [{S}Uar]', f'NPC {S} Forearm [{S}Lar]'),
                 (f'NPC {S} Forearm [{S}Lar]', f'NPC {S} Hand [{S}Hnd]'), (f'NPC {S} Hand [{S}Hnd]', f'NPC {S} Finger20 [{S}F20]'),
                 (f'NPC {S} Thigh [{S}Thg]', f'NPC {S} Calf [{S}Clf]'), (f'NPC {S} Calf [{S}Clf]', foot), (foot, f'NPC {S} Toe0 [{S}Toe]')]
        for f in range(5):
            aims += [(f'NPC {S} Finger{f}0 [{S}F{f}0]', f'NPC {S} Finger{f}1 [{S}F{f}1]'), (f'NPC {S} Finger{f}1 [{S}F{f}1]', f'NPC {S} Finger{f}2 [{S}F{f}2]')]
    return aims

def write_job(name, fbx, maps, mats, extra=()):
    lines = [f'in {fbx}', f'out {A.mesh_out(name)}', f'skeleton {SKEL}', *extra]
    lines += [f'map {s}|{t}' for s, t in maps]
    lines += [f'aim {a}|{b}' for a, b in humanoid_aims()]
    lines += mats
    job = OUT.parent / f'rig_{pathlib.Path(name).stem}.txt'; job.write_text('\n'.join(lines) + '\n')
    r = subprocess.run([CONV, str(job)], capture_output=True, text=True)
    print(r.stdout.strip(), r.stderr.strip())
    if r.returncode: raise SystemExit('skinconv failed')

# ---------------------------------------------------------------- zombie (3ds Max Biped rig)
def zombie():
    d = A.SRC / 'zombie'; T = lambda n: d / 'textures' / n
    dif = A.save_dds(A.load(T('Zombie.png'), 1024), 'zombie/zombie_d.dds')
    nrm = A.save_dds(A.normal_with_spec(T('Zombie_nm.png'), None, 1024), 'zombie/zombie_n.dds', alpha=True)
    glow = A.save_dds(A.load(T('zombie_emission.png'), 1024), 'zombie/zombie_g.dds')
    B = 'Base Human'
    maps = [(f'{B}Pelvis', 'NPC Pelvis [Pelv]'), (f'{B}Spine1', 'NPC Spine [Spn0]'), (f'{B}Spine2', 'NPC Spine1 [Spn1]'), (f'{B}Ribcage', 'NPC Spine2 [Spn2]'),
            (f'{B}Neck', 'NPC Neck [Neck]'), (f'{B}Head', 'NPC Head [Head]'), (f'{B}HeadJaw', 'NPC Head [Jaw]')]
    for S in 'LR':
        leg = 'LLeg' if S == 'L' else 'R'
        foot = 'NPC L Foot [LLft ]' if S == 'L' else 'NPC R Foot [Rft ]'
        maps += [(f'{B}{S}ArmCollarbone', f'NPC {S} Clavicle [{S}Clv]'), (f'{B}{S}ArmUpperarm', f'NPC {S} UpperArm [{S}Uar]'),
                 (f'{B}{S}ArmForearm', f'NPC {S} Forearm [{S}Lar]'), (f'{B}{S}ArmPalm', f'NPC {S} Hand [{S}Hnd]'),
                 (f'{B}{leg}Thigh', f'NPC {S} Thigh [{S}Thg]'), (f'{B}{leg}Calf', f'NPC {S} Calf [{S}Clf]'), (f'{B}{leg}Foot', foot),
                 (f'{B}{leg}Digit11', f'NPC {S} Toe0 [{S}Toe]')]
        for f in range(1, 6):
            for j in range(1, 4):
                maps.append((f'{B}{S}ArmDigit{f}{j}', f'NPC {S} Finger{f-1}{j-1} [{S}F{f-1}{j-1}]'))
    write_job('zombie/zombie.nif', d / 'source/Zombie1.FBX', maps,
              [f'mat * diffuse={dif} normal={nrm} glow={glow} emit=0.6,1.0,0.4 emitmult=1.6 spec=0.15 gloss=20'])

# ---------------------------------------------------------------- dragon boss (Auto-Rig Pro biped with wings and tail)
def boss():
    d = A.SRC / 'dragon'; T = lambda n: d / 'textures' / n
    tiles = sorted({p.name.split('.')[-2] for p in (d / 'textures').glob('DRAGON_BAJA_BAJA5_BaseColor.*.jpg')})
    mats = []
    for t in tiles:
        dif = A.save_dds(A.load(T(f'DRAGON_BAJA_BAJA5_BaseColor.{t}.jpg'), 1024), f'boss/boss_{t}_d.dds')
        rough = T(f'DRAGON_BAJA_BAJA5_Roughness.{t}.jpg')
        nrm = A.save_dds(A.normal_with_spec(T(f'DRAGON_BAJA_BAJA5_Normal.{t}.jpg'), rough if rough.exists() else None, 1024), f'boss/boss_{t}_n.dds', alpha=True)
        line = f'mat DRAGO_{t} diffuse={dif} normal={nrm} spec=0.35 gloss=40'
        em = T(f'DRAGON_BAJA_BAJA5_Emissive.{t}.jpg')
        if em.exists():
            glow = A.save_dds(A.load(em, 1024), f'boss/boss_{t}_g.dds')
            line += f' glow={glow} emit=1.0,0.45,0.15 emitmult=2.5'
        mats.append(line)
    maps = [('root_x', 'NPC Pelvis [Pelv]'), ('root', 'NPC Pelvis [Pelv]'), ('RL_BoneRoot', 'NPC Pelvis [Pelv]'),
            ('spine_01_x', 'NPC Spine [Spn0]'), ('spine_02_x', 'NPC Spine1 [Spn1]'), ('spine_03_x', 'NPC Spine2 [Spn2]'),
            ('neck_x', 'NPC Neck [Neck]'), ('head_x', 'NPC Head [Head]')]
    maps += [(f'c_tail_0{i}_x', 'NPC Pelvis [Tail]') for i in range(7)]
    maps += [(f'c_breast_0{i}_{s}', 'NPC Spine2 [Spn2]') for i in (1, 2) for s in 'lr']
    for s in 'lr':
        S = s.upper(); foot = 'NPC L Foot [LLft ]' if S == 'L' else 'NPC R Foot [Rft ]'
        maps += [(f'shoulder_{s}', f'NPC {S} Clavicle [{S}Clv]'), (f'arm_stretch_{s}', f'NPC {S} UpperArm [{S}Uar]'), (f'arm_twist_{s}', f'NPC {S} UpperArm [{S}Uar]'),
                 (f'forearm_stretch_{s}', f'NPC {S} Forearm [{S}Lar]'), (f'forearm_twist_{s}', f'NPC {S} Forearm [{S}Lar]'), (f'hand_{s}', f'NPC {S} Hand [{S}Hnd]'),
                 (f'thigh_stretch_{s}', f'NPC {S} Thigh [{S}Thg]'), (f'thigh_twist_{s}', f'NPC {S} Thigh [{S}Thg]'),
                 (f'leg_stretch_{s}', f'NPC {S} Calf [{S}Clf]'), (f'leg_twist_{s}', f'NPC {S} Calf [{S}Clf]'), (f'foot_{s}', foot), (f'toes_01_{s}', f'NPC {S} Toe0 [{S}Toe]')]
        for f, fn in enumerate(['thumb', 'index', 'middle', 'ring', 'pinky']):
            for j in range(3):
                src = f'{fn}{j+1}_{s}'
                maps.append((src, f'NPC {S} Finger{f}{j} [{S}F{f}{j}]'))
        # wings ride on the upper back
        for b in [f'shoulder_dupli_001_{s}', f'arm_stretch_dupli_001_{s}', f'arm_twist_dupli_001_{s}', f'forearm_stretch_dupli_001_{s}', f'forearm_twist_dupli_001_{s}',
                  f'hand_dupli_001_{s}', f'c_arm_feather_01_01_01_dupli_001_{s}', f'c_forearm_feather_01_01_01_dupli_001_{s}', f'c_hand_feather_01_01_01_dupli_001_{s}']:
            maps.append((b, 'NPC Spine2 [Spn2]'))
    write_job('boss/boss.nif', d / 'source/Dragon Character 3d by Oscar Creativo.fbx', maps, mats)

# ---------------------------------------------------------------- shadow elite (Mixamo rig)
def shadow():
    d = A.SRC / 'shadow'; T = lambda n: d / 'textures' / n
    dif = A.save_dds(A.load(T('Image_1_1.jpeg'), 2048), 'shadow/shadow_d.dds')
    nrm = A.save_dds(A.normal_with_spec(T('Image_2_0.jpeg'), T('Image_0_2@channels=G.jpeg'), 2048), 'shadow/shadow_n.dds', alpha=True)
    # violet ember glow in the creature's cracks: brightest bits of the albedo, tinted
    a = np.asarray(A.load(T('Image_1_1.jpeg'), 1024, 'L'), dtype=np.float32)
    g = np.clip((a - 70) * 3, 0, 255)
    glow = A.save_dds(Image.fromarray(np.dstack([g * 0.7, g * 0.3, g]).astype(np.uint8), 'RGB'), 'shadow/shadow_g.dds')
    X = 'mixamorig:'
    maps = [(f'{X}Hips', 'NPC Pelvis [Pelv]'), (f'{X}Spine', 'NPC Spine [Spn0]'), (f'{X}Spine1', 'NPC Spine1 [Spn1]'), (f'{X}Spine2', 'NPC Spine2 [Spn2]'),
            (f'{X}Neck', 'NPC Neck [Neck]'), (f'{X}Head', 'NPC Head [Head]'), (f'{X}HeadTop_End', 'NPC Head [Head]')]
    for side, S in (('Left', 'L'), ('Right', 'R')):
        foot = 'NPC L Foot [LLft ]' if S == 'L' else 'NPC R Foot [Rft ]'
        maps += [(f'{X}{side}Shoulder', f'NPC {S} Clavicle [{S}Clv]'), (f'{X}{side}Arm', f'NPC {S} UpperArm [{S}Uar]'), (f'{X}{side}ForeArm', f'NPC {S} Forearm [{S}Lar]'),
                 (f'{X}{side}Hand', f'NPC {S} Hand [{S}Hnd]'), (f'{X}{side}HandIndex1', f'NPC {S} Finger10 [{S}F10]'), (f'{X}{side}HandIndex2', f'NPC {S} Finger11 [{S}F11]'),
                 (f'{X}{side}HandIndex3', f'NPC {S} Finger12 [{S}F12]'), (f'{X}{side}HandIndex4', f'NPC {S} Finger12 [{S}F12]'),
                 (f'{X}{side}UpLeg', f'NPC {S} Thigh [{S}Thg]'), (f'{X}{side}Leg', f'NPC {S} Calf [{S}Clf]'), (f'{X}{side}Foot', foot),
                 (f'{X}{side}ToeBase', f'NPC {S} Toe0 [{S}Toe]'), (f'{X}{side}Toe_End', f'NPC {S} Toe0 [{S}Toe]')]
    write_job('shadow/shadow.nif', d / 'source/lesnoy.glb', maps,
              [f'mat * diffuse={dif} normal={nrm} glow={glow} emit=0.7,0.3,1.0 emitmult=2.2 spec=0.25 gloss=30'])

if __name__ == '__main__':
    for n, fn in {'zombie': zombie, 'boss': boss, 'shadow': shadow}.items():
        if not ONLY or n in ONLY:
            print('==', n); fn()
