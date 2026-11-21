"""a file read out of a Skyrim SE archive (.bsa, v105) arrives LZ4-framed after its 4-byte original size: unpacked here (no lz4
module needed). Used to bring the player's own Skyrim models and textures into builds - never committed as they are.

    python3 tools/bsalz4.py <in.raw|in.lz> <out>"""
import struct, sys


def unpack_block(src, out):
    i, n = 0, len(src)
    while i < n:
        tok = src[i]; i += 1
        lit = tok >> 4
        if lit == 15:
            while True:
                b = src[i]; i += 1; lit += b
                if b != 255: break
        out += src[i:i + lit]; i += lit
        if i >= n: break
        off = src[i] | (src[i + 1] << 8); i += 2
        ml = tok & 15
        if ml == 15:
            while True:
                b = src[i]; i += 1; ml += b
                if b != 255: break
        ml += 4
        st = len(out) - off
        if off >= ml:
            out += out[st:st + ml]
        else:   # (an overlapping match: the run repeats)
            pat = bytes(out[st:])
            out += (pat * (ml // off + 1))[:ml]


def unpack(d):
    orig = struct.unpack('<I', d[:4])[0]; fr = d[4:]
    assert struct.unpack('<I', fr[:4])[0] == 0x184D2204, 'not an LZ4 frame'
    flg = fr[4]; p = 6 + (8 if flg & 0x08 else 0) + 1
    out = bytearray()
    while True:
        bs = struct.unpack('<I', fr[p:p + 4])[0]; p += 4
        if bs == 0: break
        unc = bs & 0x80000000; bs &= 0x7FFFFFFF
        blk = fr[p:p + bs]; p += bs
        if unc: out += blk
        else: unpack_block(blk, out)
        if flg & 0x10: p += 4
    assert len(out) == orig, (len(out), orig)
    return bytes(out)


if __name__ == '__main__':
    open(sys.argv[2], 'wb').write(unpack(open(sys.argv[1], 'rb').read()))
    print(sys.argv[2])
