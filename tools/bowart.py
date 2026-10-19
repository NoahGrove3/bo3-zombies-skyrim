"""The altar's bows, finished by hand (tools/bows.py): 2K maps laid out along each limb (u: bottom tip to top tip,
v: round the limb, its seam on the back toward the target), every part worked like a master bowyer's piece.

  limbs   the bow's own material (charred horn, bone, blued iron, ash, bronze-shod wood) with its grain running the
          length and fine pores/hammer marks, a pale horn belly and dark sinew back, worn bright on the arrises
  runes   a bevelled channel cut down each flank of both limbs, its floor dark, a braided knotwork border either side,
          the Dirge's runes cut into it and inlaid with gold that glows in the bow's element
  grip    leather wrapped in overlapping diagonal turns, each edge stitched, darkened by the hand
  metal   chased collars and tip caps: engraved scrollwork, a raised rim and rivets
  blades  (the bow's spurs and blades: trim_maps) element-forged metal etched with runic filigree, veins of the element
          glowing through it

    body_maps(kind, body_col, glow, seed) -> diffuse RGB, normal RGBA (alpha = specular), glow RGB
    trim_maps(element, glow, seed)        -> the same for the blades
"""
import math
import numpy as np
from PIL import Image, ImageDraw, ImageFilter
import skymat

GLYPHS = [[(0, -1, 0, 1), (0, -0.3, 0.6, -0.9)], [(-0.5, 1, 0, -1), (0.5, 1, 0, -1), (-0.3, 0.3, 0.3, 0.3)], [(0, -1, 0, 1), (-0.6, 0, 0.6, 0)],
          [(-0.6, -1, 0.6, 1), (0.6, -1, -0.6, 1)], [(0, -1, 0, 1), (0, 0.1, 0.6, 0.9)], [(-0.6, -0.8, 0.6, -0.8), (0, -0.8, 0, 1)],
          [(-0.5, -1, -0.5, 1), (-0.5, 0, 0.5, -0.6), (-0.5, 0, 0.5, 0.6)], [(0, -1, 0.6, 0), (0.6, 0, 0, 1), (0, 1, -0.6, 0), (-0.6, 0, 0, -1)],
          [(-0.5, -1, 0.5, -1), (0, -1, 0, 1), (-0.5, 1, 0.5, 1)], [(0, -1, 0, 1), (0, -1, 0.55, -0.45), (0, -0.2, 0.55, 0.35)],
          [(-0.55, -1, 0.55, 1), (-0.55, 1, 0, 0)], [(0, -1, 0, 1), (-0.5, -0.5, 0.5, -0.5), (-0.5, 0.5, 0.5, 0.5)]]

METALS = {   # the blades' metal per element: base, highlight
    'fire': ((46, 36, 32), (120, 80, 60)), 'frost': ((150, 168, 186), (230, 240, 250)), 'shock': ((62, 64, 78), (150, 150, 180)),
    'wind': ((168, 176, 170), (235, 240, 236)), 'sun': ((196, 150, 70), (255, 228, 150)), 'blood': ((70, 22, 26), (150, 60, 60)),
}


def fbm(W, H, scale, seed, octaves=5, stretch=(1, 1)):
    rng = np.random.default_rng(seed)
    out = np.zeros((H, W), np.float32); amp = 1.0; tot = 0.0
    for o in range(octaves):
        cw, ch = max(2, int(scale * 2 ** o / stretch[0])), max(2, int(scale * 2 ** o / stretch[1]))
        g = rng.random((ch, cw)).astype(np.float32)
        out += np.asarray(Image.fromarray((g * 255).astype(np.uint8)).resize((W, H), Image.BICUBIC), np.float32) / 255 * amp
        tot += amp; amp *= 0.55
    return out / tot


def blur(a, r):
    return np.asarray(Image.fromarray(np.clip(a * 255, 0, 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(r)), np.float32) / 255


def braid(u, v, vc, half, period):
    """a two-strand braid along u centred on v=vc, half its width 'half' (in v), one twist per 'period' (in u):
    height of the strands (0..1), each passing over then under the other"""
    ph = 2 * np.pi * u / period
    a_c = vc + half * 0.55 * np.sin(ph)
    b_c = vc - half * 0.55 * np.sin(ph)
    w = half * 0.42
    A = np.clip(1 - np.abs(v - a_c) / w, 0, 1) ** 0.6
    B = np.clip(1 - np.abs(v - b_c) / w, 0, 1) ** 0.6
    over = np.cos(ph) > 0
    top = np.where(over, A, B); bot = np.where(over, B, A)
    return np.maximum(top, bot * (1 - 0.85 * top)), np.clip(1 - np.abs(v - vc) / half, 0, 1)


def scrolls(W, H, x0, x1, seed, n=6):
    """engraved scrollwork (spiral curls on a running vine) between u-pixels x0..x1, as a line mask"""
    m = Image.new('L', (W, H), 0); d = ImageDraw.Draw(m)
    rng = np.random.default_rng(seed)
    for vy in (0.25, 0.75):
        cy = vy * H
        pts = [(x, cy + math.sin((x - x0) / max(1, x1 - x0) * math.pi * 3) * H * 0.08) for x in np.linspace(x0, x1, 40)]
        d.line(pts, fill=255, width=3)
        for k in range(n):
            t = (k + 0.5) / n
            cx = x0 + t * (x1 - x0); base_y = cy + math.sin(t * math.pi * 3) * H * 0.08
            side = 1 if k % 2 else -1
            spiral = [(cx + math.cos(a) * r * 0.5, base_y + side * (H * 0.06 + math.sin(a) * r)) for a, r in
                      ((a, H * 0.05 * (1 - a / (4 * math.pi))) for a in np.linspace(0, 4 * math.pi, 50))]
            d.line(spiral, fill=255, width=2)
    return np.asarray(m.filter(ImageFilter.GaussianBlur(0.8)), np.float32) / 255


def glyph_mask(W, H, cells, seed, width=0.24):
    rng = np.random.default_rng(seed)
    m = Image.new('L', (W, H), 0); d = ImageDraw.Draw(m)
    for cx, cy, hs in cells:
        for x0, y0, x1, y1 in GLYPHS[int(rng.integers(len(GLYPHS)))]:
            # the rune stands across the channel: its own up runs round the limb (v)
            d.line([(cx + y0 * hs * 0.8, cy + x0 * hs), (cx + y1 * hs * 0.8, cy + x1 * hs)], fill=255, width=max(3, int(hs * width)))
    return np.asarray(m.filter(ImageFilter.GaussianBlur(1.0)), np.float32) / 255


def normals(height, strength):
    gy, gx = np.gradient(height * strength)
    n = np.dstack([-gx, gy, np.ones_like(gx)])
    return n / np.linalg.norm(n, axis=2, keepdims=True)


def pack(col, height, strength, spec, glow):
    n = normals(height, strength)
    nimg = Image.fromarray(np.dstack([(n * 0.5 + 0.5) * 255, np.clip(spec, 0, 1)[..., None] * 255]).astype(np.uint8), 'RGBA')
    return (Image.fromarray(np.clip(col, 0, 255).astype(np.uint8), 'RGB'), nimg,
            Image.fromarray(np.clip(glow, 0, 255).astype(np.uint8), 'RGB'))


def body_maps(kind, body_col, glow, seed, W=2048, H=1024):
    u = np.linspace(0, 1, W, dtype=np.float32)[None, :].repeat(H, 0)
    v = np.linspace(0, 1, H, dtype=np.float32)[:, None].repeat(W, 1)
    gc = np.array(glow, np.float32) * 255
    # ---- the limb's material, grain along its length, and its fine surface
    d0, n0 = skymat.make(kind, body_col, seed=seed, size=W)
    mat = np.asarray(d0.rotate(90, expand=True).resize((W, W)), np.float32)[:H]
    mh = np.asarray(n0.rotate(90, expand=True).resize((W, W)).convert('L'), np.float32)[:H] / 255
    pores = fbm(W, H, 160, seed + 11, 2, stretch=(6, 1))
    micro = (pores - pores.mean()) * (0.5 if kind.startswith('wood') else 0.25)
    if kind in ('iron', 'steel', 'bronze'):  # hammer marks
        dents = fbm(W, H, 40, seed + 12, 3)
        micro += (np.clip(dents - 0.62, 0, 1) * -1.2)
    mat = mat * (1 + 0.35 * micro[..., None])
    # ---- parts along the bow
    grip = (np.abs(u - 0.5) < 0.065).astype(np.float32)
    collar_band = (np.abs(np.abs(u - 0.5) - 0.074) < 0.011)
    caps = (u < 0.04) | (u > 0.96)
    metal = (collar_band | caps).astype(np.float32)
    limbs = (1 - grip) * (1 - metal)
    on_limb = ((u > 0.09) & (u < 0.41)) | ((u > 0.59) & (u < 0.91))
    # composite layers: horn belly (toward you), sinew back, worn bright arrises between
    belly = np.clip(1 - np.abs(v - 0.5) / 0.1, 0, 1) ** 0.5 * limbs
    back = np.clip(1 - np.minimum(v, 1 - v) / 0.08, 0, 1) ** 0.5 * limbs
    arris = (np.exp(-((v - 0.4) / 0.012) ** 2) + np.exp(-((v - 0.6) / 0.012) ** 2) + np.exp(-((v - 0.08) / 0.012) ** 2) + np.exp(-((v - 0.92) / 0.012) ** 2)) * limbs
    wear = arris * np.clip(fbm(W, H, 30, seed + 13, 3) * 1.6 - 0.4, 0, 1)
    horn = np.array([190, 172, 140], np.float32) * 0.65 + np.array(body_col, np.float32) * 0.35
    hornstreak = fbm(W, H, 60, seed + 14, 3, stretch=(10, 1))
    col = mat * (1 - 0.6 * belly[..., None]) + horn * (0.6 * belly[..., None]) * (0.8 + 0.4 * hornstreak[..., None])
    col *= (1 - 0.4 * back[..., None])
    col = col + 50 * wear[..., None]
    # ---- the rune channels, braided borders, runes inlaid with glowing gold
    ch = np.zeros((H, W), np.float32); bevel = np.zeros((H, W), np.float32); knot = np.zeros((H, W), np.float32); knot_band = np.zeros((H, W), np.float32)
    for vc in (0.25, 0.75):
        dist = np.abs(v - vc) / 0.055
        ch = np.maximum(ch, (dist < 1).astype(np.float32) * on_limb)
        bevel = np.maximum(bevel, np.clip(1 - np.abs(dist - 1) / 0.25, 0, 1) * on_limb)
        for side in (-1, 1):
            k, band = braid(u, v, vc + side * 0.085, 0.022, 0.018)
            knot = np.maximum(knot, k * on_limb); knot_band = np.maximum(knot_band, band * on_limb)
    cells = []
    for vc in (0.25, 0.75):
        for u0, u1 in ((0.105, 0.395), (0.605, 0.895)):
            n = 12
            cells += [((u0 + (k + 0.5) / n * (u1 - u0)) * W, vc * H, H * 0.036) for k in range(n)]
    gly = glyph_mask(W, H, cells, seed + 15)
    inlay = np.clip(blur(gly, 2.2) * 2.2 - gly * 1.2, 0, 1)  # the gold rim round each rune
    metal_d, _ = skymat.make({'wood': 'bronze', 'wood_dark': 'iron', 'iron': 'steel', 'bone': 'bronze', 'steel': 'silver', 'bronze': 'gold'}.get(kind, 'steel'), None, seed=seed + 5, size=W)
    met = np.asarray(metal_d.rotate(90, expand=True).resize((W, W)), np.float32)[:H]
    gold = np.array([200, 160, 82], np.float32)
    col = col * (1 - 0.55 * ch[..., None]) * (1 - 0.25 * bevel[..., None])
    col = col * (1 - knot[..., None]) + (met * 0.85 + 20) * knot[..., None]
    col = col * (1 - 0.4 * (knot_band - knot).clip(0, 1)[..., None])           # (the shadow between the strands)
    col = col * (1 - inlay[..., None]) + gold * inlay[..., None]
    col = col * (1 - gly[..., None]) + (gc * 0.6)[None, None] * gly[..., None]
    # ---- the grip: leather in overlapping turns, stitched at each edge
    turns = ((u * W / 70 + v * H / 70 * 0.9) % 1.0)
    overlap = np.clip(1 - turns / 0.12, 0, 1)
    leather = np.array([62, 40, 28], np.float32) * (0.85 + 0.3 * fbm(W, H, 120, seed + 16, 3)[..., None])
    leather *= (1 - 0.45 * overlap[..., None]) * (1 - 0.35 * np.clip(fbm(W, H, 8, seed + 17, 3) - 0.5, 0, 1)[..., None] * 2)  # (darkened by the hand)
    stitch_u = (np.abs(np.abs(u - 0.5) - 0.061) < 0.0016) & ((v * H) % 16 < 8)
    col = col * (1 - grip[..., None]) + leather * grip[..., None]
    col = np.where(stitch_u[..., None] & (grip[..., None] > 0), np.array([170, 150, 120], np.float32), col)
    # ---- the metal: collars and caps, chased with scrollwork, a raised rim and rivets
    scr = np.zeros((H, W), np.float32)
    for a, b in ((0.0, 0.04), (0.96, 1.0), (0.42, 0.437), (0.563, 0.58)):
        scr = np.maximum(scr, scrolls(W, H, a * W, b * W, seed + int(a * 100)))
    rim = np.zeros((H, W), np.float32)
    for e in (0.04, 0.96, 0.415, 0.437, 0.563, 0.585):
        rim = np.maximum(rim, np.clip(1 - np.abs(u - e) * W / 5, 0, 1))
    rivet = np.zeros((H, W), np.float32)
    for uc in (0.02, 0.98, 0.426, 0.574):
        for vc in (0.0, 0.25, 0.5, 0.75, 1.0):
            rivet = np.maximum(rivet, np.clip(1 - np.hypot((u - uc) * W, (v - vc) * H) / 9, 0, 1) ** 0.5)
    mcol = met * (1 - 0.55 * scr[..., None]) + 40 * rim[..., None] + 60 * rivet[..., None]
    col = col * (1 - metal[..., None]) + mcol * metal[..., None]
    # ---- height, shine, glow
    height = (mh * 0.4 + micro * 0.3) * limbs + belly * 0.05 - ch * 0.3 + bevel * 0.12 + knot * 0.35 - gly * 0.3 + inlay * 0.08
    height += grip * (0.5 + 0.35 * (1 - overlap) - 0.2 * stitch_u)
    height += metal * (0.7 - 0.25 * scr + 0.3 * rim + 0.4 * rivet)
    spec = 0.15 + 0.12 * belly + 0.25 * wear + 0.5 * knot + 0.6 * inlay + 0.55 * metal + 0.3 * gly - 0.08 * grip
    halo = blur(gly, 6)
    g = np.clip(gly * 1.0 + 0.4 * halo + 0.12 * inlay, 0, 1)
    return pack(col, height, 9.0, spec, g[..., None] * gc[None, None])


def trim_maps(element, glow, seed, W=2048, H=1024):
    """the blades and spurs: forged metal of the element, etched with runic filigree, the element glowing through veins"""
    base, hi = METALS.get(element, METALS['shock'])
    u = np.linspace(0, 1, W, dtype=np.float32)[None, :].repeat(H, 0)
    v = np.linspace(0, 1, H, dtype=np.float32)[:, None].repeat(W, 1)
    gc = np.array(glow, np.float32) * 255
    forged = fbm(W, H, 24, seed, 5, stretch=(4, 1))
    pits = np.clip(fbm(W, H, 120, seed + 1, 2) - 0.66, 0, 1) * 3
    col = np.array(base, np.float32)[None, None] * (0.75 + 0.5 * forged[..., None]) - 30 * pits[..., None]
    # filigree: a running braid down the middle of each face and scrollwork either side
    knot, band = braid(u, v, 0.25, 0.05, 0.03)
    knot2, band2 = braid(u, v, 0.75, 0.05, 0.03)
    knot = np.maximum(knot, knot2); band = np.maximum(band, band2)
    scr = scrolls(W, H, 0, W, seed + 3, n=28)
    col = col * (1 - 0.5 * scr[..., None]) + np.array(hi, np.float32) * knot[..., None] * 0.7 + col * (1 - knot[..., None]) * 0.0
    col = col * (1 - 0.35 * (band - knot).clip(0, 1)[..., None])
    # the element's veins: branching cracks of light (lightning-like for storm, flowing for others)
    m = Image.new('L', (W, H), 0); d = ImageDraw.Draw(m)
    rng = np.random.default_rng(seed + 4)
    for _ in range(26):
        x, y = rng.uniform(0, W), rng.uniform(0, H); ang = rng.uniform(0, 2 * np.pi); pts = [(x, y)]
        jag = 1.1 if element == 'shock' else 0.45
        for _ in range(16):
            ang += rng.uniform(-jag, jag); x += math.cos(ang) * 26; y += math.sin(ang) * 14; pts.append((x, y))
        d.line(pts, fill=255, width=3)
    veins = np.asarray(m.filter(ImageFilter.GaussianBlur(1.2)), np.float32) / 255
    cells = [((k + 0.5) / 24 * W, vc * H, H * 0.04) for k in range(24) for vc in (0.25, 0.75)]
    gly = glyph_mask(W, H, cells, seed + 5, 0.2) * (1 - knot)
    col = col * (1 - veins[..., None] * 0.7) + gc * 0.5 * veins[..., None]
    col = col * (1 - gly[..., None]) + gc * 0.55 * gly[..., None]
    edge = np.exp(-((np.minimum(v, 1 - v)) / 0.03) ** 2) + np.exp(-((v - 0.5) / 0.02) ** 2)   # a bright honed edge
    col = col + np.array(hi, np.float32) * 0.4 * edge[..., None]
    height = forged * 0.3 - pits * 0.2 - scr * 0.3 + knot * 0.4 - veins * 0.25 - gly * 0.3 + edge * 0.1
    spec = 0.55 + 0.3 * knot + 0.25 * edge - 0.3 * pits + 0.2 * gly
    g = np.clip(veins * 0.8 + gly + 0.35 * blur(veins + gly, 5), 0, 1)
    return pack(col, height, 8.0, spec, g[..., None] * gc[None, None])
