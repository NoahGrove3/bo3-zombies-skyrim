"""Vahlok's chains (Noah, 2026-11-25, on the Jailor's Keyhammer's heavy: "Chains actually grabbing nearby enemies and grouping
them closer together"): a length of spectral chain - real links, each a ring of glowing iron in three dimensions, every second one
turned a quarter - wearing Skyrim's own enchantment fire (textures\\effects\\enchflameproject01.dds) in the Keyhammer's green.
src/Gun.cpp VahlokChains lays these lengths end to end from each of the dead it grabs to the one struck, and keeps them taut
while the dead are dragged in.

    python3 tools/vahlokchain.py   -> assets/fx/Meshes/CurseOfAlduin/fx/vahlok_chain.nif
"""
import math
import pathlib
import subprocess

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT = ROOT / 'assets' / 'fx' / 'Meshes' / 'CurseOfAlduin' / 'fx' / 'vahlok_chain.nif'
WORK = ROOT / 'build' / 'vahlokchain'
TOOLS = pathlib.Path('/home/claude/tools')

LINKS, PITCH = 4, 9.0          # (one length: four links, 36 units along +Y; src/Gun.cpp kChainLen)
RL, RW, R = 6.2, 3.6, 1.15     # (a link's half length and half width, and the thickness of its iron)
NU, NV = 28, 10


def link(cy, quarter):
    """one link: a stadium-shaped ring round +Y at cy (lying in XY, or in YZ when turned a quarter)"""
    V, T = [], []
    # the ring's centre line: two half circles joined by straights (a stadium)
    straight = 2 * (RL - RW)
    per = 2 * straight + 2 * math.pi * RW
    for i in range(NU):
        d = (i / NU) * per
        if d < straight:
            p, t = (RW, -straight / 2 + d), (0, 1)
        elif d < straight + math.pi * RW:
            a = (d - straight) / RW
            p, t = (RW * math.cos(a), straight / 2 + RW * math.sin(a)), (-math.sin(a), math.cos(a))
        elif d < 2 * straight + math.pi * RW:
            dd = d - straight - math.pi * RW
            p, t = (-RW, straight / 2 - dd), (0, -1)
        else:
            a = math.pi + (d - 2 * straight - math.pi * RW) / RW
            p, t = (RW * math.cos(a), -straight / 2 + RW * math.sin(a)), (-math.sin(a), math.cos(a))
        nx, ny = t[1], -t[0]   # (outward in the link's plane)
        for j in range(NV + 1):
            b = j / NV * 2 * math.pi
            off_in = math.cos(b) * R; off_up = math.sin(b) * R
            x, y, z = p[0] + nx * off_in, p[1] + ny * off_in, off_up
            if quarter: x, z = z, x
            # (its glow: brightest on the outside of the iron, the fire's texture running round the ring)
            a_ = 0.55 + 0.45 * max(0.0, math.cos(b))
            V.append((x, y + cy, z, i / NU * 2.0, j / NV, a_))
    for i in range(NU):
        for j in range(NV):
            a = i * (NV + 1) + j; b = ((i + 1) % NU) * (NV + 1) + j
            T += [(a, b, a + 1), (b, b + 1, a + 1)]
    return V, T


def build():
    WORK.mkdir(parents=True, exist_ok=True); OUT.parent.mkdir(parents=True, exist_ok=True)
    rows = [f'out {OUT}',
            'shape links tex=effects\\enchflameproject01.dds color=1,1,1,1 scale=1.0 blend=add '
            'palette=CurseOfAlduin\\fx\\pal_green.dds clamp=0 vscroll=1.4']
    base = 0
    for k in range(LINKS):
        V, T = link(PITCH * k + PITCH / 2, k % 2 == 1)
        rows += [f'v {x:.3f} {y:.3f} {z:.3f} {u:.4f} {v:.4f} 1 1 1 {a:.3f}' for x, y, z, u, v, a in V]
        rows += [f't {a + base} {b + base} {c + base}' for a, b, c in T]
        base += len(V)
    job = WORK / 'vahlok_chain.job'
    job.write_text('\n'.join(rows) + '\n')
    r = subprocess.run([str(TOOLS / 'fxwrite'), str(job)], capture_output=True, text=True)
    if r.returncode: raise SystemExit(r.stderr[-500:])
    print(OUT, OUT.stat().st_size, 'bytes')


if __name__ == '__main__':
    build()
