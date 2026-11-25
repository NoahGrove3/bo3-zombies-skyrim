"""Each weapon's own signature, laid over its look (Noah, 2026-11-25: "there needs to be more variety ... to match what the
weapons are actually designed to do so we arent just recycling the same effect with a different color. Remake these and build
what effects each weapon should have based on their weapon description and model" - Both looks and abilities, all at once).

The look Noah picked (its family and colour) stays as each weapon's ground; over it each weapon wears what only it does - the
Keyhammer's chains, the Grief Blade's tears, the Sacrifice Knife's venom, the Gravebell's tolling rings - drawn from what the
weapon does (docs/workshop/effects_design.json) and fitted to its own model. Every piece comes to a point or fades at its ends
(no hard cut-offs) and stands in 3D round the steel, not flat on it. Textures 2K; 4K for the wonders and Alduin's Bane.

    import weaponsig; weaponsig.textures()            (tools/weaponfx.py textures calls it)
    weaponsig.apply(F, emit, cols, small, rng)        (tools/weaponfx.py art_part calls it, after the look)
"""
import numpy as np

# the weapon's art key -> its signature and the signature's own settings
SIG = {
    'priest_vahlok': ('shackles', {}), 'priest_nahkriin': ('glare', {}), 'priest_volsung': ('gust', {}),
    'priest_zahkriisos': ('crystals', {'n': 26, 'len': (1.6, 4.2)}), 'priest_otar': ('arcs', {'n': 9}),
    'priest_rahgot': ('venom', {}), 'priest_krosis': ('tears', {}), 'priest_hevnoraak': ('seal', {}),
    'priest_vokun': ('dream', {}), 'priest_morokei': ('runering', {}),
    'tab_stormtalons': ('clawarcs', {}), 'tab_gravebell': ('toll', {}), 'tab_talons': ('clawdrip', {}),
    'tab_riverstone': ('stone', {}), 'tab_wyrmspine': ('dragonfire', {}), 'tab_lanternwick': ('snow', {}),
    'tab_cinderclaws': ('clawembers', {}), 'tab_eclipse': ('eclipse', {}), 'tab_frostmourn': ('breath', {}),
    'tab_silverscythe': ('glint', {}), 'tab_bonereaper': ('souls', {}), 'tab_wyrmtooth': ('fangs', {}),
    'tab_bloodmoon': ('bloodrun', {}), 'fang': ('alduin', {}),
    'alch': ('darkembers', {}), 'carian': ('glintstone', {}), 'radiant': ('sunrays', {}),
    'ss_none': ('none', {}), 'ss_fire': ('tipsparks', {}), 'ss_frost': ('crystals', {'n': 30, 'len': (0.5, 1.3), 'w': 0.28}),
    'ss_shock': ('tiparcs', {}), 'ss_sun': ('solarflare', {}), 'ss_blood': ('wispstream', {}), 'slayer': ('runes', {}),
}


def sig_key(key):
    """the art key's signature key: a class weapon's tier models share their look key's; the left claws their right's"""
    if key.startswith('cls_'): return key.rsplit('_', 1)[-1] if not key.split('_', 2)[-1].startswith('ss_') else 'ss_' + key.rsplit('_', 1)[-1]
    return key[:-2] if key.endswith('_l') else key


# ---------------------------------------------------------------- the signatures' textures (u: 0 on the ribbon's centre line,
# 1 at its sides; v along it)
def textures():
    import weaponfx as W
    fbm, smooth, grey, save = W.fbm, W.smooth, W.grey, W.save_tex
    rng = np.random.default_rng(2511)
    H, Wd = 2048, 256
    u = np.linspace(0, 1, Wd)[None, :].repeat(H, 0)
    v = np.linspace(0, 1, H, endpoint=False)[:, None].repeat(Wd, 1)
    yy, xx = np.mgrid[0:H, 0:Wd].astype(float)
    # chain links: oval links face-on and edge-on by turns, glowing from inside their iron
    n = 16; ph = (v * n) % 1.0; k = np.floor(v * n) % 2
    ex = np.hypot((ph - 0.5) / 0.62, u / 0.55)
    face = np.exp(-((ex - 0.78) / 0.1) ** 2)
    edge = np.exp(-(u / 0.14) ** 2) * smooth(0.0, 0.12, ph) * smooth(1.0, 0.88, ph)
    link = np.where(k == 0, face, edge)
    save(grey(0.45 + 0.55 * link, np.clip(link * 1.2 + np.exp(-(u / 0.6) ** 2) * 0.08, 0, 1)), 'sig_chain')
    # one sweep: a soft band of glare across the ribbon, once along its length
    band = np.exp(-((v - 0.5) / 0.045) ** 2) + np.exp(-((v - 0.5) / 0.012) ** 2) * 0.6
    save(grey(0.7 + 0.3 * band, np.clip(band, 0, 1) * smooth(1.0, 0.35, u)), 'sig_band', detail=False)
    # streaks: thin lines drawn out along the ribbon, broken into long dashes (wind, tears, souls)
    lines = fbm(1, Wd, 1, 22, 31)[0][None, :].repeat(H, 0)
    thin = np.exp(-((((u * 22 + lines * 3) % 1.0) - 0.5) / 0.09) ** 2)
    dash = smooth(0.45, 0.7, fbm(H, Wd, 6, 22, 33))
    save(grey(0.55 + 0.45 * thin, np.clip(thin * dash * 1.3, 0, 1) * smooth(1.0, 0.3, u)), 'sig_streak')
    # drops: beads and long drips along the ribbon (venom, blood)
    d = np.full((H, Wd), 0.0)
    for _ in range(46):
        cy, cx = rng.uniform(0, H), rng.uniform(0, Wd * 0.55); L = rng.uniform(30, 120); r = rng.uniform(7, 15)
        dy = (yy - cy + H / 2) % H - H / 2
        body = np.exp(-((xx - cx) / r) ** 2 - (np.clip(dy, -L, 0) / L * 0 + np.maximum(dy, 0) / r) ** 2) * (dy > -L)
        tail = np.exp(-((xx - cx) / (r * 0.35)) ** 2) * smooth(-L, 0, dy) * (dy <= 0) * (dy > -L)
        d = np.maximum(d, np.maximum(body, tail * 0.7))
    d = np.maximum(d, d[:, ::-1] * 0)   # (one side; the ribbon mirrors it)
    save(grey(0.35 + 0.65 * d, np.clip(d * 1.3, 0, 1)), 'sig_drops')
    # concentric rings rolling outward (v the radius)
    rings = np.exp(-(((v * 5) % 1.0 - 0.5) / 0.07) ** 2)
    save(grey(0.6 + 0.4 * rings, rings * smooth(1.0, 0.2, u) * 0.9), 'sig_rings', detail=False)
    # runes along the ribbon's centre (each glyph a few strokes)
    img = np.zeros((H, Wd)); G = 26
    for g in range(G):
        cy = (g + 0.5) * H / G; s_ = H / G * 0.36
        for _ in range(3):
            a = np.array([rng.uniform(0, 0.5) * Wd, cy + rng.uniform(-1, 1) * s_]); b = np.array([rng.uniform(0, 0.5) * Wd, cy + rng.uniform(-1, 1) * s_])
            pa = np.dstack([xx - a[0], yy - a[1]]); ba = b - a
            t = np.clip((pa @ ba) / (ba @ ba), 0, 1)
            img = np.maximum(img, np.exp(-(np.hypot(xx - (a[0] + ba[0] * t), yy - (a[1] + ba[1] * t)) / 5.0) ** 2))
    save(grey(0.5 + 0.5 * img, np.clip(img * 1.2 + np.exp(-(u / 0.5) ** 2) * 0.1, 0, 1)), 'sig_runes')
    save(grey(0.5 + 0.5 * img, np.clip(img * 1.2 + np.exp(-(u / 0.5) ** 2) * 0.1, 0, 1)), 'sig_runes4k', long_side=4096)
    # embers: sparks with short tails, scattered along the ribbon
    e = np.zeros((H, Wd))
    for _ in range(70):
        cy, cx = rng.uniform(0, H), rng.uniform(0, Wd * 0.8); L = rng.uniform(20, 70)
        dy = (yy - cy + H / 2) % H - H / 2
        e = np.maximum(e, np.exp(-((xx - cx) / 4.5) ** 2) * np.exp(-np.maximum(dy, 0) / 4.0) * (dy > -L) * np.exp(np.minimum(dy, 0) / L))
    save(grey(0.55 + 0.45 * e, np.clip(e * 1.5, 0, 1)), 'sig_embers')
    save(grey(0.55 + 0.45 * e, np.clip(e * 1.5, 0, 1)), 'sig_embers4k', long_side=4096)
    # snow: flakes drifting along the ribbon
    sn = np.zeros((H, Wd))
    for _ in range(140):
        cy, cx = rng.uniform(0, H), rng.uniform(0, Wd); r = rng.uniform(2.5, 6)
        dy = (yy - cy + H / 2) % H - H / 2
        sn = np.maximum(sn, np.exp(-(((xx - cx) / r) ** 2 + (dy / (r * 1.8)) ** 2)))
    save(grey(0.85 + 0.15 * sn, np.clip(sn * 1.3, 0, 1) * smooth(1.0, 0.4, u)), 'sig_snow')
    # cards (square)
    S = 1024
    cy, cx = np.mgrid[0:S, 0:S].astype(float); X = (cx - S / 2 + 0.5) / (S / 2); Y = (cy - S / 2 + 0.5) / (S / 2); R = np.hypot(X, Y)
    TH = np.arctan2(Y, X)
    # an ice shard: a long faceted crystal standing up from the card's foot (v = 1 the foot)
    w_ = (1 - (1 - (cy / S))) * 0.0 + 0.32 * (cy / S) ** 0.7
    shard = smooth(0.06, 0.0, np.abs(X) - w_) * smooth(0.0, 0.04, cy / S)
    facet = 0.55 + 0.45 * np.cos(X / np.maximum(w_, 1e-3) * 2.2) * smooth(0.0, 0.3, cy / S)
    rim = np.exp(-((np.abs(X) - w_) / 0.02) ** 2) * smooth(0.0, 0.05, cy / S)
    save(grey(np.clip(0.45 + 0.4 * facet + rim * 0.5, 0, 1), np.clip(shard * 0.75 + rim, 0, 1)), 'sig_crystal')
    # a seal: a ring of glyphs round a cracked disc
    seal = np.exp(-((R - 0.78) / 0.025) ** 2) + np.exp(-((R - 0.6) / 0.018) ** 2) * 0.8
    glyph = (np.abs(np.sin(TH * 12)) > 0.75) * smooth(0.64, 0.66, R) * smooth(0.76, 0.74, R) * 0.9
    cr = np.zeros((S, S))
    for k_ in range(9):
        a0 = rng.uniform(0, 2 * np.pi); path = W._bolt(rng, (S / 2, S / 2), (S / 2 + np.cos(a0) * S * 0.38, S / 2 + np.sin(a0) * S * 0.38), 0.22, 6)
        cr = np.maximum(cr, np.exp(-(W._poly_field(S, S, [path], wrapv=False) / 3.0) ** 2))
    sl = np.clip(seal + glyph + cr * 0.9, 0, 1) * smooth(1.0, 0.92, R)
    save(grey(0.5 + 0.5 * sl, sl), 'sig_seal')
    # the eclipse: a dark disc crossing a bright corona
    cor = np.exp(-np.maximum(R - 0.42, 0) / 0.12) * smooth(0.40, 0.44, R) * (0.75 + 0.25 * np.cos(TH * 18) * smooth(0.45, 0.8, R))
    dark = smooth(0.43, 0.39, np.hypot(X + 0.08, Y - 0.05))
    save(np.dstack([np.clip(0.5 + 0.5 * cor, 0, 1)] * 3 + [np.clip(cor + dark * 0.85, 0, 1)]), 'sig_eclipse')
    # a flare: a star with long rays
    fl = np.exp(-R / 0.08) + np.exp(-np.abs(Y) / 0.012) * np.exp(-np.abs(X) / 0.6) + np.exp(-np.abs(X) / 0.012) * np.exp(-np.abs(Y) / 0.6)
    fl += 0.35 * np.exp(-R / 0.3)
    save(grey(np.clip(0.6 + 0.4 * fl, 0, 1), np.clip(fl, 0, 1) * smooth(1.0, 0.85, R)), 'sig_flare')
    # glintstone sparkles: a 4 x 4 flipbook, each cell a few stars waxing and waning
    cs = 256; atlas = np.zeros((cs * 4, cs * 4, 4))
    yy2, xx2 = np.mgrid[0:cs, 0:cs].astype(float)
    stars = [(rng.uniform(20, cs - 20), rng.uniform(20, cs - 20), rng.uniform(0, 1), rng.uniform(6, 16)) for _ in range(9)]
    for f in range(16):
        c = np.zeros((cs, cs))
        for (sy, sx, phs, sz) in stars:
            k_ = max(0.0, np.sin(2 * np.pi * (f / 16 + phs))) ** 3
            dx, dy = (xx2 - sx) / sz, (yy2 - sy) / sz
            c = np.maximum(c, k_ * (np.exp(-np.hypot(dx, dy) * 2.2) + np.exp(-np.abs(dy) * 9) * np.exp(-np.abs(dx) * 0.9) * 0.6 + np.exp(-np.abs(dx) * 9) * np.exp(-np.abs(dy) * 0.9) * 0.6))
        atlas[(f // 4) * cs:(f // 4 + 1) * cs, (f % 4) * cs:(f % 4 + 1) * cs] = grey(np.clip(0.6 + 0.4 * c, 0, 1), np.clip(c, 0, 1))
    save(atlas, 'sig_sparkle', detail=False)
    # river stone: a crust of pebbles with moss-light in the seams
    S2 = 512; pts = rng.random((120, 2)) * S2
    from scipy.spatial import cKDTree
    yy3, xx3 = np.mgrid[0:S2, 0:S2].astype(float)
    P = np.vstack([pts + [ox, oy] for ox in (-S2, 0, S2) for oy in (-S2, 0, S2)])
    dd, _ = cKDTree(P).query(np.c_[xx3.ravel(), yy3.ravel()], k=2)
    seam = (1 - smooth(0.0, 5.0, (dd[:, 1] - dd[:, 0]).reshape(S2, S2)))
    keep = smooth(0.3, 0.6, fbm(S2, S2, 4, 4, 77))
    save(grey(0.4 + 0.6 * seam, seam * keep * 0.9), 'sig_stone')


# ---------------------------------------------------------------- the weapon's shape, seen down its length
def profile(F):
    """for each column of the steel's mask (x along the blade): its top, bottom and middle across it (nan where empty)"""
    mask, res, lo = F['mask'], F['res'], F['lo']
    gh, gw = mask.shape
    xs = lo[0] + (np.arange(gw) + 0.5) * res
    has = mask.any(0)
    ys = lo[1] + (np.arange(gh) + 0.5) * res
    top = np.where(has, np.array([ys[mask[:, i]].max() if has[i] else np.nan for i in range(gw)]), np.nan)
    bot = np.where(has, np.array([ys[mask[:, i]].min() if has[i] else np.nan for i in range(gw)]), np.nan)
    return xs, bot, top, has


def depth_at(F, x, y):
    i = int(np.clip((y - F['lo'][1]) / F['res'], 0, F['mask'].shape[0] - 1)); j = int(np.clip((x - F['lo'][0]) / F['res'], 0, F['mask'].shape[1] - 1))
    return float(F['depth'][i, j])


# ---------------------------------------------------------------- drawing
class Pen:
    def __init__(s, F, emit, pal, small, rng, gain):
        s.F, s.emit, s.pal, s.small, s.rng, s.gain = F, emit, pal, small, rng, gain
        s.n = 0
        import weaponfx as W
        s.W = W

    def P(s, x, y, d=0.0):
        return s.W.to3(s.F, np.array([[x, y]]), d)[0]

    def ribbon(s, C, Sd, w, v, tex, gain=1.0, alpha=None, scroll=0.0, flip=None, fps=None, blend='add', dark=False, pal=None, pulse=None):
        """a ribbon down the points C (N x 3), its width w (N) out along Sd (N x 3) to both sides: the texture's centre line (u 0)
        down its middle, fading to nothing at its sides; its ends tapered to points, its alpha (N) easing in and out"""
        C = np.asarray(C, float); Sd = np.asarray(Sd, float); N = len(C)
        if N < 2: return
        w = np.broadcast_to(np.asarray(w, float), (N,)).copy() * 1.3   # (seen at arm's length in game: a touch broader)
        t = np.linspace(0, 1, N); tap = np.clip(np.minimum(t, 1 - t) / 0.18, 0, 1); tap = tap * tap * (3 - 2 * tap)
        w = w * (0.08 + 0.92 * tap)
        al = (np.ones(N) if alpha is None else np.asarray(alpha, float)) * tap
        verts, uvs, cols, tris = [], [], [], []
        for i in range(N):
            for off, uu, aa in ((-1.0, 1.0, 0.0), (-0.55, 0.55, 0.8), (0.0, 0.0, 1.0), (0.55, 0.55, 0.8), (1.0, 1.0, 0.0)):
                verts.append(C[i] + Sd[i] * off * w[i]); uvs.append((uu, v[i])); cols.append(al[i] * aa)
        for i in range(N - 1):
            for r in range(4):
                a, b = i * 5 + r, i * 5 + r + 1
                tris += [(a, b, b + 5), (a, b + 5, a + 5)]
        s.emit(f'sig{s.n}', tex, pal or s.pal, gain * s.gain * 1.5, verts, uvs, tris, cols, flip=flip, fps=fps, vscroll=0 if flip else scroll,
               clamp=None if flip else 1, blend=blend, dark=dark, pulse=pulse)
        s.n += 1

    def card(s, at, a1, a2, tex, gain=1.0, pulse=None, flip=None, fps=None, pal=None, blend='add', dark=False):
        """a quad round 'at' spanning +-a1 (u) and +-a2 (v): both faces"""
        at = np.asarray(at, float); a1 = np.asarray(a1, float); a2 = np.asarray(a2, float)
        verts = [at - a1 - a2, at + a1 - a2, at + a1 + a2, at - a1 + a2]
        uvs = [(0, 1), (1, 1), (1, 0), (0, 0)]
        s.emit(f'sig{s.n}', tex, pal or s.pal, gain * s.gain, verts, uvs, [(0, 1, 2), (0, 2, 3)], [1, 1, 1, 1], flip=flip, fps=fps,
               pulse=pulse, blend=blend, dark=dark)
        s.n += 1


def apply(F, emit, cols, small, rng):
    import weaponfx as W
    key = sig_key(F['key'])
    sig, opt = SIG.get(key, ('none', {}))
    if sig == 'none' or not F['edges']: return 0
    pen = Pen(F, emit, cols[-1] if sig != 'eclipse' else cols[0], small, rng, F.get('gain', 1.0))
    xs, bot, top, has = profile(F)
    X = xs[has]; x0, x1 = X.min(), X.max()
    L = x1 - x0
    mid = lambda x: float(np.interp(x, xs[has], ((bot + top) / 2)[has]))
    half = lambda x: float(np.interp(x, xs[has], ((top - bot) / 2)[has]))
    e1, e2, e3 = F['e1'], F['e2'], F['e3']
    sc = 0.45 if small else 1.0
    edges = F['edges']
    allp = np.concatenate([p for p, _ in edges]); alln = np.concatenate([n for _, n in edges])
    tipi = int(np.argmax(allp[:, 0])); tip = allp[tipi]

    def ring_pts(x, r, n=48, tilt=0.0, phase=0.0):
        th = np.linspace(0, 2 * np.pi, n) + phase
        C = np.array([pen.P(x + np.sin(t) * r * tilt, mid(x) + np.cos(t) * r, np.sin(t) * r) for t in th])
        Sd = np.repeat(e1[None, :], n, 0)
        return C, Sd, th

    def edge_samples(k, lo_f=0.0, hi_f=1.0):
        """k points along the edges between those fractions of the blade's length"""
        ok = np.where((allp[:, 0] >= x0 + lo_f * L) & (allp[:, 0] <= x0 + hi_f * L))[0]
        if len(ok) == 0: return []
        return [ok[i] for i in rng.choice(len(ok), size=min(k, len(ok)), replace=False)]

    def streak(i, out, back, length, tex, w, gain, scroll, d_lift=None, n=14, pal=None, blend='add', dark=False):
        """a streak peeling off edge point i: out along its normal, back toward the hilt (negative back: toward the tip)"""
        p, nn = allp[i], alln[i]
        dirv = nn * out + np.array([-back, 0.0]); dirv /= np.linalg.norm(dirv) + 1e-9
        dl = rng.uniform(-1.2, 1.2) * sc if d_lift is None else d_lift
        t = np.linspace(0, 1, n)
        C = np.array([pen.P(*(p + dirv * length * tt + np.array([0, 0.0])), dl * tt + (depth_at(F, *p) if dl == 0 else 0)) for tt in t])
        tan = np.diff(C, axis=0, append=C[-1:] + (C[-1:] - C[-2:-1])); tan /= np.linalg.norm(tan, axis=1, keepdims=True) + 1e-9
        Sd = np.cross(tan, e3); Sd = Sd * 0.6 + e3 * 0.4; Sd /= np.linalg.norm(Sd, axis=1, keepdims=True)
        pen.ribbon(C, Sd, w, t * length / 10.0, tex, gain, scroll=scroll, pal=pal, blend=blend, dark=dark)

    if sig == 'shackles':   # the Keyhammer: two chains winding round the head, the links creeping toward it
        a = x0 + 0.05 * L
        for k, phs in enumerate((0.0, np.pi)):
            n = 90; t = np.linspace(0, 1, n); x = a + (x1 - a) * t; th = t * 2 * np.pi * 3.2 + phs
            r = np.array([half(xx) for xx in x]) + 0.9 * sc
            C = np.array([pen.P(x[i], mid(x[i]) + np.cos(th[i]) * r[i], np.sin(th[i]) * r[i]) for i in range(n)])
            seg = np.r_[0, np.cumsum(np.linalg.norm(np.diff(C, axis=0), axis=1))]
            pen.ribbon(C, np.repeat(e1[None], n, 0), 1.1 * sc, seg / 9.0, 'sig_chain', 1.2, scroll=-9.0, blend='alpha', dark=True)   # (the iron)
            pen.ribbon(C, np.repeat(e1[None], n, 0), 1.1 * sc, seg / 9.0, 'sig_chain', 0.7, scroll=-9.0)   # (the light inside it)
    elif sig == 'glare':   # the Mistblade: a blinding glare sweeping up over both faces now and then
        n = 40; x = np.linspace(x0, x1, n)
        for side in (-1, 1):
            C = np.array([pen.P(xx, mid(xx), side * (depth_at(F, xx, mid(xx)) + 0.25)) for xx in x])
            w = np.array([half(xx) * 1.15 for xx in x])
            pen.ribbon(C, np.repeat(e2[None], n, 0), w, (x - x0) / (L * 2.6), 'sig_band', 0.55, scroll=3.4)
    elif sig == 'gust':   # the Windcutter: smoke torn back off the edge in long wind streaks
        for i in edge_samples(16, 0.1, 1.0):
            streak(i, 0.55, 1.0, rng.uniform(7, 13) * sc, 'sig_streak', rng.uniform(0.5, 0.9) * sc, 0.9, 0.7)
    elif sig == 'crystals':   # ice standing out of the edges, glinting in turn
        lo_, hi_ = opt.get('len', (1.6, 4.2)); wv = opt.get('w', 0.55)
        for i in edge_samples(opt.get('n', 24), 0.05, 0.97):
            p, nn = allp[i], alln[i]; ln = rng.uniform(lo_, hi_) * sc
            tilt = rng.uniform(-0.7, 0.7)
            out = pen.P(*(p + nn * ln * 0.5), tilt * ln * 0.5) - pen.P(*p, 0)
            base = pen.P(*p, 0)
            across = np.cross(out / (np.linalg.norm(out) + 1e-9), e3); across = across * 0.7 + e3 * rng.uniform(-0.5, 0.5)
            across = across / np.linalg.norm(across) * wv * sc * rng.uniform(0.7, 1.3)
            pen.card(base + out, across, -out, 'sig_crystal', 1.0, pulse=(rng.uniform(1.6, 3.4), 0.55, 1.35))
    elif sig in ('arcs', 'clawarcs', 'tiparcs'):   # lightning leaping across the steel (Otar), off the claws' points, off the point
        n_ = {'arcs': opt.get('n', 9), 'clawarcs': 4, 'tiparcs': 5}[sig]
        for k in range(n_):
            if sig == 'arcs':
                x = x0 + rng.uniform(0.12, 0.92) * L; side = rng.choice([-1, 1]); n = 16; t = np.linspace(0, 1, n)
                y = mid(x) + (t - 0.5) * 2 * (half(x) + 0.6 * sc)
                xx = x + np.sin(t * np.pi) * rng.uniform(-3, 3) * sc
                C = np.array([pen.P(xx[i], y[i], side * (depth_at(F, x, mid(x)) + 0.3 + np.sin(t[i] * np.pi) * 1.6 * sc)) for i in range(n)])
            else:
                n = 14; t = np.linspace(0, 1, n)
                ang = rng.uniform(-1.0, 1.0); dl = rng.uniform(-0.8, 0.8)
                ln = (rng.uniform(4, 8) if sig == 'clawarcs' else rng.uniform(3, 6)) * sc
                C = np.array([pen.P(tip[0] + np.cos(ang) * ln * tt, tip[1] + np.sin(ang) * ln * tt + np.sin(tt * np.pi) * rng.uniform(-1, 1) * sc, dl * ln * tt) for tt in t])
            seg = np.r_[0, np.cumsum(np.linalg.norm(np.diff(C, axis=0), axis=1))]
            tan = np.gradient(C, axis=0); Sd = np.cross(tan, e2 if sig == 'arcs' else e3); Sd /= np.linalg.norm(Sd, axis=1, keepdims=True) + 1e-9
            pen.ribbon(C, Sd, 0.9 * sc, seg / max(seg[-1], 1e-3), 'bolt_edge', 1.5, flip=(8, 4), fps=24 + k)
    elif sig in ('venom', 'bloodrun', 'clawdrip'):   # drops: venom on the edges running off the point; blood down the faces; off the claws' points
        if sig == 'venom':
            for i in edge_samples(7, 0.35, 0.98):
                p = allp[i]; n = 18; t = np.linspace(0, 1, n)
                x = p[0] + (tip[0] + 3.5 * sc - p[0]) * t; y = p[1] + (tip[1] - p[1]) * t ** 1.6
                C = np.array([pen.P(x[j], y[j], 0) for j in range(n)])
                pen.ribbon(C, np.repeat(e3[None], n, 0), 0.45 * sc, (x - x0) / 12.0, 'sig_drops', 1.1, scroll=-2.6)
        elif sig == 'bloodrun':
            for k in range(6):
                side = rng.choice([-1, 1]); yoff = rng.uniform(-0.6, 0.6)
                xa = x0 + rng.uniform(0.2, 0.55) * L; n = 22; t = np.linspace(0, 1, n)
                x = xa + (x1 + 2.5 * sc - xa) * t
                C = np.array([pen.P(x[j], mid(min(x[j], x1)) + yoff * half(min(x[j], x1)), side * (depth_at(F, min(x[j], x1), mid(min(x[j], x1))) + 0.15)) for j in range(n)])
                pen.ribbon(C, np.repeat(e2[None], n, 0), 0.5 * sc, (x - x0) / 14.0, 'sig_drops', 1.0, scroll=-3.6)
        else:
            for k in range(3):
                n = 12; t = np.linspace(0, 1, n); ang = rng.uniform(-0.5, 0.5)
                C = np.array([pen.P(tip[0] + np.cos(ang) * 3.0 * tt, tip[1] + np.sin(ang) * 3.0 * tt, rng.uniform(-0.2, 0.2)) for tt in t])
                pen.ribbon(C, np.repeat(e3[None], n, 0), 0.32, t * 0.5, 'sig_drops', 1.1, scroll=-1.6)
    elif sig == 'tears':   # the Grief Blade: slow streaks weeping down both faces toward the hilt
        for k in range(10):
            side = rng.choice([-1, 1]); yoff = rng.uniform(-0.8, 0.8)
            xa = x0 + rng.uniform(0.05, 0.35) * L; xb = x0 + rng.uniform(0.6, 0.98) * L; n = 20
            x = np.linspace(xa, xb, n); wob = np.sin(np.linspace(0, rng.uniform(2, 4), n) + rng.uniform(0, 6)) * 0.12
            C = np.array([pen.P(xx, mid(xx) + (yoff + wob[j]) * half(xx), side * (depth_at(F, xx, mid(xx)) + 0.18)) for j, xx in enumerate(x)])
            pen.ribbon(C, np.repeat(e2[None], n, 0), 0.4 * sc, (x - x0) / 16.0, 'sig_drops', 1.1, scroll=-5.5)
    elif sig == 'seal':   # the Sealbreaker: a broken seal glowing in each face, pulsing outward
        xc = (x0 + x1) / 2; r = min(half(xc) * 1.05, L * 0.45)
        for side in (-1, 1):
            at = pen.P(xc, mid(xc), side * (depth_at(F, xc, mid(xc)) + 0.2))
            pen.card(at, e1 * r, e2 * r, 'sig_seal', 1.25, pulse=(2.2, 0.5, 1.3))
    elif sig == 'dream':   # Nightcaller: slow rings of dream-light turning round the blade, drowsy
        for k, f in enumerate((0.3, 0.55, 0.8)):
            x = x0 + f * L; r = half(x) + 1.8 * sc
            C, Sd, th = ring_pts(x, r, 56, tilt=0.25, phase=k)
            pen.ribbon(C, Sd, 1.6 * sc, th / (2 * np.pi), 'sig_streak', 2.4, scroll=7.0 + 2 * k)
    elif sig == 'runering':   # the Arcane Maul: a ring of runes turning round the maul's head
        x = x0 + 0.72 * L; r = half(x) + 2.2 * sc
        for tilt, sp in ((0.2, 9.0), (-0.35, -12.0)):
            C, Sd, th = ring_pts(x, r, 64, tilt=tilt)
            pen.ribbon(C, Sd, 1.3 * sc, th / (2 * np.pi) * 2, 'sig_runes', 1.3, scroll=sp)
    elif sig == 'toll':   # the Gravebell: rings of sound rolling off the bell head on a slow beat
        x = x0 + 0.78 * L; r0 = half(x) * 0.6
        for plane in (0, 1):
            n = 48; th = np.linspace(0, 2 * np.pi, n)
            for ring in range(2):
                rr = r0 + 3.0 + ring * 4.0
                if plane == 0: C = np.array([pen.P(x + np.cos(t) * rr * 0.25, mid(x) + np.cos(t) * rr, np.sin(t) * rr) for t in th])
                else: C = np.array([pen.P(x + np.cos(t) * rr, mid(x) + np.sin(t) * rr, 0) for t in th])
                Sd = C - pen.P(x, mid(x), 0); Sd /= np.linalg.norm(Sd, axis=1, keepdims=True) + 1e-9
                pen.ribbon(C, Sd, 1.6, np.full(n, 0.5) + ring * 0.2, 'sig_rings', 2.4, scroll=1.8)
    elif sig == 'stone':   # the Riverstone: river stone crusted on the blade, moss-light in its seams
        for side in (-1, 1):
            n = 30; x = np.linspace(x0, x1, n)
            for yo in (-0.5, 0.5):
                C = np.array([pen.P(xx, mid(xx) + yo * half(xx), side * (depth_at(F, xx, mid(xx)) + 0.16)) for xx in x])
                pen.ribbon(C, np.repeat(e2[None], n, 0), np.array([half(xx) * 0.7 for xx in x]), (x - x0) / 18.0, 'sig_stone', 0.9, scroll=0, pulse=(5.0, 0.6, 1.1))
    elif sig == 'dragonfire':   # the Wyrmspine Pike: fire pouring forward off its point like a dragon's breath
        for k, ang in enumerate((0.0, 2.1, 4.2)):
            n = 20; t = np.linspace(0, 1, n); ln = 12.0 * sc
            spread = 0.15 + t * 0.35
            C = np.array([pen.P(tip[0] + ln * tt, tip[1] + np.cos(ang) * spread[j] * 3 * sc, np.sin(ang) * spread[j] * 3 * sc) for j, tt in enumerate(t)])
            Sd = np.repeat((e2 * np.cos(ang + 1.57) + e3 * np.sin(ang + 1.57))[None], n, 0)
            pen.ribbon(C, Sd, (0.5 + t * 2.4) * sc, t * 1.4, 'fire_rise0', 1.0, scroll=-0.8)
    elif sig == 'snow':   # the Rimewick: snow whirling round each head
        for (pts, _) in edges:
            xa, xb = pts[:, 0].min(), pts[:, 0].max(); n = 70; t = np.linspace(0, 1, n)
            x = xa + (xb - xa) * t; th = t * 2 * np.pi * 2.2
            r = np.array([half(xx) for xx in x]) + 1.6 * sc
            C = np.array([pen.P(x[i], mid(x[i]) + np.cos(th[i]) * r[i], np.sin(th[i]) * r[i]) for i in range(n)])
            seg = np.r_[0, np.cumsum(np.linalg.norm(np.diff(C, axis=0), axis=1))]
            pen.ribbon(C, np.repeat(e1[None], n, 0), 1.4 * sc, seg / 16.0, 'sig_snow', 1.9, scroll=-4.0)
    elif sig in ('clawembers', 'darkembers', 'tipsparks', 'alduin'):
        if sig == 'clawembers':   # embers streaming back off each claw's point as it moves
            for k in range(4):
                n = 14; t = np.linspace(0, 1, n); ang = np.pi + rng.uniform(-0.5, 0.5)
                C = np.array([pen.P(tip[0] + np.cos(ang) * 6 * tt, tip[1] + np.sin(ang) * 6 * tt + tt * rng.uniform(-1, 1), rng.uniform(-0.6, 0.6) * tt) for tt in t])
                pen.ribbon(C, np.repeat(e3[None], n, 0), 0.6, t * 0.6, 'sig_embers', 1.3, scroll=-1.2)
        elif sig == 'darkembers':   # the Shadowflame: dark embers lifting off the fire
            for i in edge_samples(12, 0.05, 1.0):
                streak(i, 1.0, -0.3, rng.uniform(5, 9) * sc, 'sig_embers', 0.9 * sc, 1.2, -1.6)
        elif sig == 'tipsparks':   # the rapier in fire: sparks lifting off the point
            for k in range(5):
                n = 12; t = np.linspace(0, 1, n); ang = rng.uniform(-0.8, 0.8)
                C = np.array([pen.P(tip[0] + np.cos(ang) * 5 * tt, tip[1] + np.sin(ang) * 5 * tt, rng.uniform(-0.8, 0.8) * tt) for tt in t])
                pen.ribbon(C, np.repeat(e3[None], n, 0), 0.5, t * 0.6, 'sig_embers', 1.3, scroll=-1.0)
        else:   # Alduin's Bane: Alduin's fire under the mist - embers rising and dark smoke curling through (4K)
            for i in edge_samples(14, 0.05, 1.0):
                streak(i, 1.0, -0.2, rng.uniform(6, 11), 'sig_embers4k', 0.9, 1.3, -1.4)
            for i in edge_samples(8, 0.1, 0.95):
                streak(i, 0.7, 0.5, rng.uniform(5, 8), 'shade_strip', 2.2, 0.8, 3.0, blend='alpha', dark=True)
    elif sig == 'eclipse':   # the Eclipse Edge: a dark ring crossing a bright one at the spearhead's heart
        xc = x0 + 0.45 * L; r = half(xc) * 1.6
        for side in (-1, 1):
            at = pen.P(xc, mid(xc), side * (depth_at(F, xc, mid(xc)) + 0.3))
            pen.card(at, e1 * r, e2 * r, 'sig_eclipse', 1.4, blend='alpha', dark=True)   # (the dark disc)
            pen.card(at + e3 * side * 0.05, e1 * r, e2 * r, 'sig_eclipse', 1.6, pulse=(3.0, 0.75, 1.2))   # (its corona)
    elif sig == 'breath':   # Frostmourn: cold breath sinking off the blade toward the hilt
        for i in edge_samples(12, 0.2, 1.0):
            streak(i, 0.45, 1.0, rng.uniform(5, 9) * sc, 'vapour_strip', 1.4 * sc, 0.7, 2.2)
    elif sig == 'glint':   # the Silver Reaping Scythe: a bright glint sweeping along its edge
        for (pts, nor) in edges:
            n = len(pts)
            if n < 4: continue
            C = np.array([pen.P(*(pts[i] + nor[i] * 0.25 * sc), 0) for i in range(n)])
            seg = np.r_[0, np.cumsum(np.linalg.norm(np.diff(pts, axis=0), axis=1))]
            pen.ribbon(C, np.repeat(e3[None], n, 0), 0.7 * sc, seg / max(seg[-1] * 2.2, 1e-3), 'sig_band', 3.2, scroll=1.9)
    elif sig == 'souls':   # the Bonereaper: souls streaming up off the blade in thin ghostly trails
        for i in edge_samples(10, 0.1, 1.0):
            streak(i, 1.0, rng.uniform(-0.3, 0.3), rng.uniform(10, 16) * sc, 'sig_streak', 0.7 * sc, 0.8, 2.6)
    elif sig == 'wispstream':   # the rapier in arcane: blue wisps streaming tipward off the blade
        for i in edge_samples(8, 0.3, 1.0):
            streak(i, 0.35, -1.0, rng.uniform(6, 10) * sc, 'wisp_strip', 1.0 * sc, 0.9, -2.2)
    elif sig == 'fangs':   # the Wyrmtooth: the dragon teeth along the blade lit at their tips
        far = allp[:, 1] - np.array([mid(x) for x in allp[:, 0]])
        dist = np.abs(far)
        from scipy.signal import argrelextrema
        idx = argrelextrema(dist, np.greater, order=6)[0]
        idx = [i for i in idx if dist[i] > np.percentile(dist, 70)][:14]
        for i in idx:
            at = pen.P(*allp[i], 0); r = 1.1 * sc
            pen.card(at, e1 * r, e2 * r, 'sig_flare', 1.0, pulse=(rng.uniform(1.4, 2.6), 0.4, 1.3))
            pen.card(at, e1 * r, e3 * r, 'sig_flare', 0.8, pulse=(rng.uniform(1.4, 2.6), 0.4, 1.3))
    elif sig == 'glintstone':   # the Carian Wand: glintstone sparkles turning in its mist
        for k in range(10):
            x = x0 + rng.uniform(0.05, 0.98) * L; y = mid(x) + rng.uniform(-1, 1) * half(x); side = rng.choice([-1, 1])
            at = pen.P(x, y, side * (depth_at(F, x, y) + rng.uniform(0.2, 1.2))); r = rng.uniform(0.7, 1.4) * sc
            pen.card(at, e1 * r, e2 * r, 'sig_sparkle', 1.3, flip=(4, 4), fps=rng.uniform(5, 9))
    elif sig == 'sunrays':   # the Radiant Quarterstaff: short rays of sunlight standing off both heads
        for (pts, _) in edges:
            end = pts[np.argmax(np.abs(pts[:, 0]))]; x = end[0]; sgn = np.sign(x) or 1
            for k in range(8):
                ang = k / 8 * 2 * np.pi; n = 10; t = np.linspace(0, 1, n); ln = 6 * sc
                C = np.array([pen.P(x - sgn * 1.5 * sc + sgn * tt * ln * 0.4, mid(x) + np.cos(ang) * tt * ln, np.sin(ang) * tt * ln) for tt in t])
                Sd = np.repeat(e1[None], n, 0)
                pen.ribbon(C, Sd, 0.8 * sc, t, 'ray_strip', 0.9, scroll=6.0, pulse=(2.5 + k * 0.1, 0.6, 1.2))
    elif sig == 'solarflare':   # the rapier in sun: a flare burning at its point
        r = 2.4 * sc
        pen.card(pen.P(*tip, 0), e1 * r, e2 * r, 'sig_flare', 1.3, pulse=(1.8, 0.7, 1.25))
        pen.card(pen.P(*tip, 0), e2 * r, e3 * r, 'sig_flare', 1.0, pulse=(2.3, 0.7, 1.25))
    elif sig == 'runes':   # the Dragon Slayer: its runes burning brighter under the mist (4K)
        n = 40; x = np.linspace(x0 + 0.05 * L, x1 - 0.03 * L, n)
        for side in (-1, 1):
            C = np.array([pen.P(xx, mid(xx), side * (depth_at(F, xx, mid(xx)) + 0.2)) for xx in x])
            pen.ribbon(C, np.repeat(e2[None], n, 0), np.array([half(xx) * 0.55 for xx in x]), (x - x0) / 30.0, 'sig_runes4k', 1.2, scroll=14.0, pulse=(3.0, 0.7, 1.2))
    return pen.n
