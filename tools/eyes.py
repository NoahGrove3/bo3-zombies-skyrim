"""Glowing eyes for the undead: two small additive glows skinned to each model's head bone, found from the model
itself (the vertices that follow the head: the front of the face at eye height), written as a NIF per creature that
gen_esp.py adds to the creature's skin as a second armor addon (slot FX01). So the eyes turn, nod and fall with the head.

    python tools/eyes.py <out Data dir> [preview.png]

Needs the built creature NIFs (paths below) and the nifly tools (vweights, nifnodes, rigwrite)."""
import pathlib, re, subprocess, sys
import numpy as np
from PIL import Image

OUT = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else '/home/claude/work/out5/Data')
T = '/home/claude/tools/'
W = pathlib.Path('/home/claude/work')
UP = pathlib.Path('/mnt/user-data/uploads/common--Skyrim Special Edition/Data/Meshes/CurseOfAlduin')
ICE, EMBER, VIOLET = (0.62, 0.85, 1.0), (1.0, 0.48, 0.16), (0.78, 0.5, 1.0)
RED = (1.0, 0.10, 0.06)
MODELS = {  # creature id: (nif, glow colour, size)
    'zombie': (W / 'creatures/Data/Meshes/CurseOfAlduin/zombie/zombie.nif', ICE, 1.0),   # (Mixamo's zombie now: tools/rigs.py zombie)
    'runner': (W / 'creatures/Data/Meshes/CurseOfAlduin/hooded/hooded.nif', ICE, 0.9),
    'wailer': (W / 'creatures/Data/Meshes/CurseOfAlduin/hag/hag.nif', ICE, 0.9),
    'bloater': (W / 'out3/Data/Meshes/CurseOfAlduin/abom/abom.nif', ICE, 1.2),
    'shadow': (UP / 'shadow/shadow.nif', ICE, 1.0),
    'reaper': (W / 'out3/Data/Meshes/CurseOfAlduin/reaper/reaper.nif', EMBER, 1.1),
    'necro': (W / 'out3/Data/Meshes/CurseOfAlduin/necro/necro.nif', VIOLET, 1.0),
    'knight': (W / 'out3/Data/Meshes/CurseOfAlduin/knight/knight.nif', EMBER, 1.1),
    'king': (W / 'out3/Data/Meshes/CurseOfAlduin/king/king.nif', EMBER, 1.1),
    'lich': (W / 'out3/Data/Meshes/CurseOfAlduin/lich/lich.nif', VIOLET, 1.0),
    'boss': (UP / 'boss/boss.nif', EMBER, 1.8),
}
HEAD = 'NPC Head [Head]'


def head_bone(nif):
    lines = subprocess.run([T + 'nifnodes', str(nif)], capture_output=True, text=True).stdout.split('\n')
    for i, l in enumerate(lines):
        if HEAD in l and 'world t(' in l:
            t = [float(x) for x in re.search(r'world t\(([-\d. ]+)\)', l).group(1).split()]
            r = [float(x) for x in re.findall(r'-?\d+\.\d+', lines[i + 1])]
            return np.array(t), np.array(r).reshape(3, 3)
    raise SystemExit(f'{nif}: no head bone')


def head_verts(nif):
    out = []
    for l in subprocess.run([T + 'vweights', str(nif)], capture_output=True, text=True).stdout.split('\n'):
        m = re.match(r'\s*([-\d.]+)\s+([-\d.]+)\s+([-\d.]+) :(.*)', l)
        if not m:
            continue
        w = re.search(re.escape(HEAD) + r'=([\d.]+)', m.group(4))
        if w and float(w.group(1)) >= 0.45:
            out.append([float(m.group(1)), float(m.group(2)), float(m.group(3))])
    return np.array(out)


def find_eyes(nif):
    ht, hr = head_bone(nif)
    V = head_verts(nif)
    if len(V) < 10:
        raise SystemExit(f'{nif}: too few head vertices')
    core = V[np.abs(V[:, 0] - ht[0]) < 7]                      # the face, not horns or hood wings
    core = core if len(core) > 8 else V
    top = np.percentile(core[:, 2], 97)
    ez = ht[2] + 0.42 * max(4.0, top - ht[2])                   # eyes a little under half way up the skull
    band = core[np.abs(core[:, 2] - ez) < 3.0]
    band = band if len(band) > 4 else core
    fy = np.percentile(band[:, 1], 92)                          # the front of the face at that height (models face +Y)
    half = np.percentile(np.abs(V[np.abs(V[:, 2] - ez) < 3.0][:, 0] - ht[0]) if (np.abs(V[:, 2] - ez) < 3.0).sum() > 4 else [4.0], 75)
    ex = float(np.clip(0.36 * half, 1.6, 3.6))
    if 'boss' in str(nif):  # the wyrm's long snout: its eyes sit back on the skull, a little higher
        fy -= 9.0; ez += 2.0; ex = max(ex, 3.2)
    return ht, hr, [(ht[0] - ex, fy + 0.5, ez), (ht[0] + ex, fy + 0.5, ez)]


def glow_tex(color, name):
    n = 64
    yy, xx = np.mgrid[0:n, 0:n].astype(np.float32)
    r = np.sqrt((xx - n / 2 + 0.5) ** 2 + (yy - n / 2 + 0.5) ** 2) / (n / 2)
    core = np.clip(1 - r * 3.6, 0, 1) * 0.8; halo = np.clip(1 - r, 0, 1) ** 1.8 * 1.3
    rgb = np.clip(np.stack([core + halo * c for c in color], -1), 0, 1)
    p = OUT / 'Textures' / 'CurseOfAlduin' / 'eyes' / f'{name}.dds'
    p.parent.mkdir(parents=True, exist_ok=True)
    Image.fromarray((rgb * 255).astype(np.uint8), 'RGB').save(p, pixel_format='DXT1')
    return f'CurseOfAlduin\\eyes\\{name}.dds'


def write(cid, nif, color, size):
    ht, hr, eyes = find_eyes(nif)
    tname = {ICE: 'glow_ice', EMBER: 'glow_ember', VIOLET: 'glow_violet'}[color]
    tex = glow_tex(color, tname)
    out = OUT / 'Meshes' / 'CurseOfAlduin' / 'eyes' / f'{cid}_eyes.nif'
    out.parent.mkdir(parents=True, exist_ok=True)
    rot = ' '.join(f'{x:.5f}' for x in hr.flatten())
    lines = [f'out {out}', 'bones 1', f'bone {HEAD}|{ht[0]:.5f} {ht[1]:.5f} {ht[2]:.5f} {rot}',
             f'shape {cid}_eyes diffuse={tex} normal={tex} glow={tex} emit={color[0]},{color[1]},{color[2]} emitmult=7.0 alpha=add spec=0 gloss=1']
    s = 1.25 * size; vi = 0; tris = []
    for (x, y, z) in eyes:
        for ax in range(3):  # three crossed quads: a point of light from any side
            u, v = [(0, 2), (1, 2), (0, 1)][ax]
            n = [0, 0, 0]; n[3 - u - v] = 1
            for (a, b, uu, vv) in ((-s, -s, 0, 1), (s, -s, 1, 1), (s, s, 1, 0), (-s, s, 0, 0)):
                p = [x, y, z]; p[u] += a; p[v] += b
                lines.append(f'v {p[0]:.4f} {p[1]:.4f} {p[2]:.4f} {uu} {vv} {n[0]} {n[1]} {n[2]} 0 1.0')
            tris += [(vi, vi + 1, vi + 2), (vi, vi + 2, vi + 3)]; vi += 4
    lines += [f't {a} {b} {c}' for a, b, c in tris]
    job = OUT.parent / f'eyes_{cid}.txt'; job.write_text('\n'.join(lines) + '\n')
    r = subprocess.run([T + 'rigwrite', str(job)], capture_output=True, text=True)
    print(cid, 'eyes at', [tuple(round(c, 1) for c in e) for e in eyes], '|', (r.stdout + r.stderr).strip()[-120:])
    return eyes


def write_glow(cid, nif, color, size, out_root):
    """the eyes as a lit-from-within glow the game hangs on the head bone at run time (src/Undead.cpp Eyes): an unlit,
    additive effect mesh in the head bone's own space (where the skin's head vertices are), a hot core in a soft halo,
    set a little out in front of the face so nothing hides it"""
    ht, hr, eyes = find_eyes(nif)
    import json
    sock = pathlib.Path(__file__).with_name('eyes_sockets.json')
    picked = json.loads(sock.read_text()).get(cid) if sock.exists() else None
    if picked:  # the sockets, picked on a front render of each model (x, surface y, z in model space)
        eyes = [(x, y - 0.7, z) for x, y, z in picked]
    color = RED   # Noah: red eyes, set in the sockets
    out = pathlib.Path(out_root) / 'Meshes' / 'CurseOfAlduin' / 'eyes' / f'{cid}_glow.nif'
    out.parent.mkdir(parents=True, exist_ok=True)
    R = hr.reshape(3, 3)
    lines = [f'out {out}']
    for name, tex, scale, half in (('halo', 'CurseOfAlduin\\fx\\soft.dds', 1.8, 1.9 * size), ('core', 'CurseOfAlduin\\fx\\core.dds', 3.2, 0.75 * size)):
        lines.append(f"shape {name} tex={tex} color={color[0]:.3f},{color[1]:.3f},{color[2]:.3f},1 scale={scale} blend=add falloff=0")
        vi = 0
        for (x, y, z) in eyes:
            e = np.array([x, y, z])                   # in the socket
            loc = R.T @ (e - ht)                      # into the head bone's space (as the skin binds it)
            for ax in range(3):
                u, v = [(0, 2), (1, 2), (0, 1)][ax]
                for (a, b, uu, vv) in ((-half, -half, 0, 1), (half, -half, 1, 1), (half, half, 1, 0), (-half, half, 0, 0)):
                    q = list(e); q[u] += a; q[v] += b
                    ql = R.T @ (np.array(q) - ht)
                    lines.append(f'v {ql[0]:.4f} {ql[1]:.4f} {ql[2]:.4f} {uu} {vv}')
                lines += [f't {vi} {vi + 1} {vi + 2}', f't {vi} {vi + 2} {vi + 3}']
                vi += 4
    job = pathlib.Path('/tmp/claude-0') / f'eyeglow_{cid}.txt'; job.write_text('\n'.join(lines) + '\n')
    r = subprocess.run([T + 'fxwrite', str(job)], capture_output=True, text=True)
    print(cid, 'glow', (r.stdout + r.stderr).strip()[-80:])


if __name__ == '__main__':
    if len(sys.argv) > 1 and sys.argv[1] == 'glow':
        root = sys.argv[2] if len(sys.argv) > 2 else str(pathlib.Path(__file__).resolve().parent.parent / 'assets' / 'fx')
        for cid, (nif, col, size) in MODELS.items():
            if pathlib.Path(nif).exists():
                write_glow(cid, nif, col, size, root)
            else:
                print(cid, 'model missing:', nif)
        sys.exit(0)
    for cid, (nif, col, size) in MODELS.items():
        if pathlib.Path(nif).exists():
            write(cid, nif, col, size)
        else:
            print(cid, 'model missing:', nif)
