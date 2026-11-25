# Preview renders for the per-build review doc

Moving previews of the weapons' effect art and the altar trial's soul and ring, rendered from the same jobs `tools/fxwrite`
writes into the game's models (`build/wfx/*.job`, `build/trialfx/*.job`), with the same textures, palettes and timings.

    python3 tools/preview/export.py shared              # the looks' textures and colour ramps
    python3 tools/preview/export.py <art key> ...       # each weapon: its model (lit shapes) and its art
    python3 tools/preview/shoot.py <art key> ...        # -> build/preview/out/<key>.webp (24 frames, 12 fps)
    python3 tools/preview/export_trial.py               # the ring, the soul and the stone's flash
    python3 tools/preview/shoot_trial.py wide|close     # -> build/preview/out/trial_<mode>.webp

The renders need three.js r128 at `$THREE_JS` (default /tmp/three_r128/build/three.min.js) and Playwright's Chromium.
Weapon models come from `assets/fx/Meshes`, then Noah's staged install; textures not in the repo are shown as a flat steel
colour guessed from the texture's name. In game the effects glow a little more, from Skyrim's bloom.
