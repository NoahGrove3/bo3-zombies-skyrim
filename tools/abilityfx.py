"""Every ability's own effect, the same in the game and in the workshop (Noah, 2026-11-25: "One custom effect each" - our own
effect for each ability, made from Skyrim's textures, replacing what plays now; "Same effect in doc and game").

An ability's effect is a handful of ELEMENTS - simple shapes (a flash card, a ball, a ring on the ground, a wall, a column of
light, spikes, a spiral, a crescent, smoke cards, falling bolts) each wearing one of Skyrim's own effect textures (its fire
atlas, ice wisps, shock bolts, caustics, vapour, swirls, the enchantment fill...; read from Noah's install, brought to 2K) -
each grown from one size to another and faded over its life by the game (src/Gun.cpp Grow), exactly as the workshop plays it.

The style Noah picked for an ability in the workshop (burst, shockwave, drain, bind ...) is its TEMPLATE (data: STYLES below);
the ability's own textures, colours, size and timing make it its own (ABIL below; docs/workshop/abilities.json for the page).

    python3 tools/abilityfx.py              -> assets/fx/Meshes/CurseOfAlduin/fx/a/*.nif, Textures/CurseOfAlduin/fxa/*.dds,
                                               the own-effects block of fxmap.ini, docs/workshop/abilities.json, build/preview/abil/
"""
import json, math, pathlib, subprocess, sys
import numpy as np
from PIL import Image
HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent
sys.path.insert(0, str(HERE))
MESH = ROOT / 'assets' / 'fx' / 'Meshes' / 'CurseOfAlduin' / 'fx' / 'a'
TEXD = ROOT / 'assets' / 'fx' / 'Textures' / 'CurseOfAlduin' / 'fxa'
WORK = ROOT / 'build' / 'fxa'
PREV = ROOT / 'build' / 'preview' / 'abil'
GT = 'CurseOfAlduin\\fxa\\'
SKFX = pathlib.Path('/mnt/user-data/uploads/un_tmp/skfx/textures/effects')
TOOLS = pathlib.Path('/home/claude/tools')

# ---------------------------------------------------------------- the textures (Skyrim's own, read from Noah's install, 2K)
# name: (source, flipbook cols x rows or None, how its grey/alpha are read)
TEX = {
    'fire':    ('fxfireatlas02', (4, 2)),    # Skyrim's fire atlas (its flames, eight)
    'ice':     ('magicicewisps', (2, 2)),    # its frost wisps
    'bolt':    ('shockparticles02', (2, 4)), # its shock bolts
    'caustic': ('magiccaustic01', (2, 2)),   # its absorb caustics
    'squig':   ('magicsquiggles01', (2, 2)), # its magic squiggles
    'wisps':   ('magicwisps01', (8, 8)),     # its magic wisps
    'sparks':  ('fxglowysparks', (4, 4)),    # its glowing sparks
    'swirl':   ('darkswirls', None),         # its dark swirls (tiles)
    'vapour':  ('vaportile01', None),        # its vapour (tiles)
    'fill':    ('enchflameproject01', None), # its enchantment fill (tiles)
    'frostc':  ('cloudtilefrostsmall', None),# its frost cloud (tiles)
    'shock':   ('shocktile01', None),        # its shock web (tiles)
}


def textures():
    import weaponfx as W
    TEXD.mkdir(parents=True, exist_ok=True); WORK.mkdir(parents=True, exist_ok=True)
    for name, (src, grid) in TEX.items():
        im = np.asarray(Image.open(SKFX / f'{src}.dds').convert('RGBA')).astype(float) / 255
        lum = im[..., :3].max(2); a = im[..., 3]
        if a.std() < 0.02: a = lum                       # (no alpha of its own: its brightness is its body)
        lum = lum / max(1e-3, np.percentile(lum, 99.5))
        rgba = np.dstack([np.clip(lum, 0, 1)] * 3 + [np.clip(a, 0, 1)])
        if grid is None and name in ('vapour', 'frostc'): rgba[..., 3] = W.smooth(0.25, 0.95, lum)
        _save(W, rgba, name, detail=grid is None)
    # ours, where Skyrim has none: a soft glow, a band for rings and walls, a rune circle
    S = 512; yy, xx = np.mgrid[0:S, 0:S].astype(float); R = np.hypot(xx - S / 2 + 0.5, yy - S / 2 + 0.5) / (S / 2)
    _save(W, np.dstack([np.clip(1.1 - R, 0, 1)] * 3 + [np.exp(-(R * 2.2) ** 2)]), 'glow', detail=False)
    u = np.linspace(0, 1, 256)[None, :].repeat(512, 0); v = np.linspace(0, 1, 512)[:, None].repeat(256, 1)
    n = W.fbm(512, 256, 8, 2, 4242)
    band = np.exp(-((u - 0.5) / 0.18) ** 2) * (0.55 + 0.45 * n) + np.exp(-((u - 0.5) / 0.05) ** 2) * 0.6
    _save(W, np.dstack([np.clip(0.4 + 0.6 * band, 0, 1)] * 3 + [np.clip(band, 0, 1)]), 'band')
    TH = np.arctan2(yy - S / 2, xx - S / 2)
    ring = np.exp(-((R - 0.86) / 0.03) ** 2) + np.exp(-((R - 0.66) / 0.02) ** 2)
    glyph = (np.abs(np.sin(TH * 16)) > 0.8) * W.smooth(0.69, 0.71, R) * W.smooth(0.83, 0.81, R)
    spokes = np.exp(-((np.abs(np.sin(TH * 3)) * R) / 0.02) ** 2) * W.smooth(0.62, 0.5, R) * 0.5
    rc = np.clip(ring + glyph + spokes, 0, 1) * W.smooth(1.0, 0.93, R)
    _save(W, np.dstack([0.5 + 0.5 * rc] * 3 + [rc]), 'runes', detail=False)


def _save(W, rgba, name, detail=True):
    import assets
    big = W.enhance(rgba, 2048, detail, sum(map(ord, name)))
    im = Image.fromarray((np.clip(big, 0, 1) * 255).astype(np.uint8), 'RGBA')
    assets.write_dds_mips(im, TEXD / f'{name}.dds', 'DXT5')
    small = im.copy(); small.thumbnail((1024, 1024), Image.LANCZOS); small.save(WORK / f'{name}.png')


# ---------------------------------------------------------------- the shapes (local space; 50 units its measure, z up)
def geo(kind, rng):
    """vertices (x, y, z, u, v, a), triangles, and how it faces: 'bb' a camera-facing card, else fixed"""
    V, T = [], []
    def quad(p0, p1, p2, p3, uv=((0, 1), (1, 1), (1, 0), (0, 0)), a=(1, 1, 1, 1)):
        b = len(V)
        for p, (u, v), aa in zip((p0, p1, p2, p3), uv, a): V.append((*p, u, v, aa))
        T.extend([(b, b + 1, b + 2), (b, b + 2, b + 3)])
    if kind == 'flash':
        quad((-50, 0, -50), (50, 0, -50), (50, 0, 50), (-50, 0, 50))
        return V, T, 'bb'
    if kind == 'sphere':
        nu, nv = 18, 12
        for j in range(nv + 1):
            ph = math.pi * j / nv
            for i in range(nu + 1):
                th = 2 * math.pi * i / nu
                V.append((50 * math.sin(ph) * math.cos(th), 50 * math.sin(ph) * math.sin(th), 50 * math.cos(ph), i / nu * 2, j / nv, 1.0))
        for j in range(nv):
            for i in range(nu):
                a = j * (nu + 1) + i; b = a + nu + 1
                T.extend([(a, b, a + 1), (a + 1, b, b + 1)])
        return V, T, None
    if kind in ('ring', 'disc'):
        n = 48; r0 = 30 if kind == 'ring' else 0
        if kind == 'disc':   # (round: rings of vertices, clear at its rim; u, v laid flat across it)
            nr = 6
            for j in range(nr + 1):
                r = 50 * j / nr
                for i in range(n + 1):
                    th = 2 * math.pi * i / n
                    x, y = r * math.cos(th), r * math.sin(th)
                    V.append((x, y, 1, 0.5 + x / 100, 0.5 + y / 100, 1.0 if j < nr - 1 else (0.5 if j < nr else 0.0)))
            for j in range(nr):
                for i in range(n):
                    a = j * (n + 1) + i; T.extend([(a, a + n + 1, a + 1), (a + 1, a + n + 1, a + n + 2)])
            return V, T, None
        for i in range(n + 1):
            th = 2 * math.pi * i / n
            for r, u, a in ((r0, 0.0, 0.0), (40, 0.5, 1.0), (50, 1.0, 0.0)):
                V.append((r * math.cos(th), r * math.sin(th), 2, u, i / n * 4, a))
        for i in range(n):
            for k in range(2):
                a = i * 3 + k; T.extend([(a, a + 3, a + 1), (a + 1, a + 3, a + 4)])
        return V, T, None
    if kind in ('wall', 'column', 'spiral'):
        n = 40
        if kind == 'spiral':
            m = 90
            for j in range(m + 1):
                t = j / m; th = t * 2 * math.pi * 3; r = 18 + 32 * t; z = t * 200
                for o, u, a in ((-10, 1.0, 0.0), (0, 0.5, 1.0), (10, 0.0, 0.0)):
                    V.append((r * math.cos(th), r * math.sin(th), z + o, u, t * 3, a))
            for j in range(m):
                for k in range(2):
                    a = j * 3 + k; T.extend([(a, a + 3, a + 1), (a + 1, a + 3, a + 4)])
            return V, T, None
        r, h = (50, 40) if kind == 'wall' else (14, 400)
        for i in range(n + 1):
            th = 2 * math.pi * i / n
            for z, u, a in ((0, 0.0, 1.0), (h * 0.6, 0.6, 0.8), (h, 1.0, 0.0)):
                V.append((r * math.cos(th), r * math.sin(th), z, i / n * (3 if kind == 'wall' else 1), u * (1 if kind == 'wall' else 3), a))
        for i in range(n):
            for k in range(2):
                a = i * 3 + k; T.extend([(a, a + 3, a + 1), (a + 1, a + 3, a + 4)])
        return V, T, None
    if kind in ('spikes', 'rays'):
        k = 9
        for i in range(k):
            th = 2 * math.pi * (i + rng.uniform(-0.3, 0.3)) / k
            up = rng.uniform(0.25, 0.9) if kind == 'spikes' else 0.0
            d = np.array([math.cos(th) * math.cos(up), math.sin(th) * math.cos(up), math.sin(up)])
            side = np.cross(d, [0, 0, 1]); side = side / (np.linalg.norm(side) + 1e-9)
            w = 7 if kind == 'spikes' else 10; L = 50 * rng.uniform(0.7, 1.0)
            base = np.zeros(3) if kind == 'spikes' else np.array([0, 0, 2])
            tip = base + d * L
            for s2 in (side, np.cross(d, side)):
                quad(base - s2 * w, base + s2 * w, tip + s2 * 0.5, tip - s2 * 0.5)
        return V, T, None
    if kind == 'smoke':   # (five round puffs: each a 5 x 5 card, clear at its rim - no square edges)
        for i in range(5):
            o = rng.normal(0, 14, 3); o[2] = abs(o[2]) + 10
            s = rng.uniform(22, 34); b = len(V)
            for j in range(5):
                for k in range(5):
                    x, z = (k / 4 - 0.5) * 2, (j / 4 - 0.5) * 2
                    r = min(1.0, math.hypot(x, z))
                    V.append((o[0] + x * s, o[1], o[2] + z * s, k / 4, 1 - j / 4, max(0.0, 1 - r) ** 1.2))
            for j in range(4):
                for k in range(4):
                    a = b + j * 5 + k; T.extend([(a, a + 1, a + 6), (a, a + 6, a + 5)])
        return V, T, 'bbmany'
    if kind == 'bolts':
        for i in range(2):
            th = math.pi / 2 * i
            dx, dy = math.cos(th) * 30, math.sin(th) * 30
            quad((-dx, -dy, 0), (dx, dy, 0), (dx, dy, 600), (-dx, -dy, 600))
        return V, T, None
    if kind == 'crescent':
        n = 30
        for i in range(n + 1):
            t = i / n; th = math.pi * (t - 0.5) * 1.3
            for r, u, a in ((36, 1.0, 0.0), (46, 0.0, 1.0), (54, 1.0, 0.0)):
                w = math.sin(t * math.pi)
                rr = 46 + (r - 46) * w
                V.append((rr * math.cos(th), rr * math.sin(th) * 0.0 + 0, rr * math.sin(th), u, t * 2, a * w))
        for i in range(n):
            for k in range(2):
                a = i * 3 + k; T.extend([(a, a + 3, a + 1), (a + 1, a + 3, a + 4)])
        return V, T, None
    raise ValueError(kind)


# ---------------------------------------------------------------- the styles (Noah's picks in the workshop): each a template of
# elements - (shape, texture slot, z, s0, s1, grow seconds, life, fade-from, delay, flipbook fps or scroll seconds, gain, ground)
# slots: A the ability's main texture, B its second, G a glow (ours)
def E(geo_, slot, z, s0, s1, grow, life, fade, delay=0.0, anim=0.0, gain=1.0, ground=False, face=False):
    return dict(geo=geo_, slot=slot, z=z, s0=s0, s1=s1, grow=grow, life=life, fade=fade, delay=delay, anim=anim, gain=gain, ground=ground, face=face)


STYLES = {
    'burst':     [E('flash', 'G', 70, 0.4, 1.6, 0.12, 0.5, 0.1, gain=1.6), E('sphere', 'A', 70, 0.15, 0.9, 0.35, 0.8, 0.2, anim=1.2),
                  E('smoke', 'B', 60, 0.3, 1.0, 0.6, 1.3, 0.4, delay=0.1, anim=3.0, gain=0.7)],
    'shockwave': [E('ring', 'A', 0, 0.3, 6.0, 0.55, 0.9, 0.3, anim=1.5, gain=1.4, ground=True), E('wall', 'B', 0, 0.3, 5.6, 0.55, 0.8, 0.25, anim=1.0, ground=True),
                  E('flash', 'G', 40, 0.6, 2.2, 0.1, 0.4, 0.08, gain=1.3)],
    'nova':      [E('spikes', 'A', 20, 0.2, 3.4, 0.35, 1.2, 0.6, anim=1.0, gain=1.2, ground=True), E('ring', 'B', 0, 0.3, 5.0, 0.6, 1.0, 0.3, anim=2.0, ground=True),
                  E('flash', 'G', 50, 0.6, 2.4, 0.12, 0.45, 0.1)],
    'pillar':    [E('column', 'A', 0, 1.0, 2.2, 0.25, 1.3, 0.7, anim=0.8, gain=1.4, ground=True), E('ring', 'B', 0, 0.3, 3.4, 0.5, 1.0, 0.4, anim=1.6, ground=True),
                  E('flash', 'G', 20, 0.8, 2.6, 0.15, 0.6, 0.15, gain=1.4)],
    'strike':    [E('bolts', 'A', 0, 0.6, 1.0, 0.05, 0.5, 0.25, anim=26, gain=1.6, ground=True), E('flash', 'G', 30, 1.0, 3.0, 0.08, 0.4, 0.05, gain=1.6),
                  E('ring', 'B', 0, 0.3, 3.2, 0.35, 0.7, 0.2, anim=1.0, ground=True)],
    'chain':     [E('flash', 'G', 70, 0.4, 1.2, 0.08, 0.35, 0.05, gain=1.5), E('spikes', 'A', 70, 0.3, 1.3, 0.12, 0.45, 0.15, anim=24, gain=1.3)],
    'bolt':      [E('sphere', 'A', 70, 0.2, 0.7, 0.2, 0.55, 0.2, anim=0.8, gain=1.3), E('flash', 'G', 70, 0.3, 1.5, 0.1, 0.4, 0.08, gain=1.4),
                  E('smoke', 'B', 70, 0.2, 0.7, 0.4, 0.9, 0.3, anim=3, gain=0.6)],
    'meteor':    [E('sphere', 'A', 30, 0.3, 1.6, 0.3, 1.0, 0.35, anim=1.0, gain=1.4), E('ring', 'B', 0, 0.4, 5.0, 0.6, 1.1, 0.35, anim=1.6, ground=True),
                  E('smoke', 'B', 40, 0.4, 2.0, 0.9, 1.8, 0.6, delay=0.15, anim=4.0, gain=0.6), E('flash', 'G', 40, 1.0, 3.6, 0.12, 0.5, 0.1, gain=1.5)],
    'cyclone':   [E('spiral', 'A', 0, 0.6, 1.4, 0.5, 2.2, 1.5, anim=0.7, gain=1.3, ground=True), E('spiral', 'B', 0, 0.9, 1.8, 0.6, 2.2, 1.5, anim=0.9, ground=True),
                  E('ring', 'B', 0, 0.5, 2.6, 0.5, 2.0, 1.4, anim=1.5, ground=True)],
    'cloud':     [E('smoke', 'A', 50, 0.6, 2.4, 0.8, 3.0, 2.0, anim=5.0, gain=0.55), E('smoke', 'B', 30, 0.8, 2.8, 1.0, 3.2, 2.2, delay=0.2, anim=6.0, gain=0.4),
                  E('flash', 'G', 50, 0.5, 1.6, 0.2, 0.6, 0.2, gain=0.6)],
    'pool':      [E('disc', 'A', 0, 0.5, 2.6, 0.4, 3.0, 2.2, anim=3.0, gain=1.0, ground=True), E('wall', 'B', 0, 0.4, 2.4, 0.4, 3.0, 2.2, anim=1.0, gain=0.7, ground=True)],
    'drain':     [E('spiral', 'A', 0, 1.6, 0.3, 0.7, 0.8, 0.5, anim=0.5, gain=1.2), E('flash', 'G', 70, 0.8, 0.3, 0.6, 0.8, 0.5, gain=1.2),
                  E('sphere', 'B', 70, 0.8, 0.2, 0.7, 0.8, 0.5, anim=0.8)],
    'bind':      [E('disc', 'R', 0, 0.6, 1.8, 0.25, 2.0, 1.5, gain=1.3, ground=True), E('column', 'A', 0, 0.3, 0.5, 0.25, 2.0, 1.5, anim=0.8, gain=0.8, ground=True),
                  E('spikes', 'B', 60, 0.6, 0.9, 0.2, 2.0, 1.5, anim=1.5, gain=0.8)],
    'freeze':    [E('spikes', 'A', 10, 0.2, 1.3, 0.25, 2.0, 1.5, anim=2.0, gain=1.2, ground=True), E('sphere', 'B', 60, 0.6, 1.0, 0.2, 2.0, 1.4, anim=4.0, gain=0.6),
                  E('flash', 'G', 60, 0.5, 1.6, 0.1, 0.4, 0.1)],
    'toll':      [E('ring', 'A', 2, 0.3, 4.5, 0.6, 1.0, 0.4, anim=1.4, gain=1.2, ground=True), E('ring', 'A', 2, 0.3, 4.5, 0.6, 1.0, 0.4, delay=0.3, anim=1.4, ground=True),
                  E('ring', 'B', 2, 0.3, 4.5, 0.6, 1.0, 0.4, delay=0.6, anim=1.4, gain=0.8, ground=True), E('flash', 'G', 60, 0.6, 1.8, 0.1, 0.5, 0.1)],
    'wave':      [E('crescent', 'A', 40, 0.6, 4.0, 0.5, 0.8, 0.35, anim=1.0, gain=1.3, face=True), E('ring', 'B', 0, 0.4, 3.0, 0.5, 0.8, 0.3, anim=1.2, ground=True),
                  E('smoke', 'B', 20, 0.4, 2.0, 0.6, 1.2, 0.5, anim=3.0, gain=0.5)],
    'summon':    [E('disc', 'R', 0, 0.3, 1.6, 0.4, 2.0, 1.4, gain=1.3, ground=True), E('column', 'A', 0, 0.6, 1.4, 0.4, 1.6, 1.0, anim=0.8, gain=1.0, ground=True),
                  E('smoke', 'B', 20, 0.4, 1.8, 0.8, 1.8, 1.0, anim=4, gain=0.6)],
    'mark':      [E('disc', 'R', 160, 0.2, 0.6, 0.25, 2.2, 1.7, gain=1.2), E('flash', 'G', 160, 0.2, 0.8, 0.2, 2.2, 1.7, gain=0.8),
                  E('sphere', 'A', 70, 0.6, 0.9, 0.2, 0.6, 0.3, anim=1.0, gain=0.6)],
    'slash':     [E('crescent', 'A', 70, 0.6, 1.8, 0.18, 0.45, 0.15, anim=0.6, gain=1.5, face=True), E('flash', 'G', 70, 0.4, 1.2, 0.08, 0.35, 0.06, gain=1.2),
                  E('sphere', 'B', 70, 0.2, 0.6, 0.2, 0.5, 0.2, anim=1.0, gain=0.6)],
}

# ---------------------------------------------------------------- each ability: its owner key (src/Gun.cpp FxScope) and kind,
# its card in the workshop, its textures (A, B) - Skyrim's own, chosen for what it does - its style, colour and size (Noah's
# picks, the workshop 2026-11-25)
A = lambda owner, card, tA, tB, style, col, size, kind='burst': dict(owner=owner, card=card, A=tA, B=tB, style=style, col=col, size=size, kind=kind)
ABIL = [
    A('priest_vahlok_hit', 'vahlok_hit', 'squig', 'sparks', 'summon', 'green', 100), A('priest_vahlok_heavy', 'vahlok_heavy', 'caustic', 'wisps', 'drain', 'green', 100),
    A('priest_nahkriin_hit', 'nahkriin_hit', 'fill', 'sparks', 'slash', 'white', 60), A('priest_nahkriin_heavy', 'nahkriin_heavy', 'vapour', 'frostc', 'cloud', 'white', 100),
    A('priest_volsung_hit', 'volsung_light', 'vapour', 'swirl', 'wave', 'white', 90), A('priest_volsung_heavy', 'volsung_heavy', 'swirl', 'vapour', 'wave', 'blue', 150),
    A('priest_zahkriisos_hit', 'zahkriisos_hit', 'ice', 'frostc', 'freeze', 'iceblue', 100), A('priest_zahkriisos_heavy', 'zahkriisos_heavy', 'ice', 'frostc', 'nova', 'iceblue', 130),
    A('priest_otar_hit', 'otar_hit', 'bolt', 'shock', 'chain', 'blue', 100), A('priest_otar_heavy', 'otar_heavy', 'bolt', 'shock', 'strike', 'blue', 120),
    A('priest_rahgot_hit', 'rahgot_hit', 'vapour', 'squig', 'cloud', 'poison', 70), A('priest_rahgot_heavy', 'rahgot_heavy', 'caustic', 'vapour', 'burst', 'poison', 120),
    A('priest_krosis_hit', 'krosis_hit', 'caustic', 'wisps', 'drain', 'red', 80), A('priest_krosis_heavy', 'krosis_heavy', 'swirl', 'wisps', 'toll', 'red', 120),
    A('priest_hevnoraak_hit', 'hevnoraak_hit', 'sparks', 'caustic', 'burst', 'green', 90), A('priest_hevnoraak_heavy', 'hevnoraak_heavy', 'shock', 'vapour', 'shockwave', 'green', 120),
    A('priest_vokun_hit', 'vokun_hit', 'squig', 'wisps', 'bind', 'purple', 100), A('priest_vokun_heavy', 'vokun_heavy', 'swirl', 'vapour', 'cloud', 'shadow', 140),
    A('priest_morokei_hit', 'morokei_hit', 'fill', 'caustic', 'slash', 'drain', 100), A('priest_morokei_heavy', 'morokei_heavy', 'fill', 'squig', 'shockwave', 'drain', 130),
    A('tab_stormtalons_hit', 'stormtalons_third', 'bolt', 'sparks', 'chain', 'blue', 100), A('tab_stormtalons_heavy', 'stormtalons_heavy', 'bolt', 'shock', 'strike', 'blue', 100),
    A('tab_gravebell_hit', 'gravebell_fifth', 'fill', 'sparks', 'toll', 'gold', 120), A('tab_gravebell_heavy', 'gravebell_fifth', 'fill', 'sparks', 'toll', 'gold', 140),
    A('tab_talons_hit', 'talons_hit', 'caustic', 'squig', 'drain', 'red', 80), A('tab_talons_heavy', 'talons_hit', 'caustic', 'squig', 'drain', 'red', 100),
    A('tab_riverstone_hit', 'riverstone_hit', 'vapour', 'sparks', 'burst', 'green', 90), A('tab_riverstone_heavy', 'riverstone_heavy', 'vapour', 'swirl', 'wave', 'green', 140),
    A('tab_wyrmspine_hit', 'wyrmspine_pike_hit', 'fire', 'sparks', 'slash', 'fear', 100), A('tab_wyrmspine_heavy', 'wyrmspine_pike_heavy', 'fire', 'vapour', 'shockwave', 'fear', 90),
    A('tab_lanternwick_hit', 'lanternwick_light', 'ice', 'frostc', 'freeze', 'iceblue', 90), A('tab_lanternwick_heavy', 'lanternwick_heavy', 'frostc', 'ice', 'cyclone', 'iceblue', 100),
    A('tab_cinderclaws_hit', 'cinderclaws_hit', 'fire', 'sparks', 'burst', 'orange', 80), A('tab_cinderclaws_heavy', 'cinderclaws_heavy', 'fire', 'vapour', 'wave', 'orange', 120),
    A('tab_eclipse_hit', 'eclipse_hit', 'fill', 'ice', 'slash', 'orange', 100), A('tab_eclipse_heavy', 'eclipse_heavy', 'ice', 'fill', 'shockwave', 'iceblue', 100),
    A('tab_wanderstar_hit', 'wanderstar_fifth', 'sparks', 'fill', 'burst', 'gold', 110), A('tab_wanderstar_heavy', 'wanderstar_fifth', 'sparks', 'fill', 'burst', 'gold', 130),
    A('tab_frostmourn_hit', 'frostmourn_hit', 'ice', 'sparks', 'slash', 'iceblue', 90), A('tab_frostmourn_heavy', 'frostmourn_heavy', 'ice', 'frostc', 'nova', 'iceblue', 140),
    A('tab_silverscythe_hit', 'silverscythe_fourth', 'squig', 'sparks', 'bind', 'moon', 120), A('tab_silverscythe_heavy', 'silverscythe_fourth', 'squig', 'sparks', 'bind', 'moon', 140),
    A('tab_bonereaper_hit', 'bonereaper_kill', 'wisps', 'vapour', 'summon', 'white', 100), A('tab_bonereaper_heavy', 'bonereaper_kill', 'wisps', 'vapour', 'summon', 'white', 120),
    A('tab_wyrmtooth_hit', 'wyrmtooth_hit', 'caustic', 'sparks', 'slash', 'red', 80), A('tab_wyrmtooth_heavy', 'wyrmtooth_heavy', 'swirl', 'vapour', 'shockwave', 'red', 150),
    A('tab_bloodmoon_hit', 'bloodmoon_hit', 'caustic', 'wisps', 'drain', 'red', 90), A('tab_bloodmoon_heavy', 'bloodmoon_heavy', 'caustic', 'swirl', 'drain', 'red', 140),
    A('bane_light', 'bane_light', 'fire', 'vapour', 'meteor', 'orange', 140), A('bane_heavy', 'bane_heavy', 'swirl', 'vapour', 'cyclone', 'white', 170),
    A('arc_purple', 'scythe_light', 'fire', 'swirl', 'burst', 'shadow', 100), A('firecyclone', 'scythe_heavy', 'fire', 'swirl', 'burst', 'shadow', 130),
    A('arc_white', 'staff_light', 'fill', 'sparks', 'burst', 'white', 110), A('arc_carian', 'wand_hit', 'squig', 'sparks', 'burst', 'purple', 90),
    A('arc_fire', 'rapier_light', 'fire', 'sparks', 'burst', 'orange', 100), A('arc_frost', 'rapier_light', 'ice', 'frostc', 'burst', 'iceblue', 100),
    A('arc_shock', 'rapier_light', 'bolt', 'sparks', 'burst', 'blue', 100), A('arc_sun', 'rapier_light', 'fill', 'sparks', 'burst', 'gold', 100),
    A('arc_blood', 'rapier_light', 'caustic', 'wisps', 'burst', 'arcane', 100), A('arc_arcane', 'rapier_light', 'caustic', 'wisps', 'burst', 'arcane', 100),
    A('arc_gold', 'slayer_light', 'fire', 'sparks', 'bolt', 'gold', 120), A('arc_bane', 'bane_light', 'vapour', 'swirl', 'wave', 'white', 100),
    A('cyclone_fire', 'rapier_heavy', 'fire', 'vapour', 'burst', 'orange', 80), A('cyclone_frost', 'rapier_heavy', 'ice', 'frostc', 'burst', 'iceblue', 80),
    A('cyclone_shock', 'rapier_heavy', 'bolt', 'shock', 'burst', 'blue', 80), A('cyclone_white', 'rapier_heavy', 'fill', 'sparks', 'burst', 'gold', 80),
    A('cyclone_blood', 'rapier_heavy', 'caustic', 'wisps', 'burst', 'arcane', 80), A('cyclone_storm', 'rapier_heavy', 'bolt', 'shock', 'burst', 'blue', 90),
    A('cyclone_violet', 'scythe_heavy', 'fire', 'swirl', 'burst', 'shadow', 80),
    A('fly_fire', 'javelin_land', 'fire', 'vapour', 'shockwave', 'orange', 110), A('fly_frost', 'javelin_land', 'ice', 'frostc', 'shockwave', 'iceblue', 110),
    A('fly_shock', 'javelin_land', 'bolt', 'shock', 'shockwave', 'blue', 110), A('fly_wind', 'javelin_land', 'vapour', 'swirl', 'shockwave', 'white', 110),
    A('fly_sun', 'javelin_land', 'fill', 'sparks', 'shockwave', 'gold', 110), A('fly_blood', 'javelin_land', 'caustic', 'wisps', 'shockwave', 'arcane', 110),
    A('fly_void', 'javelin_land', 'swirl', 'caustic', 'shockwave', 'shadow', 110), A('fly_poison', 'javelin_land', 'vapour', 'caustic', 'shockwave', 'poison', 110),
    A('flask_fire', 'ffire_land', 'fire', 'vapour', 'burst', 'orange', 110), A('flask_frost', 'ffrost_land', 'ice', 'frostc', 'nova', 'iceblue', 110),
    A('flask_shock', 'fshock_land', 'bolt', 'shock', 'strike', 'blue', 110), A('flask_poison', 'facid_land', 'vapour', 'caustic', 'cloud', 'poison', 110),
    A('flask_sun', 'fholy_land', 'fill', 'sparks', 'pillar', 'gold', 110), A('flask_blood', 'fvoid_land', 'caustic', 'swirl', 'drain', 'arcane', 120),
    A('flask_philosopher', 'mutagen_elixir', 'fill', 'sparks', 'burst', 'gold', 130),
]
# the class spells' own impacts (they ride over nothing now: each spell's effect in our scope is its own - src/Events.cpp CofferHit)
SPELLS = {   # name: (A, B, style, colour, size)
    'Firebolt': ('fire', 'sparks', 'burst', 'orange', 60), 'Fireball': ('fire', 'vapour', 'burst', 'orange', 90), 'Incinerate': ('fire', 'fill', 'burst', 'orange', 80),
    'Immolate': ('fire', 'vapour', 'pillar', 'orange', 70), 'Ice Spike': ('ice', 'sparks', 'freeze', 'iceblue', 60), 'Ice Storm': ('frostc', 'ice', 'cloud', 'iceblue', 70),
    'Icy Spear': ('ice', 'frostc', 'nova', 'iceblue', 70), 'Throat of the World': ('ice', 'frostc', 'nova', 'iceblue', 100),
    'Chain Lightning': ('bolt', 'sparks', 'chain', 'blue', 70), 'Shock Nova': ('shock', 'bolt', 'shockwave', 'blue', 80),
    'Finger of the Mountain': ('bolt', 'shock', 'strike', 'blue', 80), 'Solar Flare': ('fill', 'sparks', 'burst', 'gold', 70),
    'Sunbeam': ('fill', 'sparks', 'mark', 'gold', 50), 'Forbidden Sun': ('fill', 'vapour', 'pillar', 'gold', 80), 'Daybreak': ('fill', 'sparks', 'shockwave', 'gold', 90),
    'Arcane Bolt': ('caustic', 'sparks', 'burst', 'arcane', 60), 'Mana Void': ('swirl', 'caustic', 'drain', 'arcane', 80), 'Lunar Tide': ('squig', 'vapour', 'shockwave', 'moon', 80),
    'Apocalypse': ('caustic', 'swirl', 'meteor', 'arcane', 100), 'Wall of Flames': ('fire', 'vapour', 'pool', 'orange', 40), 'Wall of Frost': ('frostc', 'ice', 'pool', 'iceblue', 40),
    'Wall of Storms': ('shock', 'bolt', 'pool', 'blue', 40), 'Inferno': ('fire', 'swirl', 'pool', 'orange', 50), 'Frost Nova': ('ice', 'frostc', 'nova', 'iceblue', 60),
    'Lightning Storm': ('bolt', 'shock', 'chain', 'blue', 50), 'Multivortex': ('swirl', 'caustic', 'cyclone', 'arcane', 50), 'Lightning Strike': ('bolt', 'shock', 'strike', 'blue', 90),
    'Cinder Storm': ('fire', 'swirl', 'cloud', 'orange', 60),
    'Burning Touch': ('fire', 'sparks', 'slash', 'orange', 50), 'Flame Touch': ('fire', 'vapour', 'slash', 'orange', 60), 'Fiery Grasp': ('fire', 'fill', 'burst', 'orange', 70),
    'Chilling Touch': ('ice', 'sparks', 'slash', 'iceblue', 50), 'Icy Touch': ('frostc', 'ice', 'slash', 'iceblue', 60), 'Freezing Grasp': ('ice', 'frostc', 'freeze', 'iceblue', 70),
    'Shocking Touch': ('bolt', 'sparks', 'slash', 'blue', 50), 'Static Touch': ('shock', 'sparks', 'slash', 'blue', 60), 'Thundering Grasp': ('bolt', 'shock', 'chain', 'blue', 70),
    'Sunlight Touch': ('fill', 'sparks', 'slash', 'gold', 50), 'Radiant Touch': ('fill', 'vapour', 'slash', 'gold', 60), 'Searing Grasp': ('fill', 'sparks', 'burst', 'gold', 70),
    'Arcane Edge': ('caustic', 'sparks', 'slash', 'arcane', 50), 'Arcane Brand': ('squig', 'caustic', 'mark', 'arcane', 60), 'Arcane Cascade': ('caustic', 'wisps', 'burst', 'arcane', 70),
}
import weaponfx as _W
COLOURS = list(_W.COL)   # (the workshop's colours, each a ramp in the game too: tools/realfire.py)


def slug(n): return 'spell_' + ''.join(c.lower() if c.isalnum() else '_' for c in n)


PICKS = ROOT / 'docs' / 'workshop' / 'ability_picks.json'   # (each card's style, colour and size, as Noah set them in the workshop)


def card_key(ab):
    import re
    if ab['card'].startswith('spell:'):
        tid = re.sub(r'[^a-z0-9]', '', ab['card'][6:].lower())
        for k in json.loads(PICKS.read_text()):
            if k.split('_', 1)[1] == tid: return k
    return ab['card']


def all_abilities():
    out = list(ABIL)
    for n, (a, b, st, col, sz) in SPELLS.items():
        out.append(dict(owner=slug(n), card='spell:' + n, A=a, B=b, style=st, col=col, size=sz, kind='spell_hit'))
    # (the workshop and the game the same: each card's style and size as picked there; its colour too, but where one card speaks
    # for several (the rapier's elements, the javelin's, the flasks) each keeps its own element's colour)
    picks = json.loads(PICKS.read_text()) if PICKS.exists() else {}
    for ab in out:
        pk = picks.get(card_key(ab))
        if not pk: continue
        ab['style'], ab['size'] = pk['style'], pk['size']
        if not ab['owner'].startswith(('arc_', 'cyclone_', 'fly_', 'flask_')) or ab['owner'] in ('arc_purple', 'arc_white', 'arc_gold', 'arc_bane'): ab['col'] = pk['col']
        if ab['style'] not in STYLES: ab['style'] = 'burst'
    return out


# ---------------------------------------------------------------- the elements: each (shape, texture) once, as a NIF
TILE = {'fire': 'fill', 'ice': 'frostc', 'bolt': 'shock', 'caustic': 'swirl', 'squig': 'swirl', 'wisps': 'vapour', 'sparks': 'fill'}   # (a flipbook's tile partner)


def fps_of(anim):
    return anim if anim > 6 else 14   # (a flipbook plays at its own pace; a tile's anim is its scroll seconds)


def jitter(owner, i):
    """an ability's own beat: the same in the game and the workshop (FNV-1a of its owner and the element's place)"""
    h = 2166136261
    for ch in f'{owner}#{i}'.encode(): h = ((h ^ ch) * 16777619) & 0xFFFFFFFF
    return 1.0 + ((h % 1000) / 1000.0 - 0.5) * 0.24


def element_name(geo_, tex, anim):
    return f'{geo_}_{tex}' + (f'_{str(anim).replace(".", "p")}' if anim else '')


def build_element(geo_, tex, anim, rows_out=None):
    """one element: the shape wearing the texture (a flipbook played at 'anim' fps, or a tile scrolled once in 'anim' s)"""
    V, T, face = geo(geo_, np.random.default_rng(sum(map(ord, geo_))))
    texname = tex
    grid = TEX.get(tex, (None, None))[1] if tex in TEX else None
    name = element_name(geo_, tex, anim)
    rows = [f'out {MESH / (name + ".nif")}']
    def head(nm, extra=''):
        h = f'shape {nm} tex={GT}{texname}.dds color=1,1,1,1 scale=1.0 blend=add palette=CurseOfAlduin\\fx\\pal_white.dds clamp=3'
        if grid: h += f' flip={grid[0]}x{grid[1]} fps={fps_of(anim)} phase=0'
        elif anim and not grid: h += f' vscroll={anim}'
        return h + extra
    if face == 'bbmany':   # (each smoke card its own camera-facing card round its own middle)
        nv = len(V) // 5; nt = len(T) // 5
        for q in range(5):
            vs = V[q * nv:(q + 1) * nv]; c = np.mean([p[:3] for p in vs], axis=0)
            rows.append(head(f'e{q}', f' billboard=1 at={c[0]:.1f},{c[1]:.1f},{c[2]:.1f}'))
            for p in vs:
                x, y, z = p[0] - c[0], p[1] - c[1], p[2] - c[2]
                u_, v_ = (p[3], p[4]) if not grid else (p[3] / grid[0], p[4] / grid[1])   # (each card one whole cell)
                rows.append(f'v {x:.2f} {z:.2f} 0 {u_:.4f} {v_:.4f} 1 1 1 {p[5]:.3f}')
            rows += [f't {a - q * nv} {b - q * nv} {c_ - q * nv}' for a, b, c_ in T[q * nt:(q + 1) * nt]]
    else:
        rows.append(head('e0', ' billboard=1 at=0,0,0' if face == 'bb' else ''))
        um = max(1e-6, max(p[3] for p in V)); vm = max(1e-6, max(p[4] for p in V))
        for p in V:
            u_, v_ = p[3], p[4]
            if grid: u_, v_ = u_ / um / grid[0], v_ / vm / grid[1]   # (a flipbook's cell stretched over the whole shape: no seams)
            if face == 'bb': rows.append(f'v {p[0]:.2f} {p[2]:.2f} 0 {u_:.4f} {v_:.4f} 1 1 1 {p[5]:.3f}')
            else: rows.append(f'v {p[0]:.2f} {p[1]:.2f} {p[2]:.2f} {u_:.4f} {v_:.4f} 1 1 1 {p[5]:.3f}')
        rows += [f't {a} {b} {c}' for a, b, c in T]
    if rows_out is not None: rows_out[name] = rows
    return name, rows


def compose(ab):
    """the ability's elements: its style's template with its own textures, colour, size and a timing of its own"""
    k = ab['size'] / 100.0
    out = []
    for i, e in enumerate(STYLES[ab['style']]):
        tex = {'A': ab['A'], 'B': ab['B'], 'G': 'glow', 'R': 'runes'}[e['slot']]
        if e['geo'] in ('sphere', 'ring', 'wall', 'column', 'spiral', 'disc', 'crescent'): tex = TILE.get(tex, tex)   # (round shapes wear a tile)
        jit = jitter(ab['owner'], i)   # (its own timing: no two the same beat)
        out.append(dict(e, tex=tex, s0=e['s0'] * k, s1=e['s1'] * k * jit, grow=e['grow'] * jit, life=e['life'] * jit, fade=e['fade'] * jit,
                        el=element_name(e['geo'], tex, e['anim'])))
    return out


def fx_line(owner, kind, els, col):
    parts = []
    for e in els:
        o = f'CurseOfAlduin\\fx\\a\\{e["el"]}_{col}.nif grow={e["s0"]:.2f},{e["s1"]:.2f},{e["grow"]:.2f},{e["life"]:.2f},{e["fade"]:.2f}'
        if e['delay']: o += f' delay={e["delay"]:.2f}'
        if not e['ground'] and e['z'] != 70: o += f' z={e["z"] - 70:.0f}'   # (set off at the chest of the one struck: 70 up)
        if e['ground']: o += ' ground'
        if e['face']: o += ' face'
        parts.append(o)
    return f'CurseOfAlduin\\fx\\w\\{owner}_{kind}.nif = ' + ' ; '.join(parts)


MARK0 = '# ==== own effects (tools/abilityfx.py) ===='
MARK1 = '# ==== end own effects ===='


def build():
    MESH.mkdir(parents=True, exist_ok=True); WORK.mkdir(parents=True, exist_ok=True); PREV.mkdir(parents=True, exist_ok=True)
    abil = all_abilities()
    needed, lines, cols = {}, [], set()
    doc = {'styles': STYLES, 'abilities': []}
    for ab in abil:
        els = compose(ab)
        for e in els: needed[e['el']] = (e['geo'], e['tex'], e['anim'])
        cols.add(ab['col'])
        lines.append(fx_line(ab['owner'], ab['kind'], els, ab['col']))
        doc['abilities'].append(dict(ab, els=els))
    rows_all = {}
    for name, (g, t, a) in needed.items():
        _, rows = build_element(g, t, a, rows_all)
        jf = WORK / f'{name}.job'; jf.write_text('\n'.join(rows) + '\n')
        r = subprocess.run([str(TOOLS / 'fxwrite'), str(jf)], capture_output=True, text=True)
        if r.returncode: raise SystemExit(f'{name}: {r.stderr[-300:]}')
    # every element in every colour any ability wears (the workshop may pick another: all the colours, for the picks to come)
    colour_lines = [f'CurseOfAlduin\\fx\\a\\{n}_{c}.nif = CurseOfAlduin\\fx\\a\\{n}.nif pal={c}' for n in sorted(needed) for c in COLOURS]
    f = ROOT / 'assets' / 'fx' / 'SKSE' / 'Plugins' / 'CurseOfAlduin' / 'fxmap.ini'
    txt = f.read_text()
    for m0 in (MARK0, '# ==== own effects (tools/fxown.py) ===='):
        if m0 in txt: txt = txt[:txt.index(m0)].rstrip('\n') + '\n'
    block = ['', MARK0, '# (Noah, 2026-11-25: "One custom effect each" - our own, from Skyrim\'s textures, replacing what played; the same in the',
             '# workshop) each ability\'s elements, grown and faded by the game: grow=from,to,seconds,life,fade-from [delay=] [z=] [ground] [face]',
             'CurseOfAlduin\\fx\\spell_hit.nif = none   # (a class spell with no impact of its own here: only the spell\'s own)'] + lines + \
            ['# the elements in each colour'] + colour_lines + [MARK1]
    f.write_text(txt + '\n'.join(block) + '\n')
    (ROOT / 'docs' / 'workshop' / 'abilities.json').write_text(json.dumps(doc, indent=1))
    # the page's copy of every element (its rows) and texture
    geos = {}
    for g in sorted({e['geo'] for st in STYLES.values() for e in st}):
        V, T, face = geo(g, np.random.default_rng(sum(map(ord, g))))
        geos[g] = dict(v=[[round(x, 2) for x in p] for p in V], t=T, face=face)
    (PREV / 'geos.json').write_text(json.dumps(dict(geos=geos, tex={k: (v[1] if v[1] else None) for k, v in TEX.items()} | {'glow': None, 'band': None, 'runes': None}), separators=(',', ':')))
    import shutil
    (PREV / 'tex').mkdir(exist_ok=True)
    for p in WORK.glob('*.png'): shutil.copy(p, PREV / 'tex' / p.name)
    print(len(abil), 'abilities,', len(needed), 'elements,', len(colour_lines), 'coloured')


if __name__ == '__main__':
    if sys.argv[1:] == ['textures']: textures()
    else:
        if not (TEXD / 'glow.dds').exists(): textures()
        build()
