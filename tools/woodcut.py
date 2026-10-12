"""Intro film stills, restyled as woodcut / ink on parchment (the 'legend' look of an old storybook).

    python3 tools/woodcut.py            -> assets/ui/intro_<name>.jpg for every intro still (from art/intro_src)
    python3 tools/woodcut.py parchment  -> assets/ui/intro_parchment.jpg (the bare page, used behind the text)

The in-game captures (art/intro_src/intro_*.jpg) stay the source. Each is lifted out of the night, cut into a few flat ink
tones, outlined with a brush-weight contour, hatched in the mid tones like a block cut, then printed in sepia ink onto
a generated parchment with a faint watercolour wash of the picture's own hues (fire, magic) and a burnt vignette.
"""
import sys
import numpy as np
import cv2

SRC = 'art/intro_src/intro_{}.jpg'   # the graded captures (tools/plates.py writes them; move them here)
OUT = 'assets/ui/intro_{}.jpg'
NAMES = ['helgen', 'sky', 'graves', 'horde', 'champions', 'wyrm', 'shrine']
W, H = 1920, 1080

PAPER = np.array([214, 196, 160], np.float32)   # RGB
PAPER_DARK = np.array([168, 140, 100], np.float32)
INK = np.array([46, 30, 20], np.float32)


def fbm(h, w, seed, octaves=6, base=4):
    rng = np.random.default_rng(seed)
    acc = np.zeros((h, w), np.float32)
    amp, tot = 1.0, 0.0
    for o in range(octaves):
        n = base * 2 ** o
        g = rng.random((n * h // w + 2, n + 2)).astype(np.float32)
        acc += amp * cv2.resize(g, (w, h), interpolation=cv2.INTER_CUBIC)
        tot += amp
        amp *= 0.55
    return acc / tot


def parchment(seed=7):
    h, w = H, W
    n = fbm(h, w, seed)
    blot = fbm(h, w, seed + 1, octaves=4, base=3)
    t = np.clip((n - 0.5) * 1.6 + 0.5, 0, 1)
    img = PAPER[None, None, :] * (1 - 0.18 * t[..., None]) + PAPER_DARK[None, None, :] * (0.18 * t[..., None])
    # stains
    st = np.clip((blot - 0.62) * 5, 0, 1)
    img = img * (1 - 0.22 * st[..., None])
    # fibres: short streaks
    rng = np.random.default_rng(seed + 2)
    fib = np.zeros((h, w), np.float32)
    for _ in range(2600):
        x, y = rng.integers(0, w), rng.integers(0, h)
        a = rng.random() * np.pi
        L = rng.integers(6, 26)
        cv2.line(fib, (int(x), int(y)), (int(x + L * np.cos(a)), int(y + L * np.sin(a))), float(rng.random() * 0.5 + 0.3), 1, cv2.LINE_AA)
    fib = cv2.GaussianBlur(fib, (0, 0), 0.6)
    img = img * (1 - 0.06 * fib[..., None])
    # grain
    img *= (0.96 + 0.08 * rng.random((h, w, 1)).astype(np.float32))
    # burnt edges
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    dx = np.minimum(xx, w - 1 - xx) / w
    dy = np.minimum(yy, h - 1 - yy) / h
    d = np.minimum(dx * 1.0, dy * 1.78)
    edge = np.clip(1 - (d + (fbm(h, w, seed + 3, 5, 6) - 0.5) * 0.05) / 0.09, 0, 1) ** 1.6
    img = img * (1 - 0.55 * edge[..., None]) + np.array([60, 36, 18], np.float32) * 0.55 * edge[..., None]
    return np.clip(img, 0, 255)


def hatch(h, w, angle, spacing, width=0.42):
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    a = np.deg2rad(angle)
    # wobble the lines like a hand-cut block
    wob = (fbm(h, w, abs(int(angle)) + 11, 4, 8) - 0.5) * spacing * 0.9
    u = (xx * np.cos(a) + yy * np.sin(a) + wob) / spacing
    f = np.abs(u - np.round(u))  # 0 on a line .. 0.5 between
    return f, width


def woodcut(name, paper):
    src = cv2.imread(SRC.format(name))
    src = cv2.resize(src, (W, H), interpolation=cv2.INTER_AREA)
    rgb = cv2.cvtColor(src, cv2.COLOR_BGR2RGB).astype(np.float32) / 255
    lab = cv2.cvtColor(src, cv2.COLOR_BGR2LAB)
    L = lab[..., 0]
    # lift the night: local contrast, then a gamma so the scene reads on paper
    L = cv2.createCLAHE(clipLimit=3.0, tileGridSize=(8, 8)).apply(L)
    v = (L.astype(np.float32) / 255) ** 0.55
    v = np.clip((v - 0.06) / 0.80, 0, 1)
    # flat shapes (the cut): smooth the picture into areas, keeping their edges
    v8 = cv2.cvtColor((v * 255).astype(np.uint8), cv2.COLOR_GRAY2BGR)
    v8 = cv2.pyrMeanShiftFiltering(v8, 9, 22)
    v8 = cv2.edgePreservingFilter(v8, flags=1, sigma_s=40, sigma_r=0.25)
    tone = cv2.GaussianBlur(v8[..., 0].astype(np.float32) / 255, (0, 0), 1.0)
    # contours: difference of gaussians on the lifted picture, thresholded into brush lines
    g1 = cv2.GaussianBlur(v, (0, 0), 1.4)
    g2 = cv2.GaussianBlur(v, (0, 0), 3.2)
    dog = g1 - 0.985 * g2
    line = np.clip((-dog - 0.03) / 0.03, 0, 1)
    edges = cv2.Canny((tone * 255).astype(np.uint8), 60, 150).astype(np.float32) / 255
    edges = cv2.GaussianBlur(cv2.dilate(edges, np.ones((2, 2), np.uint8)), (0, 0), 0.9)
    line = np.maximum(line * 0.8, edges * 0.95)
    # hatching
    f1, w1 = hatch(H, W, 35, 7.0)
    f2, w2 = hatch(H, W, -40, 9.0)
    dark = 1 - tone
    # line thickness grows with darkness; a second cross-hatch comes in only in the shadows
    band = np.clip((dark - 0.42) / 0.05, 0, 1)   # light areas stay bare paper
    h1 = np.clip((0.12 + 0.35 * np.clip(dark - 0.42, 0, 1) / 0.3 - f1) / 0.05 + 0.5, 0, 1) * band
    h2 = np.clip((np.clip(dark - 0.62, 0, 1) * 1.4 - f2) / 0.05 + 0.5, 0, 1)
    solid = np.clip((dark - 0.74) / 0.04, 0, 1)
    ink = np.clip(np.maximum.reduce([h1 * 0.92, h2 * 0.95, solid, line]), 0, 1)
    # rough print: the ink doesn't take everywhere
    rng = np.random.default_rng(sum(map(ord, name)))
    speck = cv2.GaussianBlur(rng.random((H, W)).astype(np.float32), (0, 0), 0.9)
    ink *= np.clip(1.0 + (speck - 0.5) * 0.8, 0.93, 1)
    # watercolour wash: the picture's own saturated hues, faint, bled
    hsv = cv2.cvtColor(src, cv2.COLOR_BGR2HSV).astype(np.float32)
    sat = hsv[..., 1] / 255 * (hsv[..., 2] / 255) ** 0.5
    wash = cv2.GaussianBlur(rgb, (0, 0), 6)
    wash = wash / np.maximum(wash.max(axis=2, keepdims=True), 1e-3)
    wa = cv2.GaussianBlur(np.clip((sat - 0.18) * 1.8, 0, 0.55), (0, 0), 8)[..., None]
    page = paper * (1 - wa) + (paper * wash * 1.05) * wa
    # print
    out = page * (1 - ink[..., None]) + INK[None, None, :] * ink[..., None]
    # the burnt page edge goes over the print too
    return np.clip(out, 0, 255).astype(np.uint8)


if __name__ == '__main__':
    paper = parchment()
    if len(sys.argv) > 1 and sys.argv[1] == 'parchment':
        cv2.imwrite('assets/ui/intro_parchment.jpg', cv2.cvtColor(paper.astype(np.uint8), cv2.COLOR_RGB2BGR), [cv2.IMWRITE_JPEG_QUALITY, 80])
        sys.exit()
    names = sys.argv[1:] or NAMES
    for n in names:
        img = woodcut(n, paper)
        cv2.imwrite(OUT.format(n), cv2.cvtColor(img, cv2.COLOR_RGB2BGR), [cv2.IMWRITE_JPEG_QUALITY, 84])
        print('wrote', OUT.format(n))
