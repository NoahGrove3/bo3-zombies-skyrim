"""Build Undying Nights' meshes and textures from the source models in assets/models (not in git: third-party
files the player supplied). Textures -> DDS (Pillow), recolours per weapon element, models -> NIF via modelconv.
    python tools/assets.py <out Data dir> [only...]"""
import colorsys, json, pathlib, subprocess, sys
import numpy as np
from PIL import Image

ROOT = pathlib.Path(__file__).parent.parent
SRC = ROOT / 'assets' / 'models'
CONV = pathlib.Path('/home/claude/tools/modelconv')
OUT = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / 'build' / 'assets' / 'Data')
ONLY = set(sys.argv[2:])
TEX = 'UndyingNights'  # Data\Textures\UndyingNights\..., Data\Meshes\UndyingNights\...

def load(p, size=None, mode='RGB'):
    im = Image.open(p)
    if im.mode in ('I;16', 'I'):
        a = np.asarray(im, dtype=np.float32); im = Image.fromarray((a / max(1.0, a.max()) * 255).astype(np.uint8), 'L')
    im = im.convert(mode)
    return im.resize((size, size), Image.LANCZOS) if size and im.size[0] != size else im

def save_dds(im, rel, alpha=False):
    p = OUT / 'Textures' / TEX / rel
    p.parent.mkdir(parents=True, exist_ok=True)
    im = im.convert('RGBA' if alpha else 'RGB')
    im.save(p, pixel_format='DXT5' if alpha else 'DXT1')
    return f'{TEX}\\{rel}'.replace('/', '\\')

def normal_with_spec(normal_path, rough_path=None, size=1024, flip_green=False):
    n = np.asarray(load(normal_path, size), dtype=np.uint8).copy()
    if flip_green: n[..., 1] = 255 - n[..., 1]
    spec = 255 - np.asarray(load(rough_path, size, 'L')) if rough_path else np.full(n.shape[:2], 60, np.uint8)
    return Image.fromarray(np.dstack([n, (spec * 0.6).astype(np.uint8)]), 'RGBA')

def recolor(im, hue_deg, sat=1.0, val=1.0, min_sat=0.12):
    """Shift coloured areas to a new hue (leaves greys/whites), for element variants."""
    a = np.asarray(im.convert('RGB'), dtype=np.float32) / 255
    mx, mn = a.max(-1), a.min(-1); d = mx - mn
    s = np.where(mx > 0, d / np.maximum(mx, 1e-6), 0)
    mask = (s > min_sat)[..., None]
    target = np.array(colorsys.hsv_to_rgb(hue_deg / 360, 1, 1), np.float32)
    lum = (mx * val)[..., None]
    col = lum * ((1 - np.clip(s[..., None] * sat, 0, 1)) + np.clip(s[..., None] * sat, 0, 1) * target)
    out = np.where(mask, col, a)
    return Image.fromarray((np.clip(out, 0, 1) * 255).astype(np.uint8), 'RGB')

def tint(im, rgb, gain=1.4):
    a = np.asarray(im.convert('L'), dtype=np.float32)[..., None] / 255
    return Image.fromarray((np.clip(a * np.array(rgb, np.float32) * gain, 0, 255)).astype(np.uint8), 'RGB')

def run_job(lines):
    job = OUT.parent / ('job_%d.txt' % abs(hash(lines[0])))
    job.parent.mkdir(parents=True, exist_ok=True)
    job.write_text('\n'.join(lines) + '\n')
    r = subprocess.run([str(CONV), str(job)], capture_output=True, text=True)
    print(r.stdout.strip() or r.stderr.strip())
    if r.returncode: raise SystemExit('modelconv failed')

def mesh_out(rel):
    p = OUT / 'Meshes' / TEX / rel; p.parent.mkdir(parents=True, exist_ok=True); return p

# ---------------------------------------------------------------- tomes (spell weapons)
ELEMENTS = {  # id: (cover hue, emissive colour)
    'apprentice': (28, (230, 200, 140)),
    'fire': (12, (255, 110, 30)),
    'frost': (200, (140, 210, 255)),
    'storm': (275, (190, 120, 255)),
    'ember': (32, (255, 160, 40)),
}
def tomes():
    d = SRC / 'spellbook'
    alb = load(d / 'textures/material_albedo.jpeg', 1024); emi = load(d / 'textures/material_emissive.jpeg', 1024)
    nrm = save_dds(normal_with_spec(d / 'textures/material_normal.png', d / 'textures/material_roughness.jpeg'), 'tome/tome_n.dds', alpha=True)
    for el, (hue, glow) in ELEMENTS.items():
        dif = save_dds(recolor(alb, hue, sat=0.9, val=0.85 if el != 'apprentice' else 0.9), f'tome/tome_{el}_d.dds')
        gl = save_dds(tint(emi, glow), f'tome/tome_{el}_g.dds')
        # Held like a staff: spine in the palm, cover facing sideways, book rising out of the hand.
        run_job([f'in {d}/source/model/model.dae', f'out {mesh_out(f"tome/tome_{el}.nif")}', 'fit 30', 'rotate 0 90 0',
                 'offset 0 12 0', f'mat * diffuse={dif} normal={nrm} glow={gl} emit={glow[0]/255:.2f},{glow[1]/255:.2f},{glow[2]/255:.2f} emitmult=1.6 spec=0.4 gloss=60'])

# ---------------------------------------------------------------- flying grimoires (wonder weapons)
WONDERS = {  # id: (cover hue, rune colour)
    'wabbajack': (290, (170, 255, 90)),
    'thuum': (42, (255, 210, 90)),
    'skyforge': (205, (120, 220, 255)),
}
def grimoires():
    d = SRC / 'flyingbook'
    T = lambda n: d / 'textures' / n
    pages = [save_dds(load(T(f'page_{i}_albedo.jpeg'), 1024), f'grimoire/page_{i}_d.dds') for i in (1, 2, 3)]
    pg_glow = load(T('page_1_emissive.jpeg'), 1024)
    flat_n = save_dds(Image.new('RGBA', (64, 64), (128, 128, 255, 40)), 'grimoire/flat_n.dds', alpha=True)
    for wid, (hue, rune) in WONDERS.items():
        cover = save_dds(recolor(load(T('book_albedo.jpeg'), 1024), hue, sat=0.8, val=0.9), f'grimoire/{wid}_cover_d.dds')
        pglow = save_dds(tint(pg_glow, rune), f'grimoire/{wid}_page_g.dds')
        circ = []
        for c in (1, 2):
            emi = tint(load(T(f'circle_{c}_emissive.jpeg'), 1024), rune, 1.6)
            op = load(T(f'circle_{c}_opacity.jpeg'), 1024, 'L')
            circ.append((save_dds(Image.merge('RGBA', (*emi.split(), op)), f'grimoire/{wid}_circle{c}_d.dds', alpha=True),
                         save_dds(emi, f'grimoire/{wid}_circle{c}_g.dds')))
        e = f'{rune[0]/255:.2f},{rune[1]/255:.2f},{rune[2]/255:.2f}'
        # Floats above the open hand (staff grip), facing the player.
        run_job([f'in {d}/source/model.fbx', f'out {mesh_out(f"grimoire/{wid}.nif")}', 'fit 34', 'rotate 90 0 0', 'offset 0 26 4',
                 f'mat book diffuse={cover} normal={flat_n} spec=0.3',
                 f'mat page_1 diffuse={pages[0]} normal={flat_n} glow={pglow} emit={e} emitmult=1.5 doublesided=1',
                 f'mat page_2 diffuse={pages[1]} normal={flat_n} glow={pglow} emit={e} emitmult=1.2 doublesided=1',
                 f'mat page_3 diffuse={pages[2]} normal={flat_n} glow={pglow} emit={e} emitmult=1.2 doublesided=1',
                 f'mat circle_1 diffuse={circ[0][0]} normal={flat_n} glow={circ[0][1]} emit={e} emitmult=3 alpha=blend doublesided=1',
                 f'mat circle_2 diffuse={circ[1][0]} normal={flat_n} glow={circ[1][1]} emit={e} emitmult=3 alpha=blend doublesided=1'])

# ---------------------------------------------------------------- Sheogorath's Coffer
def coffer():
    d = SRC / 'coffer'; T = lambda n: d / 'textures' / f'initialShadingGroup_{n}.jpeg'
    dif = save_dds(load(T('albedo'), 1024), 'coffer/coffer_d.dds')
    nrm = save_dds(normal_with_spec(T('normal'), T('roughness')), 'coffer/coffer_n.dds', alpha=True)
    gl = save_dds(tint(load(T('emissive'), 1024), (190, 110, 255)), 'coffer/coffer_g.dds')
    run_job([f'in {d}/source/Weapon Chest Animated.fbx', f'out {mesh_out("coffer/coffer.nif")}', 'fit 95', 'rotate 90 0 0', 'ground 1',
             f'mat * diffuse={dif} normal={nrm} glow={gl} emit=0.75,0.45,1.0 emitmult=2.0 spec=0.5'])

# ---------------------------------------------------------------- brew machine (one per town, menu of all nine)
def brewmachine():
    d = SRC / 'vending'; T = lambda n: d / 'textures' / f'DefaultMaterial_{n}_1001.png'
    dif = save_dds(load(T('Base_color'), 2048), 'brewmachine/brewmachine_d.dds')
    nrm = save_dds(normal_with_spec(T('Normal_DirectX'), T('Roughness'), 2048, flip_green=True), 'brewmachine/brewmachine_n.dds', alpha=True)
    gl = save_dds(load(T('Emissive'), 2048), 'brewmachine/brewmachine_g.dds')
    run_job([f'in {d}/source/vendingmachine1.fbx', f'out {mesh_out("brewmachine/brewmachine.nif")}', 'fit 160', 'rotate 90 0 0', 'ground 1',
             f'mat * diffuse={dif} normal={nrm} glow={gl} emit=1,0.85,0.6 emitmult=1.5 spec=0.6 gloss=80'])

STEPS = {'tomes': tomes, 'grimoires': grimoires, 'coffer': coffer, 'brewmachine': brewmachine}
if __name__ == '__main__':
    for name, fn in STEPS.items():
        if not ONLY or name in ONLY:
            print('==', name); fn()
