"""Make a Papyrus script from another (compiled) one by renaming strings in its string table: references in a .pex
are string-table indices, so strings may change length freely. Used for the MCM script (a bare MCM_ConfigBase).

    python tools/pexrename.py <in.pex> <out.pex> OldName=NewName [...]      (no args after in.pex: list the strings)
"""
import struct, sys


def read(path):
    d = open(path, 'rb').read()
    i = 0
    magic, maj, mnr, game, ctime = struct.unpack_from('>IBBHQ', d, 0); i = 16
    def wstr():
        nonlocal i
        n = struct.unpack_from('>H', d, i)[0]; s = d[i + 2:i + 2 + n]; i += 2 + n
        return s
    head_strs = [wstr() for _ in range(3)]  # source file, user, machine
    head_end = i
    n = struct.unpack_from('>H', d, i)[0]; i += 2
    table = [wstr() for _ in range(n)]
    return d, head_end, table, i


def main():
    src = sys.argv[1]
    d, head_end, table, rest = read(src)
    if len(sys.argv) < 3:
        for k, s in enumerate(table):
            print(k, s)
        return
    out, pairs = sys.argv[2], dict(a.split('=', 1) for a in sys.argv[3:])
    table = [pairs.get(s.decode('latin1'), s.decode('latin1')).encode('latin1') for s in table]
    # the header's source file name too
    i = 16
    hs = []
    for _ in range(3):
        n = struct.unpack_from('>H', d, i)[0]; s = d[i + 2:i + 2 + n].decode('latin1'); i += 2 + n
        for a, b in pairs.items():
            s = s.replace(a, b)
        hs.append(s.encode('latin1'))
    b = bytearray(d[:16])
    for s in hs:
        b += struct.pack('>H', len(s)) + s
    b += struct.pack('>H', len(table))
    for s in table:
        b += struct.pack('>H', len(s)) + s
    b += d[rest:]
    open(out, 'wb').write(bytes(b))
    print('wrote', out, len(b), 'bytes')


main()
