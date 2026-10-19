"""The wonder weapons' own models (Noah's downloads: Gravecaller staff, Frostbinder gauntlet, Wolf Queen's Crown, Horn of
the Barrow-Jarls, Alduin's Fang) as static NIFs posed for the node each hangs on in the hands (src/Echo.cpp WonderProps):

  gravecaller  WEAPON node (right hand): the staff up its +Y, the grip at the origin (as Skyrim's staffs)
  frostbinder  NPC L Hand [LHnd] as it is, NPC R Hand [RHnd] mirrored: fingers down the bone's +Z, the back of the
               hand +Y, the thumb +X (right) / -X (left) - the bones' frames read from Skyrim's skeleton.nif
  wolfcrown    NPC Head [Head] (third person; in first person a small one floats over the left hand)
  barrowhorn   SHIELD node (left hand): the bell forward (+Y), the curve up (-X), held at its middle
  fang         WEAPON node: the blade up +Y, the grip at the origin (Stage 5's sword)

Textures: each model's own (base colour, normal + specular from roughness, glow from its emissive), recoloured to the
wonder's colour where the source says otherwise.     python tools/wonderprops.py [id ...]"""
import pathlib, subprocess, sys
import numpy as np
from PIL import Image, ImageFilter

ROOT = pathlib.Path(__file__).parent.parent
SRC = pathlib.Path('/home/claude/models_in/wonders')
DL = pathlib.Path('/home/claude/work/dlw')
OUT = ROOT / 'assets' / 'fx'
WORK = ROOT / 'build' / 'wonderprops'
CONV = '/home/claude/tools/modelconv'


def load_obj(path):
    V, T, F, M = [], [], [], []
    mat = 'm0'
    for l in open(path):
        if l.startswith('v '): V.append([float(x) for x in l.split()[1:4]])
        elif l.startswith('vt '): T.append([float(x) for x in l.split()[1:3]])
        elif l.startswith('usemtl'): mat = l.split()[1]
        elif l.startswith('f '):
            ids = [int(x.split('/')[0]) - 1 for x in l.split()[1:]]
            for i in range(1, len(ids) - 1): F.append((ids[0], ids[i], ids[i + 1])); M.append(mat)
    return np.array(V, float), np.array(T, float), np.array(F, int), M


def dds(img, rel, alpha=False, size=2048):
    p = OUT / 'Textures' / 'CurseOfAlduin' / 'wonder' / rel; p.parent.mkdir(parents=True, exist_ok=True)
    if max(img.size) > size: img = img.resize((size, size), Image.LANCZOS)
    tmp = WORK / (rel.replace('/', '_') + '.png'); img.convert('RGBA' if alpha else 'RGB').save(tmp)
    subprocess.run(['convert', str(tmp), '-define', f'dds:compression={"dxt5" if alpha else "dxt1"}', '-define', 'dds:mipmaps=12', str(p)], check=True)
    return f'CurseOfAlduin\\wonder\\{rel}'.replace('/', '\\')


def textures(tag, base, normal=None, rough=None, emissive=None, glow=(1, 1, 1), tint=None, tint_amt=0.0, opacity=None,
             dx=False, glow_floor=0.0, size=2048):
    """base / normal / roughness / emissive image paths -> (diffuse, normal+spec, glow) dds paths"""
    b = Image.open(base).convert('RGB'); b = b.resize((min(size, b.size[0]),) * 2, Image.LANCZOS); W = b.size
    da = np.asarray(b, np.float32)
    if tint is not None:
        lum = da.mean(2, keepdims=True) / 255
        da = da * (1 - tint_amt) + np.array(tint, np.float32) * (0.3 + 1.2 * lum) * tint_amt
    if opacity:
        a = np.asarray(Image.open(opacity).convert('L').resize(W, Image.LANCZOS), np.uint8)
        D = dds(Image.fromarray(np.dstack([da.clip(0, 255).astype(np.uint8), a]), 'RGBA'), f'{tag}_d.dds', True)
    else:
        D = dds(Image.fromarray(da.clip(0, 255).astype(np.uint8)), f'{tag}_d.dds')
    if normal:
        na = np.asarray(Image.open(normal).convert('RGB').resize(W, Image.LANCZOS), np.uint8).copy()
        if dx: na[..., 1] = 255 - na[..., 1]
    else:
        na = np.zeros((W[1], W[0], 3), np.uint8); na[...] = (128, 128, 255)
    if rough:
        r = Image.open(rough)
        ra = np.asarray(r.convert('RGB').resize(W, Image.LANCZOS), np.float32)[..., 1] / 255 if r.mode in ('RGB', 'RGBA') else np.asarray(r.convert('L').resize(W, Image.LANCZOS), np.float32) / 255
        sa = (1 - ra) * 220 + 20
    else:
        sa = np.full(na.shape[:2], 120.0)
    N = dds(Image.fromarray(np.dstack([na, sa.clip(0, 255).astype(np.uint8)]), 'RGBA'), f'{tag}_n.dds', True)
    gc = np.array(glow, np.float32) * 255
    if emissive:
        em = np.asarray(Image.open(emissive).convert('L').resize(W, Image.LANCZOS), np.float32) / 255
        em = np.clip(em * 2.2, 0, 1)
        em = np.maximum(em, np.asarray(Image.fromarray((em * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(8)), np.float32) / 255 * 0.7)
    else:
        em = np.zeros(na.shape[:2], np.float32)
    if glow_floor:   # the brightest of the base colour lit faintly in the wonder's colour too (its inlays)
        lum = da.mean(2) / 255
        em = np.maximum(em, np.clip((lum - 0.55) / 0.35, 0, 1) ** 2 * glow_floor)
    G = dds(Image.fromarray((em[..., None] * gc).clip(0, 255).astype(np.uint8)), f'{tag}_g.dds')
    return D, N, G


def write(pid, parts, mats, nif_rel):
    """parts: list of (V, T, F, M) already in the prop's node space; mats: name -> modelconv mat line options"""
    WORK.mkdir(parents=True, exist_ok=True)
    obj = WORK / f'{pid}.obj'
    allV = np.vstack([p[0] for p in parts])
    lo, hi = allV.min(0), allV.max(0); centre = (lo + hi) / 2
    with open(obj, 'w') as f:
        base = 0
        for V, T, F, M in parts:
            f.writelines(f'v {x:.5f} {y:.5f} {z:.5f}\n' for x, y, z in V)
            f.writelines(f'vt {u:.5f} {v:.5f}\n' for u, v in (T if len(T) else np.zeros((len(V), 2))))
            cur = None
            for (a, b, c), m in zip(F, M):
                if m != cur: f.write(f'usemtl {m}\n'); cur = m
                f.write(f'f {a+1+base}/{a+1+base} {b+1+base}/{b+1+base} {c+1+base}/{c+1+base}\n')
            base += len(V)
    out = OUT / 'Meshes' / nif_rel; out.parent.mkdir(parents=True, exist_ok=True)
    job = [f'in {obj}', f'out {out}', 'scale 1', 'ground 0', f'offset {centre[0]:.4f} {centre[1]:.4f} {centre[2]:.4f}']
    for m, opts in mats.items():
        job.append(f'mat {m} ' + opts)
    jf = WORK / f'{pid}.job'; jf.write_text('\n'.join(job) + '\n')
    r = subprocess.run([CONV, str(jf)], capture_output=True, text=True)
    print(pid, (r.stdout.strip().split('\n') or [''])[-1], r.stderr.strip()[-300:])
    if r.returncode: raise SystemExit(f'modelconv failed for {pid}')
    print(f'   bounds {np.round(lo, 1)}..{np.round(hi, 1)} -> {out.relative_to(OUT)}')


def gather(d, skip=()):
    parts = []
    for p in sorted((SRC / d).glob('*.obj')):
        if p.stem in skip: continue
        if sum(1 for l in open(p) if l.startswith('v ')) > 60000:   # (a NIF shape holds at most 65535 corners)
            out = WORK / f'{d}_{p.stem}_s.obj'
            if not out.exists():
                subprocess.run(['/home/claude/tools/simplify', str(p), str(out), '70000'], check=True, capture_output=True)
            p = out
        parts.append(load_obj(p))
    return parts


def matopts(D, N, G, emit, mult, spec=1.0, gloss=60, alpha=0):
    e = ','.join(f'{c:.3f}' for c in emit)
    return f'diffuse={D} normal={N} glow={G} emit={e} emitmult={mult} spec={spec} gloss={gloss} alpha={alpha} doublesided={1 if alpha else 0}'


def build(pid):
    if pid == 'gravecaller':   # the staff: long axis (source y) up +Y, trident up; 120 long, grip 45 % up from the butt
        parts = gather('gravecaller')
        V = np.vstack([p[0] for p in parts]); L = np.ptp(V[:, 1]); s = 128.0 / L
        lo = V[:, 1].min()
        P = [((p[0] - [V[:, 0].mean(), lo + 0.42 * L, V[:, 2].mean()]) * s, p[1], p[2], p[3]) for p in parts]
        D, N, G = textures('gravecaller', SRC / 'gravecaller/tex0.png', SRC / 'gravecaller/tex2.png', SRC / 'gravecaller/tex1.png',
                           SRC / 'gravecaller/tex3.png', glow=(0.49, 1.0, 0.69), tint=(30, 34, 32), tint_amt=0.55, glow_floor=0.35)
        write(pid, P, {'m0': matopts(D, N, G, (0.49, 1.0, 0.69), 2.5, 1.2, 80)}, 'CurseOfAlduin/wonder/gravecaller.nif')
    elif pid == 'frostbinder':  # the gauntlet: a left hand (fingers -z, back of the hand +y, thumb +x)
        parts = gather('frostbinder')
        V = parts[0][0]
        L = np.ptp(V[:, 2]); s = 18.0 / L
        wrist_z = V[:, 2].min() + 0.57 * L
        ring = V[np.abs(V[:, 2] - wrist_z) < 0.04 * L]
        c = np.array([ring[:, 0].mean(), ring[:, 1].mean(), wrist_z])
        tx = DL / 'Frostbinder Gauntlets/textures'
        D, N, G = textures('frostbinder', tx / 'Gauntlet_low_standardSurface1_BaseColor.png', tx / 'Gauntlet_low_standardSurface1_Normal.png',
                           tx / 'Gauntlet_low_standardSurface1_Roughness.png', tx / 'Gauntlet_low_standardSurface1_Emissive.png',
                           glow=(0.56, 0.9, 1.0), tint=(150, 205, 235), tint_amt=0.35, glow_floor=0.2)
        D2, N2, G2 = textures('frostbinder_glass', tx / 'Gauntlet_low_Glass_BaseColor.png', tx / 'Gauntlet_low_Glass_Normal.png',
                              tx / 'Gauntlet_low_Glass_Roughness.png', None, glow=(0.56, 0.9, 1.0), glow_floor=1.0, size=512)
        mats = {'m0': matopts(D, N, G, (0.56, 0.9, 1.0), 2.0, 1.4, 90), 'm1': matopts(D2, N2, G2, (0.56, 0.9, 1.0), 3.0, 2.0, 120)}
        for side, sx in (('l', -1.0), ('r', 1.0)):
            P = []
            for V0, T, F, M in parts:
                Q = (V0 - c) * s
                Q = np.stack([sx * Q[:, 0], Q[:, 1], -Q[:, 2]], 1)   # left: (-x, y, -z); right: mirrored (x, y, -z)
                P.append((Q, T, F[:, ::-1] if sx > 0 else F, M))   # (the mirror turns the faces inside out: wound back)
            write(f'{pid}_{side}', P, mats, f'CurseOfAlduin/wonder/frostbinder_{side}.nif')
    elif pid == 'wolfcrown':    # the helm on the head: source y up -> +Z, source z forward -> +Y, x -> -X
        parts = gather('wolfcrown')
        V = np.vstack([p[0] for p in parts]); H = np.ptp(V[:, 1]); s = 26.0 / H
        c = np.array([0.0, V[:, 1].min() + 0.32 * H, np.median(V[:, 2])])
        tx = DL / 'Wolf Queen’s Crown/textures'
        D, N, G = textures('wolfcrown', tx / 'Metal006_1K_Color.jpg', tx / 'Metal006_1K_NormalDX.jpg', tx / 'Metal006_1K_Roughness.jpg', None,
                           glow=(0.75, 0.55, 1.0), tint=(70, 52, 96), tint_amt=0.6, dx=True, glow_floor=0.5, size=1024)
        Dg = dds(Image.new('RGB', (64, 64), (190, 140, 255)), 'wolfcrown_emi_d.dds'); Gg = dds(Image.new('RGB', (64, 64), (200, 150, 255)), 'wolfcrown_emi_g.dds')
        mats = {'m0': matopts(D, N, G, (0.75, 0.55, 1.0), 2.0, 1.5, 100), 'm1': matopts(Dg, N, Gg, (0.75, 0.55, 1.0), 4.0, 1.0, 60)}
        P = []
        for V0, T, F, M in parts:
            Q = (V0 - c) * s
            P.append((np.stack([-Q[:, 0], Q[:, 2], Q[:, 1]], 1), T, F, M))
        write(pid, P, mats, 'CurseOfAlduin/wonder/wolfcrown.nif')
    elif pid == 'barrowhorn':   # the horn: source x (mouthpiece -x, bell +x) -> +Y, source y (its curve) -> -X
        parts = gather('barrowhorn')
        V = np.vstack([p[0] for p in parts]); L = np.ptp(V[:, 0]); s = 46.0 / L
        c = np.array([V[:, 0].min() + 0.45 * L, np.median(V[:, 1]), V[:, 2].mean()])
        tx = DL / 'Horn of the Barrow-Jarls/textures'
        D, N, G = textures('barrowhorn', tx / 'LP_DefaultMaterial_BaseColor.png', tx / 'LP_DefaultMaterial_Normal.png', tx / 'LP_DefaultMaterial_Roughness.png',
                           None, glow=(1.0, 0.75, 0.38), glow_floor=0.4)
        P = []
        for V0, T, F, M in parts:
            Q = (V0 - c) * s
            P.append((np.stack([-Q[:, 1], Q[:, 0], Q[:, 2]], 1), T, F, M))
        write(pid, P, {'m0': matopts(D, N, G, (1.0, 0.75, 0.38), 1.6, 1.0, 70)}, 'CurseOfAlduin/wonder/barrowhorn.nif')
    elif pid == 'fang':         # Alduin's Fang: blade along source -y -> +Y; grip (source y 0..~60) at the origin
        parts = gather('fang')
        V = np.vstack([p[0] for p in parts]); L = np.ptp(V[:, 1]); s = 118.0 / L
        c = np.array([0.0, 30.0, np.median(V[:, 2])])
        tx = DL / 'fang/textures'
        D, N, G = textures('fang', tx / 'DefaultMaterial_Base_color.png', tx / 'DefaultMaterial_Normal_OpenGL.png', tx / 'DefaultMaterial_Roughness.png',
                           None, glow=(1.0, 0.45, 0.15), opacity=tx / 'DefaultMaterial_Opacity.png', glow_floor=0.25)
        P = []
        for V0, T, F, M in parts:
            Q = (V0 - c) * s
            P.append((np.stack([Q[:, 0], -Q[:, 1], -Q[:, 2]], 1), T, F, M))
        write(pid, P, {'m0': matopts(D, N, G, (1.0, 0.45, 0.15), 1.5, 1.4, 90, alpha=1)}, 'CurseOfAlduin/wonder/fang.nif')


if __name__ == '__main__':
    for p in (sys.argv[1:] or ['gravecaller', 'frostbinder', 'wolfcrown', 'barrowhorn', 'fang']):
        build(p)
