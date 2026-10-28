#!/usr/bin/env python3
"""(Noah: "an audio mixing pass") the night's creatures' sounds brought to one loudness.

Each creature sound (SNDR) of the creature packs the night uses is measured by its files' mean level (ffmpeg
volumedetect, measured on Noah's PC: loud.json) less its own static attenuation; any above the target is turned down
by the difference (never up), at most 18 dB. Writes sheets/audiomix.json (gen.py makes it kAudioMix; src/Fantasy.cpp
applies it as the data loads).

    python3 tools/audiomix.py <loud.json> <plugin.esp> ...
"""
import json, pathlib, struct, sys, zlib

TARGET = -21.0   # dB mean, after the sound's own attenuation (Skyrim's creatures sit near here)
CAP = 18.0

def recs(b, o=0, end=None):
    end = end or len(b)
    while o < end:
        t = b[o:o + 4]; sz = struct.unpack('<I', b[o + 4:o + 8])[0]
        if t == b'GRUP':
            yield from recs(b, o + 24, o + sz); o += sz; continue
        fl, fid = struct.unpack('<II', b[o + 8:o + 16]); data = b[o + 24:o + 24 + sz]
        if fl & 0x40000: data = zlib.decompress(data[4:])
        yield t, fid, data; o += 24 + sz

def subs(d):
    o = 0
    while o < len(d):
        n = d[o:o + 4]; s = struct.unpack('<H', d[o + 4:o + 6])[0]; yield n, d[o + 6:o + 6 + s]; o += 6 + s

def masters(b):
    t, fid, data = next(recs(b[:struct.unpack('<I', b[4:8])[0] + 24]))
    return sum(1 for n, v in subs(data) if n == b'MAST')

def main():
    loud = json.load(open(sys.argv[1]))
    rows = []
    for path in sys.argv[2:]:
        b = open(path, 'rb').read()
        nm = masters(b)
        plugin = pathlib.Path(path).name
        for t, fid, data in recs(b):
            if t != b'SNDR' or (fid >> 24) != nm: continue
            files, att, edid = [], 0, ''
            for n, v in subs(data):
                if n == b'EDID': edid = v.rstrip(b'\0').decode(errors='ignore')
                elif n == b'ANAM':
                    f = v.rstrip(b'\0').decode(errors='ignore').lower().replace('/', '\\')
                    for pre in ('data\\sound\\', 'sound\\'):
                        if f.startswith(pre): f = f[len(pre):]
                    files.append(f)
                elif n == b'BNAM' and len(v) >= 6: att = struct.unpack_from('<H', v, 4)[0] / 100.0
            levels = [loud[f][0] for f in files if f in loud]
            if not levels: continue
            eff = max(levels) - att
            extra = min(CAP, eff - TARGET)
            if extra >= 1.0:
                rows.append(dict(plugin=plugin, localId=f'0x{fid & 0xFFFFFF:06X}', edid=edid, measured=round(max(levels), 1), attenuation=att, extraDb=round(extra, 1)))
    out = dict(sheet='audiomix', about='The night creatures\' sounds turned down to one loudness (tools/audiomix.py): extraDb added to each sound\'s static attenuation.',
               columns=['plugin', 'localId', 'edid', 'measured', 'attenuation', 'extraDb'], count=len(rows), rows=rows)
    p = pathlib.Path(__file__).resolve().parent.parent / 'sheets' / 'audiomix.json'
    p.write_text(json.dumps(out, indent=1) + '\n')
    print(f'audiomix: {len(rows)} sounds turned down')

if __name__ == '__main__':
    main()
