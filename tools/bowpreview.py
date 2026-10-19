"""bowpreview.py <out.png> <bow id> [...]: a textured look at our bow NIFs (nifexport), side-on and from 3/4, with their glow
and the flame wings as a translucent wash in their colour - for checking a new bow's fit on the rig before the game."""
import math, pathlib, subprocess, sys, tempfile
import numpy as np
from PIL import Image, ImageDraw

ROOT = pathlib.Path(__file__).parent.parent
TEX = ROOT / 'assets' / 'fx' / 'Textures'


def tex(path):
    if not path: return None
    p = path.lower().replace('\\', '/')
    if p.startswith('textures/'): p = p[9:]
    for c in TEX.rglob('*.dds'):
        if str(c.relative_to(TEX)).lower() == p:
            return np.asarray(Image.open(c).convert('RGB').resize((512, 512)), np.float32) / 255
    return None


def shapes(nif):
    d = tempfile.mkdtemp()
    subprocess.run(['/home/claude/tools/nifexport', nif, d], check=True, capture_output=True)
    out = []
    for f in sorted(pathlib.Path(d).glob('shape_*.txt')):
        meta, V, F = {}, [], []
        for l in open(f):
            if l.startswith('v '): V.append([float(x) for x in l.split()[1:6]])
            elif l.startswith('f '): F.append([int(x) for x in l.split()[1:4]])
            else:
                k, _, v = l.rstrip('\n').partition('\t'); meta.setdefault(k, v)
        if V: out.append((meta, np.array(V), np.array(F)))
    return out


def render(sh, yaw, W=520, H=700, glowcol=(1, 1, 1)):
    zb = np.full((H, W), -1e9); img = np.zeros((H, W, 3), np.float32); add = np.zeros((H, W, 3), np.float32)
    c, s = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
    sc = H / 150.0
    rng = np.random.default_rng(1)
    L = np.array([0.4, 0.5, 0.75]); L /= np.linalg.norm(L)
    for meta, V, F in sh:
        P = V[:, :3].copy(); x, z = P[:, 0] * c + P[:, 2] * s, -P[:, 0] * s + P[:, 2] * c
        P = np.stack([x, P[:, 1], z], 1)
        T = P[F]; UV = V[F][:, :, 3:5]
        nrm = np.cross(T[:, 1] - T[:, 0], T[:, 2] - T[:, 0]); nrm /= np.linalg.norm(nrm, axis=1, keepdims=True) + 1e-12
        flame = meta.get('tex2', '') == '' and 'flame' in meta.get('name', '')
        d = tex(meta.get('tex0')); g = tex(meta.get('tex2'))
        e1 = T[:, 1, :2] - T[:, 0, :2]; e2 = T[:, 2, :2] - T[:, 0, :2]
        area = np.abs(e1[:, 0] * e2[:, 1] - e1[:, 1] * e2[:, 0]) * sc * sc / 2
        n = np.clip(area * 3, 4, 3000).astype(int)
        rep = np.repeat(np.arange(len(F)), n)
        r = rng.random((len(rep), 2)); m = r.sum(1) > 1; r[m] = 1 - r[m]
        Tp = T[rep]; Up = UV[rep]
        pts = Tp[:, 0] + r[:, :1] * (Tp[:, 1] - Tp[:, 0]) + r[:, 1:] * (Tp[:, 2] - Tp[:, 0])
        uv = Up[:, 0] + r[:, :1] * (Up[:, 1] - Up[:, 0]) + r[:, 1:] * (Up[:, 2] - Up[:, 0])
        px = (W / 2 + pts[:, 0] * sc).astype(int); py = (H / 2 - pts[:, 1] * sc).astype(int)
        ok = (px >= 0) & (px < W) & (py >= 0) & (py < H)
        px, py, pz, uv, rep = px[ok], py[ok], pts[ok, 2], uv[ok], rep[ok]
        if 'flame' in meta.get('name', ''):
            np.add.at(add, (py, px), np.array(glowcol) * 0.02)
            continue
        ui = (np.mod(uv[:, 0], 1) * 511).astype(int); vi = (np.mod(uv[:, 1], 1) * 511).astype(int)
        col = d[vi, ui] if d is not None else np.full((len(ui), 3), 0.6)
        sh_ = 0.3 + 0.7 * np.abs(nrm[rep] @ L)
        col = col * sh_[:, None]
        if g is not None: col = col + g[vi, ui] * 0.8
        o = np.argsort(pz); px, py, pz, col = px[o], py[o], pz[o], col[o]
        upd = pz > zb[py, px]
        zb[py[upd], px[upd]] = pz[upd]; img[py[upd], px[upd]] = col[upd]
    bg = np.array([0.09, 0.09, 0.11])
    img[zb < -1e8] = bg
    return np.clip(img + np.clip(add, 0, 0.8), 0, 1)


if __name__ == '__main__':
    import json
    rows = {r['id']: r for r in json.load(open(ROOT / 'sheets' / 'bows.json'))['rows']}
    ids = sys.argv[2:]
    tiles = []
    for b in ids:
        sh = shapes(str(ROOT / 'assets/fx/Meshes/CurseOfAlduin/bow' / f'{b}.nif'))
        for yaw in (0, 60, 90):
            tiles.append((b, yaw, render(sh, yaw, glowcol=rows[b]['glow'])))
    W, H = 520, 700
    out = Image.new('RGB', (W * 3, H * len(ids)))
    for i, (b, yaw, im) in enumerate(tiles):
        out.paste(Image.fromarray((im * 255).astype(np.uint8)), ((i % 3) * W, (i // 3) * H))
        ImageDraw.Draw(out).text(((i % 3) * W + 8, (i // 3) * H + 8), f'{b} yaw {yaw}', fill=(255, 255, 255))
    # the rig's string ends and grip, for reference
    dr = ImageDraw.Draw(out)
    for r in range(len(ids)):
        for (x, y) in ((-13.69, 54.8), (-13.69, -54.8), (1.31, 0)):
            cx, cy = W / 2 + x * H / 150, r * H + H / 2 - y * H / 150
            dr.ellipse([cx - 4, cy - 4, cx + 4, cy + 4], outline=(255, 40, 40))
    out.save(sys.argv[1])
