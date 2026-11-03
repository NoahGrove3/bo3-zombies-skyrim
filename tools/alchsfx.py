"""(Noah: "it still makes dagger sound effects when drawing ... and swinging/attacking. The alchemist class should have its own
combat noises not steel sword sound fx") the Alchemist's flask sounds, made here from scratch (no samples, no paid service):
glass and liquid. Ringing glass is a few inharmonic partials (a thin-walled bottle's modes) with a fast strike and a short
decay; the liquid a band of noise swelling and sloshing; a shatter a hard crack of bright noise over a scatter of tiny
tinkles; the throw a soft cloth-and-air whoosh with the slosh inside it.

    python3 tools/alchsfx.py  ->  assets/sound/un/alch_*.wav (44.1 kHz mono 16-bit; CI copies them to Sound\\fx\\CurseOfAlduin\\un)"""
import pathlib, wave
import numpy as np

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT = ROOT / 'assets' / 'sound' / 'un'
SR = 44100
rng = np.random.default_rng(1771)


def t_(sec):
    return np.arange(int(sec * SR)) / SR


def bandnoise(sec, lo, hi):
    n = rng.standard_normal(int(sec * SR))
    f = np.fft.rfft(n)
    fr = np.fft.rfftfreq(len(n), 1 / SR)
    f[(fr < lo) | (fr > hi)] = 0
    return np.fft.irfft(f, len(n))


def clink(base, sec=0.9, amp=1.0, bright=1.0):
    t = t_(sec)
    s = np.zeros_like(t)
    for ratio, a, d in ((1.0, 1.0, 7.0), (2.32, 0.55 * bright, 11.0), (3.91, 0.32 * bright, 16.0), (5.43, 0.18 * bright, 23.0), (7.1, 0.1 * bright, 30.0)):
        f = base * ratio * (1 + rng.uniform(-0.004, 0.004))
        s += a * np.sin(2 * np.pi * f * t + rng.uniform(0, 6.28)) * np.exp(-d * t)
    tick = bandnoise(sec, 3000, 12000) * np.exp(-140 * t) * 0.35
    return (s + tick) * amp


def slosh(sec, amp=1.0, rate=7.0):
    t = t_(sec)
    body = bandnoise(sec, 180, 1400)
    env = np.clip(np.sin(np.pi * t / sec), 0, 1) ** 1.5 * (0.55 + 0.45 * np.sin(2 * np.pi * rate * t + 1.3) ** 2)
    bubbles = np.zeros_like(t)
    for _ in range(int(sec * 18)):
        at = rng.uniform(0, sec * 0.9); f = rng.uniform(600, 1600); i = int(at * SR)
        tt = t[: int(0.05 * SR)]
        b = np.sin(2 * np.pi * (f + 2200 * tt) * tt) * np.exp(-90 * tt) * 0.25
        bubbles[i:i + len(b)] += b[: max(0, len(t) - i)]
    return (body * env * 0.6 + bubbles) * amp


def place(dst, src, at):
    i = int(at * SR)
    n = min(len(src), len(dst) - i)
    if n > 0: dst[i:i + n] += src[:n]


def norm(s, peak=0.85):
    s = s - np.mean(s)
    m = np.max(np.abs(s)) or 1.0
    fade = np.ones_like(s); k = min(len(s), int(0.01 * SR)); fade[-k:] = np.linspace(1, 0, k)
    return s / m * peak * fade


def save(name, s):
    OUT.mkdir(parents=True, exist_ok=True)
    with wave.open(str(OUT / name), 'wb') as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(SR)
        w.writeframes((np.clip(s, -1, 1) * 32767).astype('<i2').tobytes())
    print(name, f'{len(s) / SR:.2f} s')


def main():
    # drawn: the bottle lifted off the belt - a bright clink and the liquid rocking
    for k in range(3):
        s = np.zeros(int(0.9 * SR)); place(s, clink(2350 + 180 * k, 0.8), 0.0); place(s, clink(3100 + 150 * k, 0.6, 0.35, 0.7), 0.07); place(s, slosh(0.6, 0.45), 0.05)
        save(f'alch_draw_{k}.wav', norm(s, 0.7))
    # put away: two soft clinks against the others on the belt, a last slosh
    s = np.zeros(int(0.8 * SR)); place(s, clink(1900, 0.6, 0.6, 0.6), 0.0); place(s, clink(2600, 0.5, 0.4, 0.5), 0.12); place(s, slosh(0.5, 0.35), 0.1)
    save('alch_sheath.wav', norm(s, 0.6))
    # thrown: a soft whoosh (air, no steel ring) with the liquid sloshing as it leaves the hand
    for k in range(3):
        sec = 0.55
        t = t_(sec)
        wh = bandnoise(sec, 300 + 60 * k, 2600) * np.exp(-((t - 0.22) / 0.11) ** 2)
        s = np.zeros(len(t)); place(s, wh, 0.0); place(s, slosh(0.4, 0.5, 11.0), 0.05)
        save(f'alch_throw_{k}.wav', norm(s, 0.65))
    # shatter: the crack, a spray of shards tinkling down, the contents splashing out
    for k in range(3):
        sec = 1.3
        t = t_(sec)
        s = np.zeros(len(t))
        # (Noah: "a satisfying and quiet ... impact") a soft glassy pop and a muffled whump, a few fine tinkles, the splash
        place(s, bandnoise(0.18, 1500, 8000) * np.exp(-38 * t_(0.18)) * 0.45, 0.0)
        place(s, bandnoise(0.22, 60, 320) * np.exp(-22 * t_(0.22)) * 1.1, 0.0)
        for _ in range(12):
            place(s, clink(rng.uniform(3000, 6200), 0.22, rng.uniform(0.05, 0.16), 0.45), rng.uniform(0.02, 0.6))
        place(s, slosh(0.7, 0.6, 5.0), 0.02)
        save(f'alch_shatter_{k}.wav', norm(s, 0.85))


if __name__ == '__main__':
    main()
