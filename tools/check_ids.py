"""For each CommonLib function the plugin calls, pull the address IDs from its body and check they
exist in the Skyrim 1.7.104 Address Library. usage: check_ids.py <commonlib src dir> <versionlib.bin> Class::Func ..."""
import re, struct, sys, pathlib
src, db = pathlib.Path(sys.argv[1]), open(sys.argv[2], 'rb').read()
assert struct.unpack_from('<i', db, 0)[0] == 5
count = struct.unpack_from('<i', db, 92)[0]
def rva(i): return struct.unpack_from('<I', db, 96 + 4 * i)[0] if 0 <= i < count else 0
files = {p: p.read_text(errors='ignore') for p in src.rglob('*.cpp')}
bad = 0
for q in sys.argv[3:]:
    cls, fn = q.split('::')
    found = False
    for p, t in files.items():
        for m in re.finditer(r'\b' + cls + r'::' + fn + r'\s*\(', t):
            # skip calls; require a body start '{' after the closing paren of the signature
            j = t.find('{', m.end()); k = t.find(';', m.end())
            if j < 0 or (0 <= k < j): continue
            depth, e = 0, j
            while e < len(t):
                depth += t[e] == '{'; depth -= t[e] == '}'
                e += 1
                if depth == 0: break
            body = t[j:e]
            ids = re.findall(r'RELOCATION_ID\(\s*(\d+)\s*,\s*(\d+)\s*\)|RelocationID\(\s*(\d+)\s*,\s*(\d+)', body)
            ae = [int(a[1] or a[3]) for a in ids]
            calls = re.findall(r'\b([A-Z][A-Za-z]+)::([A-Z][A-Za-z]+)\(', body)
            found = True
            status = []
            for i in ae:
                ok = rva(i) != 0; bad += not ok
                status.append(f'{i}:{"ok" if ok else "MISSING"}')
            vt = 'vfunc' if 'func(' in body and not ae else ''
            print(f'{q:42s} {p.name:24s} AE ids {status or "-"} {vt}')
    if not found: print(f'{q:42s} (no body found: inline/virtual)')
sys.exit(1 if bad else 0)
