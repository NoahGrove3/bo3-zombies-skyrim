# Scan a Bethesda .esm/.esp (SSE format) and print FormID, type, EditorID for records whose
# type is in TYPES and EditorID matches any PATTERN (case-insensitive substring).
# usage: esmscan.py <file.esm> TYPE[,TYPE] pattern [pattern...]
import struct, sys, zlib
path, types, pats = sys.argv[1], set(sys.argv[2].split(',')), [p.lower() for p in sys.argv[3:]]
d = open(path, 'rb').read()
def subrecs(b):
    i = 0
    while i + 6 <= len(b):
        t = b[i:i+4]; n = struct.unpack_from('<H', b, i+4)[0]; yield t, b[i+6:i+6+n]; i += 6 + n
def walk(off, end):
    while off < end:
        t = d[off:off+4]; size = struct.unpack_from('<I', d, off+4)[0]
        if t == b'GRUP':
            walk(off+24, off+size); off += size; continue
        flags, fid = struct.unpack_from('<II', d, off+8)
        body = d[off+24:off+24+size]
        if t.decode() in types:
            if flags & 0x40000:
                body = zlib.decompress(body[4:])
            edid = full = ''
            for st, sb in subrecs(body):
                if st == b'EDID': edid = sb.rstrip(b'\0').decode('latin1')
                if st == b'FULL' and len(sb) == 4: full = 'lstr:%d' % struct.unpack('<I', sb)[0]
            if not pats or any(p in edid.lower() for p in pats):
                print('%08X %s %s %s' % (fid, t.decode(), edid, full))
        off += 24 + size
hdr = struct.unpack_from('<I', d, 4)[0]
walk(24 + hdr, len(d))
