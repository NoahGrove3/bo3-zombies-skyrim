"""Undying Nights: the ten spells' visuals, built from scratch (sheet: the SPELLS table below; used by src/Spellcraft.cpp).

    python tools/spellfx.py <out Data dir>

Per spell (all unlit additive effect meshes written with tools/modelconv/fxwrite, textures generated here):
  fx/<id>_bolt.nif    the projectile: a hot core, its halo and a trail (flies along +Y)
  fx/<id>_orb.nif     the power gathered between both hands (and the lobbed / thrown charged shots)
  fx/<id>_beam.nif    a beam segment 64 units long along +Y (the code lays them end to end): lances, lightning arcs
  fx/<id>_hand.nif    the glow round one hand (left-click charge, idle)
  fx/<id>_burst.nif   an impact: flash, ring and shards (scaled and faded by the code)
Textures go to Textures/UndyingNights/fx/. The spell colours are the single source the code and the HUD use too.
"""
import json, math, pathlib, subprocess, sys
import numpy as np
from PIL import Image

OUT = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else pathlib.Path(__file__).resolve().parent.parent / 'assets' / 'fx')
FXWRITE = '/home/claude/tools/fxwrite'
ROOT = pathlib.Path(__file__).resolve().parent.parent

def _hex(h): return tuple(int(h[k:k + 2], 16) / 255.0 for k in (0, 2, 4))
SPELLS = [(r['id'], r['name'], _hex(r['color']), _hex(r['accent']), r['style']) for r in json.load(open(ROOT / 'sheets' / 'spellcraft.json'))['rows']]
TEX = OUT / 'Textures' / 'UndyingNights' / 'fx'
MESH = OUT / 'Meshes' / 'UndyingNights' / 'fx'
TPATH = 'UndyingNights\\fx\\'


# ----------------------------------------------------------------------------------------------------- textures
N = 1024  # texture size: sharp up close (the orb sits a hand's width from the eye), with a full mip chain


def write_dds(path, img):
    """uncompressed A8R8G8B8 DDS with every mip level (no block artefacts on soft glows, no shimmer far off)"""
    import struct
    h, w = img.shape[:2]
    levels = [img]
    while levels[-1].shape[0] > 1 and levels[-1].shape[1] > 1:
        a = levels[-1].astype(np.float32)
        a = (a[0::2, 0::2] + a[1::2, 0::2] + a[0::2, 1::2] + a[1::2, 1::2]) / 4
        levels.append(np.clip(a + 0.5, 0, 255).astype(np.uint8))
    flags = 0x1 | 0x2 | 0x4 | 0x1000 | 0x8 | 0x20000                 # caps height width pixelformat pitch mipmapcount
    hdr = struct.pack('<4sIIIIIII44x', b'DDS ', 124, flags, h, w, w * 4, 0, len(levels))
    pf = struct.pack('<IIIIIIII', 32, 0x41, 0, 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000)
    caps = struct.pack('<IIIII', 0x1000 | 0x8 | 0x400000, 0, 0, 0, 0)
    with open(path, 'wb') as f:
        f.write(hdr + pf + caps)
        for lv in levels:
            f.write(lv[..., [2, 1, 0, 3]].tobytes())                    # BGRA


def save(name, a, rgb=None):
    """a: alpha 0..1 (HxW); rgb defaults to white"""
    h, w = a.shape
    if rgb is None:
        rgb = np.ones((h, w, 3), np.float32)
    img = np.dstack([np.clip(rgb, 0, 1), np.clip(a, 0, 1)[..., None]])
    TEX.mkdir(parents=True, exist_ok=True)
    write_dds(TEX / f'{name}.dds', (img * 255 + 0.5).astype(np.uint8))
    return TPATH + f'{name}.dds'


def grid(n=N):
    yy, xx = np.mgrid[0:n, 0:n].astype(np.float32)
    return (xx + 0.5) / n * 2 - 1, (yy + 0.5) / n * 2 - 1


def noise(n, seed, scale=8):
    r = np.random.default_rng(seed)
    g = r.random((scale + 1, scale + 1)).astype(np.float32)
    return np.asarray(Image.fromarray(g).resize((n, n), Image.BICUBIC))


def textures():
    t = {}
    x, y = grid(N); r = np.sqrt(x * x + y * y)
    t['soft'] = save('soft', np.clip(1 - r, 0, 1) ** 2.2)                         # a soft round glow
    t['core'] = save('core', np.clip(1 - r * 1.6, 0, 1) ** 1.4)                   # a hot little core
    t['ring'] = save('ring', np.exp(-((r - 0.78) / 0.07) ** 2) * (r < 1))          # a thin ring (novas, orb halo)
    a = np.zeros_like(r)                                                            # a star of rays (dawn)
    ang = np.arctan2(y, x)
    a = np.clip(1 - r, 0, 1) ** 1.2 * (0.35 + 0.65 * np.abs(np.cos(ang * 4)) ** 12) + np.clip(1 - r * 2.2, 0, 1)
    t['rays'] = save('rays', a)
    # a streak along v (trails, beams): bright down the middle, soft edges, fading toward v=1
    u, v = grid(N); uu = (u + 1) / 2; vv = (v + 1) / 2
    t['streak'] = save('streak', np.exp(-(u / 0.35) ** 2) * (1 - vv) ** 1.3)
    t['beam'] = save('beam', np.exp(-(u / 0.30) ** 2) * (0.85 + 0.15 * np.sin(vv * 2 * math.pi * 3)) + np.exp(-(u / 0.07) ** 2) * 0.8)
    # flame tongues (fire): noisy, licking upward
    n1 = noise(N, 3, 6); n2 = noise(N, 4, 14)
    t['flame'] = save('flame', np.clip(1 - r * (0.9 + 0.6 * n1), 0, 1) ** 1.5 * (0.6 + 0.4 * n2))
    # frost: a six-pointed crystal
    a = np.zeros_like(r)
    for k in range(6):
        th = k * math.pi / 3
        d = np.abs(x * math.sin(th) - y * math.cos(th)); along = x * math.cos(th) + y * math.sin(th)
        a = np.maximum(a, np.exp(-(d / 0.05) ** 2) * (along > 0) * np.clip(1 - along, 0, 1))
    t['crystal'] = save('crystal', np.clip(a + np.clip(1 - r * 2.5, 0, 1), 0, 1))
    # lightning: a jagged bolt down v
    rng = np.random.default_rng(9); pts = np.cumsum(rng.normal(0, 0.18, 24)); pts -= pts.mean(); pts = np.clip(pts, -0.6, 0.6)
    path = np.interp(np.linspace(0, 23, N), np.arange(24), pts)
    a = np.exp(-((u - path[:, None]) / 0.07) ** 2) + 0.4 * np.exp(-((u - path[:, None]) / 0.25) ** 2)
    t['bolt'] = save('lightning', a)
    # thorns: a long spike
    t['thorn'] = save('thorn', np.clip(1 - np.abs(u) / (0.08 + 0.4 * (1 - vv) ** 2), 0, 1) * (vv < 0.98))
    # blood: a droplet
    t['drop'] = save('drop', np.clip(1 - np.sqrt(x * x + ((y + 0.2) * (1.3 - 0.4 * (y > -0.2))) ** 2), 0, 1) ** 1.5)
    # wind: a swirl of arcs
    a = np.zeros_like(r)
    for k in range(3):
        a += np.exp(-((r - (0.35 + 0.2 * k)) / 0.04) ** 2) * (0.5 + 0.5 * np.sin(ang * 1 + k * 2.1 + r * 6)) ** 3
    t['swirl'] = save('swirl', np.clip(a, 0, 1) * (r < 1))
    # a crescent wave (the gust)
    t['crescent'] = save('crescent', np.exp(-((r - 0.75) / 0.12) ** 2) * np.clip(-y * 1.5, 0, 1) ** 0.8)
    # soul: a wisp with flame-like flicker
    t['wisp'] = save('wisp', np.clip(1 - r * (1.0 + 0.5 * noise(N, 7, 5)), 0, 1) ** 1.8)
    # void: a dark disc (alpha blend) with a bright rim (the rim is drawn additive separately)
    t['dark'] = save('dark', np.clip(1.15 - r * 1.2, 0, 1) ** 0.8, rgb=np.zeros((N, N, 3), np.float32))
    # sparks: little points scattered (bursts)
    a = np.zeros((N, N), np.float32)
    for _ in range(40):
        cx, cy = rng.integers(N // 16, N - N // 16, 2); a[cy - 4:cy + 4, cx - 4:cx + 4] = 1
    from scipy.ndimage import gaussian_filter
    t['sparks'] = save('sparks', np.clip(gaussian_filter(a, 4.0) * 4, 0, 1))
    # the flame aura round a spell's logo (one frame of assets/ui/flame_ring.png, white: tinted per spell)
    fr = np.asarray(Image.open(ROOT / 'assets' / 'ui' / 'flame_ring.png').convert('RGBA')).astype(np.float32)[:256, :256] / 255
    t['flamering'] = save('flamering', fr[..., 3], fr[..., :3])
    return t


# ----------------------------------------------------------------------------------------------------- geometry
class Mesh:
    def __init__(self):
        self.shapes = []

    def shape(self, name, tex, color, scale=1.0, blend='add', falloff=False):
        s = {'name': name, 'tex': tex, 'color': color, 'scale': scale, 'blend': blend, 'falloff': falloff, 'v': [], 't': []}
        self.shapes.append(s)
        return s

    @staticmethod
    def quad(s, c, ax_u, ax_v, hu, hv, uv=(0, 0, 1, 1), col=(1, 1, 1, 1), col_far=None):
        b = len(s['v'])
        c = np.asarray(c, float); au = np.asarray(ax_u, float) * hu; av = np.asarray(ax_v, float) * hv
        cf = col_far or col
        for (du, dv, uu, vv, cc) in ((-1, -1, uv[0], uv[1], col), (1, -1, uv[2], uv[1], col), (1, 1, uv[2], uv[3], cf), (-1, 1, uv[0], uv[3], cf)):
            p = c + au * du + av * dv
            s['v'].append((*p, uu, vv, *cc))
        s['t'] += [(b, b + 1, b + 2), (b, b + 2, b + 3)]

    @staticmethod
    def ring(s, ax_u, ax_v, radius, width, seg=56):
        """a circle of radius in the plane (ax_u, ax_v): a band in the plane and a band across it, each width wide"""
        u = np.asarray(ax_u, float); v = np.asarray(ax_v, float); u /= np.linalg.norm(u); v /= np.linalg.norm(v)
        n = np.cross(u, v); n /= np.linalg.norm(n)
        for k in range(seg):
            a0, a1 = 2 * math.pi * k / seg, 2 * math.pi * (k + 1) / seg
            for side in (0, 1):
                pts = []
                for a in (a0, a1):
                    rdir = math.cos(a) * u + math.sin(a) * v
                    c = rdir * radius
                    off = rdir * (width / 2) if side == 0 else n * (width / 2)
                    pts.append((c - off, c + off))
                b = len(s['v'])
                vv0, vv1 = k / seg, (k + 1) / seg
                for p, uu, vv in ((pts[0][0], 0, vv0), (pts[0][1], 1, vv0), (pts[1][1], 1, vv1), (pts[1][0], 0, vv1)):
                    s['v'].append((*p, uu, vv, 1, 1, 1, 1))
                s['t'] += [(b, b + 1, b + 2), (b, b + 2, b + 3)]

    def billboard(self, s, c, size, axes=3):
        X, Y, Z = (1, 0, 0), (0, 1, 0), (0, 0, 1)
        for au, av in [(X, Z), (Y, Z), (X, Y)][:axes]:
            self.quad(s, c, au, av, size, size)

    def trail(self, s, c, width, length, planes=2):
        """a streak along -Y from c (behind a projectile)"""
        c = np.asarray(c, float)
        for k in range(planes):
            ang = k * math.pi / planes
            side = (math.cos(ang), 0, math.sin(ang))
            self.quad(s, c - np.array([0, length / 2, 0]), side, (0, 1, 0), width, length / 2, uv=(0, 1, 1, 0))

    def write(self, path):
        lines = [f'out {path}']
        for s in self.shapes:
            r, g, b, a = s['color']
            lines.append(f"shape {s['name']} tex={s['tex']} color={r:.3f},{g:.3f},{b:.3f},{a:.3f} scale={s['scale']:.2f} blend={s['blend']} falloff={1 if s['falloff'] else 0}")
            for v in s['v']:
                lines.append('v ' + ' '.join(f'{x:.4f}' for x in v))
            for t in s['t']:
                lines.append(f't {t[0]} {t[1]} {t[2]}')
        job = pathlib.Path('/tmp/claude-0') / ('fx_' + pathlib.Path(path).stem + '.txt')
        job.write_text('\n'.join(lines) + '\n')
        r = subprocess.run([FXWRITE, str(job)], capture_output=True, text=True)
        if r.returncode:
            raise SystemExit(r.stderr)


def mix(a, b, t):
    return tuple(a[i] * (1 - t) + b[i] * t for i in range(3))


def build(spell, t):
    sid, name, col, acc, style = spell
    MESH.mkdir(parents=True, exist_ok=True)
    pale = mix(col, (1, 1, 1), 0.65)
    main_tex = {'arcane': t['soft'], 'fire': t['flame'], 'ice': t['crystal'], 'shock': t['soft'], 'thorn': t['soft'], 'blood': t['drop'],
                'wind': t['swirl'], 'soul': t['wisp'], 'dawn': t['rays'], 'void': t['soft']}[style]
    # ---- the glow: a ball of the spell's power, flat quads in the XZ plane (the code turns them to face the eye):
    # a wide soft bloom, the spell's own pattern, a white-hot core. Used for the projectile, the orbs and the hands.
    m = Mesh()
    if style == 'void':
        d = m.shape('dark', t['dark'], (0, 0, 0, 1), 1.0, blend='alpha'); Mesh.quad(d, (0, 0.2, 0), (1, 0, 0), (0, 0, 1), 11, 11)
    for nm, tex, c_, sc, half in (('bloom', t['soft'], col, 0.8, 30), ('body', main_tex, col, 1.9, 16), ('rim', t['ring'], acc, 1.2, 15), ('core', t['core'], pale, 2.8, 8)):
        q = m.shape(nm, tex, (*c_, 1), sc)
        Mesh.quad(q, (0, 0, 0), (1, 0, 0), (0, 0, 1), half, half)
    m.write(MESH / f'{sid}_glow.nif')
    # ---- the bolt: its trail (the ball itself is the glow plus small rings, placed by the code)
    m = Mesh()
    tr = m.shape('trail', t['streak'] if style != 'shock' else t['bolt'], (*col, 1), 1.4); m.trail(tr, (0, 0, 0), 7, 120 if style != 'ice' else 160)
    if style == 'thorn':  # the thorn itself: a long spike along +Y
        sp = m.shape('spike', t['thorn'], (*acc, 1), 1.8)
        for k in range(2):
            ang = k * math.pi / 2
            Mesh.quad(sp, (0, 4, 0), (math.cos(ang), 0, math.sin(ang)), (0, 1, 0), 3, 16, uv=(0, 1, 1, 0))
    if style == 'ice':  # the ice beam bolt: a long crystal lance
        sp = m.shape('lance', t['streak'], (*acc, 1), 2.0)
        for k in range(3):
            ang = k * math.pi / 3
            Mesh.quad(sp, (0, 10, 0), (math.cos(ang), 0, math.sin(ang)), (0, 1, 0), 3.5, 30, uv=(0, 0, 1, 1))
    if style == 'wind':  # the gust: a broad crescent wave, flat and wide
        cr = m.shape('wave', t['crescent'], (*col, 0.9), 1.5)
        Mesh.quad(cr, (0, 0, 0), (1, 0, 0), (0, 1, 0), 70, 40)
        Mesh.quad(cr, (0, -8, 6), (1, 0, 0), (0, 1, 0), 60, 34)
    if style == 'dawn':
        rr = m.shape('flare', t['rays'], (*acc, 1), 1.2); m.billboard(rr, (0, 0, 0), 30, axes=2)
    if style == 'shock':
        sk = m.shape('crackle', t['bolt'], (*acc, 1), 1.6)
        for k in range(3):
            ang = k * math.pi / 3
            Mesh.quad(sk, (0, 0, 0), (math.cos(ang), math.sin(ang) * 0.3, math.sin(ang)), (0, 1, 0.2), 6, 18)
    m.write(MESH / f'{sid}_bolt.nif')
    # ---- the orb between the hands (unit radius ~12; the code scales it with the charge)
    m = Mesh()
    # (the glow is its own mesh, <id>_glow.nif, turned to face the eye every frame by the code: no crossed planes)
    # the rings: thin circular lines orbiting the glowing core (each a narrow band in its plane and one across it,
    # so it reads as a line from every side, never as a flat disc)
    ring = m.shape('ring', t['beam'], (*acc, 1), 1.9)
    for au, av, rad in (((1, 0, 0), (0, 0, 1), 19.0), ((0.7071, 0.7071, 0), (0, 0, 1), 16.0), ((1, 0, 0), (0, 0.7071, 0.7071), 22.0)):
        Mesh.ring(ring, au, av, rad, 0.9)
    m.write(MESH / f'{sid}_orb.nif')
    # ---- the beam: a 64-unit segment along +Y (two crossed planes, the core and the glow)
    m = Mesh()
    btex = t['bolt'] if style == 'shock' else t['beam']
    for nm, w_, c_, sc in (('glow', 0.5, col, 1.3), ('core', 0.16, pale, 2.4)):
        s = m.shape(nm, btex, (*c_, 1), sc)
        for k in range(2):
            ang = k * math.pi / 2
            Mesh.quad(s, (0, 32, 0), (math.cos(ang), 0, math.sin(ang)), (0, 1, 0), w_ * 30, 32, uv=(0, 0, 1, 1))
    m.write(MESH / f'{sid}_beam.nif')
    # ---- the hand glow
    m = Mesh()
    core = m.shape('core', t['core'], (*pale, 1), 1.6); m.billboard(core, (0, 0, 0), 3)
    halo = m.shape('halo', main_tex, (*col, 1), 1.2); m.billboard(halo, (0, 0, 0), 9)
    m.write(MESH / f'{sid}_hand.nif')
    # ---- the aura round a casting hand: tongues of the spell's power flaring out from a glow (the code spins it,
    # flickers it and grows it with the charge)
    m = Mesh()
    # (its glow is the hand's ball glow, faced to the eye by the code)
    tongue_tex = {'fire': t['flame'], 'ice': t['crystal'], 'shock': t['bolt'], 'thorn': t['thorn'], 'blood': t['drop'],
                  'wind': t['swirl'], 'soul': t['wisp'], 'dawn': t['rays']}.get(style, t['streak'])
    tg = m.shape('tongues', tongue_tex, (*mix(col, acc, 0.35), 1), 1.8)
    golden = math.pi * (3 - math.sqrt(5))
    for k in range(14):
        zz = 1 - 2 * (k + 0.5) / 14; rr = math.sqrt(1 - zz * zz); th = golden * k
        d = np.array([math.cos(th) * rr, math.sin(th) * rr, zz])
        side = np.cross(d, [0, 0, 1] if abs(zz) < 0.9 else [1, 0, 0]); side /= np.linalg.norm(side)
        Mesh.quad(tg, d * 9.0, side, d, 3.2, 8.0, uv=(0, 1, 1, 0))
    sp = m.shape('motes', t['sparks'], (*acc, 1), 1.6); m.billboard(sp, (0, 0, 0), 16, axes=2)
    m.write(MESH / f'{sid}_aura.nif')
    # ---- the burst (unit size ~40; the code expands and fades it)
    m = Mesh()
    fl = m.shape('flash', t['soft'], (*pale, 1), 2.0); m.billboard(fl, (0, 0, 0), 40)
    rg = m.shape('ring', t['ring'], (*col, 1), 1.6)
    Mesh.quad(rg, (0, 0, 0), (1, 0, 0), (0, 1, 0), 60, 60)
    Mesh.quad(rg, (0, 0, 0), (1, 0, 0), (0, 0, 1), 50, 50)
    sp = m.shape('sparks', t['sparks'] if style not in ('ice',) else t['crystal'], (*acc, 1), 1.6); m.billboard(sp, (0, 0, 0), 55)
    # tongues of the spell's power thrown outward, and a flat shockwave across the ground
    tg = m.shape('tongues', {'fire': t['flame'], 'ice': t['crystal'], 'shock': t['bolt'], 'thorn': t['thorn'], 'blood': t['drop'],
                             'wind': t['swirl'], 'soul': t['wisp'], 'dawn': t['rays']}.get(style, t['streak']), (*mix(col, acc, 0.3), 1), 1.9)
    golden = math.pi * (3 - math.sqrt(5))
    for k in range(20):
        zz = 1 - 2 * (k + 0.5) / 20; rr = math.sqrt(1 - zz * zz); th = golden * k
        d = np.array([math.cos(th) * rr, math.sin(th) * rr, zz])
        side = np.cross(d, [0, 0, 1] if abs(zz) < 0.9 else [1, 0, 0]); side /= np.linalg.norm(side)
        Mesh.quad(tg, d * 30.0, side, d, 9.0, 22.0, uv=(0, 1, 1, 0))
    sw = m.shape('shock', t['ring'], (*col, 1), 1.5)
    Mesh.quad(sw, (0, 0, -6), (1, 0, 0), (0, 1, 0), 75, 75)
    m.write(MESH / f'{sid}_burst.nif')
    # ---- the spell's logo as a model: the Magic menu's display and the shrine's roll (the medallion in its flames)
    ic = np.asarray(Image.open(ROOT / 'assets' / 'ui' / f'w_{sid}.png').convert('RGBA').resize((512, 512), Image.LANCZOS)).astype(np.float32) / 255
    itex = save(f'icon_{sid}', ic[..., 3], ic[..., :3])
    m = Mesh()
    au = m.shape('aura', t['flamering'], (*col, 1), 1.6)
    Mesh.quad(au, (0, 0.4, 0), (1, 0, 0), (0, 0, 1), 15.7, 15.7, uv=(0, 1, 1, 0))
    lg = m.shape('logo', itex, (1, 1, 1, 1), 1.0, blend='alpha')
    Mesh.quad(lg, (0, 0, 0), (1, 0, 0), (0, 0, 1), 12, 12, uv=(0, 1, 1, 0))
    m.write(MESH / f'{sid}_icon.nif')


def status(t):
    """looks on the struck (attached by the game at the actor's feet, ~130 tall): ice shell, roots, burning, poison"""
    rng = np.random.default_rng(5)
    m = Mesh()                                                                      # ice: shards round the body, a cold glow
    g = m.shape('glow', t['soft'], (0.52, 0.86, 1.0, 1), 1.0)
    for z in (30, 70, 110):
        m.billboard(g, (0, 0, z), 45, axes=2)
    sh = m.shape('shards', t['crystal'], (0.85, 0.97, 1.0, 1), 1.6)
    for k in range(14):
        a = rng.uniform(0, 2 * math.pi); z = rng.uniform(10, 140); r = rng.uniform(18, 30)
        Mesh.quad(sh, (math.cos(a) * r, math.sin(a) * r, z), (-math.sin(a), math.cos(a), 0), (0, 0, 1), rng.uniform(10, 18), rng.uniform(14, 26))
    m.write(MESH / 'status_iceshell.nif')
    m = Mesh()                                                                      # roots: thorned vines up the legs
    v = m.shape('vines', t['thorn'], (0.45, 1.0, 0.22, 1), 1.5)
    for k in range(10):
        a = k * 2 * math.pi / 10 + rng.uniform(-0.2, 0.2); r = rng.uniform(12, 24)
        Mesh.quad(v, (math.cos(a) * r, math.sin(a) * r, 34), (-math.sin(a), math.cos(a), 0), (math.cos(a) * 0.3, math.sin(a) * 0.3, 1), 6, 36, uv=(0, 1, 1, 0))
    g = m.shape('glow', t['soft'], (0.45, 1.0, 0.22, 1), 0.8); m.billboard(g, (0, 0, 10), 50, axes=1)
    m.write(MESH / 'status_roots.nif')
    m = Mesh()                                                                      # burning: flames licking up the body
    fl = m.shape('flames', t['flame'], (1.0, 0.45, 0.08, 1), 1.8)
    for k in range(9):
        a = rng.uniform(0, 2 * math.pi); z = rng.uniform(30, 120)
        Mesh.quad(fl, (math.cos(a) * 18, math.sin(a) * 18, z), (-math.sin(a), math.cos(a), 0), (0, 0, 1), 16, 24)
    m.write(MESH / 'status_burn.nif')
    m = Mesh()                                                                      # poison: a sickly green haze
    pz = m.shape('haze', t['soft'], (0.45, 1.0, 0.22, 0.6), 0.9)
    for z in (40, 90):
        m.billboard(pz, (0, 0, z), 50, axes=2)
    m.write(MESH / 'status_poison.nif')


def main():
    t = textures()
    status(t)
    for s in SPELLS:
        build(s, t)
        print('built', s[0], s[1])


if __name__ == '__main__':
    main()
