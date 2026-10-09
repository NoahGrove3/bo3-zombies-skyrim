"""Scan Skyrim.esm: base EDIDs, worldspaces, REFRs (base, pos, rot) in chosen worldspaces (or near points in Tamriel).
python3 esmscan.py Skyrim.esm out.json"""
import struct, zlib, sys, json, re
data = open(sys.argv[1], 'rb').read()
edid = {}; wrld = {}
refs = []  # (wrld formid, ref formid, base, x,y,z, rz)
def subs(buf):
    i = 0; out = []
    while i + 6 <= len(buf):
        t = buf[i:i+4]; n = struct.unpack_from('<H', buf, i+4)[0]; i += 6
        if t == b'XXXX':
            big = struct.unpack_from('<I', buf, i)[0]; i += n
            t = buf[i:i+4]; i += 6; n = big
        out.append((t, buf[i:i+n])); i += n
    return out
def walk(off, end, cur_wrld):
    while off < end:
        t = data[off:off+4]
        if t == b'GRUP':
            size, label, gtype = struct.unpack_from('<I4sI', data, off+4)
            w = cur_wrld
            if gtype == 1: w = struct.unpack('<I', label)[0]
            walk(off+24, off+size, w); off += size; continue
        size, flags, fid = struct.unpack_from('<III', data, off+4)
        body = data[off+24:off+24+size]
        if flags & 0x40000:
            try: body = zlib.decompress(body[4:])
            except Exception: body = b''
        if t in (b'REFR', b'ACHR'):
            if cur_wrld is not None:
                base = None; pos = None
                for st, sb in subs(body):
                    if st == b'NAME': base = struct.unpack('<I', sb[:4])[0]
                    elif st == b'DATA': pos = struct.unpack('<6f', sb[:24])
                if pos: refs.append((cur_wrld, fid, base, *pos[:3], pos[5]))
        else:
            for st, sb in subs(body)[:2]:
                if st == b'EDID':
                    e = sb.rstrip(b'\0').decode('latin1'); edid[fid] = e
                    if t == b'WRLD': wrld[fid] = e
                    break
        off += 24 + size
walk(0, len(data), None)
out = {'wrld': {hex(k): v for k, v in wrld.items()}, 'refs': [(hex(w), hex(f), edid.get(b, hex(b) if b else '?'), round(x), round(y), round(z), round(rz, 3)) for w, f, b, x, y, z, rz in refs if wrld.get(w, '') in ('WhiterunWorld', 'SolitudeWorld', 'WindhelmWorld', 'MarkarthWorld', 'RiftenWorld', 'Tamriel')]}
json.dump(out, open(sys.argv[2], 'w'))
print(len(refs), 'refs', len(out['refs']), 'kept', len(wrld), 'worlds')
