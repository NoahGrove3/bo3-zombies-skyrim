"""School emblems for the Coffer's spells (the HUD's spell dock and the shrine's roll): the same forged medallion as the
element emblems (tools/spellicons.py, tools/elementicons.py) - the disc still wells up in the spell's element colour - but
the sigil burning in it is the spell's school of magic (Noah: the type of spell, not its element).

    python tools/schoolicons.py [out dir]     writes sc_<school>_<element>.png (256x256) for 5 schools x 8 elements
"""
import math, pathlib, sys
import numpy as np
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import spellicons
from spellicons import P, arc_pts, chaikin
import elementicons

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / 'assets' / 'ui')
BLACK = (0, 0, 0, 255)


def sig_destruction(d, f, s):   # a shattering burst: four great curved blades and four short spikes round a cracked core
    for k in range(4):
        a = k * math.pi / 2 - math.pi / 4
        ux, uy = math.cos(a), math.sin(a)
        px, py = -uy, ux
        blade = []
        for t in np.linspace(0, 1, 18):   # a blade that sweeps out and curls like a flame tongue
            r = 0.26 + 0.78 * t
            bend = 0.22 * math.sin(t * math.pi * 0.9)
            w = 0.17 * (1 - t) ** 1.2
            blade.append((ux * r + px * (bend + w), uy * r + py * (bend + w)))
        for t in np.linspace(1, 0, 18):
            r = 0.26 + 0.78 * t
            bend = 0.22 * math.sin(t * math.pi * 0.9)
            w = 0.17 * (1 - t) ** 1.2
            blade.append((ux * r + px * (bend - w), uy * r + py * (bend - w)))
        d.polygon(P(blade), fill=f)
        b = a + math.pi / 4
        vx, vy = math.cos(b), math.sin(b)
        d.polygon(P([(vx * 0.3 - vy * 0.08, vy * 0.3 + vx * 0.08), (vx * 0.62, vy * 0.62), (vx * 0.3 + vy * 0.08, vy * 0.3 - vx * 0.08)]), fill=f)
    d.ellipse(P([(-0.3, -0.3), (0.3, 0.3)]), fill=f)
    d.line(P([(-0.14, -0.2), (0.02, -0.02), (-0.06, 0.06), (0.12, 0.22)]), fill=BLACK, width=int(4 * s))  # the crack


def sig_conjuration(d, f, s):   # a summoning gate: a pointed arch rising out of a rune circle, a soul-spark in its mouth
    d.line(P(arc_pts(0, 0.1, 0.9, math.pi * 0.08, math.pi * 0.92, 50)), fill=f, width=int(5 * s))   # the circle's lower half
    for k in range(7):                                                                                   # its runes
        a = math.pi * (0.16 + 0.68 * k / 6)
        x, y = 0.9 * math.cos(a), 0.1 + 0.9 * math.sin(a)
        d.line(P([(x * 0.82, 0.1 + (y - 0.1) * 0.82), (x * 1.08, 0.1 + (y - 0.1) * 1.08)]), fill=f, width=int(4 * s))
    arch = [(-0.5, 0.62)] + [(-0.5 + 0.0 * t, 0.62 - 0.9 * t) for t in np.linspace(0, 1, 6)][1:]
    left = [(-0.5, 0.62), (-0.5, -0.1)] + [(-0.5 + 0.5 * (1 - math.cos(t * math.pi / 2)), -0.1 - 0.9 * math.sin(t * math.pi / 2)) for t in np.linspace(0, 1, 14)]
    right = [(-x, y) for x, y in reversed(left)]
    gate = left + right[1:]
    d.polygon(P(gate), outline=f, width=int(6 * s))
    inner = [(x * 0.62, 0.62 - (0.62 - y) * 0.78) for x, y in gate]
    d.polygon(P(inner), fill=f)
    d.polygon(P([(x * 0.4, 0.62 - (0.62 - y) * 0.5) for x, y in gate]), fill=BLACK)
    d.ellipse(P([(-0.11, -0.08), (0.11, 0.14)]), fill=f)                                                  # the spark


def sig_restoration(d, f, s):   # the healer's flame: a heart-shaped flame cupped by two rising leaves, under a halo of light
    for k in range(9):                                                                                   # the halo's rays
        a = math.pi * (1.1 + 0.8 * k / 8)
        l1 = 0.98 if k % 2 == 0 else 0.88
        d.line(P([(0.74 * math.cos(a), 0.74 * math.sin(a) - 0.05), (l1 * math.cos(a), l1 * math.sin(a) - 0.05)]), fill=f, width=int(4 * s))
    d.line(P(arc_pts(0, -0.05, 0.66, math.pi * 1.08, math.pi * 1.92, 40)), fill=f, width=int(4 * s))
    flame = chaikin([(0, -0.62), (0.16, -0.36), (0.36, -0.12), (0.38, 0.14), (0.22, 0.34), (0, 0.42), (-0.22, 0.34), (-0.38, 0.14), (-0.36, -0.12), (-0.16, -0.36)])
    d.polygon(P(flame), fill=f)
    d.polygon(P(chaikin([(0, -0.16), (0.14, 0.04), (0.12, 0.2), (0, 0.26), (-0.12, 0.2), (-0.14, 0.04)])), fill=BLACK)
    for sx in (1, -1):                                                                                   # the leaves
        leaf = chaikin([(sx * 0.08, 0.86), (sx * 0.34, 0.74), (sx * 0.62, 0.5), (sx * 0.74, 0.18), (sx * 0.52, 0.36), (sx * 0.28, 0.56)], 2)
        d.polygon(P(leaf), fill=f)


def sig_alteration(d, f, s):    # change: three arcs chasing each other round a hexagon, each ending in an arrowhead
    hexa = [(0.34 * math.cos(k * math.pi / 3 + math.pi / 6), 0.34 * math.sin(k * math.pi / 3 + math.pi / 6)) for k in range(6)]
    d.polygon(P(hexa), outline=f, width=int(5 * s))
    d.polygon(P([(x * 0.45, y * 0.45) for x, y in hexa]), fill=f)
    for k in range(3):
        a0 = k * 2 * math.pi / 3 - math.pi / 2 + 0.25
        a1 = a0 + 2 * math.pi / 3 - 0.55
        d.line(P(arc_pts(0, 0, 0.74, a0, a1, 30)), fill=f, width=int(7 * s))
        ex, ey = 0.74 * math.cos(a1), 0.74 * math.sin(a1)
        tx, ty = -math.sin(a1), math.cos(a1)                       # the way the arc runs
        nx, ny = math.cos(a1), math.sin(a1)
        d.polygon(P([(ex + tx * 0.24, ey + ty * 0.24), (ex + nx * 0.16, ey + ny * 0.16), (ex - nx * 0.16, ey - ny * 0.16)]), fill=f)
        d.line(P([(0.36 * math.cos(a0 - 0.25), 0.36 * math.sin(a0 - 0.25)), (0.6 * math.cos(a0 - 0.25), 0.6 * math.sin(a0 - 0.25))]), fill=f, width=int(4 * s))


def sig_illusion(d, f, s):      # the veiled eye: an almond eye with a slit pupil, mist lines drifting under it
    top = [(x, -0.42 * math.cos(x * math.pi / 2) ** 1.1 - 0.05) for x in np.linspace(-0.95, 0.95, 30)]
    bot = [(x, 0.32 * math.cos(x * math.pi / 2) ** 1.1 - 0.05) for x in np.linspace(0.95, -0.95, 30)]
    d.polygon(P(top + bot), fill=f)
    d.polygon(P([(x * 0.86, y * 0.8 - 0.01) for x, y in top + bot]), fill=BLACK)
    d.ellipse(P([(-0.27, -0.33), (0.27, 0.21)]), fill=f)
    d.polygon(P(chaikin([(0, -0.3), (0.07, -0.06), (0, 0.18), (-0.07, -0.06)], 2)), fill=BLACK)
    for k, (y, w) in enumerate(((0.42, 0.7), (0.58, 0.5), (0.72, 0.3))):                                # the veil of mist
        pts = [(x, y + 0.04 * math.sin(x * 7 + k)) for x in np.linspace(-w, w, 24)]
        d.line(P(pts), fill=f, width=int(4 * s))
    for k in range(5):                                                                                   # lashes
        a = math.pi * (1.15 + 0.7 * k / 4)
        d.line(P([(0.62 * math.cos(a) * 1.25, 0.62 * math.sin(a) * 0.95 - 0.05), (0.8 * math.cos(a) * 1.25, 0.8 * math.sin(a) * 0.95 - 0.05)]), fill=f, width=int(4 * s))


SCHOOLS = {'destruction': sig_destruction, 'conjuration': sig_conjuration, 'restoration': sig_restoration,
           'alteration': sig_alteration, 'illusion': sig_illusion}


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    spellicons.SIGILS.update(SCHOOLS)
    for el, (c, a, _) in elementicons.ELEMENTS.items():
        for school in SCHOOLS:
            spellicons.medallion(c, a, school).save(OUT / f'sc_{school}_{el}.png')
    print(len(SCHOOLS) * len(elementicons.ELEMENTS), 'school emblems')


if __name__ == '__main__':
    main()
