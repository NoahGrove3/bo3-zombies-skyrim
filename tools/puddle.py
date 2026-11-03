"""(Noah: "the lingering acid that deals damage to enemies is invisible. It should be a puddle of acid on the ground") each
Alchemist flask's lingering pool: a flat, ragged splash of the liquid lying on the ground (its own texture: a glossy
blob of the element's colour, darker at its torn rim, slowly creeping round), with a few bubbles of the element's
flipbook rising off it. Hung in the scene where a Tier I flask comes down (src/Gun.cpp Puddle), flat in XY, ~200 wide.

    python3 tools/puddle.py   ->  assets/fx/Meshes/CurseOfAlduin/fx/puddle_<element>.nif (+ Textures/CurseOfAlduin/fx/puddle_*.dds)"""
import math, pathlib, random, subprocess
import numpy as np
from PIL import Image, ImageFilter

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT = ROOT / 'assets' / 'fx'
WORK = ROOT / 'build' / 'puddle'
# element: (liquid colour, rim colour, bubbles flipbook, bubble tint)
EL = {
    'fire':   ((255, 120, 20), (120, 20, 0), 'emberstring', '1,0.5,0.1'),
    'frost':  ((150, 220, 255), (40, 90, 180), 'rimehorn', '0.6,0.9,1'),
    'shock':  ((170, 140, 255), (60, 30, 160), 'stormreach', '0.75,0.6,1'),
    'poison': ((120, 255, 40), (20, 90, 0), 'thirstbow', '0.45,1,0.15'),
    'sun':    ((255, 210, 80), (150, 90, 0), 'dawnspire', '1,0.85,0.3'),
    'blood':  ((210, 20, 30), (70, 0, 5), 'thirstbow', '0.95,0.1,0.12'),
}


def blob(seed, n=512):
    rng = np.random.default_rng(seed)
    y, x = np.mgrid[-1:1:complex(n), -1:1:complex(n)]
    r, a = np.hypot(x, y), np.arctan2(y, x)
    edge = 0.62
    for k in range(2, 9):  # a torn, splashed rim
        edge = edge + rng.uniform(0.02, 0.09) / k * np.cos(k * a + rng.uniform(0, 6.28))
    m = np.clip((edge - r) / 0.05, 0, 1)
    for _ in range(9):  # splash droplets round it
        cx, cy, rr = rng.uniform(-0.8, 0.8), rng.uniform(-0.8, 0.8), rng.uniform(0.03, 0.08)
        if math.hypot(cx, cy) < 0.55: continue
        m = np.maximum(m, np.clip((rr - np.hypot(x - cx, y - cy)) / 0.02, 0, 1))
    noise = np.array(Image.fromarray((rng.random((64, 64)) * 255).astype(np.uint8)).resize((n, n), Image.BICUBIC), float) / 255
    return m, r / np.maximum(edge, 1e-3), noise


def texture(el):
    liquid, rim, _, _ = EL[el]
    m, rr, noise = blob(sum(map(ord, el)))
    depth = np.clip(1 - rr, 0, 1)
    t = np.clip(depth * 1.4 + (noise - 0.5) * 0.35, 0, 1)[..., None]
    col = np.array(rim, float) * (1 - t) + np.array(liquid, float) * t
    soft = np.array(Image.fromarray((noise * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(14)), float) / 255
    gloss = (np.clip(soft - 0.55, 0, 1) * 1.6)[..., None] * 200 * depth[..., None]
    rgb = np.clip(col + gloss, 0, 255)
    alpha = m * (0.55 + 0.45 * np.clip(depth * 2, 0, 1))
    img = Image.fromarray(np.dstack([rgb, alpha[..., None] * 255]).astype(np.uint8), 'RGBA').filter(ImageFilter.GaussianBlur(1.2))
    p = OUT / 'Textures' / 'CurseOfAlduin' / 'fx' / f'puddle_{el}.dds'
    p.parent.mkdir(parents=True, exist_ok=True)
    tmp = WORK / f'puddle_{el}.png'; img.save(tmp)
    subprocess.run(['convert', str(tmp), '-define', 'dds:compression=dxt5', '-define', 'dds:mipmaps=9', str(p)], check=True)
    return f'CurseOfAlduin\\fx\\puddle_{el}.dds'


def build(el):
    tex = texture(el)
    _, _, fb, tint = EL[el]
    random.seed(sum(map(ord, el)))
    out = OUT / 'Meshes' / 'CurseOfAlduin' / 'fx' / f'puddle_{el}.nif'
    L = [f'out {out}']
    R = 100.0
    for k, (sc, blend, z, rot) in enumerate(((1.25, 'alpha', 1.0, 0.0), (0.9, 'add', 1.6, 1.3))):  # the liquid, and its glow creeping round
        L.append(f'shape p{k} tex={tex} color=1,1,1,1 scale={sc} blend={blend} single=0 uscroll={0 if k == 0 else 40}')
        for (dx, dy, u, v) in ((-1, -1, 0, 1), (1, -1, 1, 1), (1, 1, 1, 0), (-1, 1, 0, 0)):
            ca, sa = math.cos(rot), math.sin(rot)
            X, Y = R * (dx * ca - dy * sa), R * (dx * sa + dy * ca)
            L.append(f'v {X:.1f} {Y:.1f} {z} {u} {v} 1 1 1 1')
        L += ['t 0 1 2', 't 0 2 3']
    for i in range(5):  # bubbles of it rising
        a, r = random.uniform(0, 6.28), random.uniform(10, 70)
        L.append(f'shape b{i} tex=CurseOfAlduin\\bow\\{fb}_arrowfx.dds color={tint},1 scale=1.4 blend=add billboard=2 flip=8x8 fps=20 phase={random.uniform(0, 2.6):.2f} at={r * math.cos(a):.1f},{r * math.sin(a):.1f},14')
        w = random.uniform(10, 16)
        for X, Y, u, v in ((-w, w, 0, 0), (w, w, 1 / 8, 0), (w, -w * 0.4, 1 / 8, 1 / 8), (-w, -w * 0.4, 0, 1 / 8)):
            L.append(f'v {X:.1f} {Y:.1f} 0 {u} {v} 1 1 1 1')
        L += ['t 0 1 2', 't 0 2 3']
    jf = WORK / f'puddle_{el}.job'; jf.write_text('\n'.join(L) + '\n')
    r = subprocess.run(['/home/claude/tools/fxwrite', str(jf)], capture_output=True, text=True)
    print(el, r.stdout.strip(), r.stderr.strip())
    if r.returncode: raise SystemExit('fxwrite failed')


if __name__ == '__main__':
    WORK.mkdir(parents=True, exist_ok=True)
    for el in EL: build(el)
