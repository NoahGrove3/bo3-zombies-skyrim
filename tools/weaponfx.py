"""(Noah, 2026-11-24: "give me previews for all the weapon effects we make for my mod so we don't have to use that nexus mod" -
"instead of separate duplicated effects can you make the effects blend with the model better ... hyper realistic" - the five
looks of the Curse Weapon Effects page, his pick saved for each weapon) our own weapon effects, in place of Visual Animated
Enchants': each one fitted to its weapon's own model - a living sheet along the real edges and a glow worked into the steel -
and worn through the weapon's enchantment, as the game hangs enchantment art on a weapon in either view.

  A  Living Flame    fire rising off the edges, hottest where it leaves the steel; the edge heated; embers
  B  Runic Light     runes cut in the flat, light running up through them; a thin band of light in the edge
  C  Storm Edge      lightning crawling over the steel and jumping off the edges, re-striking; sparks
  D  Spectral Mist   a cold haze curling off the edges, the steel breathing it out; slow motes
  E  Fractured Core  the steel cracked open, light pulsing up through the cracks; a molten edge

How a weapon is fitted: its steel (the shapes named in WEAPONS, inside an optional height band) is laid flat on the plane it
spreads in (its two widest directions), drawn into a fine grid, and its outline traced: the edges are that outline, less the
stretch where the steel meets the hand (the point nearest the grip, the model's origin). Along each edge two sheets stand -
one out from the edge in the steel's plane, one across it - textured with the look's moving strip; over both faces of the
steel lies a shell of small quads, each lit by the look (vertex alpha: how near the edge it lies). Nothing of the weapon's own
model is copied: only its outline is read (models from Noah's install are read there and never kept).

    python3 tools/weaponfx.py textures            the strips, flipbooks and palettes (assets/fx/Textures/CurseOfAlduin/wfx)
    python3 tools/weaponfx.py arts [keys]         the art models (assets/fx/Meshes/CurseOfAlduin/wfx/<key>.nif)
    python3 tools/weaponfx.py preview <key>       a still of the fitted edges and shell over the outline, for checking"""
import json, math, pathlib, subprocess, sys
import numpy as np
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parent.parent
TEX = ROOT / 'assets' / 'fx' / 'Textures' / 'CurseOfAlduin' / 'wfx'
MESH = ROOT / 'assets' / 'fx' / 'Meshes' / 'CurseOfAlduin' / 'wfx'
WORK = ROOT / 'build' / 'wfx'
GT = 'CurseOfAlduin\\wfx\\'          # (texture paths as the game reads them, under Textures)
LOCAL = ROOT / 'assets' / 'fx' / 'Meshes'
INSTALL = pathlib.Path('/mnt/user-data/uploads/common--Skyrim Special Edition/Data/Meshes')   # (Noah's own install, staged to read)
TOOLS = pathlib.Path('/home/claude/tools')
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

# the colours (as the page's): base colour; Eclipse Edge wears two, one edge each
COL = {
    'white': '#eaf1ff', 'blue': '#4f8bff', 'iceblue': '#8fd8ff', 'orange': '#ff7a1f', 'red': '#e0262b', 'green': '#48d058',
    'poison': '#9be03a', 'drain': '#6e5bff', 'purple': '#a54bff', 'moon': '#c9d4e8', 'fear': '#c0182a', 'gold': '#ffc54a',
    'ss_none': '#ff4a32', 'arcane': '#a8c4ff', 'shockv': '#9f8cff', 'shadow': '#9a3bff', 'carian': '#4682ff',
}
# each weapon: its model, its steel (shape names; '*' any lit shape), the height band that is steel (y from, to), its look and
# colour (Noah's picks, the workshop 2026-11-25)
WEAPONS = {
    'priest_vahlok':     ('CurseOfAlduin\\priests\\vahlok_keyhammer.nif', ['Nordic:0', 'Iron:1'], (27, 99), 'o1', 'green'),
    'priest_nahkriin':   ('CurseOfAlduin\\priests\\nahkriin_mistblade.nif', ['BladeBody'], None, 'o1', 'white'),
    'priest_volsung':    ('CurseOfAlduin\\priests\\volsung_windcutter.nif', ['Blade:1'], None, 'o1', 'white'),   # (Noah's pick, workshop 2b2b079)
    'priest_zahkriisos': ('CurseOfAlduin\\priests\\zahkriisos_greatsword.nif', ['Ice:1'], None, 'o3', 'iceblue'),
    'priest_otar':       ('CurseOfAlduin\\priests\\otar_stormblade.nif', ['Blade:2'], None, 'o3', 'blue'),
    'priest_rahgot':     ('CurseOfAlduin\\priests\\rahgot_dagger.nif', ['Obsidian:1'], None, 'o3', 'poison'),
    'priest_krosis':     ('CurseOfAlduin\\priests\\krosis_griefblade.nif', ['Blade:0'], None, 'o2', 'red'),   # (Noah's pick, workshop 2b2b079)
    'priest_hevnoraak':  ('CurseOfAlduin\\priests\\hevnoraak_sealbreaker.nif', ['Stone:3'], None, 'o1', 'green'),
    'priest_vokun':      ('CurseOfAlduin\\reapers\\daedric.nif', ['Blade'], None, 'o3', 'purple'),
    'priest_morokei':    ('CurseOfAlduin\\priests\\morokei_maul.nif', ['Steel:0', 'Crystal:2'], (40, 99), 'o1', 'drain'),
    'tab_stormtalons':   ('weapons\\NewArmoury\\Fists\\glassClaw.nif', ['GlassClawGlass1'], None, 'o1', 'blue'),
    'tab_gravebell':     ('CurseOfAlduin\\tablet\\gravebell.nif', ['Bronze:3'], None, 'o1', 'white'),
    'tab_talons':        ('weapons\\NewArmoury\\Fists\\DBClaw.nif', ['DBClaw_MTL_B'], None, 'o1', 'red'),
    'tab_riverstone':    ('weapons\\NewArmoury\\Halberds\\NordicHalberd.nif', ['NordicPikeShaft', 'NordicPikeTip'], (70, 999), 'o1', 'green'),
    'tab_wyrmspine':     ('weapons\\NewArmoury\\Pikes\\DragonbonePike.nif', ['DragonbonePikeTip'], None, 'o1', 'fear'),
    'tab_lanternwick':   ('weapons\\NewArmoury\\QuarterStaffs\\StalhrimStaff.nif', ['StahlrimStaffBlade'], [(42, 999), (-999, -30)], 'o3', 'iceblue'),
    'tab_cinderclaws':   ('weapons\\NewArmoury\\Fists\\DaedricClaw.nif', ['DaedricClaws_MTL_B'], None, 'o3', 'orange'),   # (Noah's pick, workshop 2b2b079)
    'tab_eclipse':       ('weapons\\NewArmoury\\Pikes\\GlassPike.nif', ['GlassPikeTip'], None, 'o1', 'moon'),
    'tab_frostmourn':    ('weapons\\NewArmoury\\Rapier\\StahlrimRapier.nif', ['StahlrimRapier:18'], (9.5, 999), 'o2', 'iceblue'),
    'tab_silverscythe':  ('CurseOfAlduin\\reapers\\silver.nif', ['Blade'], None, 'o2', 'moon'),   # (Noah's pick, workshop 2b2b079)
    'tab_bonereaper':    ('CurseOfAlduin\\reapers\\Skeleton.nif', ['Blade'], None, 'o1', 'green'),
    'tab_wyrmtooth':     ('CurseOfAlduin\\reapers\\dragonTeeth.nif', ['Blade'], None, 'o1', 'green'),
    'tab_bloodmoon':     ('CurseOfAlduin\\reapers\\vampire.nif', ['Blade'], None, 'o2', 'red'),
    'fang':              ('Light Greatswords\\dragonbone\\LightGreatsword.nif', ['DragonboneGreatsword:0'], None, 'o4+o1', 'orange'),   # (Nov 27 final: Words of Power + World-Eater's Fire)
    # (Noah, 2026-11-24: "both left and right claws ... carry the same effect") the claws' left-hand twins
    'tab_stormtalons_l': ('weapons\\NewArmoury\\Fists\\GlassClawLft.nif', ['GlassClawGlass1'], None, 'o1', 'blue'),
    'tab_talons_l':      ('weapons\\NewArmoury\\Fists\\DBClawLft.nif', ['DBClaw_MTL_B'], None, 'o1', 'red'),
    # (Noah, 2026-11-26: "Whole-weapon effects on the Mutagen Staff, the Soulcaller Staff and the javelin") their heads
    # (Noah, Nov 27: "the weapon look effect for the mutagen staff should be at the very top of the staff head where the orb is")
    'flaskweap_philosopher': ('CurseOfAlduin\\alchemist\\mutagen_staff_qs.nif', ['Brass:1', 'Orbglass:4'], (70, 99), 'o4+o1', 'poison'),   # (Nov 27 final: Transmutation Runes + Living Mutagen)
    # (Noah, Nov 27: "the effect change on the mutagen staff where its on the head needs to also be done on the other staffs like the
    # soulcallers staff") the very top of its glass head only
    'soulcaller':        ('SSS\\GlassStaff.nif', ['1stPersonGlassMace:0'], (47, 99), 'o3+o5', 'white'),   # (Nov 27 final: Spectral Mist + Soul Fire)   # (doc WSKjuLEJ: o2 white)
    'javelin':           ('CurseOfAlduin\\wyrmspine\\wyrmspine.nif', ['Dbone:1', 'Runes:0'], None, 'o4+o5', 'orange'),   # (Nov 27 final: Elemental Edge + Dragonfire)
    # (Noah, Nov 27: "I want the javelins look effect to change depending on which element is being held in the other hand like the
    # spellsword") its look in each element of the spell in your other hand (src/WeaponFx.cpp picks by it)
    'javelin_fire':      ('CurseOfAlduin\\wyrmspine\\wyrmspine.nif', ['Dbone:1', 'Runes:0'], None, 'o4+o5', 'orange'),
    'javelin_frost':     ('CurseOfAlduin\\wyrmspine\\wyrmspine.nif', ['Dbone:1', 'Runes:0'], None, 'o4+o5', 'iceblue'),
    'javelin_shock':     ('CurseOfAlduin\\wyrmspine\\wyrmspine.nif', ['Dbone:1', 'Runes:0'], None, 'o4+o5', 'shockv'),
    'tab_cinderclaws_l': ('weapons\\NewArmoury\\Fists\\DaedricClawLft.nif', ['DaedricClaws_MTL_B'], None, 'o3', 'orange'),   # (its right hand's pick)
}
# (the claws: several blades fanned out - each its own outline in its own plane)
SPLIT = {'tab_stormtalons', 'tab_talons', 'tab_cinderclaws', 'tab_stormtalons_l', 'tab_talons_l', 'tab_cinderclaws_l'}
# the left-hand twins' models (src/WeaponFx.cpp finds them by the model in the left hand)
LEFT = {'tab_stormtalons_l', 'tab_talons_l', 'tab_cinderclaws_l'}


# the class weapons: each takes another model at each tier (src/Classes.cpp AdoptTierLook), so each tier's model has its own
# art; the Spellbound Rapier's look follows the element in your other hand (its BLADE_VIZ key: src/Classes.cpp LookKey)
BV_KEYS = ['alch', 'carian', 'radiant', 'ss_none', 'ss_fire', 'ss_frost', 'ss_shock', 'ss_sun', 'ss_blood', 'slayer',
           'carian1', 'radiant_L', 'ss_none_L', 'ss_fire_L', 'ss_frost_L', 'ss_shock_L', 'ss_sun_L', 'ss_blood_L']   # (Nov 27 final: 10 Destruction's Tier I; 11-17 the Legendary layers: src/Classes.cpp HandLookKey)
BV_PICK = {'alch': ('o3', 'shadow'), 'carian': ('o1', 'purple'), 'radiant': ('o1', 'gold'), 'ss_none': ('o1', 'white'), 'ss_fire': ('o2', 'orange'), 'ss_frost': ('o1', 'iceblue'), 'ss_shock': ('o1', 'iceblue'), 'ss_sun': ('o1', 'orange'), 'ss_blood': ('o1', 'blue'), 'slayer': ('o3', 'gold')}   # (Noah's picks, the workshop 2026-11-25, doc 8cd53a1; ss_fire and slayer changed in doc 5865977, 2026-11-26)
# (Noah's final tier picks, Nov 27: Destruction Tier I Sorcery then Glintstone Mist; Legendary: the Summoner's + Dawn Halo, the
# Spellsword's + Spellbound; the Alchemist and Destruction at their brightest - the tier's strength alone)
BV_PICK.update({'carian1': ('o3', 'purple'), 'radiant_L': ('o1+o3', 'gold'), 'slayer': ('o4+o1', 'gold')})
BV_PICK.update({k + '_L': (BV_PICK[k][0] + '+o2', BV_PICK[k][1]) for k in ['ss_none', 'ss_fire', 'ss_frost', 'ss_shock', 'ss_sun', 'ss_blood']})
BV_PICK['ss_fire_L'] = ('o2+o1', BV_PICK['ss_fire'][1])   # (its own look is already Spellbound's: the bare blade's laid over it)
CLASS_MODELS = {   # the look key(s), each model: its steel shapes and band
    # (Noah, 2026-11-25: "the weapon effects are at the ends of each side of the staff ... the weapon heads, not the handles") each
    # staff's two capped heads - where its detail is - and none of the plain shaft between
    'radiant': [('weapons\\NewArmoury\\QuarterStaffs\\SteelStaff.nif', ['SteelPikeShaft'], [(66.5, 99), (-99, -29.5)]),
                ('weapons\\NewArmoury\\QuarterStaffs\\EbonyStaff.nif', ['EbonyStaffShaft'], [(71.5, 99), (-99, -34.5)]),
                ('weapons\\NewArmoury\\QuarterStaffs\\DaedricStaff.nif', ['DaedricStaffShaft'], [(60.5, 99), (-99, -23.5)]),
                ('weapons\\NewArmoury\\QuarterStaffs\\DragonBoneStaff.nif', ['DragonboneStaffBlade'], [(30, 99), (-99, -26)])],
    # (Noah, 2026-11-24: "the effects on the wand should be the whole wand but turn the intensity down" - and Tier III takes the
    # Daedric wand, the Ebony one no longer worn) every lit shape of the wand, softer (GAIN)
    'carian': [('SSS\\SteelWand.nif', ['*'], None),
               ('SSS\\StahlrimWand.nif', ['*'], None),
               ('SSS\\GlassWand.nif', ['*'], None),
               ('SSS\\DaedricWand.nif', ['*'], None)],
    'alch': [('CurseOfAlduin\\reapers\\forsworn.nif', ['Blade'], None),
             ('CurseOfAlduin\\reapers\\falmer.nif', ['Blade'], None),
             ('CurseOfAlduin\\reapers\\dwarven.nif', ['Blade'], None),
             ('CurseOfAlduin\\reapers\\dragonpriest.nif', ['Blade'], None)],
    'ss': [('weapons\\NewArmoury\\Rapier\\SteelRapier.nif', ['1stPersonSteelRapier:22'], (13, 99)),
           ('weapons\\NewArmoury\\Rapier\\EbonyRapier.nif', ['1stPersonEbonyRapier:22'], (20, 99)),
           ('weapons\\NewArmoury\\Rapier\\DaedricRapier.nif', ['1stPersonDaedricRapier:29'], (14, 99)),
           ('weapons\\NewArmoury\\Rapier\\DragonboneRapier.nif', ['1stPersonDragonboneRapier:'], (8.8, 99))],
    'slayer': [('CurseOfAlduin\\slayer\\dragonslayer.nif', ['Dbone:1'], None)],
}
CLASS = {}   # art key -> (model, shapes, band, look, colour, the look key, the model)
for group, models in CLASS_MODELS.items():
    keys = [k for k in BV_KEYS if k.startswith('ss_')] if group == 'ss' else [k for k in BV_KEYS if k == group or k in (group + '1', group + '_L')]
    for model, names, band in models:
        stem = model.split('\\')[-1][:-4].lower()
        for k in keys:
            look, col = BV_PICK[k]
            CLASS[f'cls_{stem}_{k}'] = (model, names, band, look, col, k)
GAIN = {k: 0.6 for k in CLASS if k.endswith('_carian') or k.endswith('_carian1')}   # (a weapon's own intensity)
ALL = dict(WEAPONS)
ALL.update({k: v[:5] for k, v in CLASS.items()})
SSS = pathlib.Path('/tmp/sss/meshes')   # (Fulcimentum's wands, read out of its archive on Noah's install: tools/weaponfx.py notes)


# ---------------------------------------------------------------- noise (tileable)
def lattice_noise(h, w, py, px, seed):
    """value noise on a py x px lattice that wraps (tileable both ways), sampled on an h x w grid"""
    rng = np.random.default_rng(seed)
    L = rng.random((py, px))
    y = (np.arange(h) + 0.5) / h * py
    x = (np.arange(w) + 0.5) / w * px
    y0 = np.floor(y).astype(int); x0 = np.floor(x).astype(int)
    fy = y - y0; fx = x - x0
    fy = fy * fy * (3 - 2 * fy); fx = fx * fx * (3 - 2 * fx)
    y1 = (y0 + 1) % py; x1 = (x0 + 1) % px; y0 %= py; x0 %= px
    a = L[y0][:, x0]; b = L[y0][:, x1]; c = L[y1][:, x0]; d = L[y1][:, x1]
    top = a + (b - a) * fx[None, :]; bot = c + (d - c) * fx[None, :]
    return top + (bot - top) * fy[:, None]


def fbm(h, w, py, px, seed, oct=5):
    s, a, tot = np.zeros((h, w)), 0.5, 0.0
    for o in range(oct):
        s += a * lattice_noise(h, w, py * 2 ** o, px * 2 ** o, seed + o * 17)
        tot += a; a *= 0.5
    return s / tot


def warp_sample(img, du, dv):
    """img sampled at (row + dv, col + du) with wrap (both in texels)"""
    h, w = img.shape
    yy, xx = np.mgrid[0:h, 0:w].astype(float)
    ys = (yy + dv) % h; xs = (xx + du) % w
    y0 = np.floor(ys).astype(int); x0 = np.floor(xs).astype(int); fy = ys - y0; fx = xs - x0
    y1 = (y0 + 1) % h; x1 = (x0 + 1) % w
    return (img[y0, x0] * (1 - fx) + img[y0, x1] * fx) * (1 - fy) + (img[y1, x0] * (1 - fx) + img[y1, x1] * fx) * fy


def smooth(a, b, x):
    t = np.clip((x - a) / (b - a), 0, 1)
    return t * t * (3 - 2 * t)


def enhance(rgba, long_side=2048, detail=True, seed=0):
    """(Noah, 2026-11-25: "2K textures", "4K only for the wonders and Alduin's Bane") a texture drawn small brought up to its
    full size: resampled (wrapping, so it still tiles) and given fine grain of its own at the new size, so it stays crisp up
    close instead of turning to soft blur"""
    import cv2
    h, w = rgba.shape[:2]
    f = long_side / max(h, w)
    if f <= 1.0: return rgba
    H2, W2 = int(round(h * f)), int(round(w * f))
    pad = 6
    big = cv2.copyMakeBorder(rgba.astype(np.float32), pad, pad, pad, pad, cv2.BORDER_WRAP)
    up = cv2.resize(big, (int(round((w + 2 * pad) * f)), int(round((h + 2 * pad) * f))), interpolation=cv2.INTER_LANCZOS4)
    o = int(round(pad * f)); up = up[o:o + H2, o:o + W2]
    if detail:
        fine = fbm(H2, W2, max(4, H2 // 48), max(4, W2 // 48), 9000 + seed, oct=3)
        up[..., 3] *= 0.86 + 0.28 * fine
        up[..., :3] *= (0.95 + 0.1 * fine)[..., None]
    return np.clip(up, 0, 1)


def save_tex(rgba, name, long_side=2048, detail=True):
    """RGBA float [0,1] (rows top to bottom) -> DXT5 DDS with mips, at 2K (4K for the wonders' and Alduin's Bane's own)"""
    import assets
    TEX.mkdir(parents=True, exist_ok=True)
    rgba = enhance(np.asarray(rgba, float), long_side, detail and not name.startswith(('bolt_', 'storm_')), sum(map(ord, name)))
    im = Image.fromarray((np.clip(rgba, 0, 1) * 255).astype(np.uint8), 'RGBA')
    assets.write_dds_mips(im, TEX / f'{name}.dds', 'DXT5')
    WORK.mkdir(parents=True, exist_ok=True)
    im.save(WORK / f'{name}.png')


def grey(v, a):
    v = np.clip(v, 0, 1); return np.dstack([v, v, v, np.clip(a, 0, 1)])


# ---------------------------------------------------------------- the looks' textures
def textures():
    H, W = 512, 128     # along the edge (wraps), out from it (u: 0 at the steel, 1 at the sheet's far side)
    u = np.linspace(0, 1, W)[None, :].repeat(H, 0)
    # A: flame tongues - a warped noise field, stretched along the edge; grey = heat, alpha = density
    for k, seed in ((0, 11), (1, 29)):
        w_ = fbm(H, W, 6, 2, seed + 3)
        n = warp_sample(fbm(H, W, 8, 2, seed), (w_ - 0.5) * 30, (w_ - 0.5) * 60)
        det = fbm(H, W, 24, 6, seed + 9)
        tongue = n * 2.25 - u * 2.15 - 0.48
        a = smooth(0.0, 0.2, tongue) * (0.3 + 0.7 * det)
        heat = np.clip(tongue * 1.3 + 0.12 - u * 0.4, 0, 1)
        save_tex(grey(heat, a), f'flame_strip{k}')
    # D: mist - slow, soft, wider; two layers
    for k, seed in ((0, 41), (1, 57)):
        w_ = fbm(H, W, 4, 2, seed + 5)
        n = warp_sample(fbm(H, W, 5, 2, seed), (w_ - 0.5) * 40, (w_ - 0.5) * 80)
        lit = fbm(H, W, 12, 3, seed + 11)
        dens = smooth(0.0, 0.42, n * 1.8 - u * 1.5 - 0.2)
        save_tex(grey(0.25 + 0.6 * lit, dens * 0.8), f'mist_strip{k}')
    # B: the stream - a thin core along the edge, light running along it in waves
    v = np.linspace(0, 1, H, endpoint=False)[:, None].repeat(W, 1)
    wave = 0.55 + 0.45 * np.sin(v * 2 * np.pi * 6) * (0.6 + 0.4 * fbm(H, W, 16, 1, 71))
    core = np.exp(-(u * 5.5) ** 2 * 1.3); halo = np.exp(-u * 4.0) * 0.35
    save_tex(grey(0.55 + 0.45 * core, (core * 1.2 + halo) * wave), 'stream_strip')
    # E: the molten edge - a white-hot line broken into beads
    beads = smooth(0.35, 0.75, fbm(H, W, 20, 1, 83))
    core = np.exp(-(u * 7.0) ** 2) * (0.45 + 0.55 * beads); halo = np.exp(-u * 4.5) * 0.22 * (0.6 + 0.4 * beads)
    save_tex(grey(0.5 + 0.5 * core, core * 1.4 + halo), 'molten_strip')
    # C: lightning off the edge - an 8 x 4 flipbook of strikes: thin winding bolts (iso-lines of noise). (Noah, 2026-11-24: "30
    # FPS so it looks really smooth") 32 drawings played at 28 a second, the same speed as the 16 before: each one between two
    # of the 16 strikes, its noise eased from one to the next, so the bolts crawl rather than jump
    FR, KEYS = 32, 16
    fw, fh = 64, 256
    uu = np.linspace(0, 1, fw)[None, :].repeat(fh, 0)
    K1 = [fbm(fh, fw, 6, 2, 200 + f * 7) for f in range(KEYS)]
    K2 = [fbm(fh, fw, 10, 3, 400 + f * 7) for f in range(KEYS)]
    LV = [(np.random.default_rng(900 + f).random((4, 1)) > 0.3).repeat(fh // 4, 0) for f in range(KEYS)]
    cells = []
    for f in range(FR):
        t = f * KEYS / FR; i = int(t); fr = t - i; j = (i + 1) % KEYS
        n = K1[i] * (1 - fr) + K1[j] * fr
        n2 = K2[i] * (1 - fr) + K2[j] * fr
        bolt = (1 - smooth(0.0, 0.03, np.abs(n - 0.5))) * smooth(0.95, 0.05, uu)
        twig = (1 - smooth(0.0, 0.022, np.abs(n2 - 0.5))) * smooth(0.6, 0.05, uu) * 0.6
        live = LV[i] if fr < 0.5 else LV[j]
        a = np.clip((bolt + twig) * 1.6 * live + np.exp(-uu * 7) * 0.08, 0, 1)
        cells.append(grey(0.6 + 0.4 * bolt, a))
    atlas = np.zeros((fh * 4, fw * 8, 4))
    for f, c in enumerate(cells): atlas[(f // 8) * fh:(f // 8 + 1) * fh, (f % 8) * fw:(f % 8 + 1) * fw] = c
    save_tex(atlas, 'storm_edge')
    # C: lightning crawling over the steel - an 8 x 4 flipbook of 2D strikes, eased between 16 the same way
    fs = 128
    atlas = np.zeros((fs * 4, fs * 8, 4))
    S1 = [fbm(fs, fs, 3, 3, 600 + f * 13) for f in range(KEYS)]
    S2 = [smooth(0.45, 0.6, fbm(fs, fs, 2, 2, 700 + f)) for f in range(KEYS)]
    for f in range(FR):
        t = f * KEYS / FR; i = int(t); fr = t - i; j = (i + 1) % KEYS
        n = S1[i] * (1 - fr) + S1[j] * fr
        line = 1 - smooth(0.0, 0.028, np.abs(n - 0.5))
        mask = S2[i] * (1 - fr) + S2[j] * fr
        atlas[(f // 8) * fs:(f // 8 + 1) * fs, (f % 8) * fs:(f % 8 + 1) * fs] = grey(0.55 + 0.45 * line, line * mask * 1.2)
    save_tex(atlas, 'storm_shell')
    # B: runes cut in the steel - a column of glyphs (each three strokes), tiling along the blade
    G = 32; RH, RW = 1024, 128
    img = np.zeros((RH, RW))
    rng = np.random.default_rng(5)
    yy, xx = np.mgrid[0:RH, 0:RW].astype(float)
    for g in range(G):
        cy = (g + 0.5) * RH / G; cx = RW / 2; s = RH / G * 0.38
        for _ in range(3):
            a = np.array([cx, cy]) + (rng.random(2) - 0.5) * 2 * s; b = np.array([cx, cy]) + (rng.random(2) - 0.5) * 2 * s
            pa = np.dstack([xx - a[0], yy - a[1]]); ba = b - a
            t = np.clip((pa @ ba) / (ba @ ba), 0, 1)
            d = np.hypot(xx - (a[0] + ba[0] * t), yy - (a[1] + ba[1] * t))
            img = np.maximum(img, np.exp(-(d / 2.2) ** 2))
    save_tex(grey(0.5 + 0.5 * img, img), 'runes')
    # E: the cracks - a warped cell pattern, its seams lit, some stretches dark
    S = 512
    pts = np.random.default_rng(9).random((90, 2)) * S
    yy, xx = np.mgrid[0:S, 0:S].astype(float)
    wx = (fbm(S, S, 4, 4, 21) - 0.5) * 60; wy = (fbm(S, S, 4, 4, 23) - 0.5) * 60
    X = xx + wx; Y = yy + wy
    d1 = np.full((S, S), 1e9); d2 = np.full((S, S), 1e9)
    for ox in (-S, 0, S):
        for oy in (-S, 0, S):
            for p in pts:
                d = np.hypot(X - p[0] - ox, Y - p[1] - oy)
                m = d < d1; d2 = np.where(m, d1, np.minimum(d2, d)); d1 = np.where(m, d, d1)
    crack = 1 - smooth(0.0, 4.0, d2 - d1)
    keep = smooth(0.38, 0.62, fbm(S, S, 3, 3, 31))
    save_tex(grey(0.45 + 0.55 * crack, crack * keep), 'cracks')
    # A / D: a soft moving haze for the steel (the heat over A's edge, D's breath)
    hz = fbm(256, 256, 4, 4, 51)
    save_tex(grey(0.4 + 0.6 * hz, smooth(0.35, 0.8, hz)), 'haze')
    # the motes (A's embers, C's sparks, D's slow motes): one soft round mote
    m = 64; yy, xx = np.mgrid[0:m, 0:m]; r = np.hypot(xx - m / 2 + 0.5, yy - m / 2 + 0.5) / (m / 2)
    save_tex(grey(np.clip(1.1 - r, 0, 1), np.exp(-(r * 2.4) ** 2)), 'mote')
    palettes()
    textures_v2()
    sk_textures()
    import weaponsig
    weaponsig.textures()


# ---------------------------------------------------------------- Skyrim's own effects as the base (Noah, 2026-11-25)
SKFX = pathlib.Path('/mnt/user-data/uploads/un_tmp/skfx/textures/effects')   # (read out of Noah's own Skyrim - never committed as they are)


def sk_textures():
    """(Noah, 2026-11-25: "use Skyrim's weapons and abilities and magical effects and elemental effects as a base, and then make
    our version an enhancement of it"; "Yes, base them like this") Skyrim's own enchantment membranes - the fill its enchanted
    weapons wear (EnchFireFXShader and its kin: EnchFlameProject01; the shock tile; the vapour; frost cloud; dark swirls; the
    squiggling wisps) - each brought to 2K and given fine grain, then laid over our looks as their second skin"""
    def lum(name):
        im = np.asarray(Image.open(SKFX / f'{name}.dds').convert('RGBA')).astype(float) / 255
        return im[..., :3].max(2), im[..., 3]
    for src, dst, how in (('enchflameproject01', 'sk_enchfill', 'lum'), ('shocktile01', 'sk_shock', 'lum'), ('vaportile01', 'sk_vapour', 'lum'),
                          ('cloudtilefrostsmall', 'sk_frost', 'lum'), ('darkswirls', 'sk_swirls', 'lum'), ('magicsquiggles01', 'sk_wisps', 'lum')):
        l, a = lum(src)
        l = (l - l.min()) / max(1e-3, np.ptp(l))
        if dst in ('sk_vapour', 'sk_frost'): al = smooth(0.25, 0.95, l)   # (the cloud tiles: only their bright billows)
        else: al = np.clip(l * 1.15, 0, 1) ** 1.2
        save_tex(grey(0.35 + 0.65 * l, al), dst)


# ---------------------------------------------------------------- the looks remade (Noah, 2026-11-24)
def _poly_field(h, w, lines, wrapv=True):
    """distance (texels) from every texel to the nearest of the given polylines ((N, 2) arrays of (u, v) in texels)"""
    from scipy import ndimage
    from skimage.draw import line
    m = np.zeros((h, w), bool)
    for P in lines:
        P = np.asarray(P)
        for (x0, y0), (x1, y1) in zip(P[:-1], P[1:]):
            rr, cc = line(int(round(y0)), int(round(x0)), int(round(y1)), int(round(x1)))
            if wrapv: rr = rr % h
            ok = (cc >= 0) & (cc < w) & (rr >= 0) & (rr < h)
            m[rr[ok], cc[ok]] = True
    if wrapv:   # (the field tiles along v: three copies stacked, the middle kept)
        big = np.vstack([m, m, m]); d = ndimage.distance_transform_edt(~big)[h:2 * h]
    else:
        d = ndimage.distance_transform_edt(~m)
    return d


def _bolt(rng, a, b, rough, depth=6):
    """a lightning path from a to b: midpoint displacement, each split pushed sideways"""
    pts = [np.array(a, float), np.array(b, float)]
    for lvl in range(depth):
        out = [pts[0]]
        for p, q in zip(pts[:-1], pts[1:]):
            d = q - p; n = np.array([-d[1], d[0]]) / (np.linalg.norm(d) + 1e-9)
            m = (p + q) / 2 + n * rng.normal() * np.linalg.norm(d) * rough
            out += [m, q]
        pts = out
    return np.array(pts)


def textures_v2():
    """Flame that rises from the hilt to the tip, Storm as real lightning, and the new looks: Frost, Shadow, Radiance, Spirit"""
    rng = np.random.default_rng(77)
    H, W = 512, 128
    u = np.linspace(0, 1, W)[None, :].repeat(H, 0)
    v = np.linspace(0, 1, H, endpoint=False)[:, None].repeat(W, 1)
    # FLAME: tongues of fire that stream along the blade (v runs hilt -> tip, the texture slides tipward): long licks drawn
    # out along v, curling outward, white-hot at the steel; two layers that slide at different speeds read as turbulence
    for k, seed in ((0, 301), (1, 317)):
        w1 = fbm(H, W, 3, 2, seed + 1); w2 = fbm(H, W, 7, 3, seed + 2)
        n = warp_sample(fbm(H, W, 5 + k * 2, 3, seed), (w1 - 0.5) * 26, (w2 - 0.5) * 90)
        streak = fbm(H, W, 9 + k * 3, 1, seed + 5)          # (tongues: separate licks along the blade)
        lick = (n * 1.6 + streak * 1.4) * 1.35 - u * (2.9 - 0.6 * k) - 1.3
        det = fbm(H, W, 22, 7, seed + 9)
        a = smooth(0.0, 0.14, lick) * (0.55 + 0.45 * det) * smooth(1.0, 0.55, u)
        heat = np.clip(lick * 1.6 + 0.3 - u * 0.45, 0, 1) * (0.8 + 0.2 * det)
        save_tex(grey(heat, a), f'fire_rise{k}')
    # STORM: an 8 x 4 flipbook of whole bolts running the strip's length, their ends pinned to the edge so the pieces join
    # without gaps; each strike lives three frames, fading, with forks reaching out and a steady glow along the steel
    FR = 32; fw, fh = 64, 256
    cells = []
    life = 3; strikes = FR // life + 1
    paths = []
    for sidx in range(strikes):
        r = np.random.default_rng(900 + sidx)
        main = _bolt(r, (fw * 0.16, -2), (fw * 0.16, fh + 2), 0.13, 7)
        du = main[:, 0] - fw * 0.16; mx = max(1e-3, np.abs(du).max())
        main[:, 0] = fw * 0.16 + du / mx * fw * r.uniform(0.08, 0.2) + np.abs(du / mx) * fw * 0.06
        forks = []
        for _ in range(r.integers(2, 5)):
            i0 = r.integers(len(main) // 8, len(main) * 7 // 8); p = main[i0]
            end = p + np.array([fw * r.uniform(0.25, 0.7), r.uniform(-40, 40)])
            forks.append(_bolt(r, p, end, 0.3, 5))
        paths.append((main, forks))
    for f in range(FR):
        sidx, age = divmod(f, life)
        main, forks = paths[sidx]
        dm = _poly_field(fh, fw, [main]); dfk = _poly_field(fh, fw, forks, wrapv=False) if forks else np.full((fh, fw), 1e3)
        uu = np.linspace(0, 1, fw)[None, :].repeat(fh, 0)
        fadek = [1.0, 0.72, 0.42][age]
        core = np.exp(-(dm / 1.1) ** 2) + 0.6 * np.exp(-(dfk / 0.9) ** 2) * smooth(0.9, 0.3, uu)
        glow = np.exp(-dm / 5.0) * 0.45 + np.exp(-dfk / 4.0) * 0.2
        base = np.exp(-uu * 6) * 0.25   # (the steady charge along the steel: never a gap)
        a = np.clip((core * 1.2 + glow) * fadek + base, 0, 1)
        cells.append(grey(np.clip(0.55 + 0.45 * core, 0, 1), a))
    atlas = np.zeros((fh * 4, fw * 8, 4))
    for f, c in enumerate(cells): atlas[(f // 8) * fh:(f // 8 + 1) * fh, (f % 8) * fw:(f % 8 + 1) * fw] = c
    save_tex(atlas, 'bolt_edge')
    # STORM over the steel: branching bolts crawling across each tile, the same three-frame strikes
    fs = 128
    atlas = np.zeros((fs * 4, fs * 8, 4))
    for f in range(FR):
        sidx, age = divmod(f, life)
        r = np.random.default_rng(1500 + sidx)
        lines = []
        for _ in range(2):
            a0 = r.uniform(0, fs, 2); b0 = r.uniform(0, fs, 2)
            m = _bolt(r, a0, b0, 0.25, 6); lines.append(m)
            for _ in range(2):
                p = m[r.integers(len(m))]; lines.append(_bolt(r, p, p + r.uniform(-40, 40, 2), 0.3, 4))
        d = _poly_field(fs, fs, lines, wrapv=False)
        fadek = [1.0, 0.7, 0.4][age]
        core = np.exp(-(d / 0.9) ** 2); glow = np.exp(-d / 4.0) * 0.35
        atlas[(f // 8) * fs:(f // 8 + 1) * fs, (f % 8) * fs:(f % 8 + 1) * fs] = grey(np.clip(0.55 + 0.45 * core, 0, 1), np.clip((core + glow) * fadek, 0, 1))
    save_tex(atlas, 'bolt_shell')
    # FROST: rime - needles of ice growing out from the steel (a column that tiles along the edge), and cold vapour that
    # sinks back toward the hilt
    lines = []
    for i in range(70):
        y0 = rng.uniform(0, H); L = rng.uniform(5, 22) * (1.0 if rng.random() < 0.75 else 1.6)
        ang = rng.uniform(-0.9, 0.9)
        x1 = 2 + L * np.cos(ang) * 1.2; y1 = y0 + L * np.sin(ang)
        lines.append(np.array([[1, y0], [x1, y1]]))
        if L > 18:   # (a side spur)
            t = rng.uniform(0.4, 0.8); p = np.array([1 + (x1 - 1) * t, y0 + (y1 - y0) * t]); a2 = ang + rng.choice([-1, 1]) * 0.8
            lines.append(np.array([p, p + np.array([np.cos(a2), np.sin(a2)]) * L * 0.35 * 1.4]))
    d = _poly_field(H, W, lines)
    needle = np.exp(-(d / 1.0) ** 2) + np.exp(-d / 3.5) * 0.25
    base = np.exp(-u * 9) * 0.6
    sparkle = (fbm(H, W, 40, 10, 611) > 0.78) * 0.6
    save_tex(grey(np.clip(0.5 + 0.5 * needle + sparkle * 0.3, 0, 1), np.clip(needle * smooth(1, 0.5, u) + base, 0, 1)), 'rime_strip')
    wv = fbm(H, W, 4, 2, 631)
    nv = warp_sample(fbm(H, W, 6, 2, 633), (wv - 0.5) * 30, (wv - 0.5) * 70)
    dens = smooth(0.0, 0.45, nv * 1.7 - u * 1.6 - 0.15)
    save_tex(grey(0.55 + 0.35 * fbm(H, W, 12, 3, 641), dens * 0.6), 'vapour_strip')
    # the frost over the steel: dendrites spreading over each tile
    S = 256; lines = []
    r = np.random.default_rng(653)
    def grow(p, ang, L, lvl):
        if lvl > 4 or L < 4: return
        q = p + np.array([np.cos(ang), np.sin(ang)]) * L
        lines.append(np.array([p, q]))
        for k in range(3):
            t = r.uniform(0.3, 0.9); pp = p + (q - p) * t
            grow(pp, ang + r.choice([-1, 1]) * r.uniform(0.6, 1.1), L * 0.45, lvl + 1)
    for _ in range(26): grow(r.uniform(0, S, 2), r.uniform(0, 6.28), r.uniform(24, 60), 0)
    d = _poly_field(S, S, lines, wrapv=False)
    fr = np.exp(-(d / 0.9) ** 2) + np.exp(-d / 3.0) * 0.2
    save_tex(grey(np.clip(0.55 + 0.45 * fr, 0, 1), np.clip(fr, 0, 1)), 'frost_shell')
    # SHADOW: billowing smoke (drawn over, not added: dark, with the colour only where it thins at its rim)
    w1 = fbm(H, W, 3, 2, 701)
    n = warp_sample(fbm(H, W, 5, 3, 703), (w1 - 0.5) * 34, (w1 - 0.5) * 110)
    dens = smooth(0.05, 0.5, n * 2.0 - u * 1.9 - 0.05) * smooth(0.8, 0.2, u)
    edge = np.clip(smooth(0.05, 0.25, dens) - smooth(0.25, 0.7, dens), 0, 1)
    save_tex(grey(0.08 + 0.85 * edge, dens * 0.7), 'shade_strip')
    # RADIANCE: rays of light standing out from the steel, slowly shifting
    rays = fbm(H, 1, 60, 1, 721)[:, :1].repeat(W, 1); rays2 = fbm(H, 1, 23, 1, 723)[:, :1].repeat(W, 1)
    rr = smooth(0.45, 0.9, rays * 0.6 + rays2 * 0.6)
    save_tex(grey(0.45 + 0.55 * np.exp(-u * 3), rr * np.exp(-u * 2.6) * 0.9 + np.exp(-u * 10) * 0.4), 'ray_strip')
    hz = fbm(256, 256, 3, 3, 731)
    save_tex(grey(0.5 + 0.4 * hz, 0.45 + 0.4 * hz), 'halo_shell')
    # SPIRIT: long ghostly wisps streaming off the blade, drawn out along it
    w1 = fbm(H, W, 2, 3, 741)
    n = warp_sample(fbm(H, W, 3, 6, 743), (w1 - 0.5) * 40, (w1 - 0.5) * 140)
    streak = np.clip(smooth(0.38, 0.55, n) - smooth(0.62, 0.85, n), 0, 1)
    a = np.clip(streak * 1.2, 0, 1) * smooth(0.78, 0.12, u) * (0.5 + 0.5 * fbm(H, W, 8, 4, 747))
    save_tex(grey(0.55 + 0.45 * streak, a), 'wisp_strip')
    # the shadow's palettes: near black where the smoke is thick, the colour at its thin rim
    x = (np.arange(256) + 0.5) / 256; vv = (np.arange(64) + 0.5) / 64
    for name, h in COL.items():
        c = hexrgb(h)
        rgb = np.zeros((64, 256, 3))
        for i in range(3): rgb[..., i] = np.interp(x[None, :] * (0.5 + 0.5 * vv[:, None]), [0, 0.35, 0.8, 1.0], [0.01, 0.03 + c[i] * 0.05, c[i] * 0.85, min(1, c[i] * 1.2 + 0.15)])
        a = np.clip(0.55 + x[None, :] * 0.6, 0, 1) * (0.35 + 0.65 * vv[:, None])
        Image.fromarray((np.dstack([np.clip(rgb, 0, 1), a]) * 255).astype(np.uint8), 'RGBA').save(TEX / f'pald_{name}.dds')

def hexrgb(h):
    h = h.lstrip('#'); return np.array([int(h[i:i + 2], 16) / 255 for i in (0, 2, 4)])


def ramp(c, x):
    """the weapon's colour heating to white at the core (as the page's)"""
    deep = c * c * 0.4; hot = c * 0.55 + np.array([1.0, 0.86, 0.6]) * c.max() * 0.45; core = np.array([1.0, 0.97, 0.92])
    out = np.zeros(x.shape + (3,))
    for i in range(3):
        out[..., i] = np.interp(x, [0, 0.45, 0.85, 1.0], [deep[i], c[i], hot[i], core[i]])
    return out


def palettes():
    """a palette per colour: across, the texture's grey (its heat); down, the fade (the vertex alpha); its alpha rising with
    both - for the strips' greyscale-to-palette colouring"""
    x = (np.arange(256) + 0.5) / 256; v = (np.arange(64) + 0.5) / 64
    for name, h in COL.items():
        pos = x[None, :] * (0.4 + 0.6 * v[:, None])
        rgb = ramp(hexrgb(h), pos)
        a = np.clip(x[None, :] * 1.3, 0, 1) * (0.35 + 0.65 * v[:, None])
        TEX.mkdir(parents=True, exist_ok=True)
        Image.fromarray((np.dstack([rgb, a]) * 255).astype(np.uint8), 'RGBA').save(TEX / f'pal_{name}.dds')


# ---------------------------------------------------------------- fitting a weapon
def model_path(rel):
    for base in (LOCAL, INSTALL, SSS):
        p = base / rel.replace('\\', '/')
        if p.exists(): return p
        # (case on Noah's install differs from the records')
        parent = (base / rel.replace('\\', '/')).parent
        if parent.exists():
            for c in parent.iterdir():
                if c.name.lower() == p.name.lower(): return c
    raise SystemExit(f'model not found: {rel}')


def read_model(path):
    out = subprocess.run([str(TOOLS / 'nifworld'), str(path)], capture_output=True, text=True, check=True).stdout
    shapes, cur = [], None
    for l in out.splitlines():
        if l.startswith('shape '):
            p = l[6:].split('\t'); cur = {'name': p[0], 'tex': p[1], 'kind': p[2], 'v': [], 't': []}; shapes.append(cur)
        elif l.startswith('v '): cur['v'].append([float(x) for x in l.split()[1:4]])
        elif l.startswith('t '): cur['t'].append([int(x) for x in l.split()[1:4]])
    for s in shapes: s['v'] = np.array(s['v']).reshape(-1, 3); s['t'] = np.array(s['t'], dtype=int).reshape(-1, 3)
    return shapes


def fit(key):
    """the weapon's steel outline(s): one fit for a blade, one for each of a claw's blades"""
    rel, names, band, look, col = ALL[key]
    shapes = read_model(model_path(rel))
    V, T = [], []
    for s in shapes:
        if names == ['*'] and s['kind'] != 'lit': continue
        if names != ['*'] and not any(s['name'] == n or s['name'].startswith(n + ':') or (n.endswith(':') and s['name'].startswith(n)) for n in names): continue
        if not len(s['t']): continue
        base = sum(len(x) for x in V)
        V.append(s['v']); T.append(s['t'] + base)
    if not V: raise SystemExit(f'{key}: no steel shapes found among {[s["name"] for s in shapes]}')
    V = np.concatenate(V); T = np.concatenate(T)
    if band:
        bands = band if isinstance(band, list) else [band]
        y = V[T][:, :, 1].mean(1)   # (by each triangle's middle: a long thin one is kept if it is mostly in)
        ok = np.zeros(len(T), bool)
        for b0, b1 in bands:
            if isinstance(band, list):   # (a staff's two heads: every corner inside - the long faces of the shaft never count)
                ok |= ((V[T][:, :, 1] >= b0) & (V[T][:, :, 1] <= b1)).all(1)
            else: ok |= (y >= b0) & (y <= b1)
        T = T[ok]
    parts = [T]
    if key in SPLIT:
        # the claw's blades: triangles joined by shared (welded) corners, each blade one piece
        from scipy.sparse import coo_matrix
        from scipy.sparse.csgraph import connected_components
        _, inv = np.unique(np.round(V, 2), axis=0, return_inverse=True)
        TT = inv.reshape(-1)[T]
        n = TT.max() + 1
        rows = np.r_[TT[:, 0], TT[:, 1], TT[:, 2]]; cols = np.r_[TT[:, 1], TT[:, 2], TT[:, 0]]
        nc, lab = connected_components(coo_matrix((np.ones(len(rows)), (rows, cols)), shape=(n, n)), directed=False)
        tl = lab[TT[:, 0]]
        parts = []
        for c in range(nc):
            Tc = T[tl == c]
            if len(Tc) < 8: continue
            P = V[np.unique(Tc)]
            if np.ptp(P, axis=0).max() < 9.0: continue   # (knuckle plates and rivets: not a blade)
            parts.append(Tc)
        parts.sort(key=lambda t: -len(t))
    return [fit_part(key, V, Tp, band, look, col) for Tp in parts]


def fit_part(key, V, T, band, look, col):
    used = np.unique(T)
    P = V[used]
    c = P.mean(0)
    _, _, vt = np.linalg.svd(P - c, full_matrices=False)
    e1, e2, e3 = vt[0], vt[1], vt[2]
    # (the blade runs along e1 pointing away from the grip at the origin; e3 the steel's face)
    if np.dot(c, e1) < 0: e1 = -e1
    e2 = np.cross(e3, e1); e2 /= np.linalg.norm(e2)
    q = lambda X: np.stack([(X - c) @ e1, (X - c) @ e2], -1)
    Q = q(V); D = (V - c) @ e3
    span0 = np.ptp(Q[used], axis=0).max()
    res = 0.3 if span0 > 30 else 0.15
    lo = Q[used].min(0) - 3; hi = Q[used].max(0) + 3
    gw, gh = int(math.ceil((hi[0] - lo[0]) / res)), int(math.ceil((hi[1] - lo[1]) / res))
    mask = np.zeros((gh, gw), bool); depth = np.zeros((gh, gw))
    import cv2
    for tri in T:
        pts = ((Q[tri] - lo) / res).astype(np.float32)
        poly = np.round(pts).astype(np.int32).reshape(-1, 1, 2)
        m = np.zeros((gh, gw), np.uint8); cv2.fillConvexPoly(m, poly, 1)
        mb = m.astype(bool); mask |= mb
        depth[mb] = np.maximum(depth[mb], np.abs(D[tri]).max())
    from scipy import ndimage
    mask = ndimage.binary_closing(mask, iterations=2)
    mask = ndimage.binary_fill_holes(mask)
    lab, n = ndimage.label(mask)
    if n > 1:
        sizes = ndimage.sum(mask, lab, range(1, n + 1))
        keep = [i + 1 for i, s in enumerate(sizes) if s >= sizes.max() * 0.08]
        mask = np.isin(lab, keep)
    depth = ndimage.grey_dilation(depth, size=3) * mask
    from skimage import measure
    grip = q(np.zeros((1, 3)))[0]
    contours = [cc[:, ::-1] * res + lo for cc in measure.find_contours(mask.astype(float), 0.5) if len(cc) > 12]
    if not contours: raise SystemExit(f'{key}: no outline found')
    allc = np.concatenate(contours)
    A = allc[np.argmin(np.linalg.norm(allc - grip, axis=1))]
    span = (Q[used].max(0) - Q[used].min(0))
    # (Noah, 2026-11-24: "it needs to affect their entire blade length") only the steel's own end at the hand is left bare - the
    # stretch of outline that faces the grip across the blade's root (the guard), never the edges running up to it
    xbase = allc[:, 0].min()
    two_ends = isinstance(band, list)
    rA = 0.0
    edges = []
    for cc in contours:
        # resample every ~1.2 units (0.6 on small steel), smoothed a little
        seg = np.linalg.norm(np.diff(cc, axis=0), axis=1); s = np.concatenate([[0], np.cumsum(seg)])
        if s[-1] < 6: continue
        stepv = 1.2 if span0 > 30 else 0.6
        ss = np.arange(0, s[-1], stepv)
        pts = np.stack([np.interp(ss, s, cc[:, 0]), np.interp(ss, s, cc[:, 1])], 1)
        k = 3
        raw = pts.copy()
        pts = np.stack([np.convolve(np.r_[pts[-k:, i], pts[:, i], pts[:k, i]], np.ones(2 * k + 1) / (2 * k + 1), 'valid') for i in range(2)], 1)
        # (Noah, 2026-11-24: "all the way to the tip of the blade" - "until it reaches the guard") the smoothing rounds a sharp point
        # inward: within a few units of the tip (the outline's furthest point from the hand) and of the steel's root the outline
        # keeps its own shape
        tipx = raw[:, 0].max()
        keep_raw = np.clip(1.0 - (tipx - raw[:, 0]) / (3.0 if span0 > 30 else 1.6), 0, 1)
        keep_raw = np.maximum(keep_raw, np.clip(1.0 - (raw[:, 0] - xbase) / (2.5 if span0 > 30 else 1.2), 0, 1))
        pts = pts * (1 - keep_raw[:, None]) + raw * keep_raw[:, None]
        tang = np.gradient(pts, axis=0); tang /= np.linalg.norm(tang, axis=1, keepdims=True) + 1e-9
        nor = np.stack([tang[:, 1], -tang[:, 0]], 1)
        # (outward: the side away from the steel)
        probe = ((pts + nor * 0.9 - lo) / res).astype(int)
        inside = mask[np.clip(probe[:, 1], 0, gh - 1), np.clip(probe[:, 0], 0, gw - 1)]
        if inside.mean() > 0.5: nor = -nor
        if two_ends:   # (a staff's two blades: the band already leaves the shaft out)
            keepm = np.ones(len(pts), bool)
        else:
            keepm = ~((pts[:, 0] < xbase + (0.9 if span0 > 30 else 0.45)) & (nor[:, 0] < -0.55))   # (only the end facing the guard itself)
        idx = np.where(keepm)[0]
        if not len(idx): continue
        if keepm.all():
            runs = [np.r_[np.arange(len(pts)), 0]]
        else:
            start = np.where(~keepm)[0][-1] + 1
            order = np.r_[np.arange(start, len(pts)), np.arange(0, start)]
            runs, cur = [], []
            for i in order:
                if keepm[i]: cur.append(i)
                elif cur: runs.append(np.array(cur)); cur = []
            if cur: runs.append(np.array(cur))
        for r in runs:
            if len(r) < 5: continue
            if '+' in col:   # (two colours, one each edge: the run split at its tip, the point furthest from the hand)
                k = int(np.argmax(pts[r][:, 0]))
                for part in (r[:k + 1], r[k:]):
                    if len(part) >= 5: edges.append((pts[part], nor[part]))
                continue
            edges.append((pts[r], nor[r]))
    # the shell: a coarse grid of quads over both faces, each corner's lift the steel's thickness there, its alpha how near
    # the edge it lies
    dist = ndimage.distance_transform_edt(mask) * res
    return dict(key=key, look=look, col=col, gain=GAIN.get(key, 1.0), c=c, e1=e1, e2=e2, e3=e3, lo=lo, res=res, mask=mask, depth=depth, dist=dist,
                edges=edges, grip=grip, A=A, rA=rA, span=span, small=span0 <= 30)


def to3(F, p2, d=0.0):
    """points in the steel's plane (N x 2) lifted d off it (scalar or N) - back into the model's space"""
    p2 = np.atleast_2d(p2); d = np.broadcast_to(np.asarray(d, float), (len(p2),))
    return F['c'] + F['e1'] * p2[:, :1] + F['e2'] * p2[:, 1:2] + F['e3'] * d[:, None]


# ---------------------------------------------------------------- the art model
def S(tex, w, wc, per, rep, gain, along='contour', flip=None, fps=None, blend='add', dark=False):
    return dict(tex=tex, w=w, wc=wc, per=per, rep=rep, gain=gain, along=along, flip=flip, fps=fps, blend=blend, dark=dark)


def Sh(tex, how, tile, gain, flip=None, fps=None, scroll=0.0, pulse=None, blend='add', dark=False):  # (a shell over both faces)
    return dict(tex=tex, how=how, tile=tile, gain=gain, flip=flip, fps=fps, scroll=scroll, pulse=pulse, blend=blend, dark=dark)


# (Noah, 2026-11-24: "remake the Flame, Runic, and Storm looks. Keep Mist and Core. Just make Mist less vibrant", and the new
# looks Frost, Shadow, Radiance, Spirit) each look: its strips along the edges (width out, width across the face, seconds per
# scroll, repeats per 40 units, gain; along: 'contour' round the outline, 'axis' hilt -> tip on both edges, 'axisdown' tip ->
# hilt), the shell over the steel, its motes
LOOK = {
    # fire that rises: both edges stream from the hilt to the tip, two layers at different speeds
    'flame': dict(strips=[S('fire_rise0', 7.0, 3.8, 0.9, 1.1, 0.8, 'axis'), S('fire_rise1', 5.0, 2.8, 0.55, 1.8, 0.55, 'axis')],
                  shell=Sh('haze', 'edge', 30.0, 0.35, scroll=4.0), motes=('rise', 0.55)),
    # real lightning: whole bolts the length of each stretch of edge, joined end to end, striking for three frames and gone,
    # forks reaching out, a steady charge along the steel; branching strikes over the faces
    'storm': dict(strips=[S('bolt_edge', 4.6, 2.6, 0, 1, 1.35, flip=(8, 4), fps=26), S('stream_strip', 1.6, 1.0, 0.7, 2.0, 0.55)],
                  shell=Sh('bolt_shell', 'all', 24.0, 1.1, flip=(8, 4), fps=22), motes=('spark', 0.35)),
    'mist': dict(strips=[S('mist_strip0', 5.5, 3.0, 6.0, 0.8, 0.34), S('mist_strip1', 4.2, 2.6, 4.2, 1.2, 0.26)],
                 shell=Sh('haze', 'edge', 24.0, 0.3, scroll=6.0), motes=('drift', 0.06)),
    'core': dict(strips=[S('molten_strip', 2.6, 1.4, 3.0, 1.0, 1.1)], shell=Sh('cracks', 'all', 22.0, 1.6, pulse=(2.6, 0.45, 1.15)), motes=('fall', 0.18)),
    'frost': dict(strips=[S('rime_strip', 3.8, 2.0, 24.0, 1.4, 1.4), S('vapour_strip', 6.0, 3.6, 5.0, 0.9, 0.45, 'axisdown')],
                  shell=Sh('frost_shell', 'edge', 18.0, 1.5, pulse=(3.4, 0.75, 1.1)), motes=('fall', 0.22)),
    'shadow': dict(strips=[S('shade_strip', 6.5, 3.6, 3.2, 0.9, 1.0, 'axis', blend='alpha', dark=True), S('stream_strip', 1.4, 0.9, 1.6, 1.0, 0.6)],
                   shell=Sh('haze', 'edge', 26.0, 0.8, scroll=5.0, blend='alpha', dark=True), motes=('rise', 0.15)),
    'radiance': dict(strips=[S('ray_strip', 12.0, 0, 9.0, 0.6, 0.45), S('stream_strip', 1.6, 1.0, 1.2, 1.0, 0.8)],
                     shell=Sh('halo_shell', 'all', 30.0, 0.35, pulse=(4.0, 0.7, 1.1)), motes=('drift', 0.3)),
    'spirit': dict(strips=[S('wisp_strip', 7.5, 4.0, 1.8, 0.7, 0.42, 'axis'), S('wisp_strip', 5.5, 3.0, 1.2, 1.1, 0.28, 'axis')],
                   shell=Sh('haze', 'edge', 26.0, 0.35, scroll=5.0), motes=('drift', 0.2)),
}
# (Noah, 2026-11-25: each look on Skyrim's own - Flame on its fire enchantment, Frost on its frost enchantment, Storm on its shock
# enchantment, Mist on its vapour, Shadow on its dark swirls, Spirit on its magic wisps, Radiance on the light enchantment's
# glow; Core stays our own) Skyrim's enchantment membrane as each look's second skin, scrolling over the steel
FILL = {'flame': Sh('sk_enchfill', 'all', 34.0, 0.32, scroll=3.0), 'storm': Sh('sk_shock', 'all', 20.0, 0.45, scroll=2.2),
        'mist': Sh('sk_vapour', 'band', 28.0, 0.22, scroll=7.0), 'frost': Sh('sk_frost', 'all', 24.0, 0.3, scroll=9.0),
        'shadow': Sh('sk_swirls', 'all', 26.0, 0.55, scroll=6.0, blend='alpha', dark=True), 'radiance': Sh('sk_enchfill', 'all', 30.0, 0.28, scroll=4.0),
        'spirit': Sh('sk_wisps', 'all', 24.0, 0.3, flip=(2, 2), fps=3)}
for _k, _f in FILL.items(): LOOK[_k]['fill'] = _f
LOOKS = list(LOOK)


def art(key):
    Fs = fit(key)
    rows = []
    WORK.mkdir(parents=True, exist_ok=True); MESH.mkdir(parents=True, exist_ok=True)
    rows.append(f'out {MESH / (key + ".nif")}')
    rng = np.random.default_rng(abs(hash(key)) % 2 ** 31)
    stats = []
    for pi, F in enumerate(Fs):
        for li, lk in enumerate(F['look'].split('+')):   # (Nov 27 final: a second look laid over the first)
            F2 = dict(F, look=lk)
            if li: F2['gain'] = F.get('gain', 1.0) * 0.75
            stats.append(art_part(F2, rows, rng, (f'p{pi}_' if len(Fs) > 1 else '') + (f'l{li}_' if li else '')))
    jf = WORK / f'{key}.job'
    jf.write_text('\n'.join(rows) + '\n')
    r = subprocess.run([str(TOOLS / 'fxwrite'), str(jf)], capture_output=True, text=True)
    if r.returncode: raise SystemExit(f'{key}: fxwrite failed: {r.stderr[-400:]}')
    print(f'{key}: {Fs[0]["look"]} {Fs[0]["col"]} - {len(Fs)} part(s), {sum(x[0] for x in stats)} edges, shell {"yes" if all(x[1] for x in stats) else "no"}, {sum(x[2] for x in stats)} motes')
    return Fs


def look_of(key, look):
    """(Noah, 2026-11-25: "Fully new look per weapon") a weapon's look 'o1' / 'o2' / 'o3': its own (tools/weaponlooks.py); the
    old family names still read, for the workshop's older picks"""
    import weaponlooks as WL, weaponsig as WS
    if look in LOOK: return LOOK[look]
    sk = WS.sig_key(key); n = int(look[1:]) - 1
    o = WL.LOOKS3[sk][n]
    # (seen beside the old families, these read dim: the edges a third brighter, the skins over the faces 1.7x)
    strips = [dict(S(e['tex'], e['w'], e['wc'], e['per'], e['rep'], e['gain'] * (1.0 if e['dark'] else 1.35), e['along'], e['flip'], e['fps'], e['blend'], e['dark']), col=e['col']) for e in o['edges']]
    shells = [dict(Sh(f['tex'], f['how'], f['tile'], f['gain'] * (1.0 if f['dark'] else 1.7), f['flip'], f['fps'], f['scroll'], f['pulse'], f['blend'], f['dark']), col=f['col']) for f in o['faces']]
    return dict(strips=strips, shells=shells, motes=('drift', 0.0), c2=WL.COLS2[sk][1])


def art_part(F, rows, rng, pre):
    look, cols = F['look'], F['col'].split('+')
    L = look_of(F['key'], look)
    def colour(which, base):   # (a layer's own colour: the weapon's first (picked), its second, or one named)
        return base if which in (None, 'c1') else L.get('c2', base) if which == 'c2' else which
    small = F['small']
    wsc = 0.45 if small else 1.0   # (a claw's blade: the flames and mist in its own small measure)
    G = F.get('gain', 1.0)         # (a weapon's own intensity: the Carian Wand's softer)

    def emit(name, tex, pal, scale, verts, uvs, tris, cols_=None, flip=None, fps=None, vscroll=0.0, pulse=None, clamp=None, blend='add', dark=False):
        head = f'shape {pre}{name} tex={GT}{tex}.dds color=1,1,1,1 scale={scale * G:.2f} blend={blend} palette={GT}{"pald" if dark else "pal"}_{pal}.dds'
        if flip: head += f' flip={flip[0]}x{flip[1]} fps={fps} phase={rng.random() * 2:.2f}'
        if vscroll: head += f' vscroll={vscroll:.2f}'
        if pulse: head += f' pulse={pulse[0]},{pulse[1]},{pulse[2]}'
        if clamp is not None: head += f' clamp={clamp}'
        rows.append(head)
        for i, (p, t) in enumerate(zip(verts, uvs)):
            a = 1.0 if cols_ is None else cols_[i]
            rows.append(f'v {p[0]:.3f} {p[1]:.3f} {p[2]:.3f} {t[0]:.4f} {t[1]:.4f} 1 1 1 {a:.3f}')
        for t in tris: rows.append(f't {t[0]} {t[1]} {t[2]}')

    # the edges' sheets
    for ei, (pts, nor) in enumerate(F['edges']):
        pal = cols[ei % len(cols)] if len(cols) > 1 else cols[0]
        L2 = np.linalg.norm(np.diff(pts, axis=0), axis=1); s = np.r_[0, np.cumsum(L2)]
        n = len(pts)
        fl = 0.25 if small else 0.6   # (soft over only a hair at each end: the light runs to the guard and the tip)
        fade = np.clip(np.minimum(s, s[-1] - s) / fl, 0, 1)
        if np.linalg.norm(pts[0] - pts[-1]) < 1e-3: fade = np.ones(n)
        # (Noah, 2026-11-25: "no hard cutoffs ... it needs to point on the ends ... in the shape of the weapon") where the outline
        # turns sharply - the tip, a point, a hook - a sheet as wide as the effect folds over itself and ends in a flat cut. Each
        # point's reach is held to the curve's own radius there, eased along the edge, so the light narrows into the point
        tg = np.gradient(pts, axis=0); tg /= np.linalg.norm(tg, axis=1, keepdims=True) + 1e-9
        ang = np.arctan2(tg[:, 1], tg[:, 0]); dang = np.abs(np.angle(np.exp(1j * np.gradient(np.unwrap(ang)))))
        radius = np.minimum((np.gradient(s) + 1e-6) / np.maximum(dang, 1e-4), 200.0)   # (the outline's radius of curvature)
        if not np.linalg.norm(pts[0] - pts[-1]) < 1e-3: radius[0] = radius[-1] = 0.0      # (an open run's ends are points too)
        sd = np.abs(s[:, None] - s[None, :])
        def taper(ww):
            """each point's share of the full width: a sharp turn holds it to the curve's radius, and away from it the width may
            grow only as fast as a blade's point widens (0.45 units out per unit along) - the light comes to a point like the steel"""
            base = np.clip(radius * 1.6 / ww, 0.0, 1.0)
            return np.clip((base[None, :] + sd * 0.45 / ww).min(1), 0.06, 1.0)
        # (a flipbook's cells each cover 16 units of edge: the edge is cut exactly at each cell's end, the point doubled - one
        # closing the cell at v 1, one opening the next at v 0 - so the bolts run on with no gap between cells)
        cpts, cnor, cfade, cv, ccut = [], [], [], [], []
        for i in range(n):
            if i > 0:
                c0, c1 = int(s[i - 1] // 16.0), int(s[i] // 16.0)
                for c in range(c0 + 1, c1 + 1):
                    sb = c * 16.0; f = (sb - s[i - 1]) / max(1e-6, s[i] - s[i - 1])
                    pb = pts[i - 1] + (pts[i] - pts[i - 1]) * f; nb = nor[i - 1] + (nor[i] - nor[i - 1]) * f; nb /= np.linalg.norm(nb) + 1e-9
                    fb = fade[i - 1] + (fade[i] - fade[i - 1]) * f
                    cpts += [pb, pb]; cnor += [nb, nb]; cfade += [fb, fb]; cv += [1.0, 0.0]; ccut += [False, True]
            cpts.append(pts[i]); cnor.append(nor[i]); cfade.append(fade[i]); cv.append((s[i] % 16.0) / 16.0); ccut.append(False)
        cpts = np.array(cpts); cnor = np.array(cnor)
        pal0 = pal
        for li, st in enumerate(L['strips']):
            pal = colour(st.get('col'), pal0)
            flipbook = bool(st['flip'])
            P_, N_, F_ = (cpts, cnor, cfade) if flipbook else (pts, nor, fade)
            m_ = len(P_)
            for mode in ('out', 'cross', 'diag+', 'diag-'):
                ww = (st['w'] if mode in ('out', 'diag+', 'diag-') else st['wc']) * wsc * (0.75 if mode.startswith('diag') else 1.0)
                if ww <= 0 or (mode.startswith('diag') and st['wc'] <= 0): continue
                tap = taper(ww)
                if flipbook: tap = np.interp(np.linspace(0, 1, len(cpts)), np.linspace(0, 1, n), tap)
                if flipbook: vv = cv
                elif st['along'] == 'axis': vv = list(pts[:, 0] / 40.0 * st['rep'] / wsc)
                elif st['along'] == 'axisdown': vv = list(-pts[:, 0] / 40.0 * st['rep'] / wsc)
                else: vv = [s[i] / 40.0 * st['rep'] / wsc for i in range(n)]
                verts, uvs, al, tris = [], [], [], []
                # (the sheet's far border always clear - its last quarter fades to nothing: no line traced round the effect)
                rows_ = (((-0.5 * wsc, 0.0, 1.0), (0.72, 0.72, 1.0), (1.0, 1.0, 0.0)) if mode != 'cross' else
                         ((-1.0, 1.0, 0.0), (-0.72, 0.72, 1.0), (0.0, 0.0, 1.0), (0.72, 0.72, 1.0), (1.0, 1.0, 0.0)))
                k_ = len(rows_)
                c45 = 0.7071
                for i in range(m_):
                    wi = ww * tap[i]
                    for r_, (off, uu, aa) in enumerate(rows_):
                        o = (0.0 if mode.startswith('diag') else off) if (r_ == 0 and mode != 'cross') else off * wi   # (a slanted sheet starts at the edge itself, never through the steel)
                        if mode == 'out': P = to3(F, P_[i] + N_[i] * o, 0.0)[0]
                        elif mode == 'cross': P = to3(F, P_[i], o)[0]
                        else: P = to3(F, P_[i] + N_[i] * o * c45, o * c45 * (1 if mode == 'diag+' else -1))[0]
                        verts.append(P); uvs.append((uu, vv[i])); al.append(F_[i] * aa * (0.55 if mode.startswith('diag') else 1.0))
                for i in range(m_ - 1):
                    if flipbook and ccut[i + 1]: continue   # (the doubled point: the next cell starts here)
                    for r in range(k_ - 1):
                        a, b = i * k_ + r, i * k_ + r + 1; c2, d = a + k_, b + k_
                        tris += [(a, b, d), (a, d, c2)]
                emit(f'e{ei}_{li}_{mode.replace("+", "p").replace("-", "m")}', st['tex'], pal, st['gain'] * (1.0 if mode == 'out' else 0.8), verts, uvs, tris, al,
                     flip=st['flip'], fps=st['fps'], vscroll=0 if flipbook else st['per'], clamp=None if flipbook else 1, blend=st['blend'], dark=st['dark'])
    # the shell over both faces, and Skyrim's own enchantment membrane over it (the look's fill)
    any_tris = False
    shells = L['shells'] if 'shells' in L else [L['shell'], L.get('fill')]
    for si, sh in enumerate(shells):
      if not sh: continue
      tag = 'shell' if si == 0 else f'fill{si}'
      tile = sh['tile'] * (0.5 if small else 1.0); how = sh['how']
      mask, depth, dist, res, lo = F['mask'], F['depth'], F['dist'], F['res'], F['lo']
      step = 4                                     # (a quad every 1.2 units)
      gh, gw = mask.shape
      maxd = max(0.5, dist.max())
      flipbook = bool(sh['flip'])
      tris = []
      for side in (-1, 1):
          verts, uvs, al, tris = [], [], [], []
          for yi in range(0, gh - step, step):
              for xi in range(0, gw - step, step):
                  cs = [(yi, xi), (yi, xi + step), (yi + step, xi + step), (yi + step, xi)]
                  if not all(mask[a, b] for a, b in cs): continue
                  base = len(verts)
                  for (a, b) in cs:
                      p2 = np.array([lo[0] + b * res, lo[1] + a * res])
                      dz = side * (depth[a, b] + 0.12)
                      verts.append(to3(F, p2, dz)[0])
                      if flipbook: tu, tv = (p2[1] % tile) / tile, (p2[0] % tile) / tile   # (each quad its own cell's corner)
                      else: tu, tv = p2[1] / tile, p2[0] / tile
                      uvs.append((tu, tv))
                      e = 1.0 - min(1.0, dist[a, b] / maxd)    # (0 the spine, 1 the edge)
                      w_ = {'edge': e ** 2.5, 'band': max(0.0, 1.0 - e * 1.6) if maxd > 1.2 else 0.0, 'all': 1.0}[how]
                      # (Noah, 2026-11-24: "remove the lines ... it almost looks like a sketch") the shell melts away before the edge
                      w_ *= smooth(0.0, 1.0, (dist[a, b] - res * step * 0.5) / (1.0 if small else 1.8))
                      al.append(float(w_))
                  if flipbook and (max(u for u, _ in uvs[base:]) - min(u for u, _ in uvs[base:]) > 0.5 or max(v for _, v in uvs[base:]) - min(v for _, v in uvs[base:]) > 0.5):
                      del verts[base:], uvs[base:], al[base:]; continue
                  tris += [(base, base + 1, base + 2), (base, base + 2, base + 3)]
          if tris:
              any_tris = True
              emit(f'{tag}{side + 1}', sh['tex'], colour(sh.get('col'), cols[0]), sh['gain'], verts, uvs, tris, al, flip=sh['flip'], fps=sh['fps'],
                   vscroll=0 if flipbook else sh['scroll'], pulse=sh['pulse'], blend=sh['blend'], dark=sh['dark'])
    # (Noah, 2026-11-25: "not changing their model or adding something on top") no pieces laid over the steel: tools/weaponsig.py's
    # signatures retired, each weapon's own looks instead (tools/weaponlooks.py)
    # motes: a few billboard sparks along the edges (fxwrite cards)
    kind, dens = L['motes']
    tris = any_tris
    if not F['edges']: return (0, bool(tris), 0)
    total = sum(np.sum(np.linalg.norm(np.diff(p, axis=0), axis=1)) for p, _ in F['edges'])
    nm = 0   # (Noah, 2026-11-25: the motes stood still beside the blade - "a weird cone of stagnant stars": none)
    allp = np.concatenate([p for p, _ in F['edges']]); alln = np.concatenate([n for _, n in F['edges']])
    for m in range(nm):
        i = rng.integers(len(allp))
        off = {'rise': 1.5, 'spark': 1.2, 'drift': 3.0, 'fall': 0.8}[kind] * wsc
        p3 = to3(F, allp[i] + alln[i] * off, 0.0)[0]
        sz = {'rise': 0.7, 'spark': 0.5, 'drift': 1.1, 'fall': 0.6}[kind] * (0.6 if small else 1.0)
        rows.append(f'shape {pre}m{m} tex={GT}mote.dds color=1,1,1,1 scale={1.6 * G:.2f} blend=add billboard=1 palette={GT}pal_{cols[0]}.dds '
                    f'pulse={0.6 + rng.random() * 1.4:.2f},0.0,1.0 at={p3[0]:.2f},{p3[1]:.2f},{p3[2]:.2f}')
        for (x, y, u, v) in ((-sz, -sz, 0, 1), (sz, -sz, 1, 1), (sz, sz, 1, 0), (-sz, sz, 0, 0)):
            rows.append(f'v {x:.3f} {y:.3f} 0 {u} {v} 1 1 1 1')
        rows += ['t 0 1 2', 't 0 2 3']
    return (len(F['edges']), bool(tris), nm)


def preview(key):
    import cv2
    ims = []
    for F in fit(key):
        mask = F['mask']
        im = (np.dstack([mask * 0.35] * 3) * 255).astype(np.uint8).copy()
        res, lo = F['res'], F['lo']
        for pts, nor in F['edges']:
            P = ((pts - lo) / res).astype(int)
            for a, b in zip(P[:-1], P[1:]): cv2.line(im, tuple(map(int, a)), tuple(map(int, b)), (255, 140, 40), 1)
            for p, n in zip(P[::4], nor[::4]): cv2.line(im, tuple(map(int, p)), tuple(map(int, p + n * 8)), (80, 200, 255), 1)
        ims.append(im[::-1])
    h = max(i.shape[0] for i in ims)
    im = np.hstack([np.pad(i, ((0, h - i.shape[0]), (0, 6), (0, 0))) for i in ims])
    out = WORK / f'{key}_fit.png'; WORK.mkdir(parents=True, exist_ok=True)
    Image.fromarray(im).save(out)
    print(out)


if __name__ == '__main__':
    cmd = sys.argv[1] if len(sys.argv) > 1 else ''
    if cmd == 'textures': textures()
    elif cmd == 'arts':
        for k in (sys.argv[2:] or list(ALL)): art(k)
        if not sys.argv[2:]:   # (the class weapons' arts by model and look key, for src/WeaponFx.cpp: tools/gen.py reads it)
            rows = [{'model': v[0].lower(), 'key': BV_KEYS.index(v[5]), 'art': 'CurseOfAlduin\\wfx\\' + k + '.nif'} for k, v in CLASS.items() if (MESH / (k + '.nif')).exists()]   # (only the arts made)
            # (the claws' left-hand twins: found by the model in the left hand, key -2)
            rows += [{'model': WEAPONS[k][0].lower(), 'key': -2, 'art': 'CurseOfAlduin\\wfx\\' + k + '.nif'} for k in sorted(LEFT)]
            (ROOT / 'data' / 'weaponfx_class.json').write_text(json.dumps(rows, indent=1) + '\n')
    elif cmd == 'rows':   # (the class arts' table alone, from the arts already made)
        rows = [{'model': v[0].lower(), 'key': BV_KEYS.index(v[5]), 'art': 'CurseOfAlduin\\wfx\\' + k + '.nif'} for k, v in CLASS.items() if (MESH / (k + '.nif')).exists()]
        rows += [{'model': WEAPONS[k][0].lower(), 'key': -2, 'art': 'CurseOfAlduin\\wfx\\' + k + '.nif'} for k in sorted(LEFT)]
        (ROOT / 'data' / 'weaponfx_class.json').write_text(json.dumps(rows, indent=1) + '\n')
        print(len(rows), 'rows')
    elif cmd == 'preview':
        for k in sys.argv[2:]: preview(k)
    else: print(__doc__)
