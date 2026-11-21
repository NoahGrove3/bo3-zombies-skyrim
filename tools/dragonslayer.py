"""(Noah, 2026-11-20: "our own Dragon Slayer sword" - modelled from scratch; a katana "keep its moveset", "Dragonbone +
scales", "Faint ember glow in runes") the Dragon Slayer: a katana whose blade was cut from a dragon's bone - bone-white,
polished to a soft sheen, a pale wavering temper line along its keen edge - with the Dragon tongue's letters cut along its
spine and glowing faintly like banked embers; a guard of bone with two dragon fangs curving up along the blade; a long
grip bound in overlapping black dragon scales between collars of blackened iron; a bone pommel gripped by a small claw.

As Skyrim's swords are: the grip at the origin, the blade up +Y, Prn WeaponSword; built along Z with
tools/priestweapons.py's parts and turned at the end. Its proportions are a katana's: a long gently curved blade (about
72 units), a long two-hand grip (about 26).

    python3 tools/dragonslayer.py [--keep]
      -> assets/fx/Meshes/CurseOfAlduin/slayer/dragonslayer.nif, Textures/CurseOfAlduin/slayer/slay_*.dds"""
import math, pathlib, shutil, sys
import numpy as np
from scipy.ndimage import gaussian_filter

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import priestweapons as pw
from throwdagger import Mesh, tileable, height_to_normal, tool

g = pw.g
ROOT = g.ROOT
pw.MESHDIR = ROOT / 'assets' / 'fx' / 'Meshes' / 'CurseOfAlduin' / 'slayer'
g.TEXDIR = ROOT / 'assets' / 'fx' / 'Textures' / 'CurseOfAlduin' / 'slayer'
g.TEXREL = 'CurseOfAlduin\\slayer\\'
pw.WORK = g.WORK = ROOT / 'build' / 'dragonslayer'
pw.FIT['dragonslayer'] = (1.4, 1.6)   # (the grip's top, model units -> game units: the hand just under the guard)
S = 2048


def bone_blade_tex():
    """the blade's sheet - u across (the spine at 0, the edge at 1), v along (the guard at 0): dragon bone, its grain running
    the blade's length, polished; the spine a little darker and warmer; along the edge a ground bevel, whiter, and the
    temper line wavering where the bevel meets the body; hairline age crazing here and there; a faint warm stain near the
    guard where hands and blood have been"""
    v, u = (np.mgrid[0:S, 0:S].astype(np.float32) + 0.5) / S
    grain = tileable(S, S, 0.7, 2101, aniso=(0.03, 1.0))
    fibre = tileable(S, S, 0.25, 2102, aniso=(0.06, 1.0))
    blot = tileable(S, S, 1.8, 2103)
    wav = tileable(S, S, 1.2, 2104, aniso=(1.0, 0.25))
    hamon = 0.66 + 0.025 * (wav - 0.5) * 2 + 0.008 * np.sin(v * 2 * np.pi * 34)   # (Noah: "make sure the edge is fully finished" - a calm, even temper line)
    bev = np.clip((u - hamon) / 0.015, 0, 1)
    line = np.exp(-((u - hamon) / 0.008) ** 2)
    spine = np.clip(1 - u / 0.22, 0, 1) ** 1.5
    hone = np.clip((u - 0.965) / 0.01, 0, 1)   # (the honed micro-bevel right at the edge: a crisp bright line the whole length)
    d1, d2 = g.cells(1024, 260, 2105)
    craze = np.kron(np.clip(1 - (np.sqrt(d2) - np.sqrt(d1)) / 1.2, 0, 1), np.ones((2, 2))) * (blot > 0.55) * (1 - bev)
    lum = 0.9 + 0.08 * (grain - 0.5) + 0.05 * (fibre - 0.5) + 0.06 * (blot - 0.5) - 0.1 * spine
    col = np.array([226, 216, 192], np.float32) * lum[..., None]
    col = col * (1 - spine[..., None] * 0.25) + np.array([190, 168, 128]) * spine[..., None] * 0.25
    col = col * (1 - bev[..., None]) + np.array([244, 240, 228]) * (0.94 + 0.06 * (fibre[..., None] - 0.5)) * bev[..., None]
    col = col * (1 - hone[..., None]) + np.array([250, 248, 240]) * hone[..., None]
    col = col + np.array([34, 30, 22]) * line[..., None] - np.array([26, 30, 36]) * (np.clip((hamon - u) / 0.05, 0, 1) * (1 - bev))[..., None] * 0.6
    guardstain = np.clip(1 - v / 0.12, 0, 1) ** 2 * (0.4 + 0.6 * blot)
    col = col * (1 - 0.18 * guardstain[..., None]) + np.array([150, 104, 70]) * 0.18 * guardstain[..., None]
    col = col * (1 - 0.35 * craze[..., None]) + np.array([150, 136, 110]) * 0.35 * craze[..., None]
    D = pw.save(col, 'slay_bone_d.dds')
    h = 0.35 * grain + 0.25 * fibre - 0.6 * craze + 0.2 * bev
    spec = np.clip(70 + 50 * bev + 40 * hone + 20 * line - 40 * craze + 12 * (grain - 0.5), 0, 255)   # (Noah: "less shiny")
    N = pw.save(np.dstack([height_to_normal(gaussian_filter(h, 0.7), 5.0), spec]), 'slay_bone_n.dds', True)
    return D, N


def hilt_bone_tex():
    """the guard and pommel: older, yellower bone with the grain of the fang it was cut from"""
    grain = tileable(S // 2, S // 2, 0.6, 2111, aniso=(0.05, 1.0)); blot = tileable(S // 2, S // 2, 1.5, 2112)
    por = tileable(S // 2, S // 2, 0.25, 2113); pores = np.clip((por - 0.7) * 5, 0, 1)
    col = np.array([214, 198, 160], np.float32) * (0.84 + 0.16 * (grain[..., None] - 0.5) + 0.12 * (blot[..., None] - 0.5))
    col = col * (1 - 0.4 * pores[..., None]) + np.array([96, 78, 52]) * 0.4 * pores[..., None]
    D = pw.save(col, 'slay_hiltbone_d.dds')
    N = pw.save(np.dstack([height_to_normal(gaussian_filter(0.5 * grain - 0.6 * pores + 0.3 * blot, 0.7), 6.0), np.clip(100 + 50 * grain - 70 * pores, 0, 255)]), 'slay_hiltbone_n.dds', True)
    return D, N


def scales_tex():
    """the grip's binding: black dragon scales, row over row, each a low dome with a ridge down its middle, overlapping
    toward the pommel; a faint oily green-blue sheen. Tiles both ways (u round the grip, v along it)"""
    n = 1024
    cols, rows = 8, 16
    cw, rh = n / cols, n / rows
    P = int(2 * rh) + 2; m = n + 2 * P   # (painted over a wider sheet in one fixed order - each row over the next, each
    H = np.zeros((m, m), np.float32); EDGE = np.zeros((m, m), np.float32); TINT = np.zeros((m, m), np.float32)   # scale over the one left of it - then the middle cut out: it tiles)
    tint = np.random.default_rng(2121).random((rows, cols))
    for j in range(rows + 2, -3, -1):
        for i in range(-2, cols + 2):
            cx = (i + 0.5 * (j % 2)) * cw + P; cy = j * rh + P
            ax, ay = cw * 0.62, rh * 1.25
            ys = np.arange(max(0, int(cy - 0.15 * ay) - 1), min(m, int(cy + ay) + 2)); xs = np.arange(max(0, int(cx - ax) - 1), min(m, int(cx + ax) + 2))
            if not len(ys) or not len(xs): continue
            dy = ((ys - cy) / ay)[:, None]; dx = ((xs - cx) / ax)[None, :]
            rr = dx * dx + dy * dy
            inside = (rr < 1) & (dy > -0.15)
            dome = np.sqrt(np.clip(1 - rr, 0, 1))
            ridge = np.exp(-(dx / 0.12) ** 2) * np.clip(dy, 0, 1) * 0.25
            e = np.clip(1 - (1 - rr) / 0.15, 0, 1)
            sub = (slice(ys[0], ys[-1] + 1), slice(xs[0], xs[-1] + 1))
            H[sub][inside] = (dome + ridge)[inside]; EDGE[sub][inside] = e[inside]; TINT[sub][inside] = tint[j % rows, i % cols]
    H, EDGE, TINT = (A[P:P + n, P:P + n] for A in (H, EDGE, TINT))
    fine = tileable(n, n, 0.3, 2122)
    lum = 0.55 + 0.5 * H - 0.35 * EDGE + 0.1 * (fine - 0.5) + 0.12 * (TINT - 0.5)
    col = np.array([30, 32, 34], np.float32) * lum[..., None]
    col = col + np.array([10, 22, 24]) * (H * (1 - EDGE))[..., None] * 0.6   # (the sheen on each scale's crown)
    D = pw.save(col, 'slay_scales_d.dds')
    N = pw.save(np.dstack([height_to_normal(gaussian_filter(H - 0.4 * EDGE + 0.05 * fine, 0.8), 7.0), np.clip(70 + 120 * H * (1 - EDGE), 0, 255)]), 'slay_scales_n.dds', True)
    return D, N


def build():
    bD, bN = bone_blade_tex()
    hD, hN = hilt_bone_tex()
    sD, sN = scales_tex()
    rD, rN, rG = pw.glyph_strip('slay_runes', (196, 184, 156), glow_rgb=(255, 112, 36), seed=2131, lit=(214, 100, 42))
    iD, iN = pw.metal_tex('slay_iron', (52, 50, 48), dark=0.9, seed=2141)
    M = Mesh()
    # (the blade's material 'dbone', not 'bone': kept out of roughen()'s forge dents, so its edge runs true and keen)
    # the blade: single-edged (its spine at -x, its keen edge at +x), curving back toward its spine so the edge rides the
    # outside of the curve as a katana's does, a short sharp point; the letters cut along the spine
    pw.blade(M, 'dbone', 2.6, 45.0, 2.15, 0.55, tip=0.1, seed=2151, ns=150, curve=-0.055, single=0.3,
             runes=('runes', 0.28, 0.06, 0.66, 9.6, -0.42))
    # the collar where the blade meets the guard: blackened iron
    M.add('Iron', 'iron', *pw.box(np.array([0.0, 0.0, 2.3]), 1.15, 0.42, 0.35))
    # the guard: an oval of bone, and two fangs rising from its sides, curving in toward the blade
    M.add('Guard', 'hiltbone', *pw.superbox(np.array([0, 0, 1.7]), 2.3, 1.75, 0.22, 3.0, 28, 12))
    for side in (-1, 1):
        base = np.array([side * 1.75, 0.0, 1.8])
        pw.talon(M, 'hiltbone', base, np.array([side * 0.45, 0, 1.0]), np.array([-side * 0.25, 0, 1.0]), 5.0, 0.42, 18)
    # the grip: iron collars at each end, between them the binding of black scales
    M.add('Iron', 'iron', *pw.lathe([(1.5, 0.86), (1.3, 0.94), (0.95, 0.9)], 24))
    M.add('Grip', 'scales', *pw.lathe([(1.0, 0.82)] + [(z, 0.82 + 0.06 * math.sin(math.pi * (1.0 - z) / 16.0)) for z in np.linspace(0.95, -14.4, 30)] + [(-14.5, 0.82)], 28,
                                      uvs=(1.0, 3.0, 0.0, 0.0)))
    M.add('Iron', 'iron', *pw.lathe([(-14.4, 0.9), (-14.7, 0.98), (-15.0, 0.9)], 24))
    # the pommel: a cap of bone, a small claw of iron gripping it
    pc = -15.7
    M.add('Pommel', 'hiltbone', *pw.lathe([(pc + 0.75 * math.cos(a), 0.85 * math.sin(a)) for a in np.linspace(0, math.pi, 14)], 22))
    for k in range(3):
        a = 2 * math.pi * k / 3 + 0.4
        d = np.array([math.cos(a), math.sin(a), 0.0])
        pw.talon(M, 'iron', np.array([d[0] * 0.75, d[1] * 0.75, -15.0]), d * 0.4 + np.array([0, 0, -0.7]), -d * 0.8 + np.array([0, 0, -0.5]), 1.5, 0.16)
    mats = {'dbone': f'diffuse={bD} normal={bN} spec=0.55 gloss=40 alpha=0 doublesided=0',
            'runes': f'diffuse={rD} normal={rN} glow={rG} emit=1,0.47,0.16 emitmult=1.7 spec=0.4 gloss=40 alpha=blend doublesided=0',
            'hiltbone': f'diffuse={hD} normal={hN} spec=0.3 gloss=22 alpha=0 doublesided=0',
            'scales': f'diffuse={sD} normal={sN} spec=0.5 gloss=40 alpha=0 doublesided=0',
            'iron': f'diffuse={iD} normal={iN} spec=0.45 gloss=24 alpha=0 doublesided=0'}
    return pw.finish(M, 'dragonslayer', mats, None, 'WeaponSword')


if __name__ == '__main__':
    pw.WORK.mkdir(parents=True, exist_ok=True)
    tool('addprn')
    build()
    if '--keep' not in sys.argv:
        shutil.rmtree(pw.WORK, ignore_errors=True)
