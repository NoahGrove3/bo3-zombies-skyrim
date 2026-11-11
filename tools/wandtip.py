"""The wands' tip (Noah: "the wands shouldn't have an element animation on the wand in hand - only a small compact light blue
orb looking like compact magic that brightens when casting at the very tip of the wand's model"). A little ball of pale blue
magic, ~4 units across with a soft bloom to ~12: camera-facing cards (tools/modelconv/fxwrite), a slow shimmer turning through
it. Hung on the wand's tip by code each frame (src/Classes.cpp TickWandTip), brighter and larger while the cast is held.

    python3 tools/wandtip.py [<Data dir>]      (default: assets/fx)
    ->  <Data>/Meshes/CurseOfAlduin/fx/wandtip.nif, <Data>/Textures/CurseOfAlduin/fx/wandtip_*.dds
"""
import math, pathlib, sys
import numpy as np
import lightorb as L

ROOT = pathlib.Path(__file__).resolve().parent.parent
L.WORK = ROOT / 'build' / 'wandtip'
BLUE = [(0.0, (0.92, 0.98, 1.0)), (0.3, (0.62, 0.86, 1.0)), (0.65, (0.32, 0.62, 1.0)), (1.0, (0.16, 0.36, 0.95))]


def ramp(R):
    xs = [s[0] for s in BLUE]
    return np.stack([np.interp(np.clip(R, 0, 1), xs, [s[1][c] for s in BLUE]) for c in range(3)], -1)


def build(data):
    L.WORK.mkdir(parents=True, exist_ok=True)
    X, Y, R = L.polar_grid(128)
    # the core: a bright, flat-topped little ball, white-blue, a soft limb
    disc = 1 / (1 + np.exp((R - 0.42) / 0.05))
    skirt = np.exp(-np.maximum(R - 0.4, 0) / 0.12) * 0.5 * np.clip((1 - R) / 0.25, 0, 1)
    core = L.save(np.dstack([ramp(R * 0.7), np.clip(disc + skirt * (1 - disc), 0, 1)]), data, 'wandtip_core.dds')
    # the bloom: soft, light blue, nothing at its edge
    a = (0.6 * np.exp(-(R / 0.25) ** 2) + 0.3 * np.exp(-(R / 0.55) ** 2)) * np.clip((1 - R) / 0.3, 0, 1) ** 2
    halo = L.save(np.dstack([ramp(0.35 + 0.65 * R), np.clip(a, 0, 1)]), data, 'wandtip_halo.dds')
    # the shimmer: faint wisps round it, turning (polar: u round, v out)
    h, w = 128, 512
    f = L.tileable(h, w, 1.4, 11, aniso=(1.0, 0.12))
    v = (np.arange(h, dtype=np.float32)[:, None] + 0.5) / h
    sh = np.clip((f - 0.4) * 2.2, 0, 1) * np.clip(1 - v, 0, 1) ** 1.5 * np.clip(v / 0.15, 0, 1)
    wisp = L.save(L.white(np.clip(sh, 0, 1)), data, 'wandtip_wisp.dds')
    out = pathlib.Path(data) / 'Meshes' / 'CurseOfAlduin' / 'fx' / 'wandtip.nif'
    out.parent.mkdir(parents=True, exist_ok=True)
    hs = L.Shape('WT_halo', halo, 0.7, billboard=True, col=(0.55, 0.8, 1.0)); L.quad(hs, 6.0, 6.0)
    ws = L.Shape('WT_wisp', wisp, 0.8, billboard=True, col=(0.6, 0.85, 1.0)); ws.uscroll = 3.0
    L.polar(ws, 0.5, 4.2, lambda t: (0.7, 0.9, 1.0, float(np.clip(1 - t, 0, 1))), urep=2, na=48, nr=6)
    cs = L.Shape('WT_core', core, 1.5, billboard=True); L.quad(cs, 2.0, 2.0)
    L.fx([f'out {out}'] + hs.lines() + ws.lines() + cs.lines(), 'wandtip')
    return out


if __name__ == '__main__':
    print(build(sys.argv[1] if len(sys.argv) > 1 else str(ROOT / 'assets' / 'fx')))
