"""(Noah: "all of the bows' arrows, when fired, should be like an impact grenade ... the colour of the arrow's magical attack
after impact should match the weapon's colour theme - the sunbow shouldn't have a green explosion, it should have a gold
one") each element's impact burst: the element's own flipbook (the one its bow's arrowhead burns with, tools/bowarrows.py -
flame, frost-mist, arcs, gusts, sun-rays, blood-mist, the void's little black hole) on camera-facing cards in a ragged
ball round a white-hot core, each card its own phase so the burst churns, tinted the bow's colour. Placed where an arrow
strikes and taken away a moment later (src/Gun.cpp ElemBurst; src/Bows.inl Impact).

    python3 tools/elemburst.py   ->  assets/fx/Meshes/CurseOfAlduin/fx/burst_<element>.nif"""
import math, pathlib, random, subprocess

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT = ROOT / 'assets' / 'fx' / 'Meshes' / 'CurseOfAlduin' / 'fx'
WORK = ROOT / 'build' / 'elemburst'
# element: (flipbook, core colour, body colours, fringe colour) - the colours pushed warm/red where the game's tonemap
# washes additive light toward blue
EL = {
    'fire':  ('emberstring', '1,0.85,0.6', ('1,0.45,0.08', '1,0.32,0.04', '1,0.55,0.12'), '0.9,0.18,0.02'),
    'frost': ('rimehorn', '0.9,0.97,1', ('0.55,0.85,1', '0.7,0.92,1', '0.45,0.75,1'), '0.3,0.6,1'),
    'shock': ('stormreach', '0.95,0.9,1', ('0.75,0.6,1', '0.6,0.55,1', '0.85,0.7,1'), '0.5,0.35,1'),
    'wind':  ('skyhowl', '0.95,1,0.97', ('0.75,1,0.92', '0.85,1,0.95', '0.65,0.95,0.9'), '0.55,0.85,0.8'),
    'sun':   ('dawnspire', '1,0.95,0.7', ('1,0.78,0.2', '1,0.68,0.12', '1,0.86,0.35'), '1,0.55,0.05'),
    # (Noah: "all blood/life drain magic to be a dark red pulse/explosion") deep, dark red - no pale core
    'blood': ('thirstbow', '0.85,0.1,0.08', ('0.7,0.03,0.04', '0.55,0.01,0.03', '0.8,0.06,0.05'), '0.4,0.0,0.02'),
    'void':  ('vortex', '1,0.8,1', ('1,0.3,0.85', '0.95,0.2,0.8', '1,0.4,0.95'), '0.75,0.08,0.65'),
    'poison': ('thirstbow', '0.8,1,0.6', ('0.45,1,0.15', '0.3,0.85,0.1', '0.6,1,0.25'), '0.2,0.6,0.05'),
    'gold':  ('dawnspire', '1,0.97,0.75', ('1,0.82,0.25', '1,0.75,0.15', '1,0.9,0.45'), '0.95,0.6,0.08'),
}


def card(L, name, tex, col, sc, w, h, at, phase, fps=26):
    L.append(f'shape {name} tex={tex} color={col},1 scale={sc} blend=add billboard=2 flip=8x8 fps={fps} phase={phase:.2f} at={at[0]:.1f},{at[1]:.1f},{at[2]:.1f}')
    for X, Y, u, v in ((-w, h, 0, 0), (w, h, 1 / 8, 0), (w, -h, 1 / 8, 1 / 8), (-w, -h, 0, 1 / 8)):
        L.append(f'v {X:.2f} {Y:.2f} 0 {u} {v} 1 1 1 1')
    L += ['t 0 1 2', 't 0 2 3']


def build(el):
    bid, core, body, fringe = EL[el]
    tex = f'CurseOfAlduin\\bow\\{bid}_arrowfx.dds'
    random.seed(sum(map(ord, el)))
    out = OUT / f'burst_{el}.nif'
    L = [f'out {out}']
    k = 0
    for i in range(3):  # the white-hot heart
        card(L, f'b{k}', tex, core, 1.8, 30, 30, (random.uniform(-5, 5), random.uniform(-5, 5), random.uniform(20, 40)), i * 0.83); k += 1
    for i in range(10):  # the body: a ragged ball of the element
        a = 2 * math.pi * i / 10 + random.uniform(-0.25, 0.25)
        r = random.uniform(25, 62)
        card(L, f'b{k}', tex, random.choice(body), 1.6, random.uniform(26, 38), random.uniform(26, 38),
             (r * math.cos(a), r * math.sin(a), random.uniform(5, 70)), (k * 0.37) % 2.4); k += 1
    for i in range(6):  # the fringe: low and wide, the blast spreading on the ground
        a = 2 * math.pi * i / 6 + 0.4
        card(L, f'b{k}', tex, fringe, 1.3, 44, 30, (88 * math.cos(a), 88 * math.sin(a), 6), (k * 0.53) % 2.4, fps=20); k += 1
    jf = WORK / f'burst_{el}.job'
    jf.write_text('\n'.join(L) + '\n')
    r = subprocess.run(['/home/claude/tools/fxwrite', str(jf)], capture_output=True, text=True)
    print(el, r.stdout.strip(), r.stderr.strip())
    if r.returncode: raise SystemExit('fxwrite failed')


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    WORK.mkdir(parents=True, exist_ok=True)
    for el in EL:
        if el in ('sun', 'gold'): continue  # (their fine spark bursts are tools/motes.py's)
        build(el)


if __name__ == '__main__':
    main()
