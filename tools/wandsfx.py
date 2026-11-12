"""(Noah: the wands' cast "a sound effect while casting that matches the pulsing animation" - "Deep arcane thrum": a low
humming pulse with a soft pop on release) four takes, each a little different, synthesized from nothing.
python3 tools/wandsfx.py  ->  assets/sound/un/wand_thrum_<0-3>.wav"""
import wave, pathlib
import numpy as np
from scipy.signal import lfilter
SR = 44100
rng = np.random.default_rng(23)
def t_(d): return np.arange(int(d * SR)) / SR
def lp(x, fc):
    a = np.exp(-2 * np.pi * fc / SR)
    for _ in range(2): x = lfilter([1 - a], [1, -a], x)
    return x
def take(k):
    dur = 0.55
    t = t_(dur)
    f0 = 58.0 * (1.0 + 0.04 * (k - 1.5))                 # (a low E-ish hum, each take a hair apart)
    wob = 1.0 + 0.012 * np.sin(2 * np.pi * 7.5 * t)      # (the arcane waver)
    ph = 2 * np.pi * np.cumsum(f0 * wob) / SR
    hum = np.sin(ph) + 0.55 * np.sin(2 * ph + 0.3) + 0.22 * np.sin(3 * ph + 1.1) + 0.08 * np.sin(5 * ph)
    # its swell and fall: up in 70 ms (the tip flaring), held a breath, gone by half a second
    env = np.minimum(1.0, t / 0.07) * np.exp(-np.maximum(0.0, t - 0.09) / 0.16)
    body = hum * env
    # a faint airy shimmer over it (filtered noise breathing with the hum)
    air = lp(rng.uniform(-1, 1, len(t)), 900.0) * env * 0.18 * (0.6 + 0.4 * np.sin(ph * 0.5) ** 2)
    # the soft pop of release: a short low thump and a muffled click at 80 ms
    pop = np.zeros(len(t)); i0 = int(0.08 * SR); n = int(0.06 * SR)
    tp = np.arange(n) / SR
    pop[i0:i0 + n] = (np.sin(2 * np.pi * (140 - 900 * tp) * tp) * np.exp(-tp / 0.012) * 0.9
                      + lp(rng.uniform(-1, 1, n), 2500.0) * np.exp(-tp / 0.004) * 0.5)
    s = body + air + pop
    s = s - lp(s, 30.0)                                    # (no rumble below hearing)
    s *= np.minimum(1.0, (len(t) - np.arange(len(t))) / (0.03 * SR))   # (a clean tail)
    return s / np.max(np.abs(s)) * 0.82
out = pathlib.Path(__file__).resolve().parent.parent / 'assets' / 'sound' / 'un'
out.mkdir(parents=True, exist_ok=True)
for k in range(4):
    s = (take(k) * 32767).astype('<i2')
    with wave.open(str(out / f'wand_thrum_{k}.wav'), 'wb') as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(SR); w.writeframes(s.tobytes())
print('wrote', [f'wand_thrum_{k}.wav' for k in range(4)])
