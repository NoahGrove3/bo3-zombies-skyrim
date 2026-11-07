"""(Noah: the Dragon Priests' weapons - "Jailor's bow" for Vahlok, "Dreamer's bow" for Vokun) the two priest bows,
modelled from scratch in the bow rig's own space and stood on Skyrim's bow rig (tools/bowconv: the Bow_* bones and the
behavior graph of one of our built bows) so the limbs bend and the string draws with Skyrim's own bow animations.

Rig space (as tools/bowmodels.py): y along the bow (the nocks at y +-54.8), x from the string (x -13.69) to the grip
(x 1.31) and on to the front, z across. Limb parts are weighted as Skyrim's bows are (grip bone in the middle, two limb
bones toward each tip), the string on its two string bones.

    python3 tools/priestbows.py [vahlok] [vokun] [--keep]
      -> assets/fx/Meshes/CurseOfAlduin/priests/<name>.nif (+ textures beside the other priest weapons')"""
import math, pathlib, subprocess, sys
import numpy as np
from scipy.ndimage import gaussian_filter
from PIL import Image

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import priestweapons as pw
from priestweapons import g, Mesh, unit, metal_tex, leather_tex, save, tileable, height_to_normal, torus, superbox, lathe, rotm, glow_tex

TEMPLATE = pw.ROOT / 'assets' / 'fx' / 'Meshes' / 'CurseOfAlduin' / 'bow' / 'rimehorn.nif'   # (our built bow: the rig, graph, markers)
GRIP_X, STRING_X, STRING_Y = 1.31, -13.69, 54.8
MID, LO1, LO2, SB1, UP1, UP2, SB2 = range(7)
FX = pw.FX


def limb_weights(y):
    """grip bone near the middle, then the two limb bones toward each tip (as Skyrim's bows are weighted)"""
    t = abs(y); b1, b2 = (LO1, LO2) if y < 0 else (UP1, UP2)
    if t < 5: return [(MID, 1.0)]
    if t < 14: a = (t - 5) / 9; return [(MID, 1 - a), (b1, a)]
    if t < 24: return [(b1, 1.0)]
    if t < 32: a = (t - 24) / 8; return [(b1, 1 - a), (b2, a)]
    return [(b2, 1.0)]


def limb_x(y):
    """the limbs' line: the grip at GRIP_X, sweeping back to the nocks (STRING_X at +-STRING_Y), the tips recurved"""
    t = np.clip(np.abs(y) / STRING_Y, 0, 1.2)
    return GRIP_X - 19.0 * t ** 2 + 4.0 * np.clip((t - 0.86) / 0.14, 0, None) ** 2 * (t > 0.86)


def limb_line(y0, y1, n):
    y = np.linspace(y0, y1, n)
    return np.stack([limb_x(y), y, np.zeros_like(y)], 1)


def sweep(path, wz, tx, nseg=16, sq=3.0, twist=None):
    """a flattened tube along path (in the x-y plane): half width wz across (z), half thickness tx in the plane; sq the
    section's squareness (2 an ellipse). UV: u round, v along at true scale"""
    path = np.asarray(path, float); n = len(path)
    T = unit(np.gradient(path, axis=0)); Z = np.array([0, 0, 1.0])
    Nn = unit(np.cross(Z, T))
    wz = np.broadcast_to(np.asarray(wz, float), (n,)); tx = np.broadcast_to(np.asarray(tx, float), (n,))
    th = np.linspace(0, 2 * np.pi, nseg + 1)
    c, s = np.cos(th), np.sin(th)
    e = 2.0 / sq
    cc = np.sign(c) * np.abs(c) ** e; ss = np.sign(s) * np.abs(s) ** e
    P = path[:, None, :] + Nn[:, None, :] * (tx[:, None] * cc)[..., None] + Z[None, None, :] * (wz[:, None] * ss)[..., None]
    Nv = unit(Nn[:, None, :] * (c / np.maximum(tx[:, None], 1e-3))[..., None] + Z[None, None, :] * (s / np.maximum(wz[:, None], 1e-3))[..., None])
    L = np.concatenate([[0], np.cumsum(np.linalg.norm(np.diff(path, axis=0), axis=1))])
    UV = np.stack(np.broadcast_arrays((th / (2 * np.pi))[None, :] * (2 * np.pi * max(wz.max(), tx.max())) / pw.TILE, (L / pw.TILE)[:, None]), -1)
    F = np.array(pw.grid_faces(n, nseg + 1))
    P = P.reshape(-1, 3); Nv = Nv.reshape(-1, 3); UV = UV.reshape(-1, 2) + pw.rng.random(2)
    fn = np.cross(P[F[:, 1]] - P[F[:, 0]], P[F[:, 2]] - P[F[:, 0]])
    flip = (fn * Nv[F].mean(1)).sum(1) < 0
    F[flip] = F[flip][:, [0, 2, 1]]
    return P, Nv, UV, F


def limb_profile(y):
    """half width (z) and half thickness of a limb at y: stout at the riser, slimming to the tips"""
    t = np.clip(np.abs(y) / STRING_Y, 0, 1.2)
    riser = np.clip((10 - np.abs(y)) / 3, 0, 1)
    wz = 1.5 - 0.75 * t + 0.25 * riser
    tx = 1.15 - 0.6 * t + 0.85 * riser
    return wz, tx


def write(name, M, mats, fx_lines=None):
    """the bow's parts on the rig (tools/bowconv), its string, then its effects (fxwrite)"""
    pw.roughen(M, len(name))
    W = pw.WORK; W.mkdir(parents=True, exist_ok=True)
    nif = pw.MESHDIR / f'{name}.nif'; base = W / f'{name}_rig.nif'
    lines = [f'template {TEMPLATE}', f'out {base if fx_lines else nif}']
    merged = {}
    for gname, m, V, N, UV, F in M.groups:
        e = merged.setdefault(m, [[], [], [], [], 0]); e[0].append(np.asarray(V)); e[1].append(np.asarray(N)); e[2].append(np.asarray(UV)); e[3].append(np.asarray(F) + e[4]); e[4] += len(V)
    tris = 0
    for m, (Vs, Ns, Us, Fs, _) in merged.items():
        V, N, U, F = np.vstack(Vs), np.vstack(Ns), np.vstack(Us), np.vstack(Fs)
        lines.append(f'shape {name}_{m} {mats[m]}')
        for (x, y, z), (nx, ny, nz), (u, v) in zip(V, N, U):
            lines.append(f'v {x:.4f} {y:.4f} {z:.4f} {u:.5f} {v:.5f} {nx:.4f} {ny:.4f} {nz:.4f} ' + ' '.join(f'{b} {w:.4f}' for b, w in limb_weights(y)))
        lines += [f't {a} {b} {c}' for a, b, c in F]; tris += len(F)
    # the string: a thin cord from nock to nock on the string bones
    nockB, nockT = np.array([STRING_X, -STRING_Y, 0]), np.array([STRING_X, STRING_Y, 0])
    lines.append(f'shape {name}_string {mats["string"]}')
    R0, SEG, SIDES = 0.22, 24, 6
    for s in range(SEG + 1):
        t = s / SEG; c = nockB * (1 - t) + nockT * t
        for j in range(SIDES):
            a = 2 * math.pi * j / SIDES; nx, nz = math.cos(a), math.sin(a)
            lines.append(f'v {c[0] + R0 * nx:.4f} {c[1]:.4f} {c[2] + R0 * nz:.4f} {j / SIDES:.4f} {t * 6:.4f} {nx:.4f} 0 {nz:.4f} {SB1 if c[1] < 0 else SB2} 1.0')
    for s in range(SEG):
        for j in range(SIDES):
            a = s * SIDES + j; b = s * SIDES + (j + 1) % SIDES
            lines += [f't {a} {a + SIDES} {b}', f't {b} {a + SIDES} {b + SIDES}']
    (W / f'{name}.bowjob').write_text('\n'.join(lines) + '\n')
    pw.MESHDIR.mkdir(parents=True, exist_ok=True)
    r = subprocess.run(['/home/claude/tools/bowconv', str(W / f'{name}.bowjob')], capture_output=True, text=True)
    if r.returncode: print(r.stdout, r.stderr); raise SystemExit('bowconv failed')
    if fx_lines:
        L = [f'base {base}', f'out {nif}'] + fx_lines
        (W / f'{name}_fx.job').write_text('\n'.join(g.fx_fix(L)) + '\n')
        r = subprocess.run([str(pw.TOOLS / 'fxwrite'), str(W / f'{name}_fx.job')], capture_output=True, text=True)
        if r.returncode or 'no node' in r.stderr: print(r.stdout, r.stderr); raise SystemExit('fxwrite failed')
    # a preview OBJ (rig space)
    Mo = Mesh()
    for m, (Vs, Ns, Us, Fs, _) in merged.items(): Mo.add(m.capitalize(), m, np.vstack(Vs), np.vstack(Ns), np.vstack(Us), np.vstack(Fs))
    Mo.write_obj(W / f'{name}.obj')
    lo, hi = Mo.bounds()
    print(f'{nif.name}: x {lo[0]:.1f}..{hi[0]:.1f}  y {lo[1]:.1f}..{hi[1]:.1f}  z {lo[2]:.1f}..{hi[2]:.1f}; {tris} triangles')


def limbs(M, mat, y_ends=(-STRING_Y - 1.5, STRING_Y + 1.5), n=120):
    path = limb_line(y_ends[0], y_ends[1], n)
    wz, tx = limb_profile(path[:, 1])
    M.add('Limb', mat, *sweep(path, wz, tx, 18, 3.0))


def band(M, mat, y, grow=0.18, length=1.0):
    path = limb_line(y - length / 2, y + length / 2, 4)
    wz, tx = limb_profile(np.full(4, y))
    M.add('Band', mat, *sweep(path, wz + grow, tx + grow, 18, 3.5))


def grip(M, mat, y0=-4.8, y1=4.8):
    """leather wound round the grip, its turns standing proud"""
    path = limb_line(y0, y1, 80)
    wz, tx = limb_profile(path[:, 1])
    bump = 0.08 * (np.sin(path[:, 1] * 2 * np.pi / 0.9) > 0)
    M.add('Grip', mat, *sweep(path, wz + 0.12 + bump, tx + 0.12 + bump, 18, 2.6))


def chain_helix(M, mat, y0, y1, around, turns, link_len=1.05, r=0.13, seed=0):
    """a chain wound round a limb: oval links along a helix about it, each turned a quarter from the last"""
    ys = np.linspace(y0, y1, 400)
    line = limb_line(y0, y1, 400)
    T = unit(np.gradient(line, axis=0)); Nn = unit(np.cross([0, 0, 1.0], T))
    wz, tx = limb_profile(ys)
    ph = np.linspace(0, 2 * np.pi * turns, 400)
    pts = line + Nn * ((tx + around) * np.cos(ph))[:, None] + np.array([0, 0, 1.0]) * ((wz + around) * np.sin(ph))[:, None]
    L = np.concatenate([[0], np.cumsum(np.linalg.norm(np.diff(pts, axis=0), axis=1))])
    k = 0
    for d in np.arange(0, L[-1], link_len * 0.82):
        i = int(np.searchsorted(L, d)); i = min(i, len(pts) - 2)
        c = pts[i]; tg = unit(pts[i + 1] - pts[i])
        out = unit(c - line[i])
        ax = out if k % 2 == 0 else unit(np.cross(tg, out))
        P, N, UV, F = torus(c, link_len * 0.36, r, ax, 16, 6, stretch=1.5, long=tg)
        M.add('Chain', mat, P, N, UV, F); k += 1


def vahlok():
    """(Vahlok the Jailor - Noah: "Jailor's bow" in place of the chain flail; its arrows bind with spectral chains) a
    heavy recurve of black ironwood bound in riveted iron bands, its riser a padlock of blackened iron whose keyhole
    glows ghost-blue, a leather grip; iron cuffs locked round both limbs near the tips with broken chains hanging from
    them, and a spectral chain of pale ghost-light wound round each limb from riser to cuff."""
    wD, wN = metal_tex('vahl_wood', (46, 40, 36), dark=0.7, seed=201)
    iD, iN = metal_tex('vahl_iron', (78, 80, 84), rust=0.3, dark=0.85, seed=202)
    lD, lN = leather_tex('vahl_leather', (44, 30, 22), seed=203)
    gD = save(np.full((64, 64, 3), (170, 205, 255), np.float32), 'vahl_ghost_d.dds')
    gG = save(np.full((64, 64, 3), (150, 200, 255), np.float32), 'vahl_ghost_g.dds')
    flat = save(np.dstack([np.full((64, 64), 128.0), np.full((64, 64), 128.0), np.full((64, 64), 255.0), np.full((64, 64), 60.0)]), 'priest_flat_n.dds', True)
    M = Mesh()
    limbs(M, 'wood')
    grip(M, 'leather')
    for y in (-46.0, -38.0, -30.0, -22.0, -14.0, -6.2, 6.2, 14.0, 22.0, 30.0, 38.0, 46.0):
        band(M, 'iron', y, 0.16, 1.1)
        line = limb_line(y, y + 0.01, 2)[0]; wz, tx = limb_profile(np.array([y]))
        for sz in (-1, 1):   # (a rivet each side)
            M.add('Iron', 'iron', *superbox(line + np.array([0, 0, sz * (wz[0] + 0.2)]), 0.22, 0.22, 0.14, 2.2, 10, 6))
    for sy in (-1, 1):   # (iron tips past the nocks, a hook to take the string)
        tip = limb_line(sy * (STRING_Y - 1.0), sy * (STRING_Y + 3.6), 12)
        M.add('Iron', 'iron', *sweep(tip, np.linspace(0.85, 0.25, 12), np.linspace(0.7, 0.2, 12), 14, 3.0))
    # the padlock riser: its body on the bow's front above the grip, its shackle hooped over the limb
    lc = np.array([limb_x(np.array([8.0]))[0] + 1.35, 8.0, 0.0])
    M.add('Iron', 'iron', *superbox(lc, 0.7, 2.3, 1.8, 6.0, 24, 12))
    hc = lc + np.array([-0.4, 2.3, 0])
    a = np.linspace(0, np.pi, 28)
    hoop = np.stack([hc[0] - 0.0 * a, hc[1] + 1.5 * np.sin(a), hc[2] + 1.15 * np.cos(a)], 1)
    M.add('Iron', 'iron', *pw.rod(hoop, 0.28, 10))
    M.add('Iron', 'iron', *superbox(lc + np.array([0, -2.6, 0]), 0.9, 0.35, 1.9, 4.0, 14, 6))   # (its lip, sitting on the grip)
    kc = lc + np.array([0.72, -0.2, 0])   # (the keyhole: a disc and a slot, ghost-light inside)
    M.add('Keyhole', 'ghost', *superbox(kc + np.array([0, 0.35, 0]), 0.05, 0.38, 0.38, 2.0, 12, 8))
    M.add('Keyhole', 'ghost', *superbox(kc + np.array([0, -0.45, 0]), 0.05, 0.6, 0.18, 4.0, 10, 6))
    # iron cuffs locked round the limbs near the tips, two links of broken chain hanging from each
    for sy in (-1, 1):
        y = sy * 43.0
        line = limb_line(y, y + 0.01, 2)[0]; tg = unit(limb_line(y - 0.5, y + 0.5, 2)[1] - limb_line(y - 0.5, y + 0.5, 2)[0])
        wz, tx = limb_profile(np.array([y]))
        M.add('Iron', 'iron', *torus(line, max(wz[0], tx[0]) + 0.45, 0.32, tg, 28, 10))
        prev = line + np.array([0, 0, -(wz[0] + 0.75)])
        for k in range(3):
            c = prev + np.array([0.2 * k, -0.85 if sy > 0 else -0.85, -0.25])
            M.add('Iron', 'iron', *torus(c, 0.42, 0.12, (1, 0, 0) if k % 2 == 0 else (0, 0, 1), 16, 6, stretch=1.5, long=(0, 1, 0)))
            prev = c
    # the spectral chains wound round each limb, riser to cuff
    for sy in (-1, 1):
        y0, y1 = (9.0, 41.5) if sy > 0 else (-41.5, -9.0)
        chain_helix(M, 'ghost', y0, y1, 0.55, 3.5, 1.0, 0.11, seed=sy)
    fx = [f'shape KeyGlow tex={FX["glow"]} color=0.55,0.75,1,0.6 scale=1.0 blend=add billboard=1 at={kc[0] + 0.3:.3f},{kc[1]:.3f},{kc[2]:.3f}',
          'v -2.2 2.2 0 0 0 1 1 1 1', 'v 2.2 2.2 0 1 0 1 1 1 1', 'v 2.2 -2.2 0 1 1 1 1 1 1', 'v -2.2 -2.2 0 0 1 1 1 1 1', 't 0 1 2', 't 0 2 3']
    mats = {'wood': f'diffuse={wD} normal={wN} spec=0.3 gloss=16', 'iron': f'diffuse={iD} normal={iN} spec=0.5 gloss=22',
            'leather': f'diffuse={lD} normal={lN} spec=0.2 gloss=12',
            'ghost': f'diffuse={gD} normal={flat} glow={gG} emit=0.6,0.8,1 emitmult=2.2 alpha=add spec=0.2 gloss=20',
            'string': f'diffuse={lD} normal={lN} spec=0.3 gloss=20'}
    write('vahlok_jailorsbow', M, mats, fx)


def vokun():
    """(Vokun of High Gate - Noah: "Dreamer's bow" in place of the Dreamer's staff; its arrows put enemies to sleep) a
    bow of two black stems twisted round each other, dark-silver bands down them, its tips curling into silver spirals;
    above the grip a crescent moon of dark silver standing out from the bow and cradling an orb of violet dream-smoke;
    dream smoke breathing off the orb and drifting along the limbs."""
    sD, sN = metal_tex('vokb_silver', (150, 146, 160), dark=0.75, seed=211)
    wD, wN = metal_tex('vokb_wood', (34, 28, 34), dark=0.5, seed=212)
    lD, lN = leather_tex('vokb_leather', (36, 28, 40), seed=213)
    oT = np.clip(tileable(256, 256, 0.6, 214), 0, 1)
    oD = save(np.dstack([150 + 60 * oT, 90 + 50 * oT, np.full_like(oT, 240)]), 'vokb_orb_d.dds')
    oG = save(np.dstack([140 + 80 * oT, 70 + 60 * oT, np.full_like(oT, 255)]), 'vokb_orb_g.dds')
    flat = save(np.dstack([np.full((64, 64), 128.0), np.full((64, 64), 128.0), np.full((64, 64), 255.0), np.full((64, 64), 200.0)]), 'priest_gloss_n.dds', True)
    M = Mesh()
    # two stems twisted round the limbs' line
    path = limb_line(-STRING_Y - 1.0, STRING_Y + 1.0, 240)
    T = unit(np.gradient(path, axis=0)); Nn = unit(np.cross([0, 0, 1.0], T))
    wz, tx = limb_profile(path[:, 1])
    for k in range(2):
        ph = path[:, 1] * 0.28 + math.pi * k
        off = 0.55 * (wz + tx) * 0.5
        sp = path + Nn * (off * np.cos(ph))[:, None] + np.array([0, 0, 1.0]) * (off * np.sin(ph))[:, None]
        r_ = 0.62 * (wz + tx) * 0.5 + 0.05
        M.add('Stem', 'wood', *sweep(sp, r_, r_, 12, 2.0))
    grip(M, 'leather')
    for y in (-44.0, -30.0, -16.0, -6.0, 6.0, 16.0, 30.0, 44.0):
        band(M, 'silver', y, 0.32, 0.9)
    for sy in (-1, 1):   # (silver spirals at the tips)
        base = limb_line(sy * STRING_Y, sy * STRING_Y + 0.01, 2)[0]
        a = np.linspace(0, 1.6 * np.pi, 40); rr = 2.2 * (1 - a / (2.2 * np.pi))
        sp = np.stack([base[0] + 2.2 - rr * np.cos(a), base[1] + sy * rr * np.sin(a), np.zeros_like(a)], 1)
        M.add('Silver', 'silver', *pw.rod(sp, np.linspace(0.45, 0.12, 40), 10))
    # the crescent moon above the grip, out from the bow's front, cradling the orb
    oc = np.array([limb_x(np.array([10.0]))[0] + 3.4, 10.0, 0.0])
    a = np.linspace(-2.5, 2.5, 44)
    arc = np.stack([np.full_like(a, oc[0]), oc[1] - 3.0 * np.cos(a), 3.0 * np.sin(a)], 1)
    M.add('Silver', 'silver', *pw.rod(arc, 0.62 * (1 - np.abs(np.linspace(-1, 1, 44)) ** 2) + 0.12, 12))
    M.add('Silver', 'silver', *pw.rod(np.array([[limb_x(np.array([7.0]))[0], 7.0, 0], [oc[0], oc[1] - 3.0, 0]]), 0.45, 10))
    V, F = g.sphere(oc, 1.75, 28, 14)
    M.add('Orb', 'orb', np.array([p for p, uu, vv, n in V]), np.array([n for p, uu, vv, n in V]), np.array([(uu, vv) for p, uu, vv, n in V]), F)
    fx = []
    def card(nm, tex, at, hs, col, sc, flip=None):
        extra = f' flip={flip[0]}x{flip[1]} fps={flip[2]} phase={flip[3]}' if flip else ''
        fx.append(f'shape {nm} tex={tex} color={col} scale={sc} blend=add billboard=1 at={at[0]:.3f},{at[1]:.3f},{at[2]:.3f}{extra}')
        cu, cv = (1 / flip[0], 1 / flip[1]) if flip else (1, 1)
        fx.extend([f'v {-hs} {hs} 0 0 0 1 1 1 1', f'v {hs} {hs} 0 {cu} 0 1 1 1 1', f'v {hs} {-hs} 0 {cu} {cv} 1 1 1 1', f'v {-hs} {-hs} 0 0 {cv} 1 1 1 1', 't 0 1 2', 't 0 2 3'])
    card('OrbCore', FX['core'], oc, 1.9, '0.75,0.55,1,1', 2.0, (4, 4, 7, 0))
    card('OrbHalo', FX['glow'], oc, 5.5, '0.55,0.3,1,0.45', 0.9)
    V, F = g.sphere(oc, 2.3, 24, 12)
    fx.append(f'shape OrbSmoke tex={FX["swirl"]} color=0.6,0.35,1,0.8 scale=2.0 blend=add falloff=1 uscroll=1.5 vscroll=2.5')
    for p, uu, vv, n in V:
        fx.append(f'v {p[0]:.4f} {p[1]:.4f} {p[2]:.4f} {uu:.4f} {vv:.4f}'); fx.append(f'n {n[0]:.4f} {n[1]:.4f} {n[2]:.4f}')
    fx += [f't {a_} {b_} {c_}' for a_, b_, c_ in F]
    # dream smoke drifting along the upper and lower limbs (a sleeve following each)
    for k, (y0, y1) in enumerate(((12.0, 48.0), (-48.0, -12.0))):
        ln = limb_line(y0, y1, 14)
        fx.append(f'shape LimbSmoke{k} tex={FX["swirl"]} color=0.5,0.3,0.85,0.35 scale=1.2 blend=add falloff=1 uscroll=1.2 vscroll={-1.6 if k == 0 else 1.6}')
        nu = 14
        for i, c in enumerate(ln):
            for j in range(nu + 1):
                th = 2 * math.pi * j / nu
                fade = math.sin(math.pi * i / (len(ln) - 1)) ** 1.2
                p = c + np.array([1.6 * math.cos(th), 0, 1.6 * math.sin(th)])
                fx.append(f'v {p[0]:.4f} {p[1]:.4f} {p[2]:.4f} {j / nu * 2:.4f} {i / (len(ln) - 1) * 2:.4f} 1 1 1 {fade:.3f}')
                fx.append(f'n {math.cos(th):.4f} 0 {math.sin(th):.4f}')
        for i in range(len(ln) - 1):
            for j in range(nu):
                a_ = i * (nu + 1) + j; b_ = a_ + 1; c_ = a_ + nu + 1; d_ = c_ + 1
                fx += [f't {a_} {c_} {b_}', f't {b_} {c_} {d_}']
    mats = {'wood': f'diffuse={wD} normal={wN} spec=0.3 gloss=16', 'silver': f'diffuse={sD} normal={sN} spec=0.7 gloss=28',
            'leather': f'diffuse={lD} normal={lN} spec=0.2 gloss=12',
            'orb': f'diffuse={oD} normal={flat} glow={oG} emit=0.7,0.45,1 emitmult=2.0 spec=2.0 gloss=180',
            'string': f'diffuse={sD} normal={sN} spec=0.6 gloss=40'}
    write('vokun_dreamersbow', M, mats, fx)


BOWS = {'vahlok': vahlok, 'vokun': vokun}

if __name__ == '__main__':
    pw.WORK.mkdir(parents=True, exist_ok=True)
    for n in [a for a in sys.argv[1:] if not a.startswith('--')] or list(BOWS):
        BOWS[n]()
