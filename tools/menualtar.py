#!/usr/bin/env python3
"""The main menu's altar, redrawn as the new Echo Altar (tools/echoaltar.py): the game's own models rendered into the
menu painting's projection (tools/menushrine.py: x = CX + X*S, y = CY - (Z*cos(E) - Y*sin(E))*S, Y away from you),
shrunk by M so its footprint matches the old rune floor, the platform's top at Z = 0.
    python3 tools/menualtar.py assets/ui
Writes (replacing menushrine.py's): menu_shrine_ground.png (platform and columns), menu_shrine_glow.png (their rune
light alone), menu_crystal.png (the heart stone turning: 24 frames, 6 x 4 of 320x480, its centre at 56% of a frame's
height), menu_shard0..5.png and _r.png (a rune stone over each column, and its runes' light), sized for src/Hud.cpp."""
import math, pathlib, subprocess, sys
import numpy as np
from PIL import Image, ImageFilter

ROOT = pathlib.Path(__file__).parent.parent
OUT = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / 'assets' / 'ui')
MESH = ROOT / 'assets' / 'fx' / 'Meshes' / 'CurseOfAlduin' / 'shrine'
TEXR = ROOT / 'assets' / 'fx'
WORK = ROOT / 'build' / 'menualtar'
W, H = 2560, 1440
CX, CY, S = 1160.0, 1190.0, 3.4
E = math.radians(17)
M = 0.76                      # model units -> menu units
FLOOR = 36.0 + 63 * 0.6       # the platform's top in the model
STONE_Z = 175.0
LIGHT = np.array([-0.45, -0.55, 0.70]); LIGHT /= np.linalg.norm(LIGHT)   # moonlight from front-left, above
TOPS = [(194, 0, 204.6), (96.5, -168, 231.6), (-96.6, -167.5, 282.0), (-193.4, 0, 231.6), (-96.6, 167.5, 282.0), (97.6, 168, 282.0)]


def export(nif):
    d = WORK / nif.stem
    d.mkdir(parents=True, exist_ok=True)
    subprocess.run(['/home/claude/tools/nifexport', str(nif), str(d)], capture_output=True)
    shapes = []
    for f in sorted(d.glob('shape_*.txt')):
        V, F, tex = [], [], {}
        for l in open(f):
            p = l.split()
            if not p:
                continue
            if p[0] in ('tex0', 'tex2') and len(p) > 1:
                tex[p[0]] = p[1]
            elif p[0] == 'v':
                V.append([float(x) for x in p[1:6]])
            elif p[0] == 'f':
                F.append([int(x) for x in p[1:4]])  # (nifexport counts from 0)
        if V and F:
            shapes.append((np.array(V), np.array(F), tex))
    return shapes


_tex = {}
def texture(rel):
    if not rel:
        return None
    if rel not in _tex:
        p = TEXR / rel.replace('\\', '/').replace('textures/', 'Textures/', 1)
        _tex[rel] = np.asarray(Image.open(p).convert('RGB'), np.float32) / 255 if p.exists() else None
    return _tex[rel]


def normals(V, F):
    N = np.zeros((len(V), 3))
    P = V[:, :3]
    fn = np.cross(P[F[:, 1]] - P[F[:, 0]], P[F[:, 2]] - P[F[:, 0]])
    for k in range(3):
        np.add.at(N, F[:, k], fn)
    return N / (np.linalg.norm(N, axis=1, keepdims=True) + 1e-9)


class Canvas:
    def __init__(self, w, h):
        self.w, self.h = w, h
        self.z = np.full((h, w), 1e9, np.float32)
        self.col = np.zeros((h, w, 3), np.float32)
        self.glow = np.zeros((h, w, 3), np.float32)
        self.a = np.zeros((h, w), np.float32)

    def draw(self, shapes, xf, emit=(0.4, 0.8, 1.0), gain=1.0, glow_gain=1.6):
        """xf(P) -> (screen x, screen y, depth) for model points P (n x 3), and the world-space normal turn"""
        for V, F, tex in shapes:
            dif, glo = texture(tex.get('tex0')), texture(tex.get('tex2'))
            Nn = normals(V, F)
            sx, sy, dz, Nw = xf(V[:, :3], Nn)
            UV = V[:, 3:5]
            for f in F:
                xs, ys = sx[f], sy[f]
                x0, x1 = int(max(0, np.floor(xs.min()))), int(min(self.w - 1, np.ceil(xs.max())))
                y0, y1 = int(max(0, np.floor(ys.min()))), int(min(self.h - 1, np.ceil(ys.max())))
                if x1 < x0 or y1 < y0:
                    continue
                yy, xx = np.mgrid[y0:y1 + 1, x0:x1 + 1].astype(np.float32) + 0.5
                (ax, bx, cx), (ay, by, cy) = xs, ys
                den = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy)
                if abs(den) < 1e-6:
                    continue
                l0 = ((by - cy) * (xx - cx) + (cx - bx) * (yy - cy)) / den
                l1 = ((cy - ay) * (xx - cx) + (ax - cx) * (yy - cy)) / den
                l2 = 1 - l0 - l1
                ins = (l0 >= -0.001) & (l1 >= -0.001) & (l2 >= -0.001)
                if not ins.any():
                    continue
                z = l0 * dz[f[0]] + l1 * dz[f[1]] + l2 * dz[f[2]]
                zb = self.z[y0:y1 + 1, x0:x1 + 1]
                win = ins & (z < zb)
                if not win.any():
                    continue
                u = l0 * UV[f[0], 0] + l1 * UV[f[1], 0] + l2 * UV[f[2], 0]
                v = l0 * UV[f[0], 1] + l1 * UV[f[1], 1] + l2 * UV[f[2], 1]
                n = l0[..., None] * Nw[f[0]] + l1[..., None] * Nw[f[1]] + l2[..., None] * Nw[f[2]]
                n /= np.linalg.norm(n, axis=-1, keepdims=True) + 1e-9
                lam = np.clip((n * LIGHT).sum(-1), 0, 1)
                shade = 0.22 + 0.85 * lam + 0.15 * np.clip(n[..., 2], 0, 1)
                if dif is not None:
                    th, tw = dif.shape[:2]
                    c = dif[np.clip((v % 1) * th, 0, th - 1).astype(int), np.clip((u % 1) * tw, 0, tw - 1).astype(int)]
                else:
                    c = np.full(u.shape + (3,), 0.5)
                g = np.zeros_like(c)
                if glo is not None:
                    gh, gw = glo.shape[:2]
                    g = glo[np.clip((v % 1) * gh, 0, gh - 1).astype(int), np.clip((u % 1) * gw, 0, gw - 1).astype(int)] * np.array(emit) * glow_gain
                lit = c * shade[..., None] * np.array([0.78, 0.84, 1.0]) * gain
                zb[win] = z[win]
                self.col[y0:y1 + 1, x0:x1 + 1][win] = lit[win]
                self.glow[y0:y1 + 1, x0:x1 + 1][win] = g[win]
                self.a[y0:y1 + 1, x0:x1 + 1][win] = 1.0

    def image(self, with_glow=True, ss=1):
        rgb = self.col + (self.glow if with_glow else 0)
        im = Image.fromarray(np.dstack([np.clip(rgb, 0, 1) * 255, self.a * 255]).astype(np.uint8), 'RGBA')
        return im.resize((self.w // ss, self.h // ss), Image.LANCZOS) if ss > 1 else im

    def glow_image(self, ss=1):
        g = np.clip(self.glow, 0, 1)
        a = np.clip(g.max(-1), 0, 1)
        im = Image.fromarray(np.dstack([np.clip(g / np.maximum(a[..., None], 1e-3), 0, 1) * 255, a * 255]).astype(np.uint8), 'RGBA')
        im = im.resize((self.w // ss, self.h // ss), Image.LANCZOS) if ss > 1 else im
        return im


def painting_xf(ss, yaw=0.0, at=(0, 0, 0), menu_shift=(0, 0, 0)):
    ce, se = math.cos(E), math.sin(E)
    cy_, sy_ = math.cos(yaw), math.sin(yaw)

    def xf(P, N):
        X = (P[:, 0] * cy_ - P[:, 1] * sy_ + at[0])
        Y = (P[:, 0] * sy_ + P[:, 1] * cy_ + at[1])
        Z = P[:, 2] + at[2]
        Xm, Ym, Zm = X * M + menu_shift[0], Y * M + menu_shift[1], (Z - FLOOR) * M + menu_shift[2]
        sx = (CX + Xm * S) * ss
        sy = (CY - (Zm * ce - Ym * se) * S) * ss
        depth = -Ym * ce - Zm * se  # (nearer is smaller: in this projection +Y comes toward you, lower on the painting)
        Nw = np.stack([N[:, 0] * cy_ - N[:, 1] * sy_, N[:, 0] * sy_ + N[:, 1] * cy_, N[:, 2]], 1)
        return sx, sy, depth, Nw
    return xf


def frame_xf(scale, cx, cy, yaw):
    """a sprite frame: the model's own centre at (cx, cy), scale frame px per model unit, seen down at E"""
    ce, se = math.cos(E), math.sin(E)
    c, s = math.cos(yaw), math.sin(yaw)

    def xf(P, N):
        X = P[:, 0] * c - P[:, 1] * s
        Y = P[:, 0] * s + P[:, 1] * c
        Z = P[:, 2]
        Nw = np.stack([N[:, 0] * c - N[:, 1] * s, N[:, 0] * s + N[:, 1] * c, N[:, 2]], 1)
        return cx + X * scale, cy - (Z * ce - Y * se) * scale, -Y * ce - Z * se, Nw
    return xf


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    altar = export(MESH / 'shrine.nif')
    stone = export(MESH / 'heartstone.nif')
    rock = export(MESH / 'pillarrock.nif')
    ss = 2
    cv = Canvas(W * ss, H * ss)
    cv.draw(altar, painting_xf(ss), emit=(0.45, 0.8, 1.0), glow_gain=1.3)
    cv.image(with_glow=False, ss=ss).save(OUT / 'menu_shrine_ground.png', optimize=True)
    cv.glow_image(ss=ss).save(OUT / 'menu_shrine_glow.png', optimize=True)
    print('ground and glow written')

    # the heart stone turning: frame px per model unit so a 480-high frame shows 640 painting px (src/Hud.cpp)
    fw, fh = 320, 480
    k = S * M / (640.0 / fh)
    sheet = Image.new('RGBA', (fw * 6, fh * 4), (0, 0, 0, 0))
    for fr in range(24):
        c = Canvas(fw * 2, fh * 2)
        c.draw(stone, frame_xf(k * 2, fw, fh * 2 * 0.56, fr / 24 * 2 * math.pi), emit=(0.35, 0.75, 1.0), glow_gain=1.8)
        sheet.paste(c.image(ss=2), ((fr % 6) * fw, (fr // 6) * fh))
    sheet.save(OUT / 'menu_crystal.png', optimize=True)
    print('stone sheet written')

    # the rocks over the columns: one sprite each (turned a little differently), 256 px for 200 painting px
    n = 256
    k2 = S * M * (n / 200.0)
    for i in range(9):
        c = Canvas(n * 2, n * 2)
        c.draw(rock, frame_xf(k2 * 2, n, n, i * 1.1), emit=(0.35, 0.75, 1.0), glow_gain=1.6)
        c.image(ss=2).save(OUT / f'menu_shard{i}.png', optimize=True)
        c.glow_image(ss=2).save(OUT / f'menu_shard{i}_r.png', optimize=True)
    # (Noah: the menu's rocks should turn like the altar's) one rock turning: 16 frames, 4 x 4 of 256 px (and its runes'
    # light), each rock in src/Hud.cpp at its own phase, cross-faded between frames
    turn = Image.new('RGBA', (n * 4, n * 4), (0, 0, 0, 0)); turnr = Image.new('RGBA', (n * 4, n * 4), (0, 0, 0, 0))
    for fr in range(16):
        c = Canvas(n * 2, n * 2)
        c.draw(rock, frame_xf(k2 * 2, n, n, fr / 16 * 2 * math.pi), emit=(0.35, 0.75, 1.0), glow_gain=1.6)
        turn.paste(c.image(ss=2), ((fr % 4) * n, (fr // 4) * n)); turnr.paste(c.glow_image(ss=2), ((fr % 4) * n, (fr // 4) * n))
    turn.save(OUT / 'menu_rock_turn.png', optimize=True); turnr.save(OUT / 'menu_rock_turn_r.png', optimize=True)
    print('rocks written')
    for x, y, z in TOPS:
        print(f'rock at menu ({x * M:.1f}, {y * M:.1f}, {(z - FLOOR) * M:.1f})')
    print(f'stone centre menu Z {(STONE_Z - FLOOR) * M:.1f}')
    # a still to look at
    comp = Image.new('RGBA', (W, H), (14, 16, 30, 255))
    comp.alpha_composite(Image.open(OUT / 'menu_shrine_ground.png'))
    st = Image.open(OUT / 'menu_crystal.png').crop((0, 0, fw, fh)).resize((427, 640))
    zc = (STONE_Z - FLOOR) * M
    comp.alpha_composite(st, (int(CX - 213), int(CY - zc * math.cos(E) * S - 640 * 0.56)))
    for i, (x, y, z) in enumerate(TOPS):
        im = Image.open(OUT / f'menu_shard{i}.png').resize((200, 200))
        X, Y, Z = x * M, y * M, (z - FLOOR) * M
        px, py = CX + X * S, CY - (Z * math.cos(E) - Y * math.sin(E)) * S
        comp.alpha_composite(im, (int(px - 100), int(py - 100)))
    comp.convert('RGB').resize((1280, 720)).save(WORK / 'menu_preview.jpg', quality=88)


if __name__ == '__main__':
    main()
