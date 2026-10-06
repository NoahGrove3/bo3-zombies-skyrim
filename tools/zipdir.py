"""Zip a folder with forward-slash entry names (what mod managers and Melty expect)."""
import sys, zipfile, pathlib
src, out = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
only = sys.argv[sys.argv.index('--only') + 1] if '--only' in sys.argv else None
with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED) as z:
    for p in sorted(src.rglob('*')):
        if p.is_file() and (only is None or p.name == only):
            z.write(p, p.relative_to(src).as_posix())
print(out, out.stat().st_size)
