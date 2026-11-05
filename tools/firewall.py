"""(Noah: "the shadow flame effect on enemies looks like a bunch of small ash piles with purple flames when it should be one large
wall of purple flames in a fan 15 meters in front of the attack swing" - a 90 degree fan, burning 3 s) the Shadowflame
Sword's Tier II heavy attack: a solid wall of tall violet flame (tools/alchfx.py's pillar_flame_violet flipbook, 60 fps, on
upright camera-facing cards, packed shoulder to shoulder, every card its own phase so the wall churns) filling a 90 degree
fan from the blade out to 15 m, over a violet glow lying on the ground and embers rising off it. Built as five bands from
the middle out, so src/Classes.cpp can light them one after another - the wall rolling out from the swing.

The fan's point at the model's origin, opening along +Y, the ground at z 0.
    python3 tools/firewall.py  ->  assets/fx/Meshes/CurseOfAlduin/fx/firewall_<0..4>.nif"""
import math, pathlib, random, subprocess

ROOT = pathlib.Path(__file__).resolve().parent.parent
MESH = ROOT / 'assets' / 'fx' / 'Meshes' / 'CurseOfAlduin' / 'fx'
WORK = ROOT / 'build' / 'firewall'
FX = 'CurseOfAlduin\\fx\\'
REACH, HALF, BANDS = 1050.0, math.radians(45), 5


def card(L, name, tex, col, sc, w, h0, h1, at, flip, fps, phase):
    L.append(f'shape {name} tex={tex} color={col},1 scale={sc} blend=add billboard=2 flip={flip[0]}x{flip[1]} fps={fps} phase={phase:.2f} at={at[0]:.1f},{at[1]:.1f},{at[2]:.1f}')
    cu, cv = 1 / flip[0], 1 / flip[1]
    for X, Y, u, v in ((-w, h1, 0, 0), (w, h1, cu, 0), (w, h0, cu, cv), (-w, h0, 0, cv)):
        L.append(f'v {X:.2f} {Y:.2f} 0 {u:.5f} {v:.5f} 1 1 1 1')
    L += ['t 0 1 2', 't 0 2 3']


def glow(L, name, r0, r1, col, sc):
    """the fire's light on the ground: a band of the fan lying flat, soft at its edges"""
    L.append(f'shape {name} tex={FX}arc_haze.dds color={col},1 scale={sc} blend=add single=0')
    segs = 24
    n = 0
    for k in range(segs + 1):
        a = -HALF + 2 * HALF * k / segs
        edge = math.sin(math.pi * k / segs) ** 0.5
        for j, r in enumerate((r0, (r0 + r1) / 2, r1)):
            L.append(f'v {r * math.sin(a):.1f} {r * math.cos(a):.1f} 3 {k / segs:.3f} {(0.05, 0.3, 0.85)[j]} {edge:.2f} {edge:.2f} {edge:.2f} 1')
            L.append('n 0 0 1')
    for k in range(segs):
        for j in range(2):
            a0 = k * 3 + j
            L.append(f't {a0} {a0 + 3} {a0 + 4}'); L.append(f't {a0} {a0 + 4} {a0 + 1}')


def band(b):
    random.seed(400 + b)
    r0 = 60.0 + (REACH - 60.0) * b / BANDS
    r1 = 60.0 + (REACH - 60.0) * (b + 1) / BANDS
    L = [f'out {MESH / f"firewall_{b}.nif"}']
    tex = FX + 'pillar_flame_violet.dds'
    k = 0
    rows = 3
    for j in range(rows):
        r = r0 + (r1 - r0) * (j + 0.5) / rows
        n = max(3, int(2 * HALF * r / 58.0))
        for i in range(n):
            a = -HALF + 2 * HALF * (i + random.uniform(0.2, 0.8)) / n
            rr = r + random.uniform(-18, 18)
            edge = min(1.0, (HALF - abs(a)) / 0.18)           # (lower at the fan's sides: it ends, it isn't cut off)
            reach = min(1.0, (REACH - rr) / 140.0 + 0.35)     # (and at its far rim)
            h = random.uniform(170, 240) * (0.55 + 0.45 * edge) * reach
            w = random.uniform(48, 62)
            col = random.choice(('1,1,1', '0.92,0.85,1', '1,0.92,1', '0.85,0.75,1'))
            card(L, f'f{k}', tex, col, 0.8, w, -8, h, (rr * math.sin(a), rr * math.cos(a), 0), (16, 8), random.uniform(52, 64), random.uniform(0, 2.1)); k += 1
    # embers lifting off it
    for i in range(4):
        a = random.uniform(-HALF * 0.8, HALF * 0.8); r = random.uniform(r0, r1)
        card(L, f'e{i}', FX + 'motes_rise.dds', '0.62,0.3,1', 1.4, 60, 0, 300, (r * math.sin(a), r * math.cos(a), 20), (16, 16), random.uniform(54, 66), i * 0.7)
    glow(L, 'glow', max(0.0, r0 - 40), r1 + 40, '0.45,0.12,0.9', 0.9)
    WORK.mkdir(parents=True, exist_ok=True)
    jf = WORK / f'firewall_{b}.job'; jf.write_text('\n'.join(L) + '\n')
    r = subprocess.run(['/home/claude/tools/fxwrite', str(jf)], capture_output=True, text=True)
    print(f'band {b}: {k} flames', r.stdout.strip()[-120:], r.stderr.strip()[-300:])
    if r.returncode: raise SystemExit('fxwrite failed')


if __name__ == '__main__':
    for b in range(BANDS): band(b)
