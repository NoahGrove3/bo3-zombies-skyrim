"""List or dump records of a Skyrim plugin.   dump.py <plugin> list [TYPE..] | dump <formid hex> <out.json>"""
import struct, zlib, sys, json
d = open(sys.argv[1], 'rb').read()
def walk(off, end):
    while off < end:
        t = d[off:off+4]; size = struct.unpack_from('<I', d, off+4)[0]
        if t == b'GRUP':
            yield from walk(off+24, off+size); off += size; continue
        flags, fid = struct.unpack_from('<II', d, off+8)
        body = d[off+24:off+24+size]
        if flags & 0x40000: body = zlib.decompress(body[4:])
        yield t.decode(), flags & ~0x40000, fid, body
        off += 24 + size
def subs(body):
    i = 0; out = []
    while i < len(body):
        st = body[i:i+4].decode(); n = struct.unpack_from('<H', body, i+4)[0]; out.append([st, body[i+6:i+6+n].hex()]); i += 6+n
    return out
hdr = 24 + struct.unpack_from('<I', d, 4)[0]
if sys.argv[2] == 'list':
    want = set(sys.argv[3:])
    for t, f, fid, body in walk(hdr, len(d)):
        if want and t not in want: continue
        s = dict((k, v) for k, v in subs(body))
        ed = bytes.fromhex(s.get('EDID', '')).rstrip(b'\0').decode('cp1252', 'replace')
        model = bytes.fromhex(s.get('MODL', '')).rstrip(b'\0').decode('cp1252', 'replace') if t in ('PROJ', 'WEAP', 'AMMO') else ''
        print(f'{fid:08X} {t} {ed} {model}')
else:
    target = int(sys.argv[3], 16)
    for t, f, fid, body in walk(hdr, len(d)):
        if fid == target:
            json.dump({'type': t, 'flags': f, 'formid': '%08X' % fid, 'subs': subs(body)}, open(sys.argv[4], 'w')); print(t, [s[0] for s in subs(body)]); break
