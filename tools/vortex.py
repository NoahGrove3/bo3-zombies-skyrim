"""The Subsuming Vortex's black hole (the Ranger's wonder): a purple singularity where its arrow lands (src/Gun.cpp
BlackHole places it, spins it, swells it and takes it away when it bursts).
    python tools/vortex.py [<Data dir>]      (default: assets/fx - CI copies it into Data)
Unlit layers (tools/modelconv/fxwrite), all made here from simple shapes:
  VX1  the event horizon: a black ball (alpha-blended), the hole itself
  VX2  its rim: a thin violet glow round the ball, brightest at its edge
  VX3  the accretion disc: wisps of violet light spiralling in toward the hole, flat round it
  VX4  a second, tilted disc of magenta, faster - so it never looks flat
  VX5  the pull: two thin funnels of dark violet air above and below, drawn down into the hole
  VX6  a soft violet halo that always faces you
Textures (Textures/CurseOfAlduin/fx): vortex_wisps.dds (soft streaks, tiling), vortex_core.dds (black), vortex_halo.dds."""
import math, pathlib, subprocess, sys
import numpy as np
from PIL import Image
sys.path.insert(0, str(pathlib.Path(__file__).parent))
from dragontex import dds

ROOT = pathlib.Path(__file__).parent.parent
FXWRITE = '/home/claude/tools/fxwrite'
T_WISP = r'textures\CurseOfAlduin\fx\vortex_wisps.dds'
T_CORE = r'textures\CurseOfAlduin\fx\vortex_core.dds'
T_HALO = r'textures\CurseOfAlduin\fx\vortex_halo.dds'


def textures(data):
    out = pathlib.Path(data) / 'Textures' / 'CurseOfAlduin' / 'fx'
    out.mkdir(parents=True, exist_ok=True)
    # wisps: long soft streaks along v (the way they flow), uneven, tiling both ways
    W, H = 256, 512
    rng = np.random.default_rng(77)
    U, V = np.meshgrid((np.arange(W) + 0.5) / W, (np.arange(H) + 0.5) / H)
    a = np.zeros((H, W))
    for k in range(80):
        c, wdt = rng.uniform(), rng.uniform(0.004, 0.035)
        fv, ph = int(rng.integers(1, 4)), rng.uniform()
        wob = rng.uniform(0.005, 0.03) * np.sin(2 * math.pi * (int(rng.integers(1, 4)) * V + rng.uniform()))
        d = (U - c - wob + 0.5) % 1.0 - 0.5
        a += np.exp(-(d / wdt) ** 2) * np.clip(np.sin(2 * math.pi * (fv * V + ph)), 0, 1) ** rng.uniform(1.5, 4) * rng.uniform(0.3, 1.0)
    a = np.clip(a / np.percentile(a, 99.5) * 0.95, 0, 1)
    img = np.zeros((H, W, 4), np.uint8); img[..., :3] = 255; img[..., 3] = (a * 255).astype(np.uint8)
    dds(Image.fromarray(img, 'RGBA'), out / 'vortex_wisps.dds', 'DXT5')
    # the hole: black, opaque
    img = np.zeros((32, 32, 4), np.uint8); img[..., 3] = 255
    dds(Image.fromarray(img, 'RGBA'), out / 'vortex_core.dds', 'DXT5')
    # the halo: a soft ring of light with a dark middle
    n = 256
    yy, xx = np.mgrid[0:n, 0:n]; r = np.hypot(xx - n / 2 + 0.5, yy - n / 2 + 0.5) / (n / 2)
    ring = np.exp(-((r - 0.42) / 0.16) ** 2) + 0.35 * np.exp(-((r - 0.42) / 0.45) ** 2)
    ring *= np.clip((1 - r) / 0.15, 0, 1)
    img = np.zeros((n, n, 4), np.uint8); img[..., :3] = 255; img[..., 3] = (np.clip(ring / ring.max(), 0, 1) * 255).astype(np.uint8)
    dds(Image.fromarray(img, 'RGBA'), out / 'vortex_halo.dds', 'DXT5')


def ball(L, name, r, tex, col, blend, extra=''):
    L.append(f'shape {name} tex={tex} color={col} scale={1.0 if blend == 'alpha' else 1.6} blend={blend} {extra}'.strip())
    nu, nv = 24, 14
    for j in range(nv + 1):
        th = math.pi * j / nv
        for i in range(nu + 1):
            ph = 2 * math.pi * i / nu
            n = (math.sin(th) * math.cos(ph), math.sin(th) * math.sin(ph), math.cos(th))
            L.append(f'v {r * n[0]:.2f} {r * n[1]:.2f} {r * n[2]:.2f} {i / nu:.4f} {j / nv:.4f}')
            L.append(f'n {n[0]:.4f} {n[1]:.4f} {n[2]:.4f}')
    for j in range(nv):
        for i in range(nu):
            a = j * (nu + 1) + i
            b, c, d = a + 1, a + nu + 1, a + nu + 2
            L += [f't {a} {b} {c}', f't {b} {d} {c}']


def disc(L, name, r0, r1, tilt, twist, col, vscroll, alpha, uscale=4):
    """a flat ring round the hole, its wisps wound into a spiral (v runs inward, and leans with the angle)"""
    L.append(f'shape {name} tex={T_WISP} color={col} scale=1.6 blend=add vscroll={vscroll}')
    na, nr = 48, 8
    ct, st = math.cos(tilt), math.sin(tilt)
    for j in range(nr + 1):
        t = j / nr
        r = r0 + (r1 - r0) * t
        fade = min(1.0, t / 0.2) * min(1.0, (1 - t) / 0.45)
        for i in range(na + 1):
            a = 2 * math.pi * i / na
            x, y = r * math.cos(a), r * math.sin(a)
            y, z = y * ct, y * st
            L.append(f'v {x:.2f} {y:.2f} {z:.2f} {uscale * i / na:.4f} {-t * 1.2 + twist * i / na:.4f} 1 1 1 {alpha * fade:.3f}')
    for j in range(nr):
        for i in range(na):
            a = j * (na + 1) + i
            b, c, d = a + 1, a + na + 1, a + na + 2
            L += [f't {a} {c} {b}', f't {b} {c} {d}', f't {a} {b} {c}', f't {b} {d} {c}']   # (both faces)


def funnel(L, name, top, col, alpha, vscroll):
    """a thin funnel narrowing into the hole from above (top > 0) or below"""
    L.append(f'shape {name} tex={T_WISP} color={col} scale=1.4 blend=add falloff=1 vscroll={vscroll}')
    na, nh = 36, 10
    for j in range(nh + 1):
        t = j / nh
        h = top * t
        r = 30 + 110 * t ** 1.6
        fade = min(1.0, t / 0.15) * min(1.0, (1 - t) / 0.4)
        for i in range(na + 1):
            a = 2 * math.pi * i / na
            n = np.array([math.cos(a), math.sin(a), -0.6 * math.copysign(1, top)]); n /= np.linalg.norm(n)
            L.append(f'v {r * math.cos(a):.2f} {r * math.sin(a):.2f} {h:.2f} {3 * i / na:.4f} {t * 1.4 + 1.3 * i / na:.4f} 1 1 1 {alpha * fade:.3f}')
            L.append(f'n {n[0]:.4f} {n[1]:.4f} {n[2]:.4f}')
    for j in range(nh):
        for i in range(na):
            a = j * (na + 1) + i
            b, c, d = a + 1, a + na + 1, a + na + 2
            L += [f't {a} {c} {b}', f't {b} {c} {d}']


def main():
    data = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / 'assets' / 'fx')
    textures(data)
    out = data / 'Meshes' / 'CurseOfAlduin' / 'fx' / 'vortex.nif'
    out.parent.mkdir(parents=True, exist_ok=True)
    L = [f'out {out}']
    ball(L, 'VX1', 30, T_CORE, '0,0,0,1', 'alpha')
    ball(L, 'VX2', 36, T_WISP, '0.62,0.24,1.0,1.0', 'add', 'rim=1 uscroll=1.2')
    disc(L, 'VX3', 34, 170, 0.0, 1.6, '0.58,0.22,1.0,1.0', -0.9, 0.85)
    disc(L, 'VX4', 40, 130, 0.38, 2.2, '0.95,0.3,0.85,1.0', -1.5, 0.55, 3)
    funnel(L, 'VX5', 150, '0.45,0.16,0.85,1.0', 0.35, -1.1)
    funnel(L, 'VX6', -150, '0.45,0.16,0.85,1.0', 0.35, -1.1)
    L.append(f'shape VX7 tex={T_HALO} color=0.6,0.25,1.0,1.0 scale=1.2 blend=add billboard=2')
    s = 120
    L += [f'v {-s} {-s} 0 0 1', f'v {s} {-s} 0 1 1', f'v {s} {s} 0 1 0', f'v {-s} {s} 0 0 0', 't 0 1 2', 't 0 2 3']   # (in the billboard's own plane: XY)
    job = ROOT / 'build' / 'vortex.job.txt'
    job.parent.mkdir(exist_ok=True)
    job.write_text('\n'.join(L) + '\n')
    r = subprocess.run([FXWRITE, str(job)], capture_output=True, text=True)
    print(r.stdout.strip(), r.stderr.strip())
    if r.returncode:
        raise SystemExit('fxwrite failed')


if __name__ == '__main__':
    main()
