"""(Noah: "I want the entrance to the room we made in High Hrothgar for the Curse of Alduin to be a portal ... just outside the
back of High Hrothgar. The portal should bring us to a different room") the Reliquary's portal: an upright oval of the
Tongues' light, two swirls turning against each other (polar-mapped discs whose texture slides round - no spinning node),
a hot white heart, a soft halo and the fine motes rising off its foot. Stands where the sheet's echo.reliquary.portal puts
it; walk into it (src/Echo.cpp).

    python3 tools/portal.py  ->  assets/fx/Meshes/CurseOfAlduin/reliquary/portal.nif (+ Textures/CurseOfAlduin/reliquary/portal_*.dds)"""
import math, pathlib, subprocess
import numpy as np
from PIL import Image, ImageFilter

ROOT = pathlib.Path(__file__).resolve().parent.parent
MESH = ROOT / 'assets' / 'fx' / 'Meshes' / 'CurseOfAlduin' / 'reliquary'
TEX = ROOT / 'assets' / 'fx' / 'Textures' / 'CurseOfAlduin' / 'reliquary'
WORK = ROOT / 'build' / 'portal'
TEXREL = 'CurseOfAlduin\\reliquary\\'
W, H = 112.0, 170.0   # the oval's half-width and half-height (units); its foot on the ground


def dds(img, name, mips=8):
    TEX.mkdir(parents=True, exist_ok=True); WORK.mkdir(parents=True, exist_ok=True)
    png = WORK / (name + '.png'); img.save(png)
    subprocess.run(['convert', str(png), '-define', 'dds:compression=dxt5', '-define', f'dds:mipmaps={mips}', str(TEX / (name + '.dds'))], check=True)


def swirl_tex(name, arms, twist, seed):
    """u: once round the disc, v: centre (top row) to rim (bottom row) - spiral arms, ragged and streaked"""
    rng = np.random.default_rng(seed)
    w, h = 1024, 256
    u = np.linspace(0, 1, w, endpoint=False)[None, :]
    v = np.linspace(0, 1, h)[:, None]
    ph = (u * arms + v * twist) % 1.0
    arm = np.exp(-((ph - 0.5) / 0.16) ** 2)
    n = np.array(Image.fromarray((rng.random((32, 128)) * 255).astype(np.uint8)).resize((w, h), Image.BICUBIC), float) / 255
    streak = np.array(Image.fromarray((rng.random((h, 64)) * 255).astype(np.uint8)).resize((w, h), Image.BICUBIC).filter(ImageFilter.GaussianBlur(2)), float) / 255
    val = arm * (0.55 + 0.45 * n) * (0.6 + 0.4 * streak)
    val = val * np.clip(1.15 - v, 0, 1) ** 0.8 + 0.25 * np.exp(-(v / 0.18) ** 2)   # (a brighter heart)
    val *= np.clip((1.0 - v) / 0.12, 0, 1)                                         # (the rim fades out)
    g = (np.clip(val, 0, 1) * 255).astype(np.uint8)
    dds(Image.fromarray(np.dstack([g, g, g, g]), 'RGBA'), name)


def ring_tex(name):
    n = 256
    y, x = np.mgrid[-1:1:complex(n), -1:1:complex(n)]
    r = np.hypot(x, y)
    v = np.exp(-((r - 0.86) / 0.06) ** 2) + 0.5 * np.exp(-(r / 0.35) ** 2)
    g = (np.clip(v, 0, 1) * 255).astype(np.uint8)
    dds(Image.fromarray(np.dstack([g, g, g, g]), 'RGBA'), name)


def disc(L, name, tex, col, sc, urep, scroll, y, rs=1.0, na=72, nr=10):
    L.append(f'shape {name} tex={tex} color={col},1 scale={sc} blend=add single=0 uscroll={scroll}')
    for j in range(nr + 1):
        r = j / nr
        for i in range(na + 1):
            a = 2 * math.pi * i / na
            X, Z = W * rs * r * math.cos(a), H + H * rs * r * math.sin(a)
            L.append(f'v {X:.2f} {y:.2f} {Z:.2f} {urep * i / na:.4f} {r:.4f} 1 1 1 1')
    for j in range(nr):
        for i in range(na):
            a0, a1 = j * (na + 1) + i, (j + 1) * (na + 1) + i
            L += [f't {a0} {a1} {a1 + 1}', f't {a0} {a1 + 1} {a0 + 1}']


def flat(L, name, tex, col, sc, w, h, cz, y, flip=None, fps=20, phase=0.0):
    L.append(f'shape {name} tex={tex} color={col},1 scale={sc} blend=add single=0' + (f' flip={flip} fps={fps} phase={phase:.2f}' if flip else ''))
    cu = 1 / int(flip.split('x')[0]) if flip else 1
    for X, Z, u, v in ((-w, cz + h, 0, 0), (w, cz + h, cu, 0), (w, cz - h, cu, cu), (-w, cz - h, 0, cu)):
        L.append(f'v {X:.2f} {y:.2f} {Z:.2f} {u} {v} 1 1 1 1')
    L += ['t 0 1 2', 't 0 2 3']


def main():
    WORK.mkdir(parents=True, exist_ok=True); MESH.mkdir(parents=True, exist_ok=True)
    swirl_tex('portal_swirl_a', 3, 1.6, 11)
    swirl_tex('portal_swirl_b', 5, -2.3, 12)
    ring_tex('portal_ring')
    L = [f'out {MESH / "portal.nif"}']
    # the two swirls, one turning each way, the Tongues' pale blue and a gold under it
    disc(L, 'swirl_a', TEXREL + 'portal_swirl_a.dds', '0.55,0.8,1', 1.9, 1, 6.0, 0.0)
    disc(L, 'swirl_b', TEXREL + 'portal_swirl_b.dds', '1,0.78,0.38', 1.3, 1, -9.0, -1.5, rs=0.92)
    # its rim and heart
    flat(L, 'ring', TEXREL + 'portal_ring.dds', '0.7,0.88,1', 1.6, W * 1.08, H * 1.08, H, -2.5)
    flat(L, 'halo', 'CurseOfAlduin\\fx\\glow_disc.dds', '0.35,0.5,0.8', 1.1, W * 2.0, H * 1.9, H, -3.0)
    flat(L, 'heart', 'CurseOfAlduin\\fx\\glow_disc.dds', '1,0.95,0.85', 2.2, W * 0.45, H * 0.45, H, -3.5)
    # the fine motes rising off its foot (tools/motes.py)
    for k, x in enumerate((-60.0, 0.0, 60.0)):
        flat(L, f'motes{k}', 'CurseOfAlduin\\fx\\motes_rise.dds', '0.7,0.85,1', 1.5, 34, 80, 80 + 6 * k, -5.0, flip='16x16', fps=60, phase=1.4 * k)
    jf = WORK / 'portal.job'; jf.write_text('\n'.join(L) + '\n')
    r = subprocess.run(['/home/claude/tools/fxwrite', str(jf)], capture_output=True, text=True)
    print(r.stdout.strip(), r.stderr.strip())
    if r.returncode: raise SystemExit('fxwrite failed')


if __name__ == '__main__':
    main()
