"""The Curse of Alduin SFX: cuts the supplied sound library into game-ready clips (sheet: sfx).

    python tools/sfx.py scan  <library dir>              list every file's takes (non-silent stretches)
    python tools/sfx.py build <library dir> <out dir>    write every clip in sheets/sfx.json as 16-bit PCM WAV

The library files are never modified. Each clip names its source by alias (sheet 'sources'), then picks:
  take: n            the n-th take (silence-separated stretch, -45 dB / 0.25 s)
  t0, t1             or an explicit window in seconds
  split: [k, n]      then the k-th of n parts, cut at the quietest points (one long growl -> several)
and shapes it: gain to a target peak (dB) or a fixed gain (dB), fades (ms), mono or stereo, pitch (semitones), optional seamless
loop (the tail crossfaded into the head, for charge hums, beams and ritual drones), a length cap.
"""
import json, pathlib, subprocess, sys
import numpy as np

ROOT = pathlib.Path(__file__).resolve().parent.parent
SR = 44100


def decode(path, ch=2):
    raw = subprocess.run(['ffmpeg', '-v', 'error', '-i', str(path), '-f', 'f32le', '-ac', str(ch), '-ar', str(SR), '-'],
                         capture_output=True, check=True).stdout
    return np.frombuffer(raw, np.float32).reshape(-1, ch).copy()


def takes(x, floor_db=-45.0, min_gap=0.25):
    """non-silent stretches [(s0, s1)] in samples"""
    env = np.abs(x).max(1)
    win = int(0.02 * SR)
    env = np.convolve(env, np.ones(win) / win, 'same')
    on = env > 10 ** (floor_db / 20)
    out, start, quiet = [], None, 0
    for i in range(0, len(on), win):
        if on[i]:
            if start is None:
                start = i
            quiet = 0
        elif start is not None:
            quiet += win
            if quiet >= min_gap * SR:
                out.append((start, i - quiet + win)); start = None; quiet = 0
    if start is not None:
        out.append((start, len(x)))
    return [(a, b) for a, b in out if b - a > 0.05 * SR]


def split_at_quiet(x, k, n):
    if n <= 1:
        return x
    env = np.sqrt(np.convolve((x ** 2).mean(1), np.ones(int(0.05 * SR)) / int(0.05 * SR), 'same'))
    cuts = [0]
    for j in range(1, n):
        c = int(len(x) * j / n); w = int(len(x) / n * 0.35)
        lo, hi = max(1, c - w), min(len(x) - 1, c + w)
        cuts.append(lo + int(np.argmin(env[lo:hi])))
    cuts.append(len(x))
    return x[cuts[k]:cuts[k + 1]]


def trim(x, floor_db=-50.0):
    env = np.abs(x).max(1); thr = 10 ** (floor_db / 20)
    idx = np.nonzero(env > thr)[0]
    return x[max(0, idx[0] - int(0.005 * SR)):idx[-1] + int(0.02 * SR)] if len(idx) else x


def pitch(x, semis):
    if not semis:
        return x
    f = 2 ** (semis / 12)   # resample (tape-style: lower is slower and darker, which suits the undead)
    n = int(len(x) / f)
    t = np.linspace(0, len(x) - 1, n)
    return np.stack([np.interp(t, np.arange(len(x)), x[:, c]) for c in range(x.shape[1])], 1).astype(np.float32)


def make_loop(x, xf_ms=400):
    xf = min(int(xf_ms * SR / 1000), len(x) // 3)
    head, body, tail = x[:xf], x[xf:len(x) - xf], x[len(x) - xf:]
    r = np.linspace(0, 1, xf)[:, None]
    return np.concatenate([body, tail * (1 - r) + head * r])  # the end flows straight into the start


LOUDER_DB = 3.0   # the whole effects mix a little louder (section 9 of the master list); peaks held under -1 dBFS


def louder(x, db=LOUDER_DB):
    """+db of gain, then a soft knee from -4 dBFS so the loud hits round off under -1 dBFS instead of clipping"""
    x = x * 10 ** (db / 20)
    knee, ceil = 10 ** (-4 / 20), 10 ** (-1 / 20)
    a = np.abs(x)
    over = a > knee
    y = a.copy()
    y[over] = knee + (ceil - knee) * np.tanh((a[over] - knee) / (ceil - knee))
    return np.sign(x) * y


def write_wav(path, x):
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.name.startswith('vo_'):
        x = louder(x)
    pcm = (np.clip(x, -1, 1) * 32767).astype('<i2')
    import wave
    with wave.open(str(path), 'wb') as w:
        w.setnchannels(x.shape[1]); w.setsampwidth(2); w.setframerate(SR); w.writeframes(pcm.tobytes())


def clip(lib, src_file, c, sources=None):
    ch = 2 if c.get('stereo') else 1
    x = decode(lib / src_file, 2)
    if 'onset' in c:   # the k-th step/hit found by its attack (footsteps: quiet takes the silence finder misses)
        m = np.abs(x).max(1); w = int(0.005 * SR); env = np.convolve(m, np.ones(w) / w, 'same')
        th = max(np.percentile(env, 30) * 6, 10 ** (-50 / 20)); pk = []; i = 0
        while i < len(env):
            if env[i] > th:
                j = i + int(np.argmax(env[i:i + int(0.06 * SR)])); pk.append(j); i = j + int(0.18 * SR)
            else:
                i += w
        p = pk[c['onset']]
        x = x[max(0, p - int(0.015 * SR)):p + int(c.get('len', 0.22) * SR)]
    if 'take' in c:
        t = takes(x)
        a, b = t[c['take']]
        x = x[a:b]
    elif 't0' in c:
        x = x[int(c['t0'] * SR):int(c.get('t1', len(x) / SR) * SR)]
    if 'split' in c:
        x = split_at_quiet(x, *c['split'])
    x = trim(x)
    x = pitch(x, c.get('pitch', 0))
    if c.get('maxLen'):
        x = x[:int(c['maxLen'] * SR)]
    if ch == 1:
        x = x.mean(1, keepdims=True)
    if c.get('loop'):
        x = make_loop(x)
    if c.get('layer') and sources is not None:   # a second sound under the first (an element over a wet hit, a shimmer over a crack)
        lc = dict(c['layer']); g = lc.pop('gain', -6)
        x = x * (10 ** (c.get('peak', -3.0) / 20) / (np.abs(x).max() + 1e-9))
        y = clip(lib, sources[lc['src']], {**lc, 'peak': c.get('peak', -3.0) + g, 'stereo': c.get('stereo', False)}, sources)
        n = max(len(x), len(y)); z = np.zeros((n, x.shape[1]), np.float32); z[:len(x)] += x; z[:len(y)] += y[:, :x.shape[1]]
        x = z
    fi, fo = int(c.get('fadeIn', 4) * SR / 1000), int(c.get('fadeOut', 60) * SR / 1000)
    if not c.get('loop'):
        if fi: x[:fi] *= np.linspace(0, 1, fi)[:, None]
        if fo: x[-fo:] *= np.linspace(1, 0, min(fo, len(x)))[:, None]
    if 'gain' in c:   # a fixed gain instead of peak-normalising: clips cut from one performance keep their levels
        return x * 10 ** (c['gain'] / 20)
    peak = np.abs(x).max() + 1e-9
    x *= 10 ** (c.get('peak', -3.0) / 20) / peak
    return x


def main():
    cmd, lib = sys.argv[1], pathlib.Path(sys.argv[2])
    sheet = json.load(open(ROOT / 'sheets/sfx.json'))
    if cmd == 'scan':
        for f in sorted(lib.glob('*.mp3')):
            x = decode(f)
            print(f'{len(x) / SR:7.2f}s  takes ' + ' '.join(f'[{a / SR:.2f}-{b / SR:.2f}]' for a, b in takes(x)) + f'  {f.name}')
        return
    out = pathlib.Path(sys.argv[3])
    src = sheet['sources']
    for c in sheet['rows']:
        if len(sys.argv) > 4 and c['id'] not in sys.argv[4].split(','):
            continue
        x = clip(lib, src[c['src']], c, src)
        write_wav(out / f"{c['id']}.wav", x)
        print(f"{c['id']:28s} {len(x) / SR:6.2f}s {'loop' if c.get('loop') else ''}")


if __name__ == '__main__':
    main()
