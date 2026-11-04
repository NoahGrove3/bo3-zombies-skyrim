"""(Noah: "I want them to be 60 FPS. So it looks smooth. This should apply to all the Alchemist weapons") the Alchemist's
flipbooks, drawn at 60 frames a second (the bows' were 24 and stepped visibly):

  alch_<element>.dds    the element's own living ball (tools/bowarrows.py's drawing - flame, frost-mist, arcs, gusts,
                        sun-rays, blood-mist, the void) re-drawn 128 frames to its loop instead of 64: the flasks' fizz,
                        the shatter bursts (tools/flasks.py, tools/elemburst.py)
  ground_<element>.dds  what a flask leaves burning on the ground, each element its own: tongues of flame licking up,
                        cold mist creeping low with frost glinting in it, arcs of lightning crawling over the ground,
                        poison wisps curling up off popping bubbles, shafts of sunlight shimmering up, a low blood-mist
                        (tools/motes.py ground_*.nif)

Every motion is a whole number of turns over the loop, so it never jumps when it starts again.

    python3 tools/alchfx.py"""
import math, pathlib, subprocess, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFilter

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
ARGS = sys.argv[1:]; sys.argv = sys.argv[:1]  # (bowarrows reads its own argv)
import bowarrows as BA  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parent.parent
TEX = ROOT / 'assets' / 'fx' / 'Textures' / 'CurseOfAlduin' / 'fx'
WORK = ROOT / 'build' / 'alchfx'
COLS, ROWS = 16, 8
F = COLS * ROWS          # 128 frames: played at 60 fps, 2.13 s a loop (the bows' 64 at 24 fps); the atlases
# a power of two on each side - a 2048x1280 or 1536x960 one crashed the game as it loaded the texture
FPS = 60


def grid(n):
    """frame coords for an n-pixel frame, into bowarrows' module too (its drawings read them from there)"""
    yy, xx = np.mgrid[0:n, 0:n].astype(np.float32) / n
    X, Y = xx * 2 - 1, 1 - yy * 2
    BA.N, BA.xx, BA.yy, BA.X, BA.Y = n, xx, yy, X, Y
    BA.R = np.hypot(X, Y); BA.ANG = np.arctan2(Y, X)
    return xx, yy, X, Y


def save(frames, n, name):
    at = np.zeros((n * ROWS, n * COLS, 4), np.float32)
    for f, (rgb, a) in enumerate(frames):
        cy, cx = divmod(f, COLS)
        at[cy * n:(cy + 1) * n, cx * n:(cx + 1) * n] = np.dstack([rgb * a[..., None], a])
    img = Image.fromarray((np.clip(at, 0, 1) * 255).astype(np.uint8), 'RGBA')
    TEX.mkdir(parents=True, exist_ok=True); WORK.mkdir(parents=True, exist_ok=True)
    png = WORK / (name + '.png'); img.save(png)
    subprocess.run(['convert', str(png), '-define', 'dds:compression=dxt5', '-define', 'dds:mipmaps=8', str(TEX / (name + '.dds'))], check=True)
    print(name, img.size)


# ---- the element balls: bowarrows' drawings, more frames to the same loop ----

def balls():
    grid(128)
    for el, fn in BA.FX.items():
        # (their own frame counters - the shock's new path, the frost's glints - keep the bows' pace: about 12 a second)
        save([fn(f / F, int(f / F * 64)) for f in range(F)], 128, f'alch_{el}')


# ---- the ground effects (frame: x -1..1 across, the ground along the bottom row, up the frame is up) ----

def S(tex, u, v):
    u, v = np.broadcast_arrays(np.asarray(u, np.float32), np.asarray(v, np.float32))
    return BA.samp(tex, u, v)




def smooth(n, beta, seed):
    """tileable noise, smoother than the bows' (fewer specks), centred on 0 with a spread of 1"""
    o = BA.tileable(n, beta, seed)
    return ((o - o.mean()) / o.std()).astype(np.float32)


A, B, C = smooth(256, 2.4, 31), smooth(256, 2.2, 32), smooth(256, 2.0, 33)


def ground_frames(fn, n=128):
    xx, yy, X, Y = grid(n)
    h = 1 - yy                                  # height above the ground, 0..1
    side = np.clip(1 - np.abs(X) ** 2.2, 0, 1)  # (fades out at the card's sides, so the cards blend into one another)
    return [fn(f / F, f, xx, h, X, side) for f in range(F)]


def white(i):
    i = np.clip(i, 0, 1)
    return np.dstack([i, i, i]), i


def g_fire(t, f, xx, h, X, side):
    """tongues of flame licking up off the ground, curling, and the odd ember lifting off"""
    wx = S(B, xx * 0.7 + t, h * 0.6 - t * 2) * 0.06              # (the warp: the tongues sway and curl)
    n1 = S(A, xx * 2.2 + wx, h * 0.8 - t * 3)                     # (stretched up: tongues, not puffs)
    n2 = S(C, xx * 4.4 + wx * 2, h * 1.6 - t * 5)
    fl = n1 * 0.75 + n2 * 0.4
    body = np.clip((fl * 0.5 + 0.38 - h * 1.35) * 2.0, 0, 1) * np.clip(1 - h, 0, 1) ** 0.6 * side
    base = np.exp(-(h / 0.06) ** 2) * side * 0.3
    emb = np.zeros_like(h)
    rng = np.random.default_rng(5)
    for k in range(7):
        sp = int(rng.integers(1, 3)); ph = (t * sp + k / 7) % 1.0
        x0 = rng.uniform(-0.7, 0.7) + 0.08 * math.sin(2 * math.pi * (t * 2 + k / 7))
        y0 = 0.12 + ph * 0.85
        emb += np.exp(-(((X - x0) / 0.022) ** 2 + ((h - y0) / 0.022) ** 2)) * (1 - ph) ** 1.3 * min(1.0, ph * 6)
    return white(body * (0.55 + 0.45 * (1 - h)) + base + emb * 0.9)


def g_frost(t, f, xx, h, X, side):
    """cold mist creeping low over the ground, frost glinting in it"""
    n1 = S(A, xx * 0.6 + t, h * 0.5 + 0.2)
    n2 = S(B, xx * 1.1 - t * 2, h * 0.9 - t)
    mist = np.clip(0.6 + 0.35 * (n1 * 0.7 + n2 * 0.5), 0, 1) * np.exp(-(h / 0.3) ** 1.6) * side
    rng = np.random.default_rng(9)
    gl = np.zeros_like(h)
    for k in range(9):
        x0, y0 = rng.uniform(-0.75, 0.75), rng.uniform(0.03, 0.3); fr = int(rng.integers(2, 5)); ph = rng.uniform(0, 1)
        tw = max(0.0, math.sin(2 * math.pi * (t * fr + ph))) ** 6
        dx, dy = X - x0, h - y0
        gl += tw * (np.exp(-(dx * dx + dy * dy) / 0.0003) + 0.5 * np.exp(-(dx / 0.004) ** 2 - (dy / 0.06) ** 2) + 0.5 * np.exp(-(dy / 0.004) ** 2 - (dx / 0.06) ** 2))
    return white(mist + gl)


def g_shock(t, f, xx, h, X, side, _cache={}):
    """arcs of lightning crawling over the ground: a new one every 4 frames, each a white flash that cools over a tenth
    of a second (so they flicker smoothly instead of popping in and out), with a dim blue glow along the ground"""
    n = h.shape[0]
    if not _cache:
        for b in range(0, F, 4):
            rng = np.random.default_rng(3000 + b)
            im = Image.new('L', (n, n), 0); d = ImageDraw.Draw(im)
            for _ in range(2):
                x = rng.uniform(0.05, 0.35) * n; y = n * (1 - rng.uniform(0.06, 0.16)); pts = [(x, y)]
                L = rng.uniform(0.5, 0.9) * n; a = rng.uniform(-0.35, 0.35)
                for s in range(12):
                    a = np.clip(a + rng.uniform(-0.8, 0.8), -0.35, 1.1)
                    x += math.cos(a) * L / 12; y = min(n * 0.95, y - math.sin(a) * L / 12 * 0.6 - rng.uniform(-1, 2)); pts.append((x, y))
                d.line(pts, fill=255, width=2)
                if rng.random() < 0.7:
                    j = int(rng.integers(3, 9)); bx, by = pts[j]; ba = rng.uniform(0.6, 2.4); bp = [(bx, by)]
                    for s in range(5):
                        ba += rng.uniform(-0.6, 0.6); bx += math.cos(ba) * L / 18; by -= abs(math.sin(ba)) * L / 18; bp.append((bx, by))
                    d.line(bp, fill=170, width=1)
            if rng.random() < 0.5: im = im.transpose(Image.FLIP_LEFT_RIGHT)
            arc = np.asarray(im, np.float32) / 255
            halo = np.asarray(im.filter(ImageFilter.GaussianBlur(3)), np.float32) / 255
            _cache[b] = arc * 0.9 + halo * 2.0
    out = np.zeros_like(h)
    for b, img in _cache.items():
        age = (f - b) % F
        if age < 9: out += img * math.exp(-age / 2.6)
    glow = np.exp(-(h / 0.07) ** 2) * side * (0.25 + 0.15 * math.sin(2 * math.pi * t * 23))
    return white(out * side + glow)


def g_poison(t, f, xx, h, X, side):
    """thin wisps of poison curling up off the ground, and bubbles popping at its foot"""
    wx = S(B, xx * 0.6 + t, h * 0.5 - t) * 0.08
    n = S(A, xx * 1.3 + wx, h * 0.8 - t * 2)
    ridge = np.exp(-(n / 0.22) ** 2)                                                # (the noise's zero lines: fine filaments)
    wisp = ridge * np.clip(1 - h / 0.85, 0, 1) ** 1.2 * np.clip(h / 0.05, 0, 1) * side
    haze = np.exp(-(h / 0.12) ** 2) * side * 0.3 * np.clip(0.7 + 0.3 * S(C, xx * 2 + t * 2, 0.5), 0, 1)
    rng = np.random.default_rng(11)
    bub = np.zeros_like(h)
    for k in range(6):
        x0 = rng.uniform(-0.7, 0.7); sp = int(rng.integers(1, 3)); ph = (t * sp * 2 + k / 6) % 1.0
        r = 0.02 + ph * 0.07
        d = np.hypot(X - x0, (h - 0.04 - r) * 1.0)
        bub += np.exp(-((d - r) / 0.008) ** 2) * (1 - ph) ** 1.5 * (ph < 0.95)
    return white(wisp * 0.9 + haze + bub * 0.8)


def g_sun(t, f, xx, h, X, side):
    """shafts of light shimmering up off the ground, and fine motes of light rising through them"""
    s1 = S(A, xx * 2.0 + t, h * 0.12 - t)
    s2 = S(B, xx * 3.3 - t * 2, h * 0.2 - t * 2)
    rays = np.clip((s1 * 0.6 + s2 * 0.5) * 0.5 + 0.3, 0, 1) ** 1.5 * np.clip(1 - h, 0, 1) ** 1.6 * side
    base = np.exp(-(h / 0.06) ** 2) * side * 0.6
    rng = np.random.default_rng(17)
    m = np.zeros_like(h)
    for k in range(10):
        x0 = rng.uniform(-0.75, 0.75); sp = int(rng.integers(1, 3)); ph = (t * sp + k / 10) % 1.0
        x = x0 + 0.04 * math.sin(2 * math.pi * (t * 3 + k / 10))
        m += np.exp(-(((X - x) / 0.016) ** 2 + ((h - 0.05 - ph * 0.85) / 0.016) ** 2)) * min(1.0, ph * 6) * (1 - ph)
    return white(rays + base + m)


def g_blood(t, f, xx, h, X, side):
    """a low blood-mist rolling over the ground, a few drops lifting out of it and falling back"""
    n1 = S(A, xx * 0.7 - t, h * 0.6 + 0.4)
    n2 = S(C, xx * 1.4 + t * 2, h * 1.2 - t)
    mist = np.clip(0.5 + 0.32 * (n1 * 0.7 + n2 * 0.5), 0, 1) * np.exp(-(h / 0.26) ** 1.5) * side
    rng = np.random.default_rng(23)
    dr = np.zeros_like(h)
    for k in range(5):
        x0 = rng.uniform(-0.6, 0.6); ph = (t * 2 + k / 5) % 1.0
        y = 0.05 + 4 * 0.32 * ph * (1 - ph)                                       # (up and back down)
        dr += np.exp(-(((X - x0) / 0.022) ** 2 + ((h - y) / 0.03) ** 2)) * (0.6 + 0.4 * (1 - ph))
    return white(mist * 0.95 + dr * 0.7)


def pillar_frames(fw=128, fh=256):
    """(Noah: "I can see the cutoff of the flame animations ... it looks really low quality") a tall sheet of fire for the
    Endless Flame: many fine tongues licking up, curling as they rise, hot and dense at the foot, thinning to wisps at the
    top - fading to nothing at every edge of its frame, so no card's border ever shows"""
    yy, xx = np.mgrid[0:fh, 0:fw].astype(np.float32)
    u, h = xx / fw, 1 - yy / fh                                       # across 0..1, up 0..1
    X = u * 2 - 1
    edge = np.clip(1 - np.abs(X) ** 2, 0, 1) ** 1.2 * np.clip(h / 0.06, 0, 1) * np.clip((1 - h) / 0.35, 0, 1) ** 1.5
    out = []
    for f in range(F):
        t = f / F
        wx = S(B, u * 0.8 + t, h * 0.5 - t * 2) * 0.05 + S(C, u * 1.6 - t, h * 1.0 - t * 3) * 0.025
        n1 = S(A, u * 2.6 + wx, h * 0.7 - t * 3)
        n2 = S(C, u * 5.2 + wx * 2, h * 1.5 - t * 5)
        n3 = S(B, u * 9.0 + wx * 3, h * 2.6 - t * 8)
        fl = n1 * 0.6 + n2 * 0.35 + n3 * 0.2
        body = np.clip((fl * 0.45 + 0.62 - h * 1.25) * 2.2, 0, 1)
        core = np.clip((fl * 0.4 + 0.35 - h * 1.6) * 2.5, 0, 1)
        i = (body * 0.6 + core * 0.32) * edge
        rgb = BA.ramp(np.clip(i, 0, 1), [(0, (60, 8, 0)), (0.35, (200, 50, 6)), (0.65, (250, 140, 30)), (0.88, (255, 215, 120)), (1, (255, 248, 225))])
        out.append((rgb, np.clip(i * 1.15, 0, 1)))
    return out


def blade_flame(name, stops, w=256, hh=1024):
    """(Noah: "the effects on the Madness Sword need to look like it's attached to the sword's model, not clipping through it. I
    can still see the flame hard lines") a sheath of fire for a blade: the blade down the middle of the image (u 0.5), tongues
    licking out from it to both sides, fading to nothing well before the image's sides; seamless top to bottom, so it slides
    along the blade forever (the model scrolls it) - never a card's edge, never a seam"""
    yy, xx = np.mgrid[0:hh, 0:w].astype(np.float32)
    u, v = xx / w, yy / hh
    X = np.abs(u * 2 - 1)                                             # 0 at the blade, 1 at the image's sides
    wx = S(B, u * 0.7, v * 4) * 0.03
    n1 = S(A, u * 0.6 + wx, v * 9)                                    # (v: whole turns of the tileable noise - seamless)
    n2 = S(C, u * 1.3 + wx * 2, v * 19)
    n3 = S(B, u * 2.4, v * 37)
    fl = n1 * 0.6 + n2 * 0.4 + n3 * 0.25
    reach = np.clip(0.55 + 0.32 * fl, 0.12, 0.95)                     # how far each tongue licks out
    body = np.clip(1 - X / reach, 0, 1) ** 1.1
    wisp = np.clip(1 - X / np.clip(reach * 1.35, 0.1, 1.0), 0, 1) ** 2.5 * 0.35
    core = np.exp(-(X / 0.09) ** 2) * (0.7 + 0.3 * np.clip(n2 * 0.5 + 0.5, 0, 1))
    i = np.clip(body * 0.72 + wisp + core * 0.3, 0, 1) * np.clip((1 - X) / 0.2, 0, 1)
    rgb = BA.ramp(i, stops)
    a = np.clip(i * 1.1, 0, 1)
    img = Image.fromarray((np.clip(np.dstack([rgb * a[..., None], a]), 0, 1) * 255).astype(np.uint8), 'RGBA')
    TEX.mkdir(parents=True, exist_ok=True); WORK.mkdir(parents=True, exist_ok=True)
    png = WORK / (name + '.png'); img.save(png)
    subprocess.run(['convert', str(png), '-define', 'dds:compression=dxt5', '-define', 'dds:mipmaps=8', str(TEX / (name + '.dds'))], check=True)
    print(name, img.size)


def save_rect(frames, fw, fh, name):
    at = np.zeros((fh * ROWS, fw * COLS, 4), np.float32)
    for f, (rgb, a) in enumerate(frames):
        cy, cx = divmod(f, COLS)
        at[cy * fh:(cy + 1) * fh, cx * fw:(cx + 1) * fw] = np.dstack([rgb * a[..., None], a])
    img = Image.fromarray((np.clip(at, 0, 1) * 255).astype(np.uint8), 'RGBA')
    TEX.mkdir(parents=True, exist_ok=True); WORK.mkdir(parents=True, exist_ok=True)
    png = WORK / (name + '.png'); img.save(png)
    subprocess.run(['convert', str(png), '-define', 'dds:compression=dxt5', '-define', 'dds:mipmaps=8', str(TEX / (name + '.dds'))], check=True)
    print(name, img.size)


GROUND = {'fire': g_fire, 'frost': g_frost, 'shock': g_shock, 'poison': g_poison, 'sun': g_sun, 'blood': g_blood}


def main():
    only = set(ARGS)
    balls() if not only or 'balls' in only else None
    if not only or 'blade' in only:
        blade_flame('blade_flame_red', [(0, (20, 0, 0)), (0.35, (95, 4, 2)), (0.65, (160, 14, 6)), (0.88, (205, 40, 14)), (1, (235, 95, 55))])
        blade_flame('blade_flame_violet', [(0, (14, 0, 30)), (0.3, (70, 16, 150)), (0.6, (130, 50, 235)), (0.85, (185, 120, 255)), (1, (240, 215, 255))])
    if not only or 'pillar' in only:
        save_rect(pillar_frames(), 128, 256, 'pillar_flame')
    for el, fn in GROUND.items():
        if only and el not in only and 'ground' not in only: continue
        save(ground_frames(fn), 128, f'ground_{el}')


if __name__ == '__main__':
    main()
