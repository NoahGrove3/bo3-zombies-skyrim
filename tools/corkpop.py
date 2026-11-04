"""(Noah: "make the throw sound a cork popping") the cork pulled as the flask is drawn back to throw: no cork pop in Skyrim's
own sounds or the installed mods' - so made here, kept short and dry (not the old 'toony' fizz): the cork's friction squeak
in the neck, then the pop - a hard transient, the neck's hollow resonance (rising as the air rushes in, gone in a few
hundredths of a second), a puff of air - and a little room. Three takes.

    python3 tools/corkpop.py  ->  assets/sound/un/alch_corkpop_<k>.wav (48 kHz mono)"""
import pathlib, wave
import numpy as np

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT = ROOT / 'assets' / 'sound' / 'un'
SR = 48000


def bp(x, lo, hi):
    X = np.fft.rfft(x); f = np.fft.rfftfreq(len(x), 1 / SR)
    X *= np.clip((f - lo * 0.7) / (lo * 0.3), 0, 1) * np.clip((hi * 1.3 - f) / (hi * 0.3), 0, 1)
    return np.fft.irfft(X, len(x))


def take(k, rng):
    n = int(0.42 * SR); s = np.zeros(n); t = np.arange(n) / SR
    at = 0.06 + 0.015 * k                                    # the pop's moment, after the squeak
    # the squeak: a few rough, scratchy grains of cork on glass
    for g in range(5 + k):
        g0 = int(rng.uniform(0.0, at - 0.01) * SR); L = int(rng.uniform(0.006, 0.014) * SR)
        f = rng.uniform(1600, 2600); tt = np.arange(L) / SR
        grain = np.sin(2 * np.pi * f * tt + rng.uniform(0, 6)) * np.sin(np.pi * tt / tt[-1]) ** 2
        grain += bp(rng.standard_normal(L), 1500, 6000) * 0.6
        s[g0:g0 + L] += grain * rng.uniform(0.03, 0.07)
    i = int(at * SR); L = int(0.09 * SR); tt = np.arange(L) / SR
    click = bp(rng.standard_normal(L), 600, 9000) * np.exp(-tt / 0.0012)                       # the cork leaving the lip
    f0 = 330 + 40 * k                                                                           # the neck's hollow ring, rising
    ring = np.sin(2 * np.pi * (f0 * tt + 900 * tt ** 2)) * np.exp(-tt / 0.018) * (1 - np.exp(-tt / 0.0006))
    ring += 0.25 * np.sin(2 * np.pi * (2.02 * f0 * tt + 1800 * tt ** 2)) * np.exp(-tt / 0.010)
    puff = bp(rng.standard_normal(L), 400, 3500) * np.exp(-tt / 0.012) * 0.35                   # the air
    s[i:i + L] += click * 1.0 + ring * 0.85 + puff
    # a small room (a short, dark tail)
    ir_n = int(0.16 * SR); ir = bp(rng.standard_normal(ir_n), 200, 4000) * np.exp(-np.arange(ir_n) / SR / 0.04) * 0.06
    ir[0] = 1.0
    s = np.convolve(s, ir)[:n]
    s *= 0.7 / np.max(np.abs(s))
    fade = np.ones(n); fade[-int(0.05 * SR):] = np.linspace(1, 0, int(0.05 * SR)); s *= fade
    return s


if __name__ == '__main__':
    rng = np.random.default_rng(7)
    OUT.mkdir(parents=True, exist_ok=True)
    for k in range(3):
        s = take(k, rng)
        with wave.open(str(OUT / f'alch_corkpop_{k}.wav'), 'wb') as w:
            w.setnchannels(1); w.setsampwidth(2); w.setframerate(SR)
            w.writeframes((s * 32767).astype('<i2').tobytes())
        print('alch_corkpop', k)
