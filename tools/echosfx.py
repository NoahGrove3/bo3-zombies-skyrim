"""(Noah, 2026-11-23: the Echo Altar's trials remade) the trials' own sounds, made here from scratch:
  echo_tone_<0-5>  the six stones of Repeat the Echo: a struck stone bell each, rising through a minor pentatonic
  echo_wrong       a wrong stone: the six tones at once, out of tune, sinking
  echo_sing        the altar begins its song: a low swell under a shimmer
  wisp_rise        Lay the Restless to Rest: a soul rising through the mist - an airy rising shimmer
python3 tools/echosfx.py  ->  assets/sound/un/*.wav (44.1 kHz mono 16-bit; CI copies them to Sound\\fx\\CurseOfAlduin\\un)"""
import pathlib, wave
import numpy as np

SR = 44100
OUT = pathlib.Path(__file__).resolve().parent.parent / 'assets' / 'sound' / 'un'
rng = np.random.default_rng(7)


def write(name, x, peak=0.82):
    x = x / max(1e-9, np.max(np.abs(x))) * peak
    with wave.open(str(OUT / f'{name}.wav'), 'wb') as f:
        f.setnchannels(1), f.setsampwidth(2), f.setframerate(SR)
        f.writeframes((x * 32767).astype('<i2').tobytes())


def bell(f0, secs, detune=1.0):
    """a stone bell: inharmonic partials, each its own decay, a soft strike"""
    t = np.arange(int(SR * secs)) / SR
    parts = [(1.0, 1.0, 1.6), (2.76, 0.45, 2.6), (5.40, 0.22, 4.0), (0.5, 0.30, 1.1), (8.93, 0.08, 6.0)]
    x = np.zeros_like(t)
    for r, a, k in parts:
        x += a * np.sin(2 * np.pi * f0 * r * detune * t + 0.3 * np.sin(2 * np.pi * 4.1 * t)) * np.exp(-k * t)
    strike = rng.standard_normal(t.size) * np.exp(-t * 90) * 0.25
    att = np.minimum(1.0, t / 0.004)
    return (x + strike) * att


def reverb(x, secs=1.2, mix=0.28):
    n = int(SR * secs)
    ir = rng.standard_normal(n) * np.exp(-np.arange(n) / SR * 5.0)
    wet = np.convolve(x, ir)[: x.size + n]
    wet = np.pad(wet, (0, x.size + n - wet.size))
    out = np.concatenate([x, np.zeros(n)])
    return out + mix * wet / max(1e-9, np.max(np.abs(wet))) * np.max(np.abs(x))


# A minor pentatonic from A3: A C D E G A
FREQ = [220.0, 261.63, 293.66, 329.63, 392.0, 440.0]
for i, f in enumerate(FREQ):
    write(f'echo_tone_{i}', reverb(bell(f, 2.2)))

t = np.arange(int(SR * 1.6)) / SR
wrong = sum(bell(f * (1.0 + 0.03 * (i - 2.5)), 1.6) for i, f in enumerate(FREQ))
wrong *= 1.0 - 0.35 * t / t[-1]
bend = np.cumsum(1.0 - 0.18 * t / t[-1]) / SR  # the pitch sinks as it dies
wrong = np.interp(bend * SR, np.arange(wrong.size), wrong)
write('echo_wrong', reverb(wrong, 1.0, 0.35), 0.7)

t = np.arange(int(SR * 2.4)) / SR
swell = np.sin(2 * np.pi * 110 * t) * 0.6 + np.sin(2 * np.pi * 165 * t) * 0.3
swell *= np.sin(np.pi * np.minimum(1.0, t / 2.4)) ** 1.5
shim = sum(np.sin(2 * np.pi * f * 4 * t + rng.uniform(0, 6)) * 0.05 for f in FREQ) * np.minimum(1.0, t / 1.2) * np.exp(-np.maximum(0, t - 1.6) * 3)
write('echo_sing', reverb(swell + shim, 1.5, 0.3), 0.7)

t = np.arange(int(SR * 2.0)) / SR
noise = rng.standard_normal(t.size)
# airy: noise through a resonance sweeping up, and a few glassy partials gliding up with it
from scipy.signal import lfilter
out = np.zeros_like(t)
f = 500 + 2600 * (t / t[-1]) ** 1.4
blk = 512
for s in range(0, t.size, blk):
    fc = f[s]; r = 0.985
    a = [1, -2 * r * np.cos(2 * np.pi * fc / SR), r * r]
    out[s:s + blk] = lfilter([1 - r], a, noise[s:s + blk])
ph = 2 * np.pi * np.cumsum(f * 1.5) / SR
glass = (np.sin(ph) + 0.5 * np.sin(ph * 2.01) + 0.25 * np.sin(ph * 3.02)) * 0.12
env = np.sin(np.pi * t / t[-1]) ** 1.2
write('wisp_rise', reverb((out * 0.9 + glass) * env, 1.2, 0.35), 0.6)
print('ok')
