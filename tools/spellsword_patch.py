"""Builds 'CurseOfAlduin - Spellsword.esp' (ESL-flagged): an override of the Spellsword mod's start quest
(_Spellsword_Start_Quest, "Obtain the Spellsword Manual") with its journal entries and objectives taken out, so it
never shows in the journal. Reads the user's own Spellsword.esp; the output is deployed, never committed.
usage: python3 tools/spellsword_patch.py <Spellsword.esp> <out.esp>"""
import struct, sys, zlib

src, out = sys.argv[1], sys.argv[2]
d = open(src, 'rb').read()
QUEST = 0x009A24

def subs(data):
    o = 0
    while o < len(data):
        t = data[o:o + 4]; l = struct.unpack_from('<H', data, o + 4)[0]
        yield t, data[o + 6:o + 6 + l]; o += 6 + l

def recs(b, off, end):
    while off < end:
        t = b[off:off + 4]
        if t == b'GRUP':
            sz = struct.unpack_from('<I', b, off + 4)[0]; yield from recs(b, off + 24, off + sz); off += sz
        else:
            sz, fl, fid = struct.unpack_from('<III', b, off + 4)
            yield t, b[off:off + 24], b[off + 24:off + 24 + sz]; off += 24 + sz

hdr_size = struct.unpack_from('<I', d, 4)[0]
masters = [v for t, v in subs(d[24:24 + hdr_size]) if t == b'MAST']
found = None
for t, head, data in recs(d, 24 + hdr_size, len(d)):
    if t == b'QUST' and (struct.unpack_from('<I', head, 12)[0] & 0xFFFFFF) == QUEST:
        found = (head, data); break
assert found, 'quest not found'
head, data = found
flags = struct.unpack_from('<I', head, 8)[0]
if flags & 0x40000:
    data = zlib.decompress(data[4:]); flags &= ~0x40000
kept, skip_obj = [], False
for t, v in subs(data):
    if t == b'QOBJ': skip_obj = True
    if skip_obj and t in (b'ANAM', b'ALST', b'ALLS'): skip_obj = False
    if skip_obj: continue
    if t in (b'CNAM',): continue          # stage log entries (the journal's text)
    if t == b'QSDT': v = bytes([v[0] & ~0x01 & 0xFF])  # no stage completes the quest
    kept.append(struct.pack('<4sH', t, len(v)) + v)
body = b''.join(kept)
rec = struct.pack('<4sIIIIHH', b'QUST', len(body), flags, struct.unpack_from('<I', head, 12)[0], 0, 44, 0) + body
grp = struct.pack('<4sI4sIIHH', b'GRUP', 24 + len(rec), b'QUST', 0, 0, 0, 0) + rec

def sub(t, v): return struct.pack('<4sH', t, len(v)) + v
names = masters + [b'Spellsword.esp\x00']
hsubs = sub(b'HEDR', struct.pack('<fII', 1.71, 1, 0x800)) + sub(b'CNAM', b'The Curse of Alduin\x00') + \
        sub(b'SNAM', b'Keeps the Spellsword mod\'s manual quest out of the journal\x00')
for m in names: hsubs += sub(b'MAST', m if m.endswith(b'\x00') else m + b'\x00') + sub(b'DATA', b'\x00' * 8)
tes4 = struct.pack('<4sIIIIHH', b'TES4', len(hsubs), 0x200, 0, 0, 44, 0) + hsubs  # 0x200: ESL
open(out, 'wb').write(tes4 + grp)
print('wrote', out, len(tes4 + grp), 'bytes; masters', [m.rstrip(b'\x00').decode() for m in names])
