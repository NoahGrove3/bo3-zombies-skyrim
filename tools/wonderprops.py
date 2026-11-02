"""The wonder weapons' own models (Noah's downloads: Gravecaller staff, Frostbinder gauntlet, Wolf Queen's Crown, Horn of
the Dragon Priests, Alduin's Fang) as static NIFs posed for the node each hangs on in the hands (src/Echo.cpp WonderProps):

  gravecaller  WEAPON node (right hand): the staff up its +Y, the grip at the origin (as Skyrim's staffs)
  frostbinder  NPC L Hand [LHnd] as it is, NPC R Hand [RHnd] mirrored: fingers down the bone's +Z, the back of the
               hand +Y, the thumb +X (right) / -X (left) - the bones' frames read from Skyrim's skeleton.nif
  wolfcrown    NPC Head [Head] (third person; in first person a small one floats over the left hand)
  barrowhorn   SHIELD node (left hand): the bell forward (+Y), the curve up (-X), held at its middle
  fang         WEAPON node: the blade up +Y, the grip at the origin (Stage 5's sword)
  bane         Alduin's Bane, a greatsword (Gold Skull Glory Sword): WEAPON node, blade up +Y, grip at the origin; Prn
               WeaponBack, Skyrim's steelgreatsword frame + havok hull (bane.nif), and bane_static.nif for the pedestal
  slayer       Dragon Slayer, a one-handed sword (Ayleid Crystal Saber): the same, Prn WeaponSword (slayer.nif / _static)
  riftblade    Summoner's Riftblade, a dagger (Abyssal Heart): the same, Prn WeaponDagger (riftblade.nif / _static)

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
             dx=False, glow_floor=0.0, size=2048, alpha_floor=0.0, glow_base=0.0, metal=None, ao=None):
    """base / normal / roughness / emissive image paths -> (diffuse, normal+spec, glow) dds paths
    (alpha_floor: the opacity map lifted so its clearest is still this opaque - glass; glow_base: the whole surface lit at
    least this much; metal: a metallic map - bare metal shines more than the rest; ao: ambient occlusion darkens the base)"""
    b = Image.open(base).convert('RGB'); b = b.resize((min(size, b.size[0]),) * 2, Image.LANCZOS); W = b.size
    da = np.asarray(b, np.float32)
    if ao:
        da = da * (0.4 + 0.6 * np.asarray(Image.open(ao).convert('L').resize(W, Image.LANCZOS), np.float32)[..., None] / 255)
    if tint is not None:
        lum = da.mean(2, keepdims=True) / 255
        da = da * (1 - tint_amt) + np.array(tint, np.float32) * (0.3 + 1.2 * lum) * tint_amt
    if opacity:
        a = np.asarray(Image.open(opacity).convert('L').resize(W, Image.LANCZOS), np.float32)
        a = (alpha_floor * 255 + a * (1 - alpha_floor)).clip(0, 255).astype(np.uint8)
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
    if metal:
        sa = sa * (0.55 + 0.45 * np.asarray(Image.open(metal).convert('L').resize(W, Image.LANCZOS), np.float32) / 255)
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
    if glow_base:
        em = np.maximum(em, glow_base)
    G = dds(Image.fromarray((em[..., None] * gc).clip(0, 255).astype(np.uint8)), f'{tag}_g.dds')
    return D, N, G


def write(pid, parts, mats, nif_rel, extra=()):
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
    job += list(extra)
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
    """alpha: 0 opaque, 1 alpha test (cut-outs), 'blend' see-through (glass)"""
    e = ','.join(f'{c:.3f}' for c in emit)
    return f'diffuse={D} normal={N} glow={G} emit={e} emitmult={mult} spec={spec} gloss={gloss} alpha={alpha} doublesided={1 if alpha else 0}'


GS_TEMPLATE = '/home/claude/work/vanilla/steelgreatsword.nif'   # Skyrim's own Meshes\weapons\steel\steelgreatsword.nif
WRAP = '/home/claude/tools/weaponwrap'                            # tools/modelconv/weaponwrap.cpp


def cross2(a, b):
    return a[0] * b[1] - a[1] * b[0]   # (numpy 2's np.cross takes 3-vectors only)


def blade_hull(V, maxpts=10):
    """the weapon's havok hull: its outline seen flat-on (x, y: the blade along +Y, edges +-X) as a convex polygon of at
    most maxpts corners, pushed out through its thickness (z) - the shape Skyrim's own greatswords collide with"""
    from scipy.spatial import ConvexHull
    xy = V[:, :2]
    h = ConvexHull(xy); poly = xy[h.vertices]                       # counter-clockwise
    keep = [0]
    for i in range(1, len(poly)):                                   # (corners crowded together: one is enough)
        if np.linalg.norm(poly[i] - poly[keep[-1]]) > 2.0: keep.append(i)
    if len(keep) > 3 and np.linalg.norm(poly[keep[-1]] - poly[0]) <= 2.0: keep.pop()
    poly = poly[keep]
    while len(poly) > maxpts:                                       # drop the corner whose removal loses least area
        n = len(poly)
        a = [abs(cross2(poly[(i + 1) % n] - poly[i - 1], poly[i] - poly[i - 1])) for i in range(n)]
        poly = np.delete(poly, int(np.argmin(a)), 0)
    # (the dropped corners poke out of the simplified outline: grow it a touch so the hull still holds the model)
    c = poly.mean(0)
    out = 0.0
    for p in xy[h.vertices]:
        for i in range(len(poly)):
            a, b = poly[i], poly[(i + 1) % len(poly)]
            e = b - a; nrm = np.array([e[1], -e[0]]) / (np.linalg.norm(e) + 1e-9)
            out = max(out, (p - a) @ nrm)
    poly = c + (poly - c) * (1 + out / max(1e-3, np.min(np.linalg.norm(poly - c, axis=1))))
    z0, z1 = np.percentile(V[:, 2], 1), np.percentile(V[:, 2], 99)
    z0, z1 = min(z0, -1.5), max(z1, 1.5)                            # never thinner than 3 units (a blade lies on its flat)
    lines = [f'v {x:.3f} {y:.3f} {z:.3f}' for x, y in poly for z in (z0, z1)]
    lines += [f'p 0 0 1 0 0 {z1:.3f}', f'p 0 0 -1 0 0 {z0:.3f}']
    for i in range(len(poly)):
        a, b = poly[i], poly[(i + 1) % len(poly)]
        e = b - a
        lines.append(f'p {e[1]:.5f} {-e[0]:.5f} 0 {a[0]:.3f} {a[1]:.3f} 0')   # outward for a counter-clockwise outline
    return lines


def weapon_frame(nif, V, name, inv=(4712, 6283, 0, 1.0), mass=9.0, prn=''):
    """(Noah: Alduin's Bane floats when dropped, won't sheathe, points at you in the inventory) the model put in Skyrim's
    own greatsword frame (tools/modelconv/weaponwrap): its BSFadeNode root, BSX 194 (havok), Prn WeaponBack and a
    dynamic havok body on the weapon layer (5, mass 9) - here round our blade's own outline - and a BSInvMarker standing
    it upright in the inventory and item preview (4712 6283 0: as Dawnbreaker's and nifly's default; the 1st-person
    static is this same model, and the inventory shows the 1st-person model). prn: another sheath node in place of the
    greatsword's WeaponBack (WeaponSword, WeaponDagger) - no vanilla one-handed sword / dagger NIF with a convex hull
    body is to hand, and their frames are the greatsword's otherwise (BSX 194, layer 5, a dynamic body of mass 9)"""
    hull = WORK / f'{name}_hull.txt'; hull.write_text('\n'.join(blade_hull(V)) + '\n')
    r = subprocess.run([WRAP, GS_TEMPLATE, str(nif), str(nif), name, str(hull), *map(str, inv), str(mass)] + ([prn] if prn else []),
                       capture_output=True, text=True)
    print('  ', r.stdout.strip(), r.stderr.strip())
    if r.returncode: raise SystemExit(f'weaponwrap failed for {name}')


NW = pathlib.Path('/home/claude/work/newweapons')   # Noah's 2026-11 sword downloads, unpacked (see newweapon())
UPLOADS = pathlib.Path('/mnt/user-data/uploads')
NEWWEAPONS = {   # tag: (download zip, the model inside it, OBJ parts objexp writes)
    'A': ('skyrim-ayleid-crystal-saber_1.zip', 'source/model.zip', ['Blade_Cylinder.001', 'Handle_Cylinder']),
    'B': ('abyssal-heart-runic-demon-sword-free.zip', 'source/resault.rar', ['Sphere']),
    'C': ('free-gold-skull-glory-sword-game-ready.zip', 'source/resault.rar', ['GlorySwordLP']),
}


def newweapon(tag):
    """-> the download's OBJ parts (load_obj tuples, the source's own axes and units) and its textures folder; unpacks the
    zip (textures + the model only - the archives' duplicate texture sets are left out) and runs objexp the first time"""
    zipname, inner, objs = NEWWEAPONS[tag]
    d = NW / tag; od = NW / f'obj{tag}'
    if not all((od / f'{o}.obj').exists() for o in objs):
        d.mkdir(parents=True, exist_ok=True); od.mkdir(parents=True, exist_ok=True)
        if not (d / 'textures').exists():
            subprocess.run(['unzip', '-q', '-o', str(UPLOADS / zipname), 'textures/*', '-d', str(d)], check=True)
        subprocess.run(['unzip', '-q', '-o', str(UPLOADS / zipname), inner, '-d', str(d)], check=True)
        arc = d / inner
        if inner.endswith('.zip'):   # (A: a zip in the zip - only its .dae; the textures beside it are the outer ones)
            subprocess.run(['unzip', '-q', '-o', str(arc), 'model/model.dae', '-d', str(arc.parent)], check=True)
            model = arc.parent / 'model/model.dae'
        else:                        # (B, C: a rar - libarchive's bsdtar reads it; only the .fbx)
            names = subprocess.run([BSDTAR, '-tf', str(arc)], capture_output=True, text=True, check=True).stdout.split('\n')
            fbx = next(n for n in names if n.lower().endswith('.fbx'))
            subprocess.run([BSDTAR, '-xf', str(arc), '-C', str(arc.parent), fbx], check=True)
            model = arc.parent / fbx
        arc.unlink()
        subprocess.run([OBJEXP, str(model), str(od)], check=True, capture_output=True)
    return [load_obj(od / f'{o}.obj') for o in objs], d / 'textures'


BSDTAR = '/home/claude/tools/libarchive/build/bin/bsdtar'
OBJEXP = '/home/claude/tools/objexp'                            # tools/modelconv/objexp.cpp


def hue_mask(path, size, fn):
    """an emissive image made from a base colour: fn(r, g, b) (0..255 floats) -> 0..1, saved as a png in WORK"""
    a = np.asarray(Image.open(path).convert('RGB').resize((size, size), Image.LANCZOS), np.float32)
    m = np.clip(fn(a[..., 0], a[..., 1], a[..., 2]), 0, 1)
    WORK.mkdir(parents=True, exist_ok=True)
    out = WORK / (pathlib.Path(path).stem + '_emask.png'); Image.fromarray((m * 255).astype(np.uint8)).save(out)
    return out


def posed(parts, c, s, flip):
    """parts moved so c is the origin, scaled by s; flip: half a turn round Z ((x, y) -> (-x, -y) - a rotation, the faces
    keep their winding) for a model whose hilt is up its +Y"""
    P = []
    for V0, T, F, M in parts:
        Q = (V0 - c) * s
        if flip: Q = np.stack([-Q[:, 0], -Q[:, 1], Q[:, 2]], 1)
        P.append((Q, T, F, M))
    return P


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
        tx = DL / 'Horn of the Dragon Priests/textures'
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
        # (Noah: the sword should be on fire) tongues of flame up the whole blade: the fire flipbook (tools/bowarrows.py,
        # Emberstring's) on upright billboards from the guard to the point, each its own phase, a white-hot core inside
        nif = OUT / 'Meshes/CurseOfAlduin/wonder/fang.nif'
        fx = 'CurseOfAlduin\\bow\\emberstring_arrowfx.dds'
        job = [f'base {nif}', f'out {nif}']
        V = np.vstack([p[0] for p in P]); top = V[:, 1].max()
        k = 0
        for y in np.linspace(14.0, top - 4.0, 9):
            for col, sc, w, h, z in (('1,0.5,0.16', 1.6, 6.5, 13.0, 0.0), ('1,0.9,0.7', 1.2, 3.6, 8.0, 0.4)):
                job.append(f'shape fangfire{k} tex={fx} color={col},1 scale={sc} blend=add billboard=2 flip=8x8 fps=24 phase={(k * 0.37) % 2.6:.2f} at=0,{y:.1f},0')
                for X_, Y_, u, v in ((-w, h, 0, 0), (w, h, 1 / 8, 0), (w, -h * 0.4, 1 / 8, 1 / 8), (-w, -h * 0.4, 0, 1 / 8)):
                    job.append(f'v {X_:.2f} {Y_:.2f} {z:.2f} {u} {v} 1 1 1 1')
                job += ['t 0 1 2', 't 0 2 3']
                k += 1
        jf = WORK / 'fang_fire.job'; jf.write_text('\n'.join(job) + '\n')
        r = subprocess.run(['/home/claude/tools/fxwrite', str(jf)], capture_output=True, text=True)
        print('fang fire |', r.stdout.strip() or r.stderr.strip())

    elif pid == 'bane':         # Alduin's Bane (the story's last weapon): a greatsword - (Noah, 2026-11) the Gold Skull Glory
        # Sword download (NW/C): its hilt up source +Y (pommel 219..274, grip 150..219, guard 96..150, the point at -274),
        # turned half round Z so the blade runs up +Y, the grip's middle (y 185) at the origin, 150 long
        parts, tx = newweapon('C')
        V = np.vstack([p[0] for p in parts]); s = 150.0 / np.ptp(V[:, 1])
        # (Noah: the skull on the hilt faced away from me) the skull bulges out of the guard's source +Z face; the half
        # turn round Z keeps it there: on the +Z face, toward you in the hand
        P = posed(parts, np.array([0.0, 185.0, np.median(V[:, 2])]), s, True)
        D, N, G = textures('bane', tx / 'DefaultMaterial_Base_color.png', tx / 'DefaultMaterial_Normal_OpenGL.png', tx / 'DefaultMaterial_Roughness.png',
                           None, glow=(1.0, 0.78, 0.35), glow_floor=0.3, metal=tx / 'DefaultMaterial_Metallic.png', ao=tx / 'DefaultMaterial_Mixed_AO.png')
        # (Noah: not in my hands when swinging) a weapon's model must say where it hangs (Prn): without it Skyrim leaves
        # it at the body's root, by the feet - so the greatsword's own: WeaponBack, and drawn, the hand
        write(pid, P, {'m0': matopts(D, N, G, (1.0, 0.78, 0.35), 0.9, 1.6, 110)}, 'CurseOfAlduin/wonder/bane.nif', ['prn WeaponBack'])
        # the hall's display (a STAT on its pedestal) gets a copy without the havok body, so it can never fall off its stand
        import shutil
        shutil.copyfile(OUT / 'Meshes/CurseOfAlduin/wonder/bane.nif', OUT / 'Meshes/CurseOfAlduin/wonder/bane_static.nif')
        weapon_frame(OUT / 'Meshes/CurseOfAlduin/wonder/bane.nif', np.vstack([p[0] for p in P]), 'AlduinsBane')

    elif pid == 'slayer':       # Dragon Slayer: a one-handed sword - the Ayleid Crystal Saber download (NW/A): a slim round
        # rod already up source +Y (0.02 long: the blade -0.0043..0.01, guard collar -0.005..-0.003, grip -0.009..-0.005,
        # pommel below), the grip's middle (y -0.007) at the origin, 95 long (Skyrim's one-handed swords: 90-100)
        parts, tx = newweapon('A')    # (Blade_Cylinder.001: material m0 'Blade'; Handle_Cylinder: m1 'Handle')
        V = np.vstack([p[0] for p in parts]); s = 95.0 / np.ptp(V[:, 1])
        P = posed(parts, np.array([0.0, -0.007, 0.0]), s, False)
        ice = (0.72, 0.9, 1.0)
        # the crystal: its opacity map is clear through the middle and thick at the facets - lifted so the clearest is
        # still a third opaque (glass, not nothing), blended; lit faintly all over in blue-white, brightest on its facets
        D, N, G = textures('slayer_blade', tx / 'Blade_albedo.jpeg', tx / 'Blade_normal.png', tx / 'Blade_roughness.jpeg', None,
                           glow=ice, opacity=tx / 'Blade_opacity.jpeg', alpha_floor=0.35, glow_base=0.22, glow_floor=1.0,
                           metal=tx / 'Blade_metallic.jpeg', size=1024)
        # the hilt: dark Ayleid metal; its little cyan crystals glow too
        gem = hue_mask(tx / 'Handle_albedo.jpeg', 2048, lambda r, g, b: (np.minimum(g, b) - r - 35) / 70)
        D2, N2, G2 = textures('slayer_hilt', tx / 'Handle_albedo.jpeg', tx / 'Handle_normal.png', tx / 'Handle_roughness.jpeg', gem,
                              glow=ice, metal=tx / 'Handle_metallic.jpeg', ao=tx / 'Handle_AO.jpeg')
        mats = {'m0': matopts(D, N, G, ice, 1.3, 2.0, 140, alpha='blend'), 'm1': matopts(D2, N2, G2, ice, 1.4, 1.2, 80)}
        write(pid, P, mats, 'CurseOfAlduin/wonder/slayer.nif', ['prn WeaponSword'])
        import shutil
        shutil.copyfile(OUT / 'Meshes/CurseOfAlduin/wonder/slayer.nif', OUT / 'Meshes/CurseOfAlduin/wonder/slayer_static.nif')
        weapon_frame(OUT / 'Meshes/CurseOfAlduin/wonder/slayer.nif', np.vstack([p[0] for p in P]), 'DragonSlayer', prn='WeaponSword')

    elif pid == 'riftblade':    # Summoner's Riftblade: a dagger - the Abyssal Heart download (NW/B): hilt up source +Y (pommel
        # ring 195..225, grip 135..195, guard with its eye 76..135, the point at -220), turned half round Z, the grip's
        # middle (y 163) at the origin, 60 long (a short blade in the dagger animations)
        parts, tx = newweapon('B')    # m0 Material.003 (the eye: emissive), m1 'Eye' (the pommel ring: DefaultMaterial's
        V = np.vstack([p[0] for p in parts]); s = 60.0 / np.ptp(V[:, 1])   # set), m2 Material.002 (blade, guard), m3 Material.001 (grip)
        P = posed(parts, np.array([0.0, 163.0, np.median(V[:, 2])]), s, True)
        red = (1.0, 0.22, 0.12)
        mats = {}
        for m, t, emi in (('m0', 'Material.003', True), ('m1', 'DefaultMaterial', False), ('m2', 'Material.002', False), ('m3', 'Material.001', False)):
            D, N, G = textures(f'riftblade_{t.lower().replace(".", "")}', tx / f'{t}_Base_color.png', tx / f'{t}_Normal_OpenGL.png', tx / f'{t}_Roughness.png',
                               tx / f'{t}_Emissive.png' if emi else None, glow=red, metal=tx / f'{t}_Metallic.png', ao=tx / f'{t}_Mixed_AO.png',
                               size=2048 if t == 'Material.002' else 1024)
            mats[m] = matopts(D, N, G, red, 2.2 if emi else 1.0, 1.4, 90)
        write(pid, P, mats, 'CurseOfAlduin/wonder/riftblade.nif', ['prn WeaponDagger'])
        import shutil
        shutil.copyfile(OUT / 'Meshes/CurseOfAlduin/wonder/riftblade.nif', OUT / 'Meshes/CurseOfAlduin/wonder/riftblade_static.nif')
        weapon_frame(OUT / 'Meshes/CurseOfAlduin/wonder/riftblade.nif', np.vstack([p[0] for p in P]), 'SummonersRiftblade', prn='WeaponDagger')


if __name__ == '__main__':
    for p in (sys.argv[1:] or ['gravecaller', 'frostbinder', 'wolfcrown', 'barrowhorn', 'fang']):
        build(p)
