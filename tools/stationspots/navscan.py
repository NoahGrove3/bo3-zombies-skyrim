"""NAVM triangles of the city worldspaces and around town markers in Tamriel. python3 navscan.py Skyrim.esm out.json"""
import struct, zlib, sys, json, math
data = open(sys.argv[1], 'rb').read()
CITY = {}  # filled from WRLD EDIDs
TOWNS = {'Riverwood': (21941, -44792), 'Falkreath': (-30296, -86284), 'Morthal': (-38640, 66734), 'Dawnstar': (30808, 106138), 'Winterhold': (109208, 102864)}
WANT = ('WhiterunWorld', 'SolitudeWorld', 'WindhelmWorld', 'MarkarthWorld', 'RiftenWorld', 'Tamriel')
wrld = {}
out = {}
def subs(buf):
    i = 0; res = []
    while i + 6 <= len(buf):
        t = buf[i:i+4]; n = struct.unpack_from('<H', buf, i+4)[0]; i += 6
        if t == b'XXXX':
            big = struct.unpack_from('<I', buf, i)[0]; i += n; t = buf[i:i+4]; i += 6; n = big
        res.append((t, buf[i:i+n])); i += n
    return res
bad = 0
def nav(body, w):
    global bad
    for st, sb in subs(body):
        if st != b'NVNM': continue
        try:
            ver, magic, ws = struct.unpack_from('<III', sb, 0); o = 12
            o += 4  # grid or cell
            nv = struct.unpack_from('<I', sb, o)[0]; o += 4
            V = struct.unpack_from('<%df' % (3 * nv), sb, o); o += 12 * nv
            nt = struct.unpack_from('<I', sb, o)[0]; o += 4
            T = []
            for k in range(nt):
                a, b, c = struct.unpack_from('<hhh', sb, o); o += 16
                T.append((a, b, c))
        except Exception:
            bad += 1; return
        name = wrld.get(w, '')
        keep = name != 'Tamriel'
        town = name
        if not keep:
            cx = sum(V[0::3]) / max(1, nv); cy = sum(V[1::3]) / max(1, nv)
            for t, (x, y) in TOWNS.items():
                if math.hypot(cx - x, cy - y) < 4500: keep = True; town = t
        if keep:
            out.setdefault(town, []).append({'v': [round(x, 1) for x in V], 't': T})
def walk(off, end, cw):
    while off < end:
        t = data[off:off+4]
        if t == b'GRUP':
            size, label, gtype = struct.unpack_from('<I4sI', data, off+4)
            w = struct.unpack('<I', label)[0] if gtype == 1 else cw
            walk(off+24, off+size, w); off += size; continue
        size, flags, fid = struct.unpack_from('<III', data, off+4)
        body = data[off+24:off+24+size]
        if t == b'WRLD':
            for st, sb in subs(zlib.decompress(body[4:]) if flags & 0x40000 else body)[:1]:
                if st == b'EDID': wrld[fid] = sb.rstrip(b'\0').decode('latin1')
        elif t == b'NAVM' and wrld.get(cw, '') in WANT:
            if flags & 0x40000: body = zlib.decompress(body[4:])
            nav(body, cw)
        off += 24 + size
walk(0, len(data), None)
json.dump(out, open(sys.argv[2], 'w'))
print({k: len(v) for k, v in out.items()}, 'bad', bad)
