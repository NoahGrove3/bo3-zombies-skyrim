"""(Noah: "tier 1 sends out arc slashes from the blade ... that pierce enemies, swing in the color of the sword"; "Alduin's Bane's
wind slashes should be a white arc aligning with the blade swing angle not a cone of wind") the swords' crescent slashes: a
blazing crescent bowed forward (+Y, the way it flies), its curve along X - turned to the swing's angle by src/Gun.cpp - drawn
twice: a ribbon standing across the flight (seen from behind) and the same lying flat (seen from above), each a hot core and
a soft streaked glow, fading to nothing at both tips. One model per colour.

    python3 tools/arcfx.py   ->  assets/fx/Meshes/CurseOfAlduin/fx/arc_<colour>.nif, Textures/.../arc_slash.dds"""
import math, pathlib, subprocess
import numpy as np
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parent.parent
MESH = ROOT / 'assets' / 'fx' / 'Meshes' / 'CurseOfAlduin' / 'fx'
TEX = ROOT / 'assets' / 'fx' / 'Textures' / 'CurseOfAlduin' / 'fx'
WORK = ROOT / 'build' / 'arcfx'
FX = 'CurseOfAlduin\\fx\\'
COLOURS = {   # glow, core
    'purple': ('0.62,0.22,1', '0.95,0.8,1'), 'carian': ('0.35,0.62,1', '0.85,0.95,1'), 'white': ('0.9,0.92,1', '1,1,1'),
    'red': ('1,0.16,0.08', '1,0.75,0.6'), 'fire': ('1,0.45,0.06', '1,0.9,0.6'), 'frost': ('0.45,0.8,1', '0.92,0.98,1'),
    'shock': ('0.6,0.5,1', '0.95,0.92,1'), 'sun': ('1,0.82,0.3', '1,0.98,0.85'), 'blood': ('0.6,0.02,0.04', '1,0.4,0.35'),
    'gold': ('1,0.62,0.12', '1,0.95,0.7'), 'bane': ('0.92,0.95,1', '1,1,1'),
}


def texture():
    """across the band (v) a hot thin core in a soft glow; along it (u) fine streaks, as a blade's trail smears"""
    W, H = 512, 128
    rng = np.random.default_rng(3)
    v = np.linspace(-1, 1, H)[:, None]
    streak = np.repeat(rng.random((1, W // 8)), 8, 1)
    streak = np.convolve(streak[0], np.ones(9) / 9, 'same')[None, :]
    streaks = 0.65 + 0.35 * (np.abs(np.sin(v * 9 + streak * 6)) ** 3)
    glow = np.exp(-(v / 0.55) ** 2) * streaks
    core = np.exp(-(v / 0.12) ** 2)
    a = np.clip(glow * 0.75 + core, 0, 1)
    img = np.repeat((a * 255).astype(np.uint8)[..., None], 3, 2)
    TEX.mkdir(parents=True, exist_ok=True); WORK.mkdir(parents=True, exist_ok=True)
    png = WORK / 'arc_slash.png'; Image.fromarray(img).save(png)
    subprocess.run(['convert', str(png), '-define', 'dds:compression=dxt1', '-define', 'dds:mipmaps=8', str(TEX / 'arc_slash.dds')], check=True)


def ribbon(L, name, col, scale, R, half_deg, w, standing, segs=24, vscroll=None):
    L.append(f'shape {name} tex={FX}arc_slash.dds color={col},1 scale={scale} blend=add single=0' + (f' vscroll={vscroll}' if vscroll else ''))
    for k in range(segs + 1):
        f = k / segs
        th = math.radians(-half_deg + 2 * half_deg * f)
        x, y = R * math.sin(th), R * math.cos(th) - R * 0.55
        fade = math.sin(math.pi * f) ** 0.8               # nothing at the tips
        width = w * (0.35 + 0.65 * fade)                  # thinner toward the tips
        if standing:   # across the flight: the band up and down (z)
            pts = ((x, y, -width), (x, y, width))
        else:          # flat: the band inward and outward along the curve's normal
            nx, ny = math.sin(th), math.cos(th)
            pts = ((x - nx * width, y - ny * width, 0), (x + nx * width, y + ny * width, 0))
        for (px, py, pz), vv in zip(pts, (1.0, 0.0)):
            L.append(f'v {px:.2f} {py:.2f} {pz:.2f} {f:.3f} {vv} {fade:.3f} {fade:.3f} {fade:.3f} 1')
    for k in range(segs):
        a = 2 * k
        L += [f't {a} {a + 1} {a + 3}', f't {a} {a + 3} {a + 2}']


def build(key, glow, core):
    L = [f'out {MESH / ("arc_" + key + ".nif")}']
    for stand in (True, False):
        s = 's' if stand else 'f'
        ribbon(L, f'g{s}', glow, 1.6, 95, 78, 22, stand)          # its glow
        ribbon(L, f'c{s}', core, 1.3, 95, 70, 6, stand)           # its hot core
    jf = WORK / f'arc_{key}.job'; jf.write_text('\n'.join(L) + '\n')
    r = subprocess.run(['/home/claude/tools/fxwrite', str(jf)], capture_output=True, text=True)
    print(key, r.stdout.strip(), r.stderr.strip())
    if r.returncode: raise SystemExit('fxwrite failed')


if __name__ == '__main__':
    texture()
    MESH.mkdir(parents=True, exist_ok=True)
    for k, (g, c) in COLOURS.items(): build(k, g, c)
