"""(Noah, 2026-11-24: "for the first trial, collecting the souls sound effect needs to be different. It does not match" - "Whisper +
thunk") what the altar stone sounds like as it takes a soul: a breath of a whisper - a word of the dead, torn out of the altar's
own whispered lines, slowed and blurred until it is only a voice - drawn into the stone, and a heavy muffled thunk as the stone
closes on it, the hall ringing after it. Six of them (src/Curse.cpp picks one at random), written over the six stone tones of the
old Repeat the Echo (its trial is gone - Hold the Light in its place), so no new sound records are needed.

    python3 tools/soulsfx.py   ->   assets/sound/un/echo_tone_<0-5>.wav (44.1 kHz mono 16-bit)"""
import pathlib, wave
import numpy as np

SR = 44100
ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT = ROOT / 'assets' / 'sound' / 'un'
SRC = ROOT / 'assets' / 'sound' / 'whisper'
rng = np.random.default_rng(41)


def read(p):
    with wave.open(str(p)) as w:
        x = np.frombuffer(w.readframes(w.getnframes()), '<i2').astype(float) / 32768
    return x


def write(name, x, peak=0.8):
    x = x / max(1e-9, np.max(np.abs(x))) * peak
    with wave.open(str(OUT / f'{name}.wav'), 'wb') as f:
        f.setnchannels(1), f.setsampwidth(2), f.setframerate(SR)
        f.writeframes((x * 32767).astype('<i2').tobytes())


def onepole(x, fc, high=False):
    a = np.exp(-2 * np.pi * fc / SR)
    y = np.zeros_like(x); s = 0.0
    for i, v in enumerate(x):
        s = (1 - a) * v + a * s; y[i] = s
    return x - y if high else y


def reverb(x, secs, mix, damp=5.0):
    n = int(SR * secs)
    ir = rng.standard_normal(n) * np.exp(-np.arange(n) / SR * damp)
    ir = onepole(ir, 2500.0)
    wet = np.convolve(x, ir)[: x.size + n]
    wet = np.pad(wet, (0, x.size + n - wet.size))
    out = np.concatenate([x, np.zeros(n)])
    return out + mix * wet / max(1e-9, np.max(np.abs(wet))) * np.max(np.abs(x))


def env(x, win=441):
    return np.sqrt(np.convolve(x * x, np.ones(win) / win, 'same'))


def whisper(src, seed):
    """a 0.55 s breath of a whispered word: the loudest stretch of the line, slowed 25% (lower, larger), its breath kept and its
    tone blurred - a voice that isn't saying anything any more"""
    x = read(src)
    e = env(x, 2205)
    L = int(0.42 * SR)
    i = int(np.argmax(np.convolve(e, np.ones(L), 'valid')))
    seg = x[i:i + L]
    # slowed (resampled longer: lower and darker)
    t = np.linspace(0, len(seg) - 1, int(len(seg) * 1.3))
    seg = np.interp(t, np.arange(len(seg)), seg)
    seg = onepole(onepole(seg, 220.0, True), 5200.0)
    # its breath: noise following its loudness
    br = onepole(onepole(rng.standard_normal(seg.size), 900.0, True), 6000.0) * env(seg, 882) * 1.6
    w = seg * 0.8 + br
    n = w.size
    fade = np.minimum(1.0, np.arange(n) / (0.12 * SR)) * np.minimum(1.0, (n - np.arange(n)) / (0.18 * SR))
    # drawn in: it swells as it goes into the stone
    swell = np.linspace(0.55, 1.0, n) ** 1.5
    return w * fade * swell


def thunk(seed):
    """the stone closing on it: a low knock that drops in pitch, a short dull crack on top, the stone's body ringing a moment"""
    r = np.random.default_rng(seed)
    n = int(0.9 * SR)
    t = np.arange(n) / SR
    f = 92.0 * (1 + r.uniform(-0.06, 0.06)) * np.exp(-t * 5.0) + 40.0
    ph = 2 * np.pi * np.cumsum(f) / SR
    low = np.sin(ph) * np.exp(-t * 7.5)
    body = (np.sin(2 * np.pi * 143 * t) * 0.35 + np.sin(2 * np.pi * 231 * t) * 0.18) * np.exp(-t * 22)
    crack = onepole(onepole(r.standard_normal(n), 120.0, True), 1600.0) * np.exp(-t * 140) * 1.4
    att = np.minimum(1.0, t / 0.002)
    return (low * 1.1 + body + crack) * att


if __name__ == '__main__':
    srcs = sorted(SRC.glob('echo_*_s*.wav'))
    picks = [srcs[(k * 7 + 3) % len(srcs)] for k in range(6)]
    for k, src in enumerate(picks):
        w = whisper(src, k)
        th = thunk(100 + k)
        at = int(w.size - 0.08 * SR)   # (the thunk as the whisper goes in)
        out = np.zeros(at + th.size)
        out[:w.size] += w * 0.85
        out[at:at + th.size] += th
        write(f'echo_tone_{k}', reverb(out, 1.3, 0.32))
        print(f'echo_tone_{k}: whisper from {src.name}, {out.size / SR:.2f} s')
