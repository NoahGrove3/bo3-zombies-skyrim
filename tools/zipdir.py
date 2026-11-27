"""Zip a folder with forward-slash entry names (what mod managers and Melty expect).
  --only <name>        only files of that name
  --under <prefix>     only entries under that path (e.g. Data/Textures/)
  --skip <prefix>      everything but entries under that path
(Nov 27: one release file must stay under GitHub's 2 GB - the textures go in a zip of their own)"""
import sys, zipfile, pathlib
src, out = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
arg = lambda k: sys.argv[sys.argv.index(k) + 1] if k in sys.argv else None
only, under, skip = arg('--only'), arg('--under'), arg('--skip')
with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED) as z:
    for p in sorted(src.rglob('*')):
        if not p.is_file() or (only is not None and p.name != only): continue
        name = p.relative_to(src).as_posix()
        if under is not None and not name.lower().startswith(under.lower()): continue
        if skip is not None and name.lower().startswith(skip.lower()): continue
        z.write(p, name)
print(out, out.stat().st_size)
