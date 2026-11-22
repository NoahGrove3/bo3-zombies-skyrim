"""(Noah, 2026-11-21: the class sets "hyper realistic" on Skyrim's own armour, no helmets but hoods - Summoner the Necromancer
robes, Destruction the Arch-Mage's, Alchemist the Morag Tong (the chitin light meshes in their MT textures), Spellsword
Ahzidal's) every piece of the four sets refitted by tools/armourfit.py to the player's bodies (3BA, HIMBO), both weights,
each with the body's sliders in a .tri of its own.

    python3 tools/armoursets.py [class ...]   ->  assets/fx/Meshes/CurseOfAlduin/armour/<class>/<piece>_<f|m>_<0|1>.nif + .tri"""
import pathlib, sys
from concurrent.futures import ProcessPoolExecutor
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import armourfit as A

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT = ROOT / 'assets' / 'fx' / 'Meshes' / 'CurseOfAlduin' / 'armour'
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
                    out = OUT / cls / f'{piece}_{sx}_{wt}.nif'
                    tri = (GAMEDIR + f'{cls}\\{piece}_{sx}.tri', OUT / cls / f'{piece}_{sx}.tri')
                    yield (str(src), sex, str(out), role, tri, tex.get(sx, {}))


def one(j):
    src, sex, out, role, tri, tex = j
    A.fit(src, sex, out, role=role, tri=tri, textures=tex)
    return out


if __name__ == '__main__':
    classes = sys.argv[1:] or list(SETS)
    with ProcessPoolExecutor(2) as ex:
        for o in ex.map(one, list(jobs(classes))): pass
