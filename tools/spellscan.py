"""Inventory the attack spells in spell mods (for the Coffer pool).

    python3 tools/spellscan.py <out.json> <master.esm>... -- <mod.esp>...

Reads SPEL and MGEF records (masters first, so a mod's spell can use a vanilla effect), keeps player spells
(type Spell, a name, a cost) whose effects are hostile and hurt Health (or are a damaging rune / cloak / storm),
and records for each: plugin, form id (local), editor id, name, school, element (fire/frost/shock/poison/sun/
arcane/blood/...), cast type, delivery, cost, charge time and its effects' magnitudes.
"""
import json, struct, sys, zlib, pathlib

AV = {20: 'destruction', 18: 'alteration', 19: 'conjuration', 21: 'illusion', 22: 'restoration'}
RESIST = {41: 'fire', 42: 'shock', 43: 'frost', 44: 'arcane', 40: 'poison', 45: 'disease'}
CAST = {0: 'constant', 1: 'fire-and-forget', 2: 'concentration', 3: 'scroll'}
DELIV = {0: 'self', 1: 'contact', 2: 'aimed', 3: 'target-actor', 4: 'target-location'}


def subrecs(b):
    i = 0
    big = None
    while i + 6 <= len(b):
        t = b[i:i + 4]
        n = struct.unpack_from('<H', b, i + 4)[0]
        if t == b'XXXX':
            big = struct.unpack_from('<I', b, i + 6)[0]
            i += 10
            continue
        if big is not None:
            n, big = big, None
        yield t, b[i + 6:i + 6 + n]
        i += 6 + n


def records(path, want):
    d = pathlib.Path(path).read_bytes()
    hdr = struct.unpack_from('<I', d, 4)[0]
    tes4 = d[24:24 + hdr]
    flags = struct.unpack_from('<I', d, 8)[0]
    masters = [sb.rstrip(b'\0').decode('latin1') for st, sb in subrecs(tes4) if st == b'MAST']
    out = []

    def walk(off, end):
        while off < end:
            t = d[off:off + 4]
            size = struct.unpack_from('<I', d, off + 4)[0]
            if t == b'GRUP':
                walk(off + 24, off + size)
                off += size
                continue
            rf, fid = struct.unpack_from('<II', d, off + 8)
            if t in want:
                body = d[off + 24:off + 24 + size]
                if rf & 0x40000:
                    body = zlib.decompress(body[4:])
                out.append((t.decode(), fid, list(subrecs(body))))
            off += 24 + size
    walk(24 + hdr, len(d))
    return out, masters, bool(flags & 0x80)


def main():
    a = sys.argv[1:]
    out_path = a[0]
    k = a.index('--')
    masters, mods = a[1:k], a[k + 1:]
    load = [pathlib.Path(m) for m in masters + mods]
    names = [p.name for p in load]
    mgef = {}   # global key (plugin, local id) -> info
    spells = []
    for p in load:
        recs, mast, loc = records(p, {b'MGEF', b'SPEL'})
        def key(fid):
            idx = fid >> 24
            owner = mast[idx] if idx < len(mast) else p.name
            return (owner, fid & 0xFFFFFF)
        for t, fid, subs in recs:
            s = dict()
            effs = []
            for st, sb in subs:
                if st == b'EFID':
                    effs.append([key(struct.unpack('<I', sb)[0]), None])
                elif st == b'EFIT' and effs:
                    effs[-1][1] = struct.unpack_from('<fII', sb)
                elif st not in s:
                    s[st] = sb
            edid = s.get(b'EDID', b'').rstrip(b'\0').decode('latin1')
            full = s.get(b'FULL', b'')
            name = full.rstrip(b'\0').decode('latin1', 'replace') if (full and not loc) else ''
            if t == 'MGEF' and b'DATA' in s:
                dt = s[b'DATA']
                flags, base = struct.unpack_from('<If', dt, 0)
                skill, resist = struct.unpack_from('<ii', dt, 12)
                archetype, pav, proj, expl, ctype, deliv = struct.unpack_from('<IiIIII', dt, 64)
                mgef[key(fid)] = dict(edid=edid, hostile=bool(flags & 1), skill=AV.get(skill, str(skill)), resist=RESIST.get(resist, ''),
                                      archetype=archetype, av=pav, proj=proj, expl=expl, cast=CAST.get(ctype), delivery=DELIV.get(deliv),
                                      kw=[])
            elif t == 'SPEL' and p.name in mods and b'SPIT' in s:
                cost, sflags, stype, charge, ctype, deliv = struct.unpack_from('<IIIfII', s[b'SPIT'], 0)
                spells.append(dict(plugin=p.name, id=fid & 0xFFFFFF, edid=edid, name=name, type=stype, cost=cost, flags=sflags, charge=round(charge, 2),
                                   cast=CAST.get(ctype), delivery=DELIV.get(deliv), effects=[(k_, e) for k_, e in effs]))
    rows = []
    for sp in spells:
        if sp['type'] != 0 or not sp['name']:
            continue
        eff = []
        hurts = False
        school = elem = ''
        for k_, e in sp['effects']:
            m = mgef.get(k_)
            if not m:
                continue
            mag, area, dur = e if e else (0, 0, 0)
            eff.append(dict(mgef=m['edid'], magnitude=mag, area=area, duration=dur, resist=m['resist'], av=m['av'], hostile=m['hostile']))
            if m['hostile'] and m['archetype'] in (0, 34, 18, 17) and m['av'] == 24:  # value modifier / dual / absorb on Health
                hurts = True
                school = school or m['skill']
                elem = elem or m['resist']
            if m['hostile'] and m['av'] == 24:
                hurts = True
                school = school or m['skill']
                elem = elem or m['resist']
        if not hurts:
            continue
        rows.append(dict(plugin=sp['plugin'], id='%06X' % sp['id'], edid=sp['edid'], name=sp['name'], school=school, element=elem or 'arcane',
                         cast=sp['cast'], delivery=sp['delivery'], cost=sp['cost'], charge=sp['charge'], effects=eff))
    json.dump(rows, open(out_path, 'w'), indent=1)
    from collections import Counter
    print(len(rows), 'attack spells')
    print(Counter(r['plugin'] for r in rows))
    print(Counter(r['element'] for r in rows))


if __name__ == '__main__':
    main()
