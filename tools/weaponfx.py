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

LOOKS = ['flame', 'runic', 'storm', 'mist', 'core']
# the colours (as the page's): base colour; Eclipse Edge wears two, one edge each
COL = {
    'white': '#eaf1ff', 'blue': '#4f8bff', 'iceblue': '#8fd8ff', 'orange': '#ff7a1f', 'red': '#e0262b', 'green': '#48d058',
    'poison': '#9be03a', 'drain': '#6e5bff', 'purple': '#a54bff', 'moon': '#c9d4e8', 'fear': '#c0182a', 'gold': '#ffc54a',
    'ss_none': '#ff4a32', 'arcane': '#a8c4ff', 'shockv': '#9f8cff', 'shadow': '#9a3bff', 'carian': '#4682ff',
}
# each weapon: its model, its steel (shape names; '*' any lit shape), the height band that is steel (y from, to), its look and
# colour (Noah's picks, the Curse Weapon Effects page, 2026-11-24)
WEAPONS = {
    'priest_vahlok':     ('CurseOfAlduin\\priests\\vahlok_keyhammer.nif', ['Nordic:0', 'Iron:1'], (27, 99), 'runic', 'white'),
    'priest_nahkriin':   ('CurseOfAlduin\\priests\\nahkriin_mistblade.nif', ['BladeBody'], None, 'mist', 'white'),
    'priest_volsung':    ('CurseOfAlduin\\priests\\volsung_windcutter.nif', ['Blade:1'], None, 'mist', 'blue'),
    'priest_zahkriisos': ('CurseOfAlduin\\priests\\zahkriisos_greatsword.nif', ['Ice:1'], None, 'core', 'iceblue'),
    'priest_otar':       ('CurseOfAlduin\\priests\\otar_stormblade.nif', ['Blade:2'], None, 'storm', 'blue'),
    'priest_rahgot':     ('CurseOfAlduin\\priests\\rahgot_dagger.nif', ['Obsidian:1'], None, 'runic', 'poison'),
    'priest_krosis':     ('CurseOfAlduin\\priests\\krosis_griefblade.nif', ['Blade:0'], None, 'flame', 'red'),
    'priest_hevnoraak':  ('CurseOfAlduin\\priests\\hevnoraak_sealbreaker.nif', ['Stone:3'], None, 'core', 'green'),
    'priest_vokun':      ('CurseOfAlduin\\scythes\\daedric.nif', ['daedricsword01'], None, 'mist', 'purple'),
    'priest_morokei':    ('CurseOfAlduin\\priests\\morokei_maul.nif', ['Steel:0', 'Crystal:2'], (40, 99), 'flame', 'drain'),
    'tab_stormtalons':   ('weapons\\NewArmoury\\Fists\\glassClaw.nif', ['GlassClawGlass1'], None, 'storm', 'blue'),
    'tab_gravebell':     ('CurseOfAlduin\\tablet\\gravebell.nif', ['Bronze:3'], None, 'mist', 'white'),
    'tab_talons':        ('weapons\\NewArmoury\\Fists\\DBClaw.nif', ['DBClaw_MTL_B'], None, 'flame', 'red'),
    'tab_riverstone':    ('weapons\\NewArmoury\\Halberds\\NordicHalberd.nif', ['NordicPikeShaft', 'NordicPikeTip'], (70, 999), 'flame', 'green'),
    'tab_wyrmspine':     ('weapons\\NewArmoury\\Pikes\\DragonbonePike.nif', ['DragonbonePikeTip'], None, 'flame', 'fear'),
    'tab_lanternwick':   ('weapons\\NewArmoury\\QuarterStaffs\\StalhrimStaff.nif', ['StahlrimStaffBlade'], [(42, 999), (-999, -30)], 'mist', 'iceblue'),
    'tab_cinderclaws':   ('weapons\\NewArmoury\\Fists\\DaedricClaw.nif', ['DaedricClaws_MTL_B'], None, 'flame', 'orange'),
    'tab_eclipse':       ('weapons\\NewArmoury\\Pikes\\GlassPike.nif', ['GlassPikeTip'], None, 'flame', 'gold+iceblue'),
    'tab_frostmourn':    ('weapons\\NewArmoury\\Rapier\\StahlrimRapier.nif', ['StahlrimRapier:18'], (9.5, 999), 'mist', 'iceblue'),
    'tab_silverscythe':  ('CurseOfAlduin\\scythes\\silver.nif', ['SilverGreatSword:0_2'], None, 'runic', 'moon'),
    'tab_bonereaper':    ('CurseOfAlduin\\scythes\\Skeleton.nif', ['IronLongSword01:0_3'], None, 'mist', 'white'),
    'tab_wyrmtooth':     ('CurseOfAlduin\\scythes\\dragonTeeth.nif', ['DragonBone01'], (26, 999), 'flame', 'orange'),
    'tab_bloodmoon':     ('CurseOfAlduin\\scythes\\vampire.nif', ['blade'], None, 'flame', 'red'),
    'fang':              ('Light Greatswords\\dragonbone\\LightGreatsword.nif', ['DragonboneGreatsword:0'], None, 'flame', 'orange'),
    # (Noah, 2026-11-24: "both left and right claws ... carry the same effect") the claws' left-hand twins
    'tab_stormtalons_l': ('weapons\\NewArmoury\\Fists\\GlassClawLft.nif', ['GlassClawGlass1'], None, 'storm', 'blue'),
    'tab_talons_l':      ('weapons\\NewArmoury\\Fists\\DBClawLft.nif', ['DBClaw_MTL_B'], None, 'flame', 'red'),
    'tab_cinderclaws_l': ('weapons\\NewArmoury\\Fists\\DaedricClawLft.nif', ['DaedricClaws_MTL_B'], None, 'flame', 'orange'),
}
# (the claws: several blades fanned out - each its own outline in its own plane)
SPLIT = {'tab_stormtalons', 'tab_talons', 'tab_cinderclaws', 'tab_stormtalons_l', 'tab_talons_l', 'tab_cinderclaws_l'}
# the left-hand twins' models (src/WeaponFx.cpp finds them by the model in the left hand)
LEFT = {'tab_stormtalons_l', 'tab_talons_l', 'tab_cinderclaws_l'}


# the class weapons: each takes another model at each tier (src/Classes.cpp AdoptTierLook), so each tier's model has its own
# art; the Spellbound Rapier's look follows the element in your other hand (its BLADE_VIZ key: src/Classes.cpp LookKey)
BV_KEYS = ['alch', 'carian', 'radiant', 'ss_none', 'ss_fire', 'ss_frost', 'ss_shock', 'ss_sun', 'ss_blood', 'slayer']
BV_PICK = {'alch': ('flame', 'shadow'), 'carian': ('mist', 'carian'), 'radiant': ('mist', 'white'), 'ss_none': ('runic', 'ss_none'),
           'ss_fire': ('flame', 'orange'), 'ss_frost': ('mist', 'iceblue'), 'ss_shock': ('storm', 'shockv'), 'ss_sun': ('flame', 'gold'),
           'ss_blood': ('runic', 'arcane'), 'slayer': ('flame', 'gold')}
CLASS_MODELS = {   # the look key(s), each model: its steel shapes and band
    'radiant': [('weapons\\NewArmoury\\QuarterStaffs\\SteelStaff.nif', ['SteelPikeShaft'], [(32, 99), (-99, -26)]),
                ('weapons\\NewArmoury\\QuarterStaffs\\EbonyStaff.nif', ['EbonyStaffShaft'], [(32, 99), (-99, -26)]),
                ('weapons\\NewArmoury\\QuarterStaffs\\DaedricStaff.nif', ['DaedricStaffShaft'], [(32, 99), (-99, -26)]),
                ('weapons\\NewArmoury\\QuarterStaffs\\DragonBoneStaff.nif', ['DragonboneStaffBlade'], [(30, 99), (-99, -26)])],
    'carian': [('SSS\\SteelWand.nif', ['1stPersonSteelWarhamm', '1stPersonSteelBattleA'], (2, 99)),
               ('SSS\\StahlrimWand.nif', ['2H_Axe:0'], (12, 99)),
               ('SSS\\GlassWand.nif', ['1stPersonGlassMace.01', '1stPersonGlassWarH.00', '1stPersonGlassMace:0.00'], (8, 99)),
               ('SSS\\EbonyWand.nif', ['1stPersonEbonyWarhamm', '1stPersonEbonyMace.00'], (14, 99)),
               ('SSS\\DaedricWand.nif', ['DaedricWarhammer:0'], (8, 99))],
    'alch': [('CurseOfAlduin\\scythes\\forsworn.nif', ['ForswornSword:0'], None),
             ('CurseOfAlduin\\scythes\\falmer.nif', ['BattleAxe:0'], None),
             ('CurseOfAlduin\\scythes\\dwarven.nif', ['DwarvenGreatsword:1_2_2'], None),
             ('CurseOfAlduin\\scythes\\glass.nif', ['1stPersonGlassGreatSword:1'], None),
             ('CurseOfAlduin\\scythes\\dragonpriest.nif', ['1stPersonEbonyDagger:1'], None)],
    'ss': [('weapons\\NewArmoury\\Rapier\\SteelRapier.nif', ['1stPersonSteelRapier:22'], (13, 99)),
           ('weapons\\NewArmoury\\Rapier\\EbonyRapier.nif', ['1stPersonEbonyRapier:22'], (20, 99)),
           ('weapons\\NewArmoury\\Rapier\\DaedricRapier.nif', ['1stPersonDaedricRapier:29'], (14, 99)),
           ('weapons\\NewArmoury\\Rapier\\DragonboneRapier.nif', ['1stPersonDragonboneRapier:'], (8.8, 99))],
    'slayer': [('CurseOfAlduin\\slayer\\dragonslayer.nif', ['Dbone:1'], None)],
}
CLASS = {}   # art key -> (model, shapes, band, look, colour, the look key, the model)
for group, models in CLASS_MODELS.items():
    keys = [k for k in BV_KEYS if k.startswith('ss_')] if group == 'ss' else [group]
    for model, names, band in models:
        stem = model.split('\\')[-1][:-4].lower()
        for k in keys:
            look, col = BV_PICK[k]
            CLASS[f'cls_{stem}_{k}'] = (model, names, band, look, col, k)
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


def save_tex(rgba, name):
    """RGBA float [0,1] (rows top to bottom) -> DXT5 DDS with mips"""
    import assets
    TEX.mkdir(parents=True, exist_ok=True)
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
        if not any(s['name'] == n or s['name'].startswith(n + ':') or (n.endswith(':') and s['name'].startswith(n)) for n in names): continue
        if not len(s['t']): continue
        base = sum(len(x) for x in V)
        V.append(s['v']); T.append(s['t'] + base)
    if not V: raise SystemExit(f'{key}: no steel shapes found among {[s["name"] for s in shapes]}')
    V = np.concatenate(V); T = np.concatenate(T)
    if band:
        bands = band if isinstance(band, list) else [band]
        y = V[T][:, :, 1].mean(1)   # (by each triangle's middle: a long thin one is kept if it is mostly in)
        ok = np.zeros(len(T), bool)
        for b0, b1 in bands: ok |= (y >= b0) & (y <= b1)
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
    return dict(key=key, look=look, col=col, c=c, e1=e1, e2=e2, e3=e3, lo=lo, res=res, mask=mask, depth=depth, dist=dist,
                edges=edges, grip=grip, A=A, rA=rA, span=span, small=span0 <= 30)


def to3(F, p2, d=0.0):
    """points in the steel's plane (N x 2) lifted d off it (scalar or N) - back into the model's space"""
    p2 = np.atleast_2d(p2); d = np.broadcast_to(np.asarray(d, float), (len(p2),))
    return F['c'] + F['e1'] * p2[:, :1] + F['e2'] * p2[:, 1:2] + F['e3'] * d[:, None]


# ---------------------------------------------------------------- the art model
LOOK = {   # strips: (texture, width, cross width, scroll seconds per turn, v repeat per 40 units, gain); shell: (texture, how lit, tile)
    'flame': dict(strips=[('flame_strip0', 6.5, 3.4, 1.15, 1.0, 1.1), ('flame_strip1', 5.0, 2.6, 0.8, 1.6, 0.8)], shell=('haze', 'edge', 30.0, 1.2), motes=('rise', 0.45)),
    'runic': dict(strips=[('stream_strip', 2.2, 1.2, 1.4, 1.0, 1.1)], shell=('runes', 'band', 26.0, 1.4), motes=('rise', 0.1)),
    'storm': dict(strips=[('storm_edge', 4.8, 2.8, 0, 2.5, 1.3)], shell=('storm_shell', 'all', 26.0, 1.2), motes=('spark', 0.3)),
    'mist':  dict(strips=[('mist_strip0', 8.0, 4.6, 6.0, 0.8, 0.9), ('mist_strip1', 6.0, 3.6, 4.2, 1.2, 0.7)], shell=('haze', 'edge', 24.0, 0.8), motes=('drift', 0.1)),
    'core':  dict(strips=[('molten_strip', 2.6, 1.4, 3.0, 1.0, 1.1)], shell=('cracks', 'all', 22.0, 1.6), motes=('fall', 0.18)),
}


def art(key):
    Fs = fit(key)
    rows = []
    WORK.mkdir(parents=True, exist_ok=True); MESH.mkdir(parents=True, exist_ok=True)
    rows.append(f'out {MESH / (key + ".nif")}')
    rng = np.random.default_rng(abs(hash(key)) % 2 ** 31)
    stats = [art_part(F, rows, rng, f'p{pi}_' if len(Fs) > 1 else '') for pi, F in enumerate(Fs)]
    jf = WORK / f'{key}.job'
    jf.write_text('\n'.join(rows) + '\n')
    r = subprocess.run([str(TOOLS / 'fxwrite'), str(jf)], capture_output=True, text=True)
    if r.returncode: raise SystemExit(f'{key}: fxwrite failed: {r.stderr[-400:]}')
    print(f'{key}: {Fs[0]["look"]} {Fs[0]["col"]} - {len(Fs)} part(s), {sum(x[0] for x in stats)} edges, shell {"yes" if all(x[1] for x in stats) else "no"}, {sum(x[2] for x in stats)} motes')
    return Fs


def art_part(F, rows, rng, pre):
    look, cols = F['look'], F['col'].split('+')
    L = LOOK[look]
    small = F['small']
    wsc = 0.45 if small else 1.0   # (a claw's blade: the flames and mist in its own small measure)

    def emit(name, tex, pal, scale, verts, uvs, tris, cols_=None, flip=None, fps=None, vscroll=0.0, pulse=None, clamp=None):
        head = f'shape {pre}{name} tex={GT}{tex}.dds color=1,1,1,1 scale={scale:.2f} blend=add palette={GT}pal_{pal}.dds'
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
        # (Noah: "the entire blade length") soft only over the last unit or so at each end of a run
        fl = 0.25 if small else 0.6   # (soft over only a hair at each end: the light runs to the guard and the tip)
        fade = np.clip(np.minimum(s, s[-1] - s) / fl, 0, 1) if s[-1] < 1e9 else np.ones(n)
        closed = np.linalg.norm(pts[0] - pts[-1]) < 1e-3
        if closed: fade = np.ones(n)
        for li, (tex, w, wc, per, rep, gain) in enumerate(L['strips']):
            flipbook = tex == 'storm_edge'
            for mode in ('out', 'cross'):
                ww = (w if mode == 'out' else wc) * wsc
                vv = [(s[i] % 16.0) / 16.0 if flipbook else s[i] / 40.0 * rep / wsc for i in range(n)]
                verts, uvs, al, tris = [], [], [], []
                # (the sheet's far border always clear - its last quarter fades to nothing: no line traced round the effect)
                rows_ = (((-0.5 * wsc, 0.0, 1.0), (ww * 0.72, 0.72, 1.0), (ww, 1.0, 0.0)) if mode == 'out' else
                         ((-ww, 1.0, 0.0), (-ww * 0.72, 0.72, 1.0), (0.0, 0.0, 1.0), (ww * 0.72, 0.72, 1.0), (ww, 1.0, 0.0)))
                k_ = len(rows_)
                for i in range(n):
                    for off, uu, aa in rows_:
                        if mode == 'out': P = to3(F, pts[i] + nor[i] * off, 0.0)[0]
                        else: P = to3(F, pts[i], off)[0]
                        verts.append(P); uvs.append((uu, vv[i]))
                        al.append(fade[i] * aa)
                for i in range(n - 1):
                    if flipbook and vv[i + 1] < vv[i]: continue   # (a new cell starts: no quad across the seam)
                    for r in range(k_ - 1):
                        a, b = i * k_ + r, i * k_ + r + 1; c2, d = a + k_, b + k_
                        tris += [(a, b, d), (a, d, c2)]
                emit(f'e{ei}_{li}_{mode}', tex, pal, gain * (1.0 if mode == 'out' else 0.8), verts, uvs, tris, al,
                     flip=(8, 4) if flipbook else None, fps=28 if flipbook else None, vscroll=0 if flipbook else per,
                     clamp=None if flipbook else 1)
    # the shell over both faces
    tex, how, tile, gain = L['shell']
    if small: tile *= 0.5
    mask, depth, dist, res, lo = F['mask'], F['depth'], F['dist'], F['res'], F['lo']
    step = 4                                     # (a quad every 1.2 units)
    gh, gw = mask.shape
    maxd = max(0.5, dist.max())
    flipbook = tex == 'storm_shell'
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
                    if flipbook:   # (each quad its own cell's corner: the strike fills the quad's own tile)
                        tu, tv = (p2[1] % tile) / tile, (p2[0] % tile) / tile
                    else:
                        tu, tv = p2[1] / tile, p2[0] / tile
                    uvs.append((tu, tv))
                    e = 1.0 - min(1.0, dist[a, b] / maxd)    # (0 the spine, 1 the edge)
                    w_ = {'edge': e ** 2.5, 'band': max(0.0, 1.0 - e * 1.6) if maxd > 1.2 else 0.0, 'all': 1.0}[how]
                    # (Noah, 2026-11-24: "remove the lines ... it almost looks like a sketch") the shell melts away before the
                    # steel's own edge - no stepped border of light laid round the blade
                    w_ *= smooth(0.0, 1.0, (dist[a, b] - res * step * 0.5) / (1.0 if small else 1.8))
                    al.append(float(w_))
                if flipbook and (max(u for u, _ in uvs[base:]) - min(u for u, _ in uvs[base:]) > 0.5 or max(v for _, v in uvs[base:]) - min(v for _, v in uvs[base:]) > 0.5):
                    del verts[base:], uvs[base:], al[base:]; continue
                tris += [(base, base + 1, base + 2), (base, base + 2, base + 3)]
        if tris:
            emit(f'shell{side + 1}', tex, cols[0], gain, verts, uvs, tris, al, flip=(8, 4) if flipbook else None, fps=24 if flipbook else None,
                 vscroll=0 if flipbook else (6.0 if how == 'edge' else 0), pulse=(2.6, 0.45, 1.15) if look in ('runic', 'core') else None)
    # motes: a few billboard sparks along the edges (fxwrite cards)
    kind, dens = L['motes']
    if not F['edges']: return (0, bool(tris), 0)
    total = sum(np.sum(np.linalg.norm(np.diff(p, axis=0), axis=1)) for p, _ in F['edges'])
    nm = int(min(14, max(2 if small else 3, total * dens / 6)))
    allp = np.concatenate([p for p, _ in F['edges']]); alln = np.concatenate([n for _, n in F['edges']])
    for m in range(nm):
        i = rng.integers(len(allp))
        off = {'rise': 1.5, 'spark': 1.2, 'drift': 3.0, 'fall': 0.8}[kind] * wsc
        p3 = to3(F, allp[i] + alln[i] * off, 0.0)[0]
        sz = {'rise': 0.7, 'spark': 0.5, 'drift': 1.1, 'fall': 0.6}[kind] * (0.6 if small else 1.0)
        rows.append(f'shape {pre}m{m} tex={GT}mote.dds color=1,1,1,1 scale=1.6 blend=add billboard=1 palette={GT}pal_{cols[0]}.dds '
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
            rows = [{'model': v[0].lower(), 'key': BV_KEYS.index(v[5]), 'art': 'CurseOfAlduin\\wfx\\' + k + '.nif'} for k, v in CLASS.items()]
            # (the claws' left-hand twins: found by the model in the left hand, key -2)
            rows += [{'model': WEAPONS[k][0].lower(), 'key': -2, 'art': 'CurseOfAlduin\\wfx\\' + k + '.nif'} for k in sorted(LEFT)]
            (ROOT / 'data' / 'weaponfx_class.json').write_text(json.dumps(rows, indent=1) + '\n')
    elif cmd == 'preview':
        for k in sys.argv[2:]: preview(k)
    else: print(__doc__)
