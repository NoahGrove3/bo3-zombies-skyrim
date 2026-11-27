"""(Noah, Nov 27 test of 3edd0b7: "I want the preview effects in this workshop to be exactly how it shows in game if not better"; the
flasks "only visible from certain angles and ... not looped"; "the heavy attacks ... look very poor in game but look fantastic in the
workshop preview") the workshop's own effects baked for the game: each one the preview plays (docs/workshop/v2/vfx.js makeEffect, the
same shaders, the same bloom) rendered frame by frame on black in a real browser (tools/fxbake/bake.js), the frames laid in one 4K
atlas (2K for the small impacts), and a card the game turns to face you and steps through the atlas itself, once, from the first
frame (src/Baked.inl) - so every burst starts at its start, plays whole and is seen the same from every side.

    python3 tools/fxbake.py [names...]     ->  assets/fx/Textures/CurseOfAlduin/baked/<name>.dds, assets/fx/Meshes/CurseOfAlduin/baked/<name>.nif,
                                               src/BakedFx.h (each one's grid, length and size), build/fxbake/<name>.png (the atlas, to look at)
Needs three.js r128 at /tmp/three_r128/build/three.min.js (or THREE_JS=<path>) and Playwright's Chromium."""
import base64, io, json, math, os, pathlib, subprocess, sys
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / 'tools'))
WS = ROOT / 'docs' / 'workshop' / 'v2'
TEX = ROOT / 'assets' / 'fx' / 'Textures' / 'CurseOfAlduin' / 'baked'
MESH = ROOT / 'assets' / 'fx' / 'Meshes' / 'CurseOfAlduin' / 'baked'
WORK = ROOT / 'build' / 'fxbake'
HDR = ROOT / 'src' / 'BakedFx.h'
FXWRITE = pathlib.Path('/home/claude/tools/fxwrite')
THREE = pathlib.Path(os.environ.get('THREE_JS', '/tmp/three_r128/build/three.min.js'))
FRAMES = 64


# each role's picture: its middle and size in the workshop's units at size 1 (S scales it; a pillar's height is the sky's, not S's),
# and how far the camera looks down on it
def frame_of(role, opt, S):
    if (role, opt) == ('impact', 'core'): return (0, 45 * S, 210 * S, 210 * S), .18
    if (role, opt) == ('impact', 'spark'): return (0, 60 * S, 420 * S, 340 * S), .18
    if (role, opt) == ('burst', 'spikes'): return (0, 125 * S, 470 * S, 330 * S), .22
    if (role, opt) == ('burst', 'big'): return (0, 190 * S, 640 * S, 520 * S), .2
    if (role, opt) == ('burst', 'nova'): return (0, 70 * S, 700 * S, 300 * S), .32
    if (role, opt) == ('above', 'pillar'): return (0, 400, max(260 * S, 220), 820), .12
    return (0, 60 * S, 400 * S, 400 * S), .2


# the effects the game plays (Noah's picks, the workshop database Nov 27): name -> (element, role, option, size, atlas side)
JOBS = {
    # the flasks: his pick for all six, the Ring (burst: nova) at 50%
    **{f'flask_{f}': (el, 'burst', 'nova', .5, 4096) for f, el in
       [('fire', 'fire'), ('frost', 'frost'), ('shock', 'shock'), ('poison', 'poison'), ('sun', 'holy'), ('blood', 'blood')]},
    # the class moves' Heavy: the Summoner's Pillar of Dawn and the Spellsword's Spell Pillar (above: pillar, his pick for each element)
    **{f'pillar_{el}': (el, 'above', 'pillar', 1, 4096) for el in ['holy', 'fire', 'frost', 'shock', 'arcane', 'spirit']},
    # the Light's impact (impact: core) in each class's element
    **{f'impact_{el}': (el, 'impact', 'core', 1, 2048) for el in ['shadowfire', 'holy', 'fire', 'frost', 'shock', 'arcane', 'spirit']},
    # the Alchemist's Shadowfire Eruption (burst: spikes) and Destruction's arcane heavy burst (burst: big)
    'spikes_shadowfire': ('shadowfire', 'burst', 'spikes', 1, 4096),
    'big_arcane': ('arcane', 'burst', 'big', 1, 4096),
}


def grid(aspect, n=FRAMES):
    """cols x rows of cells whose shape (side / cols) : (side / rows) is nearest the picture's"""
    best = None
    for cols in range(1, n + 1):
        rows = math.ceil(n / cols)
        err = abs(math.log((rows / cols) / aspect))
        if best is None or err < best[0]: best = (err, cols, rows)
    return best[1], best[2]


def page():
    WORK.mkdir(parents=True, exist_ok=True)
    html = WORK / 'bake.html'
    html.write_text('<!doctype html><meta charset=utf8><body>' + ''.join(
        f'<script src="file://{p}"></script>' for p in [THREE, WS / 'viewer.js', WS / 'vfx.js', ROOT / 'tools' / 'fxbake' / 'bake.js']))
    return html


def bake(names):
    from playwright.sync_api import sync_playwright
    import assets
    TEX.mkdir(parents=True, exist_ok=True); MESH.mkdir(parents=True, exist_ok=True)
    rows_out = json.loads((WORK / 'baked.json').read_text()) if (WORK / 'baked.json').exists() else {}
    with sync_playwright() as pw:
        b = pw.chromium.launch(args=['--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist', '--allow-file-access-from-files'])
        pg = b.new_page()
        errs = []
        pg.on('pageerror', lambda e: errs.append(str(e)))
        pg.goto(page().as_uri())
        pg.wait_for_function('window.bakeReady === true', timeout=60000)
        for name in names:
            el, role, opt, S, side = JOBS[name]
            (cx, cy, bw, bh), elev = frame_of(role, opt, S)
            cols, rows = grid(bw / bh)
            cw, ch = side // cols // 4 * 4, side // rows // 4 * 4
            # (a first look, small: where its light ends - the frames are spread over what is seen, none wasted on black)
            probe = pg.evaluate('j => bakeFrames(j)', dict(el=el, role=role, opt=opt, size=S, frames=48, w=128, h=max(16, int(128 * bh / bw) // 4 * 4), box=[cx, cy, bw, bh], elev=elev))
            lum = [sum(Image.open(io.BytesIO(base64.b64decode(d.split(',', 1)[1]))).convert('L').point(lambda v: 255 if v > 14 else 0).histogram()[255:]) for d in probe['frames']]
            last = max([i for i, v in enumerate(lum) if v > 3] or [47])
            tmax = min(probe['dur'], (last + 1.5) / 48 * probe['dur'])
            res = pg.evaluate('j => bakeFrames(j)', dict(el=el, role=role, opt=opt, size=S, frames=FRAMES, w=cw, h=ch, box=[cx, cy, bw, bh], elev=elev, tmax=tmax))
            if errs: raise SystemExit(f'{name}: {errs[:3]}')
            atlas = Image.new('RGB', (side, side), (0, 0, 0))
            for i, d in enumerate(res['frames']):
                im = Image.open(io.BytesIO(base64.b64decode(d.split(',', 1)[1]))).convert('RGB')
                atlas.paste(im, ((i % cols) * (side // cols), (i // cols) * (side // rows)))
            atlas.save(WORK / f'{name}.png')
            assets.write_dds_mips(atlas.convert('RGBA'), TEX / f'{name}.dds', 'DXT1')
            # the card: the picture's own shape in the workshop's units (the game scales it), its foot at the ground the effect stands
            # on; one cell of the atlas across it (the game steps it cell by cell)
            u1, v1 = (cw / side), (ch / side)
            x0, x1, z0, z1 = cx - bw / 2, cx + bw / 2, cy - bh / 2, cy + bh / 2
            job = [f'out {MESH / (name + ".nif")}', f'shape {name} tex=CurseOfAlduin\\baked\\{name}.dds color=1,1,1,1 scale=1.0 blend=add clamp=0']
            for X, Z, u, v in ((x0, z1, 0, 0), (x1, z1, u1, 0), (x1, z0, u1, v1), (x0, z0, 0, v1)):
                job.append(f'v {X:.2f} 0 {Z:.2f} {u:.6f} {v:.6f} 1 1 1 1')
            job += ['t 0 1 2', 't 0 2 3']
            (WORK / f'{name}.job').write_text('\n'.join(job) + '\n')
            r = subprocess.run([str(FXWRITE), str(WORK / f'{name}.job')], capture_output=True, text=True)
            if r.returncode: raise SystemExit(f'{name}: fxwrite failed {r.stderr[-300:]}')
            rows_out[name] = dict(cols=cols, rows=rows, frames=FRAMES, dur=res['dur'], stepU=(side // cols) / side, stepV=(side // rows) / side,
                                  w=bw, h=bh, cy=cy, el=el, role=role, opt=opt, size=S)
            print(f'{name}: {el} {role}:{opt} x{S} - {cols}x{rows} cells of {cw}x{ch}, {res["dur"]:.1f} s')
        b.close()
    (WORK / 'baked.json').write_text(json.dumps(rows_out, indent=1))
    lines = ['// Generated by tools/fxbake.py (the workshop\'s own effects baked for the game: src/Baked.inl plays them). Do not edit.', '#pragma once',
             '#include <array>', '#include <string_view>', '', 'namespace coa::baked', '{',
             '\tstruct Def { std::string_view name; int cols, rows, frames; float dur, stepU, stepV, w, h, cy; };',
             f'\tinline constexpr std::array<Def, {len(rows_out)}> kDefs{{ {{']
    for n, d in sorted(rows_out.items()):
        lines.append(f'\t\tDef{{ "{n}", {d["cols"]}, {d["rows"]}, {d["frames"]}, {d["dur"]:.3f}f, {d["stepU"]:.6f}f, {d["stepV"]:.6f}f, {d["w"]:.1f}f, {d["h"]:.1f}f, {d["cy"]:.1f}f }},')
    lines += ['\t} };', '}', '']
    HDR.write_text('\n'.join(lines))
    print(f'wrote {HDR} ({len(rows_out)} effects)')


if __name__ == '__main__':
    bake(sys.argv[1:] or list(JOBS))
