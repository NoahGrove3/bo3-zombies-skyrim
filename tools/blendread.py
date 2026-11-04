"""A small reader for .blend files (Blender 2.8x, little-endian, 64-bit pointers): the file's own SDNA (its struct catalogue)
read first, then each mesh object's vertices, faces, UVs and material slots pulled out and written as OBJ (one per object,
in world space via the object's matrix). Enough for a downloaded prop with no FBX/OBJ beside it.

    python3 tools/blendread.py <in.blend> <out dir>      ->  <out dir>/<object>.obj (+ mats.txt: object -> material names)"""
import pathlib, struct, sys


class Blend:
    def __init__(self, path):
        self.d = d = pathlib.Path(path).read_bytes()
        assert d[:7] == b'BLENDER', 'not a .blend'
        self.ptr = 8 if d[7:8] == b'-' else 4
        assert d[8:9] == b'v', 'big-endian files not read'
        off = 12
        self.blocks = []   # (code, sdna index, count, old address, data offset, size)
        while off < len(d):
            code = d[off:off + 4]
            size, = struct.unpack_from('<i', d, off + 4)
            old, = struct.unpack_from('<Q' if self.ptr == 8 else '<I', d, off + 8)
            sdna, count = struct.unpack_from('<ii', d, off + 8 + self.ptr)
            data = off + 16 + self.ptr
            self.blocks.append((code, sdna, count, old, data, size))
            if code == b'ENDB': break
            off = data + size
        self.byaddr = {b[3]: b for b in self.blocks}
        self._dna(next(b for b in self.blocks if b[0] == b'DNA1'))

    def _dna(self, b):
        d, o = self.d, b[4]
        def align(o): return (o + 3) & ~3
        assert d[o:o + 4] == b'SDNA'; o += 4
        def names(tag):
            nonlocal o
            assert d[o:o + 4] == tag, tag; o += 4
            n, = struct.unpack_from('<i', d, o); o += 4
            out = []
            for _ in range(n):
                e = d.index(b'\0', o); out.append(d[o:e].decode()); o = e + 1
            o = align(o)
            return out
        self.names = names(b'NAME')
        self.types = names(b'TYPE')
        assert d[o:o + 4] == b'TLEN'; o += 4
        self.tlen = list(struct.unpack_from(f'<{len(self.types)}h', d, o)); o = align(o + 2 * len(self.types))
        assert d[o:o + 4] == b'STRC'; o += 4
        n, = struct.unpack_from('<i', d, o); o += 4
        self.structs = []   # (type index, [(type index, name index)])
        for _ in range(n):
            t, nf = struct.unpack_from('<hh', d, o); o += 4
            f = [struct.unpack_from('<hh', d, o + 4 * i) for i in range(nf)]; o += 4 * nf
            self.structs.append((t, f))
        self.sidx = {self.types[s[0]]: i for i, s in enumerate(self.structs)}
        self.layout = {}

    def fsize(self, t, name):
        if name.startswith('*') or name.startswith('(*'):
            base = self.ptr
        else:
            base = self.tlen[t]
        mult = 1
        import re
        for m in re.findall(r'\[(\d+)\]', name): mult *= int(m)
        return base * mult

    def fields(self, sname):
        """{field name (bare): (offset, type name, full name)}"""
        if sname in self.layout: return self.layout[sname]
        t, f = self.structs[self.sidx[sname]]
        out, o = {}, 0
        for ft, fn in f:
            name = self.names[fn]
            bare = name.lstrip('*(').split('[')[0].split(')')[0]
            out[bare] = (o, self.types[ft], name)
            o += self.fsize(ft, name)
        self.layout[sname] = out
        return out

    def get(self, sname, base, field, fmt=None):
        o, tname, full = self.fields(sname)[field]
        if full.startswith('*'):
            return struct.unpack_from('<Q' if self.ptr == 8 else '<I', self.d, base + o)[0]
        if fmt: return struct.unpack_from(fmt, self.d, base + o)
        return base + o

    def block(self, addr):
        return self.byaddr.get(addr)

    def sname(self, b): return self.types[self.structs[b[1]][0]]


def main(src, out):
    B = Blend(src)
    out = pathlib.Path(out); out.mkdir(parents=True, exist_ok=True)
    mats = []
    for b in B.blocks:
        if b[0] != b'OB\0\0': continue
        base = b[4]
        name = B.d[B.get('Object', base, 'id') + B.fields('ID')['name'][0]:][:66].split(b'\0')[0].decode(errors='replace')[2:]
        data = B.get('Object', base, 'data')
        mb = B.block(data)
        if not mb or B.sname(mb) != 'Mesh': continue
        M = B.get('Object', base, 'obmat', '<16f')  # (column-major 4x4)
        m = mb[4]
        totvert, = B.get('Mesh', m, 'totvert', '<i')
        totpoly, = B.get('Mesh', m, 'totpoly', '<i')
        totloop, = B.get('Mesh', m, 'totloop', '<i')
        vb = B.block(B.get('Mesh', m, 'mvert')); pb = B.block(B.get('Mesh', m, 'mpoly')); lb = B.block(B.get('Mesh', m, 'mloop'))
        vsz = vb[5] // vb[2]; psz = pb[5] // pb[2]; lsz = lb[5] // lb[2]
        fo = B.fields('MPoly'); lo = B.fields('MLoop')
        V = []
        for i in range(totvert):
            x, y, z = struct.unpack_from('<3f', B.d, vb[4] + i * vsz)
            V.append((M[0] * x + M[4] * y + M[8] * z + M[12], M[1] * x + M[5] * y + M[9] * z + M[13], M[2] * x + M[6] * y + M[10] * z + M[14]))
        L = [struct.unpack_from('<I', B.d, lb[4] + i * lsz + lo['v'][0])[0] for i in range(totloop)]
        # the UVs: the loop custom-data layer of type CD_MLOOPUV (16)
        UV = None
        ld = B.get('Mesh', m, 'ldata')
        cd = B.fields('CustomData')
        lay = B.block(struct.unpack_from('<Q', B.d, ld + cd['layers'][0])[0])
        if lay:
            cl = B.fields('CustomDataLayer'); lsz2 = lay[5] // lay[2]
            for k in range(lay[2]):
                lo2 = lay[4] + k * lsz2
                ty, = struct.unpack_from('<i', B.d, lo2 + cl['type'][0])
                if ty == 16:
                    ub = B.block(struct.unpack_from('<Q', B.d, lo2 + cl['data'][0])[0])
                    usz = ub[5] // ub[2]
                    UV = [struct.unpack_from('<2f', B.d, ub[4] + i * usz) for i in range(totloop)]
                    break
        # material slot names
        mnames = []
        mat = B.get('Mesh', m, 'mat'); totcol, = B.get('Mesh', m, 'totcol', '<h')
        mab = B.block(mat)
        for k in range(totcol if mab else 0):
            a, = struct.unpack_from('<Q', B.d, mab[4] + 8 * k)
            x = B.block(a)
            mnames.append(B.d[x[4] + B.fields('ID')['name'][0]:][:66].split(b'\0')[0].decode(errors='replace')[2:] if x else f'mat{k}')
        lines = [f'o {name}']
        lines += [f'v {x:.6f} {y:.6f} {z:.6f}' for x, y, z in V]
        if UV: lines += [f'vt {u:.6f} {v:.6f}' for u, v in UV]
        cur = None
        for i in range(totpoly):
            ls, tl = struct.unpack_from('<ii', B.d, pb[4] + i * psz + fo['loopstart'][0])
            mi, = struct.unpack_from('<h', B.d, pb[4] + i * psz + fo['mat_nr'][0])
            mn = mnames[mi] if mi < len(mnames) else f'mat{mi}'
            if mn != cur: lines.append(f'usemtl {mn}'); cur = mn
            idx = range(ls, ls + tl)
            lines.append('f ' + ' '.join(f'{L[j] + 1}/{j + 1}' if UV else f'{L[j] + 1}' for j in idx))
        (out / f'{name}.obj').write_text('\n'.join(lines) + '\n')
        mats.append(f'{name}: {", ".join(mnames)}')
        print(name, totvert, 'verts', totpoly, 'faces', 'uv' if UV else 'no uv', mnames)
    (out / 'mats.txt').write_text('\n'.join(mats) + '\n')


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
