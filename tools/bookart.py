"""Woodcut-style ink illustrations for "Alduin's Curse" (the lore book), drawn from nothing: dark iron-gall ink on
transparent, so they sit on Skyrim's parchment pages. Data/Textures/CurseOfAlduin/Book/<name>.png
    python tools/bookart.py <out Data dir> [preview.png]"""
import math, pathlib, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFilter

OUT = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else 'build/assets/Data') / 'Textures' / 'CurseOfAlduin' / 'Book'
INK = (46, 30, 20)
SS = 2  # supersampling
rng = np.random.default_rng(11)

class Plate:
    def __init__(self, w, h):
        self.w, self.h = w * SS, h * SS
        self.a = Image.new('L', (self.w, self.h), 0)      # ink coverage
        self.d = ImageDraw.Draw(self.a)
    def P(self, pts): return [(x * SS, y * SS) for x, y in pts]
    def poly(self, pts, fill=255): self.d.polygon(self.P(pts), fill=fill)
    def line(self, pts, w=2, fill=255): self.d.line(self.P(pts), fill=fill, width=max(1, int(w * SS)), joint='curve')
    def ellipse(self, box, fill=None, outline=None, w=2):
        x0, y0, x1, y1 = box; self.d.ellipse([x0 * SS, y0 * SS, x1 * SS, y1 * SS], fill=fill, outline=outline, width=int(w * SS))
    def hatch(self, mask_pts, angle=0.8, gap=5, w=1.2, jitter=0.6, cross=False, ellipse=None):
        """parallel engraving lines inside a polygon (or an ellipse box)"""
        m = Image.new('L', (self.w, self.h), 0)
        if ellipse:
            x0, y0, x1, y1 = ellipse; ImageDraw.Draw(m).ellipse([x0 * SS, y0 * SS, x1 * SS, y1 * SS], fill=255)
        else:
            ImageDraw.Draw(m).polygon(self.P(mask_pts), fill=255)
        lines = Image.new('L', (self.w, self.h), 0); ld = ImageDraw.Draw(lines)
        for ang in ([angle, angle + 1.2] if cross else [angle]):
            c, s = math.cos(ang), math.sin(ang); L = (self.w + self.h)
            for k in np.arange(-L, L, gap * SS):
                x0, y0 = self.w / 2 + -s * k - c * L, self.h / 2 + c * k - s * L
                x1, y1 = self.w / 2 + -s * k + c * L, self.h / 2 + c * k + s * L
                ld.line([(x0, y0), (x1, y1)], fill=255, width=max(1, int(w * SS + rng.uniform(-jitter, jitter))))
        self.a.paste(255, (0, 0), Image.fromarray(np.minimum(np.asarray(lines), np.asarray(m))))
    def flame(self, x, y, h, wdt, lean=0.0, n=5):
        """woodcut fire: solid tongues with a hollow heart"""
        for k in range(n):
            ox = (k - (n - 1) / 2) * wdt / n * 0.9; hh = h * rng.uniform(0.55, 1.0); bw = wdt / n * 0.9
            def tongue(scale, fill):
                pts = []
                for t in np.linspace(0, 1, 14):
                    half = bw * scale * (1 - t) ** 0.8 * (0.6 + 0.4 * math.sin(t * math.pi))
                    cx = x + ox + lean * hh * t + math.sin(t * 6 + k) * 4 * t
                    pts.append((cx - half, y - hh * scale * t))
                for t in np.linspace(1, 0, 14):
                    half = bw * scale * (1 - t) ** 0.8 * (0.6 + 0.4 * math.sin(t * math.pi))
                    cx = x + ox + lean * hh * t + math.sin(t * 6 + k) * 4 * t
                    pts.append((cx + half, y - hh * scale * t))
                self.poly(pts, fill)
            tongue(1.0, 255); tongue(0.5, 0)
    def flame_lines(self, x, y, h, wdt, lean=0.0, n=5):
        for k in range(n):
            ox = (k - n / 2) * wdt / n + rng.uniform(-2, 2); hh = h * rng.uniform(0.55, 1.0)
            tip = (x + ox + lean * hh + rng.uniform(-4, 4), y - hh)
            pts = [(x + ox - wdt / n, y)] + [(x + ox - wdt / (2 * n) * (1 - t) + math.sin(t * 7 + k) * 3 + lean * hh * t, y - hh * t) for t in np.linspace(0.1, 0.9, 6)] + [tip] + \
                  [(x + ox + wdt / (2 * n) * (1 - t) + math.sin(t * 6 + k) * 3 + lean * hh * t, y - hh * t) for t in np.linspace(0.9, 0.1, 6)] + [(x + ox + wdt / n, y)]
            self.line(pts + [pts[0]], 1.6)
    def frame(self, w=3):
        W, H = self.w // SS, self.h // SS
        self.line([(6, 6), (W - 6, 6), (W - 6, H - 6), (6, H - 6), (6, 6)], w)
        self.line([(12, 12), (W - 12, 12), (W - 12, H - 12), (12, H - 12), (12, 12)], 1)
    def save(self, name):
        a = self.a.resize((self.w // SS, self.h // SS), Image.LANCZOS)
        a = Image.fromarray((np.asarray(a, np.float32) * (0.85 + 0.15 * rng.random(a.size[::-1]))).astype(np.uint8))  # ink grain
        im = Image.new('RGBA', a.size, INK + (0,)); im.putalpha(a)
        OUT.mkdir(parents=True, exist_ok=True); im.save(OUT / f'{name}.png'); return im

def helgen():
    """Helgen burning: the keep's tower and the town's roofs in flame, fire raining from a storm sky"""
    p = Plate(512, 300); W, H = 512, 300
    p.hatch([(14, 14), (498, 14), (498, 150), (14, 150)], angle=0.15, gap=6, w=1.0)            # storm sky
    for k in range(9):                                                                             # falling fire
        x = rng.uniform(40, 470); y = rng.uniform(20, 120); L = rng.uniform(25, 60)
        p.line([(x, y), (x - L * 0.5, y - L)], 2.4); p.ellipse((x - 4, y - 4, x + 4, y + 4), fill=255)
    p.poly([(14, 240), (498, 230), (498, 286), (14, 286)])                                         # the ground
    p.poly([(300, 240), (300, 70), (292, 70), (292, 58), (360, 58), (360, 70), (352, 70), (352, 240)])  # the keep's tower
    for x in range(296, 356, 12): p.poly([(x, 58), (x, 48), (x + 6, 48), (x + 6, 58)])             # crenels
    p.poly([(330, 72), (360, 58), (370, 64), (345, 95)], fill=0)                                   # broken corner
    for x0, w, h in ((40, 70, 46), (120, 60, 38), (200, 80, 52), (380, 70, 44), (440, 50, 36)):  # roofs
        p.poly([(x0, 240), (x0, 240 - h * 0.5), (x0 + w / 2, 240 - h), (x0 + w, 240 - h * 0.5), (x0 + w, 240)])
        p.flame(x0 + w / 2, 240 - h * 0.7, 55, w * 0.9, lean=0.15)
    p.flame(326, 70, 60, 50, lean=0.2, n=6)
    for k in range(5):                                                                             # smoke
        x = 80 + k * 90; pts = [(x + math.sin(t * 5 + k) * 18 + t * 40, 200 - t * 170) for t in np.linspace(0, 1, 30)]
        p.line(pts, 1.4)
    p.frame(); return p.save('helgen')

def graves():
    """the dead claw up out of a churchyard under a blood moon"""
    p = Plate(512, 300)
    p.ellipse((350, 28, 450, 128), fill=255)
    p.ellipse((358, 36, 442, 120), fill=0)
    p.hatch(None, angle=-0.6, gap=5, w=1.2, ellipse=(358, 36, 442, 120))
    for a in np.linspace(0, 2 * math.pi, 17)[:-1]:   # the moon's corona
        p.line([(400 + math.cos(a) * 56, 78 + math.sin(a) * 56), (400 + math.cos(a) * 70, 78 + math.sin(a) * 70)], 1.5)
    p.poly([(14, 230), (140, 215), (300, 222), (498, 210), (498, 286), (14, 286)])               # earth
    p.line([(70, 230), (70, 120), (55, 90)], 4); p.line([(70, 150), (100, 110), (120, 100)], 3); p.line([(70, 130), (40, 105)], 2.5)  # dead tree
    for x, h, tilt in ((150, 60, 0.1), (230, 72, -0.08), (310, 56, 0.15), (420, 64, -0.12)):     # tombstones
        c, s = math.cos(tilt), math.sin(tilt); pts = []
        for px, py in [(-16, 0), (-16, -h + 16)] + [(16 * math.cos(a), -h + 16 - 16 * math.sin(a)) for a in np.linspace(math.pi, 0, 10)] + [(16, 0)]:
            pts.append((x + px * c - py * s, 222 + px * s + py * c))
        p.poly(pts); p.line([(x - 6, 222 - h + 26), (x + 6, 222 - h + 26)], 2, fill=0); p.line([(x, 222 - h + 18), (x, 222 - h + 40)], 2, fill=0)
    for x, y in ((190, 222), (270, 225), (370, 215), (460, 212)):                                    # hands rising
        p.line([(x, y + 8), (x + 2, y - 26)], 5)
        for k in range(4): p.line([(x + 2, y - 26), (x - 6 + k * 5, y - 40 - (k % 2) * 5)], 2)
    p.frame(); return p.save('graves')

def wards():
    """three ward-fires on a ridge, light breaking the dark"""
    p = Plate(512, 300)
    p.hatch([(14, 14), (498, 14), (498, 200), (14, 200)], angle=0.0, gap=5, w=1.0)
    p.poly([(14, 250), (120, 210), (256, 190), (390, 212), (498, 245), (498, 286), (14, 286)])
    for x, y in ((120, 212), (256, 192), (392, 213)):
        p.poly([(x - 18, y), (x - 26, y - 34), (x + 26, y - 34), (x + 18, y)])                     # the brazier bowl
        p.line([(x - 16, y), (x - 22, y + 18)], 3); p.line([(x + 16, y), (x + 22, y + 18)], 3)
        p.ellipse((x - 70, y - 120, x + 70, y + 10), fill=0)                                       # the light it throws
        p.flame(x, y - 34, 70, 44, n=5)
        for a in np.linspace(-2.6, -0.5, 7): p.line([(x + math.cos(a) * 75, y - 40 + math.sin(a) * 75), (x + math.cos(a) * 95, y - 40 + math.sin(a) * 95)], 1.6)
    p.frame(); return p.save('wards')

def herald():
    """the Herald of the Curse: a horned wyrm's skull looming out of banks of fog"""
    p = Plate(512, 300)
    cx, cy = 256, 120
    # long snout pointing down, wide brow, cheek spikes
    skull = [(cx - 70, cy - 40), (cx - 30, cy - 62), (cx + 30, cy - 62), (cx + 70, cy - 40), (cx + 86, cy - 6), (cx + 64, cy + 18),
             (cx + 40, cy + 70), (cx + 26, cy + 130), (cx, cy + 146), (cx - 26, cy + 130), (cx - 40, cy + 70), (cx - 64, cy + 18), (cx - 86, cy - 6)]
    p.poly(skull)
    for sx in (-1, 1):
        horn = [(cx + sx * (40 + 110 * t) + sx * 30 * math.sin(t * 2.6), cy - 50 - 70 * t + 40 * t * t) for t in np.linspace(0, 1, 16)]
        width = [14 * (1 - t) + 1 for t in np.linspace(0, 1, 16)]
        p.poly([(x, y - w) for (x, y), w in zip(horn, width)] + [(x, y + w) for (x, y), w in reversed(list(zip(horn, width)))])
        for k in range(3):   # cheek spikes
            p.poly([(cx + sx * (70 - k * 8), cy + 4 + k * 18), (cx + sx * (112 - k * 10), cy + 20 + k * 22), (cx + sx * (62 - k * 8), cy + 18 + k * 18)])
        p.poly([(cx + sx * 18, cy - 30), (cx + sx * 60, cy - 26), (cx + sx * 56, cy + 2), (cx + sx * 24, cy - 4)], fill=0)   # eye sockets
        p.ellipse((cx + sx * 40 - 4, cy - 18, cx + sx * 40 + 4, cy - 10), fill=255)                                        # a glint
        p.ellipse((cx + sx * 10 - 4, cy + 108, cx + sx * 10 + 4, cy + 118), fill=0)                                        # nostrils
        for k in range(5): p.line([(cx + sx * (16 + k * 5), cy + 60 + k * 14), (cx + sx * (14 + k * 5), cy + 72 + k * 14)], 2, fill=0)  # teeth
    p.line([(cx, cy - 60), (cx, cy + 40)], 2, fill=0)
    p.hatch(skull, angle=1.2, gap=7, w=1.0)
    for k in range(6):                                                                             # fog banks
        y = 186 + k * 16; pts = [(14 + t * 484, y + math.sin(t * 12 + k * 1.7) * 6) for t in np.linspace(0, 1, 60)]
        p.poly(pts + [(498, 286), (14, 286)], fill=0)
        p.line(pts, 1.4)
    p.frame(); return p.save('herald')

def sigil():
    """the seal of the Curse: a knotwork ring round a skull rune"""
    p = Plate(320, 320); c = 160
    p.ellipse((c - 140, c - 140, c + 140, c + 140), outline=255, w=4); p.ellipse((c - 122, c - 122, c + 122, c + 122), outline=255, w=2)
    for k in range(24):                                                                            # knot crossings
        a = k * math.pi / 12; b = a + math.pi / 12
        p.line([(c + math.cos(a) * 124, c + math.sin(a) * 124), (c + math.cos(b) * 138, c + math.sin(b) * 138)], 3)
        p.line([(c + math.cos(a) * 138, c + math.sin(a) * 138), (c + math.cos(b) * 124, c + math.sin(b) * 124)], 3)
    p.ellipse((c - 52, c - 70, c + 52, c + 30), fill=255); p.poly([(c - 34, c + 10), (c + 34, c + 10), (c + 24, c + 60), (c - 24, c + 60)])
    p.ellipse((c - 36, c - 38, c - 8, c - 8), fill=0); p.ellipse((c + 8, c - 38, c + 36, c - 8), fill=0)
    p.poly([(c, c - 2), (c - 8, c + 14), (c + 8, c + 14)], fill=0)
    for k in range(-2, 3): p.line([(c + k * 11, c + 36), (c + k * 11, c + 58)], 3, fill=0)
    for a in np.linspace(0, 2 * math.pi, 9)[:-1]:                                                  # runes between the rings
        x, y = c + math.cos(a) * 95, c + math.sin(a) * 95
        p.line([(x, y - 12), (x, y + 12)], 3); p.line([(x, y - 4), (x + 8, y - 12)], 2); p.line([(x, y + 4), (x - 8, y + 12)], 2)
    return p.save('sigil')

def omen():
    """nights of omen: a bleeding moon over black pines"""
    p = Plate(512, 260)
    p.ellipse((196, 30, 316, 150), fill=255)
    p.ellipse((204, 38, 308, 142), fill=0); p.hatch(None, angle=0.4, gap=4, w=1.4, ellipse=(204, 38, 308, 142), cross=True)
    for a in np.linspace(0, 2 * math.pi, 25)[:-1]:
        p.line([(256 + math.cos(a) * 66, 90 + math.sin(a) * 66), (256 + math.cos(a) * 84, 90 + math.sin(a) * 84)], 1.4)
    for k in range(4): p.line([(256 + (k - 1.5) * 18, 150), (256 + (k - 1.5) * 18 + rng.uniform(-3, 3), 170 + k * 7)], 2)   # it drips
    for x in range(20, 500, 26):                                                                   # pines
        h = rng.uniform(60, 120); base = 246
        p.poly([(x, base - h), (x - h * 0.22, base), (x + h * 0.22, base)])
    p.poly([(14, 240), (498, 240), (498, 246), (14, 246)])
    p.frame(); return p.save('omen')

ART = {'sigil': sigil, 'helgen': helgen, 'graves': graves, 'wards': wards, 'herald': herald, 'omen': omen}
if __name__ == '__main__':
    ims = [f() for f in ART.values()]
    if len(sys.argv) > 2:   # a preview on parchment
        sheet = Image.new('RGB', (1060, 1000), (222, 205, 168))
        x, y = 10, 10
        for im in ims:
            if x + im.width > 1060: x, y = 10, y + 330
            sheet.paste(im, (x, y), im); x += im.width + 10
        sheet.save(sys.argv[2])
