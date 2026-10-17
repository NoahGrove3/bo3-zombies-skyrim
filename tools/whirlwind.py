"""Bond II's Updraft shout: the whirlwind round you (src/Movement.cpp WhirlTick spins it, swells it and fades it).
    python tools/whirlwind.py <out.nif> <Textures dir>
(Noah: higher quality, transparent wind) unlit additive layers of air (tools/modelconv/fxwrite), all faint and soft so
they read as moving air rather than solid bands:
  WW1..WW3  three nested funnels, widening as they rise, their wisps wound in a spiral and streaming upward at their own
            pace (faded where you see them edge-on, so each is a veil, not a wall)
  WW4       eighteen fine threads of air spiralling up through them
  WW5       dust and air rushing outward over the ground at your feet
  WW6       a loose ring of air spilling off the top
Texture: Textures/CurseOfAlduin/Dragon/whirl.dds (soft wisps, tiling both ways)."""
import math, pathlib, subprocess, sys
import numpy as np
from PIL import Image
sys.path.insert(0, str(pathlib.Path(__file__).parent))

FXWRITE = '/home/claude/tools/fxwrite'
TEX = r'textures\CurseOfAlduin\Dragon\whirl.dds'
TEX_THREAD = r'textures\CurseOfAlduin\Dragon\wind.dds'


def texture(texdir):
    """soft wisps of air: long streaks along v (the direction they flow), broken and uneven, tiling in u and v"""
    W, H = 256, 512
    rng = np.random.default_rng(21)
    u = (np.arange(W) + 0.5) / W
    v = (np.arange(H) + 0.5) / H
    U, V = np.meshgrid(u, v)
    a = np.zeros((H, W))
    for k in range(70):
        c = rng.uniform()                      # where across
        wdt = rng.uniform(0.004, 0.03)
        fv = int(rng.integers(1, 4))           # its length: whole cycles along v (tiles)
        ph = rng.uniform()
        wob = rng.uniform(0.005, 0.03) * np.sin(2 * math.pi * (int(rng.integers(1, 4)) * V + rng.uniform()))
        d = (U - c - wob + 0.5) % 1.0 - 0.5
        across = np.exp(-(d / wdt) ** 2)
        along = np.clip(np.sin(2 * math.pi * (fv * V + ph)), 0, 1) ** rng.uniform(1.5, 4)
        a += across * along * rng.uniform(0.3, 1.0)
    # a faint haze under the wisps
    haze = np.zeros((H, W))
    for f in (2, 3, 5):
        haze += np.sin(2 * math.pi * (f * U + rng.uniform())) * np.sin(2 * math.pi * (f * V + rng.uniform()))
    a = a / np.percentile(a, 99.5) * 0.9 + 0.08 * (haze - haze.min()) / (haze.max() - haze.min())
    a = np.clip(a, 0, 1)
    img = np.zeros((H, W, 4), np.uint8)
    img[..., :3] = 255
    img[..., 3] = (a * 255).astype(np.uint8)
    from dragontex import dds
    dds(Image.fromarray(img, 'RGBA'), pathlib.Path(texdir) / 'CurseOfAlduin' / 'Dragon' / 'whirl.dds', 'DXT5')


def funnel(L, name, r0, r1, h0, h1, twist, alpha, vscroll, uscale, col):
    """a surface of revolution r0 -> r1 from h0 up to h1, its uvs wound round it (the wisps spiral), faded at both ends"""
    L.append(f'shape {name} tex={TEX} color={col} scale=0 blend=add falloff=1 vscroll={vscroll}')
    na, nh = 40, 14
    for j in range(nh + 1):
        t = j / nh
        h = h0 + (h1 - h0) * t
        r = r0 + (r1 - r0) * t ** 1.4
        fade = min(1.0, t / 0.18) * min(1.0, (1 - t) / 0.35)
        dr = (r1 - r0) * 1.4 * max(t, 1e-3) ** 0.4 / (h1 - h0)  # (the slope, for the normal)
        for i in range(na + 1):
            a = 2 * math.pi * i / na
            x, y = r * math.cos(a), r * math.sin(a)
            n = np.array([math.cos(a), math.sin(a), -dr]); n /= np.linalg.norm(n)
            # u round it, v up it - and v leans with the angle, so the streaks wind round
            uu = uscale * i / na
            vv = t * 1.6 + twist * i / na
            L.append(f'v {x:.2f} {y:.2f} {h:.2f} {uu:.4f} {vv:.4f} 1 1 1 {alpha * fade:.3f}')
            L.append(f'n {n[0]:.4f} {n[1]:.4f} {n[2]:.4f}')
    for j in range(nh):
        for i in range(na):
            a = j * (na + 1) + i
            b, c, d = a + 1, a + na + 1, a + na + 2
            L += [f't {a} {c} {b}', f't {b} {c} {d}']


def main():
    out, texdir = sys.argv[1], sys.argv[2]
    texture(texdir)
    L = [f'out {out}']
    # the funnels: inner (tight, quick), middle, outer (wide, slow, faintest)
    funnel(L, 'WW1', 24, 62, -10, 300, 1.0, 0.42, 0.55, 3, '0.85,0.93,1.0,1.0')
    funnel(L, 'WW2', 36, 92, -10, 350, 1.4, 0.32, 0.8, 4, '0.88,0.95,1.0,1.0')
    funnel(L, 'WW3', 52, 125, -5, 400, 1.9, 0.22, 1.15, 5, '0.92,0.96,1.0,1.0')
    # the threads: thin ribbons spiralling up, each its own radius and pitch
    L.append(f'shape WW4 tex={TEX_THREAD} color=0.92,0.96,1.0,1.0 scale=0 blend=add vscroll=0.5')
    rng = np.random.default_rng(4)
    base = 0
    tris = []
    for k in range(18):
        n = 36
        a0 = rng.uniform(0, 2 * math.pi)
        turns = rng.uniform(1.0, 2.0)
        rr = rng.uniform(0.7, 1.3)
        top = rng.uniform(260, 410)
        wdt = rng.uniform(3, 8)
        for i in range(n + 1):
            t = i / n
            h = -5 + top * t
            r = (26 + 0.2 * h) * rr
            a = a0 + turns * 2 * math.pi * t
            al = min(1.0, t / 0.12) * min(1.0, (1 - t) / 0.3) * 0.55
            p = np.array([r * math.cos(a), r * math.sin(a), h])
            tang = np.array([-math.sin(a) * r * turns * 2 * math.pi, math.cos(a) * r * turns * 2 * math.pi, top]); tang /= np.linalg.norm(tang)
            side = np.cross(tang, p / (np.linalg.norm(p) + 1e-6)); side /= np.linalg.norm(side) + 1e-9
            for s in (-1, 1):
                q = p + side * s * wdt / 2
                L.append(f'v {q[0]:.2f} {q[1]:.2f} {q[2]:.2f} {0.5 + 0.5 * s:.3f} {t * 2.5 + k * 0.37:.3f} 1 1 1 {al:.3f}')
        for i in range(n):
            q = base + 2 * i
            tris += [(q, q + 1, q + 2), (q + 1, q + 3, q + 2)]
        base += 2 * (n + 1)
    L += ['t %d %d %d' % t for t in tris]
    # dust and air rushing out over the ground: a disc, its wisps streaming outward (v runs outward)
    L.append(f'shape WW5 tex={TEX} color=0.9,0.92,0.95,1.0 scale=0 blend=add uscroll=0 vscroll=0.7')
    na = 48
    rings = [(18, 0.0), (50, 0.35), (100, 0.25), (165, 0.0)]
    for j, (r, al) in enumerate(rings):
        for i in range(na + 1):
            a = 2 * math.pi * i / na
            L.append(f'v {r * math.cos(a):.2f} {r * math.sin(a):.2f} {3 + j * 2:.2f} {6 * i / na:.4f} {-r / 90:.4f} 1 1 1 {al:.3f}')
    for j in range(len(rings) - 1):
        for i in range(na):
            a = j * (na + 1) + i
            b, c, d = a + 1, a + na + 1, a + na + 2
            L += [f't {a} {c} {b}', f't {b} {c} {d}']
    # air spilling off the top
    funnel(L, 'WW6', 110, 175, 350, 440, 2.2, 0.16, 1.4, 5, '0.92,0.96,1.0,1.0')
    job = pathlib.Path(out).with_suffix('.job.txt')
    job.write_text('\n'.join(L) + '\n')
    r = subprocess.run([FXWRITE, str(job)], capture_output=True, text=True)
    print(r.stdout, r.stderr)
    job.unlink()
    if r.returncode:
        raise SystemExit('fxwrite failed')


if __name__ == '__main__':
    main()
