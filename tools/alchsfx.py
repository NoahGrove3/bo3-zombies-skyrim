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
    # (Noah: "instead of the player grunting when throwing ... a potion-y sound effect") the wind-up: the cork thumbed off with a
    # soft pop, a quick fizz of bubbles rising, the liquid rocking as the arm comes back
    for k in range(3):
        sec = 0.7
        t = t_(sec)
        s = np.zeros(len(t))
        f0 = 520 + 70 * k                                                   # the pop: a short hollow tone falling, a puff of air
        pop = np.sin(2 * np.pi * (f0 * t[: int(0.07 * SR)] - 1800 * t[: int(0.07 * SR)] ** 2)) * np.exp(-60 * t[: int(0.07 * SR)])
        place(s, pop * 0.9, 0.0)
        place(s, bandnoise(0.05, 800, 5000) * np.exp(-90 * t_(0.05)) * 0.35, 0.0)
        for _ in range(26):                                                 # the fizz
            at = rng.uniform(0.04, 0.5); f = rng.uniform(1400, 3800); n = int(0.03 * SR); tt = t_(0.03)
            place(s, np.sin(2 * np.pi * (f + 3000 * tt) * tt) * np.exp(-140 * tt) * rng.uniform(0.08, 0.2) * (1 - at / 0.6), at)
        place(s, slosh(0.45, 0.4, 9.0), 0.12)
        save(f'alch_uncork_{k}.wav', norm(s, 0.6))
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
    hits()


def glass(sec):
    """the bottle breaking: a glassy pop, a muffled whump, a few fine tinkles (the shatter's own, a touch shorter)"""
    s = np.zeros(int(sec * SR))
    place(s, bandnoise(0.16, 1500, 8000) * np.exp(-40 * t_(0.16)) * 0.42, 0.0)
    place(s, bandnoise(0.2, 60, 320) * np.exp(-24 * t_(0.2)) * 0.9, 0.0)
    for _ in range(9):
        place(s, clink(rng.uniform(3000, 6200), 0.2, rng.uniform(0.05, 0.14), 0.45), rng.uniform(0.02, 0.45))
    return s


def env_swell(sec, attack, decay):
    t = t_(sec)
    return np.clip(t / attack, 0, 1) * np.exp(-np.maximum(0, t - attack) * decay)


def hits():
    """(Noah: "I want each flask to have its own impact sound effect, depending on the element ... if it's lightning, it should sound
    like a lightning bottle breaking impact noise. Fire should sound like fire with a bottle breaking") the bottle breaking, and its
    element let loose: alch_hit_<element>_<0-2>.wav"""
    for k in range(3):
        # fire: the spirit catching - a deep whoomp of flame swelling up, then crackling as it burns
        sec = 1.6; t = t_(sec); s = glass(sec)
        place(s, bandnoise(1.3, 80, 900) * env_swell(1.3, 0.08, 2.6) * 0.9, 0.03)
        place(s, bandnoise(1.0, 600, 3500) * env_swell(1.0, 0.12, 3.2) * 0.35, 0.05)
        for _ in range(26):  # crackles
            at = rng.uniform(0.1, 1.4); n = bandnoise(0.012, 1500, 9000) * np.exp(-400 * t_(0.012))
            place(s, n * rng.uniform(0.2, 0.5) * (1.3 - at / 1.4), at)
        save(f'alch_hit_fire_{k}.wav', norm(s, 0.85))
        # frost: the cold bursting out - a crystalline crack, ice spreading with fine high ticks, a cold breath
        sec = 1.5; s = glass(sec)
        place(s, bandnoise(0.06, 2500, 12000) * np.exp(-60 * t_(0.06)) * 0.6, 0.01)
        for _ in range(30):
            at = rng.uniform(0.04, 1.0); place(s, clink(rng.uniform(4500, 9000), 0.12, rng.uniform(0.04, 0.12), 0.8), at)
        place(s, bandnoise(1.2, 2500, 9000) * env_swell(1.2, 0.15, 2.4) * 0.22, 0.05)
        save(f'alch_hit_frost_{k}.wav', norm(s, 0.8))
        # shock: lightning let out of the bottle - a hard snap, a buzzing arc, crackling sparks
        sec = 1.4; t = t_(1.0); s = glass(sec)
        place(s, bandnoise(0.04, 300, 12000) * np.exp(-80 * t_(0.04)) * 1.0, 0.0)
        buzz = sum(np.sign(np.sin(2 * np.pi * (110 + 8 * k) * h * t)) / h for h in (1, 2, 3)) * bandnoise(1.0, 200, 4000) * 0.6
        buzz *= env_swell(1.0, 0.02, 3.5) * (0.6 + 0.4 * (rng.random(len(t)) > 0.3))
        place(s, buzz * 0.5, 0.02)
        for _ in range(20):
            at = rng.uniform(0.03, 1.1); place(s, bandnoise(0.008, 2000, 14000) * rng.uniform(0.3, 0.8), at)
        save(f'alch_hit_shock_{k}.wav', norm(s, 0.85))
        # poison: the brew spilling out - a wet splash, a hiss of fumes, bubbles popping
        sec = 1.6; s = glass(sec)
        place(s, slosh(0.6, 0.7, 6.0), 0.02)
        place(s, bandnoise(1.3, 3000, 10000) * env_swell(1.3, 0.1, 2.0) * 0.3, 0.05)
        for _ in range(22):
            at = rng.uniform(0.15, 1.4); f = rng.uniform(300, 900); tt = t_(0.06)
            place(s, np.sin(2 * np.pi * (f + 2500 * tt) * tt) * np.exp(-70 * tt) * rng.uniform(0.2, 0.45), at)
        save(f'alch_hit_poison_{k}.wav', norm(s, 0.8))
        # sun (holy water): light bursting out - a bright shimmering chord swelling and fading, sparkles over it
        sec = 1.8; t = t_(1.6); s = glass(sec)
        chord = sum(np.sin(2 * np.pi * f * (1 + 0.003 * k) * t + rng.uniform(0, 6)) * w for f, w in ((523, 1), (659, 0.7), (784, 0.6), (1047, 0.4)))
        place(s, chord * env_swell(1.6, 0.06, 2.2) * 0.3, 0.02)
        for _ in range(18):
            place(s, clink(rng.uniform(5000, 9000), 0.15, rng.uniform(0.03, 0.09), 0.5), rng.uniform(0.05, 1.2))
        save(f'alch_hit_sun_{k}.wav', norm(s, 0.8))
        # blood: a thick wet burst - a heavy splat, a gush, drops spattering down
        sec = 1.3; s = glass(sec)
        place(s, bandnoise(0.25, 40, 260) * np.exp(-16 * t_(0.25)) * 1.2, 0.0)
        place(s, slosh(0.55, 0.9, 4.0), 0.01)
        for _ in range(14):
            at = rng.uniform(0.15, 1.0); place(s, bandnoise(0.03, 300, 2500) * np.exp(-90 * t_(0.03)) * rng.uniform(0.2, 0.5), at)
        save(f'alch_hit_blood_{k}.wav', norm(s, 0.85))
        # the Philosopher's Flask: molten gold - a deep bell of struck gold, a long sizzle as it pools
        sec = 2.2; t = t_(2.0); s = glass(sec)
        place(s, clink(330 + 20 * k, 1.8, 0.9, 0.7), 0.0)
        place(s, bandnoise(1.9, 2000, 8000) * env_swell(1.9, 0.2, 1.4) * 0.25, 0.05)
        save(f'alch_hit_gold_{k}.wav', norm(s, 0.85))


if __name__ == '__main__':
    main()
