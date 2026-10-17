"""The dragon's breath (src/Movement.cpp BeamPose): one beam of fire, its own nif hung on your 3D while it breathes and
placed each frame at the dragon's jaws, turned to where it breathes (as the whirlwind is: shapes moved under the dragon's
own animated nodes were never drawn). Three cones of flame along +Y from the origin, in the dragon's units (the game scales
them with the dragon): BB1 the white-hot core, BB2 the flame round it (alpha-blended, so it shows against the sky), BB3 the
flare at the jaws. Texture: Textures/CurseOfAlduin/Dragon/beam.dds (tools/dragonfx.py).
    python tools/breathbeam.py <out dir> [<Textures dir>]   (breath_beam.nif, _b1, _b2, _b3: each bond's colours baked in)"""
import math, pathlib, subprocess, sys
FXWRITE = '/home/claude/tools/fxwrite'
BEAM = r'textures\CurseOfAlduin\Dragon\beam2.dds'


def cone(L, name, length, r0, r1, a0, a1, tiles, color, vscroll, sides=14, segs=26, fade_out=0.2, blend='add'):
    L.append(f'shape {name} tex={BEAM} color={color},1 scale=0 blend={blend} vscroll={vscroll}')
    for i in range(segs + 1):
        t = i / segs
        y = 10 + (length - 10) * t
        r = r0 + (r1 - r0) * t ** 0.8
        al = (a0 + (a1 - a0) * t) * (min(1.0, t / 0.03) if t > 0 else 0.0) * min(1.0, (1 - t) / fade_out)
        for k in range(sides + 1):
            ang = 2 * math.pi * k / sides
            L.append(f'v {r * math.cos(ang):.2f} {y:.2f} {r * math.sin(ang):.2f} {2.0 * k / sides:.4f} {t * tiles:.3f} 1 1 1 {al:.3f}')
            L.append(f'n {math.cos(ang):.4f} 0 {math.sin(ang):.4f}')
    for i in range(segs):
        for k in range(sides):
            a = i * (sides + 1) + k
            b, c, d = a + 1, a + sides + 1, a + sides + 2
            L += [f't {a} {c} {b}', f't {b} {c} {d}']


# each bond's colours baked in (core, flame): Skyrim's fire, then (Noah) Bond I blue, Bond II yellow, Bond III green
COLORS = {'': ('1.0,0.80,0.40', '1.0,0.40,0.07'), '_b1': ('0.60,0.82,1.0', '0.12,0.38,1.0'), '_b2': ('1.0,0.94,0.55', '1.0,0.72,0.04'),
          '_b3': ('0.65,1.0,0.55', '0.12,0.85,0.14')}


def texture(path):
    """dense flame streaming along v (tiles in v, wraps round u): mostly opaque, brighter tongues through it, white (tinted
    by each nif's colour)"""
    import numpy as np
    from PIL import Image
    sys.path.insert(0, str(pathlib.Path(__file__).parent))
    from dragontex import dds
    W, H = 128, 512
    U, V = np.meshgrid((np.arange(W) + 0.5) / W, (np.arange(H) + 0.5) / H)
    rng = np.random.default_rng(3)
    n = np.zeros((H, W))
    for f, amp in ((2, 1.0), (3, 0.8), (5, 0.6), (8, 0.45), (13, 0.3), (21, 0.2)):
        for _ in range(2):
            n += amp * np.sin(2 * math.pi * (f * V + int(rng.integers(1, 4)) * U + rng.uniform())) * np.sin(2 * math.pi * (int(rng.integers(1, 5)) * U + 0.3 * f * V + rng.uniform()))
    n = (n - n.min()) / (n.max() - n.min())
    a = np.clip(0.55 + 0.6 * (n - 0.4), 0.25, 1.0)
    img = np.zeros((H, W, 4), np.uint8)
    img[..., :3] = (200 + 55 * n[..., None]).astype(np.uint8)
    img[..., 3] = (a * 255).astype(np.uint8)
    pathlib.Path(path).parent.mkdir(parents=True, exist_ok=True)
    dds(Image.fromarray(img, 'RGBA'), path, 'DXT5')


def main():
    d = pathlib.Path(sys.argv[1])
    if len(sys.argv) > 2:
        texture(pathlib.Path(sys.argv[2]) / 'CurseOfAlduin' / 'Dragon' / 'beam2.dds')
    for suf, (core, flame) in COLORS.items():
        build(str(d / f'breath_beam{suf}.nif'), core, flame)


def build(out, core, flame):
    L = [f'out {out}']
    # (bold: thin and faint, it vanished against snow and sky)
    cone(L, 'BB1', 7000, 45, 300, 1.0, 0.8, 5.8, core, 0.25)
    cone(L, 'BB2', 7000, 120, 750, 0.95, 0.5, 4.2, flame, 0.33, blend='alpha')
    cone(L, 'BB3', 1100, 110, 420, 1.0, 0.0, 1.0, flame, 0.2, segs=8, fade_out=0.6)
    job = pathlib.Path(out).with_suffix('.job.txt')
    job.write_text('\n'.join(L) + '\n')
    r = subprocess.run([FXWRITE, str(job)], capture_output=True, text=True)
    print(r.stdout, r.stderr)
    job.unlink()
    if r.returncode:
        raise SystemExit('fxwrite failed')


if __name__ == '__main__':
    main()
