"""(Noah, 2026-11-21: the class sets "hyper realistic" on Skyrim's own armour, no helmets but hoods - Summoner the Necromancer
robes, Destruction the Arch-Mage's, Alchemist the Morag Tong (the chitin light meshes in their MT textures), Spellsword
Ahzidal's) every piece of the four sets refitted by tools/armourfit.py to the player's bodies (3BA, HIMBO), both weights,
each with the body's sliders in a .tri of its own; then (Noah: "the appearance of the armor sets needs to change each upgraded
tier" - "Worn to legendary", the hood too) each piece at its four tiers:
  - Base: worn - faded toward grey, grimed, the trims dulled;
  - Tier I: the cloth in the class's colour;
  - Tier II: that, and the trims re-stitched bright in the class's metal;
  - Tier III: all that, and the trims glowing (a real glow map, the glow shader on).

    python3 tools/armoursets.py [class ...]   ->  assets/fx/Meshes/CurseOfAlduin/armour/<class>/<piece>_<f|m>_t<0-3>_<0|1>.nif,
                                                 <piece>_<f|m>.tri, assets/fx/Textures/CurseOfAlduin/armour/<class>/*.dds"""
import pathlib, subprocess, sys
import numpy as np
from PIL import Image
from scipy.ndimage import gaussian_filter
from concurrent.futures import ProcessPoolExecutor
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import armourfit as A

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT = ROOT / 'assets' / 'fx' / 'Meshes' / 'CurseOfAlduin' / 'armour'
BASE = ROOT / 'build' / 'armourfit' / 'sets'          # (the refitted pieces before their tier looks)
TEXOUT = ROOT / 'assets' / 'fx' / 'Textures' / 'CurseOfAlduin' / 'armour'
# the class's colours: the cloth's tint, the trims' metal, the glow (Summoner violet, Destruction ember, Alchemist green,
# Spellsword blue)
PAL = {
    'summoner':    ((0.42, 0.17, 0.66), (1.30, 1.15, 1.60), (0.68, 0.38, 1.00)),
    'destruction': ((0.62, 0.14, 0.06), (1.70, 1.25, 0.55), (1.00, 0.45, 0.12)),
    'alchemist':   ((0.17, 0.48, 0.15), (1.55, 1.35, 0.62), (0.42, 1.00, 0.35)),
    'spellsword':  ((0.12, 0.25, 0.64), (1.35, 1.42, 1.60), (0.38, 0.68, 1.00)),
}
GAMEDIR = 'CurseOfAlduin\\armour\\'
MT = 'textures\\dlc02\\armor\\chitinlight\\chitin_armor_light_{}mt.dds'

# class -> piece -> (role, female nif, male nif, {sex: {shape: {slot: texture}}})
SETS = {
    'summoner': {
        'robes': ('body', 'clothes/necromancer/necromancerrobes_f_1.nif', 'clothes/necromancer/necromancerrobes_1.nif', {}),
        'hood': ('piece', 'clothes/necromancer/necromancerhood_f_1.nif', 'clothes/necromancer/necromancerhood_1.nif', {}),
        'boots': ('piece', 'clothes/necromancer/necromancerboots_f_1.nif', 'clothes/necromancer/necromancerboots_1.nif', {}),
    },
    'destruction': {
        'robes': ('body', 'clothes/archmage/f/archmagerobesf_1.nif', 'clothes/archmage/m/archmagerobesm_1.nif', {}),
        'hood': ('piece', 'clothes/archmage/f/archmagehoodf_1.nif', 'clothes/archmage/m/archmagehoodm_1.nif', {}),
        'boots': ('piece', 'clothes/archmage/f/archmagebootsf_1.nif', 'clothes/archmage/m/archmagebootsm_1.nif', {}),
    },
    'alchemist': {
        'cuirass': ('body', 'dlc02/armor/chitinlight/f/cuirass_1.nif', 'dlc02/armor/chitinlight/m/cuirass_1.nif',
                    {'f': {'Cuirass_1': {0: MT.format('female_body')}}, 'm': {'ChitinCuirass_1': {0: MT.format('male_body')}}}),
        'gauntlets': ('piece', 'dlc02/armor/chitinlight/f/gauntlets_1.nif', 'dlc02/armor/chitinlight/m/gauntlets_1.nif',
                      {'f': {'Gauntlets_1': {0: MT.format('male_gloves')}}, 'm': {'ChitinGloves_1': {0: MT.format('male_gloves')}}}),
        'boots': ('piece', 'dlc02/armor/chitinlight/f/boots_1.nif', 'dlc02/armor/chitinlight/m/boots_1.nif',
                  {'f': {'Boots_1': {0: MT.format('male_boots')}}, 'm': {'ChitinBoots_1': {0: MT.format('male_boots')}}}),
    },
    'spellsword': {
        'cuirass': ('body', 'armor/draugr/draugrarmorfemale_1.nif', 'armor/draugr/draugrarmormale_1.nif', {}),
        'gauntlets': ('piece', 'armor/draugr/draugrgauntletsfemale_1.nif', 'armor/draugr/draugrgauntletsmale_1.nif', {}),
        'boots': ('piece', 'armor/draugr/draugrbootsfemale_1.nif', 'armor/draugr/draugrbootsmale_1.nif', {}),
    },
}


def jobs(classes):
    for cls in classes:
        for piece, (role, fn, mn, tex) in SETS[cls].items():
            for sx, sex, nif in (('f', 'female', fn), ('m', 'male', mn)):
                for wt in ('1', '0'):
                    src = A.VAN / 'meshes' / nif.replace('_1.nif', f'_{wt}.nif')
                    out = BASE / cls / f'{piece}_{sx}_{wt}.nif'
                    tri = (GAMEDIR + f'{cls}\\{piece}_{sx}.tri', OUT / cls / f'{piece}_{sx}.tri')
                    yield (str(src), sex, str(out), role, tri, tex.get(sx, {}))


def one(j):
    src, sex, out, role, tri, tex = j
    A.fit(src, sex, out, role=role, tri=tri, textures=tex)
    return out


def noise(h, w, s, seed):
    r = np.random.default_rng(seed)
    n = gaussian_filter(r.random((max(4, h // 4), max(4, w // 4))), s / 4); n = (n - n.min()) / (n.max() - n.min() + 1e-9)
    return np.asarray(Image.fromarray((n * 255).astype(np.uint8)).resize((w, h), Image.BILINEAR), np.float32) / 255


MODE = {'summoner': 'bright', 'destruction': 'bright', 'alchemist': 'engrave', 'spellsword': 'engrave'}


def trims(rgb, nrm, mode):
    """where a texture's trims are: the light embroidery (the Arch-Mage's), the stitching, rope and emblems that stand out of the
    cloth around them (in the colour map, and as the raised seams of the normal map); on armour, the engraved grooves, rivets
    and plate seams - dark lines on the metal and chitin plates. Also the plates themselves (their tint kept lighter)."""
    h, w = rgb.shape[:2]
    lum = rgb @ np.array([0.3, 0.59, 0.11]); sat = rgb.max(-1) - rgb.min(-1)
    old = np.clip((lum - 0.42) / 0.15, 0, 1) * np.clip(1 - sat * 2, 0, 1)
    # (fine detail only: a stitch or a rope stands out of the cloth a few texels round it; a fold's broad highlight does not)
    loc = lum - gaussian_filter(lum, w / 384)
    q1, q2 = np.percentile(loc, [95, 99.7])
    hi = np.clip((loc - q1) / (q2 - q1 + 1e-6), 0, 1)
    tilt = 1 - np.clip(nrm[..., 2] * 2 - 1, 0, 1) if nrm is not None else np.zeros_like(lum)
    tilt = np.clip(gaussian_filter(tilt, 0.6) - gaussian_filter(tilt, w / 256), 0, None)
    t1, t2 = np.percentile(tilt, [95, 99.7])
    edge = np.clip((tilt - t1) / (t2 - t1 + 1e-6), 0, 1)
    m = np.maximum(old, gaussian_filter(np.clip(0.75 * hi + 0.75 * edge, 0, 1), 0.7))
    plate = np.zeros_like(lum)
    if mode == 'engrave':
        # (the plates only, not the leather or fur beside them: the greyest parts - as grey as needed to find a fifth of the
        # texture's plate, so a texture that is mostly leather still has its metal found)
        for st in (0.05, 0.08, 0.11, 0.14):
            plate = gaussian_filter(np.clip(1 - (sat - st) / 0.05, 0, 1) * np.clip((lum - 0.1) / 0.08, 0, 1), 4)
            on = plate > 0.75
            if on.mean() > 0.2: break
        dark = gaussian_filter(lum, w / 256) - lum
        d1, d2 = np.percentile(dark[on], [88, 99]) if on.sum() > 100 else (1.0, 2.0)
        eng = np.clip((dark - d1) / (d2 - d1 + 1e-6), 0, 1) * on
        m = np.maximum(gaussian_filter(eng, 0.6), m * 0.2)
    return np.clip(m, 0, 1), np.clip(plate, 0, 1)


def tier_tex(src, nrm_src, cls, t):
    """one diffuse map at a tier (alpha kept), and at Tier III its glow map"""
    im = Image.open(src); im.load(); im = im.convert('RGBA')
    if max(im.size) > 2048: im = im.resize((2048, 2048 * im.size[1] // im.size[0]), Image.LANCZOS)
    a = np.asarray(im, np.float32) / 255
    rgb, al = a[..., :3], a[..., 3:]
    h, w = rgb.shape[:2]
    nrm = None
    if nrm_src and nrm_src.exists():
        n = Image.open(nrm_src); n.load(); nrm = np.asarray(n.convert('RGB').resize((w, h), Image.BILINEAR), np.float32) / 255
    cloth_c, trim_c, glow_c = (np.array(x) for x in PAL[cls])
    lum = rgb @ np.array([0.3, 0.59, 0.11])
    trim, plate = trims(rgb, nrm, MODE[cls])
    cloth = (1 - trim) * (1 - 0.55 * plate)     # (the plates take the colour as a patina, the cloth and leather fully)
    glow = None
    if t == 0:    # worn: faded, grimy, the trims dulled
        out = rgb * 0.55 + lum[..., None] * 0.45
        dirt = noise(h, w, 40, 1)[..., None]; fine = noise(h, w, 6, 2)[..., None]
        out = out * (0.62 + 0.3 * dirt) * (0.9 + 0.1 * fine)
        out = out * (1 - 0.35 * trim[..., None]) + np.array([0.33, 0.3, 0.26]) * 0.35 * trim[..., None] * (0.6 + 0.4 * dirt)
    else:
        k = {1: 0.5, 2: 0.62, 3: 0.7}[t]
        out = rgb * (1 - k * cloth[..., None]) + (np.clip(lum[..., None] * 1.25, 0, 1) * cloth_c * 1.9) * k * cloth[..., None]
        if t >= 2:   # the trims re-stitched (or inlaid) in the class's metal, brighter
            metal = np.clip(np.maximum(lum, 0.32)[..., None] * trim_c * 1.2, 0, 1)
            out = out * (1 - trim[..., None]) + metal * trim[..., None]
        if t >= 3:   # and glowing: a touch in the colour map, the light itself in the glow map
            out = np.clip(out + glow_c * 0.25 * trim[..., None], 0, 1)
            g = np.clip(gaussian_filter(trim, 1.5) * 1.3, 0, 1)[..., None] * glow_c
            glow = Image.fromarray((g * 255).astype(np.uint8)).resize((max(4, w // 2), max(4, h // 2)), Image.LANCZOS)
    alpha = bool((al < 0.98).mean() > 0.001)
    img = Image.fromarray((np.clip(np.concatenate([out, al], -1), 0, 1) * 255).astype(np.uint8), 'RGBA')
    return img, alpha, glow


def dds(img, path, alpha):
    sys.argv = sys.argv[:1]
    from assets import write_dds_mips
    path.parent.mkdir(parents=True, exist_ok=True)
    write_dds_mips(img.convert('RGBA' if alpha else 'RGB'), path, 'DXT5' if alpha else 'DXT1')


def shapes(nif):
    """each shape's name and first texture"""
    out, nrm, cur = {}, {}, None
    for l in subprocess.run([str(A.TOOLS / 'nifdump'), str(nif)], capture_output=True, text=True).stdout.splitlines():
        if l.startswith("shape '"): cur = l.split("'")[1]; out[cur] = ''; nrm[cur] = ''
        elif l.strip().startswith('tex0 ') and cur is not None and not out[cur]: out[cur] = l.strip()[5:]
        elif l.strip().startswith('tex1 ') and cur is not None and not nrm[cur]: nrm[cur] = l.strip()[5:]
    return out, nrm


def tiers(cls, made):
    """the four tier looks of each of the class's pieces: textures (each source once) and meshes"""
    for piece in SETS[cls]:
        for sx in 'fm':
            sh, nm = shapes(BASE / cls / f'{piece}_{sx}_1.nif')
            texs = {}
            for name, tex in sh.items():
                if not tex or 'actors\\character' in tex.lower(): continue
                rel = tex.lower().replace('/', '\\')
                if not rel.startswith('textures\\'): rel = 'textures\\' + rel
                src = A.VAN / rel.replace('\\', '/')
                stem = '_'.join(rel[len('textures\\'):-4].replace('\\', '_').split('_')[-3:])
                if (cls, rel) not in made:
                    for t in range(4):
                        nr = nm[name].lower().replace('\\', '/')
                        nr = A.VAN / (nr if nr.startswith('textures/') else 'textures/' + nr) if nr else None
                        img, alpha, glow = tier_tex(src, nr, cls, t)
                        dds(img, TEXOUT / cls / f'{stem}_t{t}.dds', alpha)
                        if glow is not None: dds(glow, TEXOUT / cls / f'{stem}_g.dds', False)
                    made[(cls, rel)] = stem
                texs[name] = made[(cls, rel)]
            for t in range(4):
                for wt in '01':
                    job = [f'armour {BASE / cls / f"{piece}_{sx}_{wt}.nif"}', f'out {OUT / cls / f"{piece}_{sx}_t{t}_{wt}.nif"}']
                    for name, stem in texs.items():
                        job.append(f'tex {name} 0 textures\\CurseOfAlduin\\armour\\{cls}\\{stem}_t{t}.dds')
                        if t == 3:
                            job.append(f'tex {name} 2 textures\\CurseOfAlduin\\armour\\{cls}\\{stem}_g.dds')
                            job.append(f'glow {name} ' + ' '.join(f'{x:.2f}' for x in PAL[cls][2]) + ' 1.6')
                    jf = A.WORK / f'tier_{cls}_{piece}_{sx}_{t}_{wt}.job'; jf.write_text('\n'.join(job) + '\n')
                    (OUT / cls).mkdir(parents=True, exist_ok=True)
                    r = subprocess.run([str(A.TOOLS / 'armourfit'), str(jf)], capture_output=True, text=True)
                    if r.returncode: raise SystemExit(f'{jf.name}: {r.stderr}')
        print(cls, piece, 'tiers', {k: v for k, v in texs.items()})


# (Noah, 2026-11-23: "part of my forearm is invisible as a Male with the summoner class") in first person Skyrim draws an addon's
# first-person model, and ours was the third-person robe - nothing of it showed there. Skyrim's own first-person piece of each
# body armour (its sleeves, as Skyrim draws them in first person) wears the tier's own texture instead
FIRST = {'summoner': ('clothes/necromancer/1stpersonnecromancerrobes_f_1.nif', 'clothes/necromancer/1stpersonnecromancerrobes_1.nif'),
         'destruction': ('clothes/archmage/f/1stpersonarchmagerobesf_1.nif', 'clothes/archmage/m/1stpersonarchmagerobesm_1.nif'),
         'alchemist': ('dlc02/armor/chitinlight/f/1stpersoncuirass_1.nif', 'dlc02/armor/chitinlight/m/1stpersoncuirass_1.nif'),
         'spellsword': ('armor/draugr/1stpersondraugrarmorfemale_1.nif', 'armor/draugr/1stpersondraugrarmormale_1.nif')}


def first_person(cls):
    piece = next(p for p, v in SETS[cls].items() if v[0] == 'body')
    for sx, nif in zip('fm', FIRST[cls]):
        third, _ = shapes(BASE / cls / f'{piece}_{sx}_1.nif')
        stems = []
        for tex in third.values():
            if not tex or 'actors\\character' in tex.lower(): continue
            rel = tex.lower().replace('/', '\\')
            if not rel.startswith('textures\\'): rel = 'textures\\' + rel
            st = '_'.join(rel[len('textures\\'):-4].replace('\\', '_').split('_')[-3:])
            if st not in stems: stems.append(st)
        for t in range(4):
            for wt in '01':
                src = A.VAN / 'meshes' / nif.replace('_1.nif', f'_{wt}.nif')
                fsh, _ = shapes(src)
                job = [f'armour {src}', f'out {OUT / cls / f"{piece}_{sx}_1p_t{t}_{wt}.nif"}']
                for name, tex in fsh.items():
                    if not tex or 'actors\\character' in tex.lower(): continue   # (its bare skin stays the player's own)
                    words = set(tex.lower().replace('\\', '_').replace('.', '_').split('_'))
                    st = max(stems, key=lambda x: len(words & set(x.split('_'))))   # (the tier texture most like its own)
                    job.append(f'tex {name} 0 textures\\CurseOfAlduin\\armour\\{cls}\\{st}_t{t}.dds')
                    if t == 3:
                        job.append(f'tex {name} 2 textures\\CurseOfAlduin\\armour\\{cls}\\{st}_g.dds')
                        job.append(f'glow {name} ' + ' '.join(f'{x:.2f}' for x in PAL[cls][2]) + ' 1.6')
                jf = A.WORK / f'fp_{cls}_{sx}_{t}_{wt}.job'; jf.write_text('\n'.join(job) + '\n')
                r = subprocess.run([str(A.TOOLS / 'armourfit'), str(jf)], capture_output=True, text=True)
                if r.returncode: raise SystemExit(f'{jf.name}: {r.stderr}')
        print(cls, sx, 'first person:', stems)


# (Noah, 2026-11-26: "when I'm wearing the spellsword armor, it makes my character invisible" - the body under it gone) Skyrim
# draws one addon per body slot of an armour: the cuirass's own addon and Skyrim's NakedTorso both claimed the body, so the
# player's body never came. A cuirass that doesn't cover the arms and legs itself (the Spellsword's, Skyrim's Draugr armour,
# whose arms and legs were its own skin, dropped for the player's body) wears its armour on a slot of its own (58), the body
# keeping slot 32 for NakedTorso - src/../tools/gen_esp.py gives that addon slot 58
OWN_SLOT = {('spellsword', 'cuirass'): 58}


def own_slot():
    for (cls, piece), slot in OWN_SLOT.items():
        for f in (OUT / cls).glob(f'{piece}_[mf]_t[0-3]_[01].nif'):
            tmp = f.with_suffix('.part.nif')
            subprocess.run([str(A.TOOLS / 'setpart'), str(f), str(tmp), str(slot)], check=True, capture_output=True)
            tmp.replace(f)


def zero_bounds():
    """(2026-11-24: the sleeves culled in first person) every skinned shape's bounding sphere left empty, as Skyrim's own armour
    keeps it (nifly works one out round the bind pose on save) - tools/modelconv/zerobounds.cpp"""
    for f in list(OUT.glob('*/*.nif')):
        subprocess.run([str(A.TOOLS / 'zerobounds'), str(f)], capture_output=True)


if __name__ == '__main__':
    classes = [a for a in sys.argv[1:] if a in SETS] or list(SETS)
    if '--tiers-only' not in sys.argv:
        with ProcessPoolExecutor(2) as ex:
            for o in ex.map(one, list(jobs(classes))): pass
    if '--first-person' in sys.argv:
        for cls in classes: first_person(cls)
        own_slot()
        zero_bounds()
        raise SystemExit
    made = {}
    for cls in classes: tiers(cls, made)
    for cls in classes: first_person(cls)
    own_slot()
    zero_bounds()
