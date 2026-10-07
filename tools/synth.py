"""Undying Nights: every sound in the mode is composed and synthesized here, from nothing.
Writes 16-bit mono WAVs to <out>/fx/UndyingNights/<id>.wav (Skyrim loads them from Data/Sound/).
Deterministic: same code, same files.   python tools/synth.py <out_dir>"""
import sys, wave, pathlib
import numpy as np
from scipy.signal import lfilter

SR = 44100
rng = np.random.default_rng(7)

# ---------- building blocks ----------
def t_(d): return np.arange(int(d * SR)) / SR
def env(n, a=0.005, d=0.1, s=0.6, r=0.2, hold=None):
    """ADSR over n samples; hold = seconds at sustain (default fills)."""
    A, D, R = int(a * SR), int(d * SR), int(r * SR)
    H = max(0, n - A - D - R) if hold is None else int(hold * SR)
    e = np.concatenate([np.linspace(0, 1, max(A, 1)), np.linspace(1, s, max(D, 1)), np.full(H, s), np.linspace(s, 0, max(R, 1))])
    return np.pad(e, (0, max(0, n - len(e))))[:n]
def expdec(n, tau): return np.exp(-np.arange(n) / (tau * SR))
def hz(note):
    """'A4', 'C#3', 'Eb5' -> Hz"""
    names = {'C': -9, 'D': -7, 'E': -5, 'F': -4, 'G': -2, 'A': 0, 'B': 2}
    n = names[note[0]]; i = 1
    if note[i:i + 1] == '#': n += 1; i += 1
    elif note[i:i + 1] == 'b': n -= 1; i += 1
    return 440.0 * 2 ** ((n + 12 * (int(note[i:]) - 4)) / 12)
def _onepole(x, cutoff):
    a = np.exp(-2 * np.pi * cutoff / SR)
    return lfilter([1 - a], [1, -a], x)
def lp_fast(x, cutoff, order=2):
    for _ in range(order): x = _onepole(x, cutoff)
    return x
def hp(x, cutoff): return x - lp_fast(x, cutoff, 1)
def noise(n): return rng.uniform(-1, 1, n)
def mix(*parts):
    n = max(len(p) for p in parts); out = np.zeros(n)
    for p in parts: out[:len(p)] += p
    return out
def place(buf, x, at):
    i = int(at * SR)
    if i + len(x) > len(buf): buf = np.pad(buf, (0, i + len(x) - len(buf)))
    buf[i:i + len(x)] += x
    return buf
def reverb(x, wet=0.25, size=1.0):
    out = np.pad(x, (0, int(1.2 * SR)))
    acc = np.zeros_like(out)
    for s_, g in zip((0.0297, 0.0371, 0.0411, 0.0437), (0.80, 0.78, 0.76, 0.74)):
        dly = int(SR * s_ * size); a = np.zeros(dly + 1); a[0] = 1; a[dly] = -g
        acc += lfilter([1], a, out)
    acc /= 4
    for dly, g in ((int(SR * 0.005), 0.7), (int(SR * 0.0017), 0.7)):
        b = np.zeros(dly + 1); b[0] = -g; b[dly] = 1; a = np.zeros(dly + 1); a[0] = 1; a[dly] = -g
        acc = lfilter(b, a, acc)
    return out * (1 - wet) + acc * wet
def norm(x, peak=0.89):
    m = np.max(np.abs(x)) or 1.0
    return x / m * peak
def fade_tail(x, s=0.05):
    n = min(int(s * SR), len(x) // 4); x = x.copy(); x[len(x) - n:] *= np.linspace(1, 0, n); return x

# ---------- instruments ----------
def pluck(f, d, bright=0.5, decay=0.996):
    """Karplus-Strong lute/harp string."""
    n = int(d * SR); p = max(2, int(SR / f)); buf = noise(p) * 0.8
    buf = lp_fast(buf, 2000 + 6000 * bright, 1)
    out = np.empty(n); idx = 0
    for i in range(n):
        v = buf[idx]; nxt = buf[(idx + 1) % p]
        buf[idx] = decay * 0.5 * (v + nxt); out[i] = v; idx = (idx + 1) % p
    return out * env(n, 0.001, 0.05, 1.0, 0.05)
def brass(f, d, bright=0.6, vib=4.5):
    tt = t_(d); ph = 2 * np.pi * f * tt + 0.004 * np.sin(2 * np.pi * vib * tt) * f / 10
    x = sum(np.sin(k * ph) / k ** (1.4 - bright * 0.6) for k in range(1, 12))
    x *= env(len(tt), 0.04, 0.12, 0.75, 0.12)
    return lp_fast(x, 900 + 2500 * bright, 1) * 0.5
def flute(f, d):
    tt = t_(d); vib = 1 + 0.006 * np.sin(2 * np.pi * 5.2 * tt)
    x = np.sin(2 * np.pi * f * tt * vib) + 0.2 * np.sin(4 * np.pi * f * tt * vib)
    breath = lp_fast(noise(len(tt)), 3000, 1) * 0.08
    return (x + breath) * env(len(tt), 0.06, 0.1, 0.8, 0.1) * 0.5
def bell(f, d, ratio=2.76, idx=3.0):
    tt = t_(d); m = idx * expdec(len(tt), d / 3) * np.sin(2 * np.pi * f * ratio * tt)
    return np.sin(2 * np.pi * f * tt + m) * expdec(len(tt), d / 4) * 0.5
def pad(f, d, voices=5):
    tt = t_(d); x = np.zeros(len(tt))
    for v in range(voices):
        det = 1 + (v - voices // 2) * 0.004
        x += np.sin(2 * np.pi * f * det * tt + rng.uniform(0, 6.28)) + 0.3 * np.sin(4 * np.pi * f * det * tt)
    return x / voices * env(len(tt), d * 0.3, 0.1, 0.9, d * 0.3) * 0.6
def choir(f, d):
    """'aah' pad: detuned saws through two vowel formants."""
    tt = t_(d); x = np.zeros(len(tt))
    for v in range(4):
        det = 1 + (v - 1.5) * 0.005
        ph = (f * det * tt + 0.003 * np.sin(2 * np.pi * (5 + v * 0.3) * tt)) % 1.0
        x += 2 * ph - 1
    x /= 4
    a = lp_fast(x, 1200, 2) - lp_fast(x, 600, 2)    # formant ~ 600-1200 Hz
    b = lp_fast(x, 2800, 2) - lp_fast(x, 2200, 2)
    return (a + 0.5 * b) * env(len(tt), d * 0.25, 0.1, 0.9, d * 0.35) * 1.4
def kick(d=0.5, f0=120, f1=40):
    tt = t_(d); f = f1 + (f0 - f1) * np.exp(-tt * 30)
    return np.sin(2 * np.pi * np.cumsum(f) / SR) * expdec(len(tt), 0.12)
def taiko(d=0.9):
    return norm(kick(d, 90, 45) + lp_fast(noise(int(d * SR)), 700, 2) * expdec(int(d * SR), 0.05) * 0.6) * 0.9
def frame_drum(d=0.3):
    n = int(d * SR); return (lp_fast(noise(n), 1800, 2) * expdec(n, 0.04) + kick(d, 200, 90) * 0.4) * 0.7
def zap(f0, f1, d):
    tt = t_(d); f = f0 * (f1 / f0) ** (tt / d)
    car = np.sin(2 * np.pi * np.cumsum(f) / SR + 2.5 * np.sin(2 * np.pi * np.cumsum(f * 1.5) / SR))
    return car * expdec(len(tt), d / 2.5) * 0.6

def melody(inst, notes, bpm, **kw):
    """notes: list of (note or None, beats)."""
    beat = 60 / bpm; out = np.zeros(1); at = 0.0
    for n, b in notes:
        if n:
            x = inst(hz(n), b * beat * 1.15, **kw) if inst is not pluck else inst(hz(n), max(b * beat * 1.6, 0.6), **kw)
            out = place(out, x, at)
        at += b * beat
    return out

# ---------- brew jingles (original compositions) ----------
def j_trollshide():
    bpm = 84; b = 60 / bpm
    mel = melody(brass, [('D3', 1), ('F3', 0.5), ('A3', 0.5), ('G3', 1), ('F3', 0.5), ('E3', 0.5), ('D3', 2), (None, 0.5), ('A2', 0.5), ('D3', 2)], bpm, bright=0.4)
    dr = np.zeros(1)
    for k in range(9): dr = place(dr, taiko() * (1.0 if k % 2 == 0 else 0.6), k * b)
    low = melody(brass, [('D2', 4), ('A1', 2), ('D2', 2)], bpm, bright=0.2)
    return reverb(mix(mel, dr * 0.8, low * 0.7), 0.3)
def j_quicksilver():
    bpm = 150; s = 0.25
    arp = []
    for root in ('A3', 'C4', 'G3', 'A3'):
        r = hz(root)
        for k, iv in enumerate((0, 3, 7, 10, 12, 10, 7, 3)): arp.append((r * 2 ** (iv / 12), s))
    out = np.zeros(1); at = 0
    for f, bt in arp:
        out = place(out, pluck(f, 0.5, bright=0.8), at); at += bt * 60 / bpm
    bells = melody(bell, [('E5', 2), ('D5', 2), ('B4', 2), ('A5', 2)], bpm)
    return reverb(mix(out, bells * 0.5), 0.2)
def j_twinstrike():
    bpm = 120; b = 60 / bpm
    dr = np.zeros(1)
    for bar in range(4):
        for off in (0, 0.25, 1.5, 1.75):
            dr = place(dr, frame_drum() * 0.9, (bar * 2 + off) * b)
    call = melody(brass, [('G3', 0.5), ('G3', 0.5), (None, 1), ('Bb3', 0.5), ('Bb3', 0.5), (None, 1), ('C4', 0.5), ('C4', 0.5), ('D4', 1), ('G3', 2)], bpm, bright=0.8)
    resp = melody(pluck, [(None, 1), ('D4', 1), (None, 1), ('F4', 1), (None, 2), ('G4', 2)], bpm)
    return reverb(mix(dr, call, resp * 0.6), 0.2)
def j_hearthfire():
    bpm = 76
    lute = melody(pluck, [('F3', 0.5), ('A3', 0.5), ('C4', 0.5), ('F4', 0.5), ('E4', 0.5), ('C4', 0.5), ('D4', 1), ('Bb3', 0.5), ('D4', 0.5), ('F4', 0.5), ('A4', 0.5), ('G4', 1), ('F4', 2)], bpm, bright=0.3)
    ch = mix(choir(hz('F3'), 3.2), place(np.zeros(1), choir(hz('Bb3'), 2.0), 3.0), place(np.zeros(1), choir(hz('C4'), 2.4), 4.6))
    return reverb(mix(lute, ch * 0.5), 0.35)
def j_windrunner():
    bpm = 140
    run = melody(flute, [('E4', 0.25), ('F#4', 0.25), ('G#4', 0.25), ('A4', 0.25), ('B4', 0.25), ('C#5', 0.25), ('D5', 0.25), ('E5', 0.25), ('E5', 1), ('D5', 0.5), ('B4', 0.5), ('E5', 0.5), ('F#5', 0.5), ('E5', 2)], bpm)
    dr = np.zeros(1)
    for k in range(16): dr = place(dr, frame_drum(0.15) * (0.7 if k % 4 == 0 else 0.35), k * 0.5 * 60 / bpm)
    return reverb(mix(run, dr), 0.25)
def j_hawkeye():
    bpm = 96
    pz = melody(pluck, [('B3', 0.5), (None, 0.5), ('F#4', 0.5), (None, 0.5), ('D4', 0.5), ('E4', 0.5), ('F#4', 1), (None, 0.5), ('B3', 0.5), ('A3', 0.5), ('B3', 2)], bpm, bright=0.9, decay=0.99)
    bl = melody(bell, [(None, 2), ('F#5', 2), (None, 1), ('B5', 3)], bpm, ratio=3.5, idx=2)
    return reverb(mix(pz, bl * 0.4), 0.3, 1.3)
def j_mammoth():
    bpm = 108; b = 60 / bpm
    oom = np.zeros(1)
    for k, n in enumerate(['G1', 'D2', 'G1', 'D2', 'C2', 'G1', 'D2', 'G1']):
        oom = place(oom, brass(hz(n), b * 0.5, bright=0.25), k * b)
        oom = place(oom, pluck(hz('B3'), 0.3) * 0.4, (k + 0.5) * b)
    tune = melody(brass, [('G3', 1), ('B3', 1), ('D4', 1.5), ('B3', 0.5), ('C4', 1), ('A3', 1), ('G3', 2)], bpm, bright=0.5)
    return reverb(mix(oom * 0.9, tune), 0.2)
def j_frostbite():
    bpm = 100
    crawl = melody(bell, [('E5', 0.25), ('F5', 0.25), ('E5', 0.25), ('D#5', 0.25), ('E5', 0.5), (None, 0.5), ('B4', 0.25), ('C5', 0.25), ('B4', 0.25), ('A#4', 0.25), ('B4', 1), ('G5', 0.5), ('F#5', 0.5), ('E5', 2)], bpm, ratio=5.4, idx=1.5)
    glass = pad(hz('E5'), 4.5) * 0.3 + pad(hz('B5'), 4.5) * 0.15
    return reverb(mix(crawl, glass), 0.45, 1.4)
def j_stormcrown():
    bpm = 132; b = 60 / bpm
    dr = np.zeros(1)
    for k in range(16):
        dr = place(dr, kick(0.25, 140, 50) * 0.8, k * 0.5 * b) if k % 2 == 0 else place(dr, frame_drum(0.12) * 0.5, k * 0.5 * b)
    z = np.zeros(1)
    for k, f in enumerate([hz('C5'), hz('Db5'), hz('C5'), hz('G4'), hz('C5'), hz('Db5'), hz('Eb5'), hz('C5')]):
        z = place(z, zap(f * 2, f, 0.22), k * 0.5 * b)
    riff = melody(brass, [('C3', 0.5), ('C3', 0.5), ('Db3', 0.5), ('C3', 0.5), ('Bb2', 1), ('C3', 2)], bpm, bright=0.9)
    return reverb(mix(dr, z * 0.6, riff * 0.8), 0.2)

# ---------- the Coffer ----------
def coffer_spin():
    bpm = 168
    waltz = []
    for bar, (bass, a, c) in enumerate([('D3', 'F#4', 'A4'), ('A2', 'E4', 'G4'), ('D3', 'F#4', 'A4'), ('G2', 'D4', 'B4'), ('A2', 'C#4', 'E4'), ('D3', 'D4', 'F#4')]):
        waltz += [(bass, 1), (a, 0.5), (c, 0.5), (a, 1)]
    box = melody(bell, waltz, bpm, ratio=4.0, idx=1.2)
    # a mad giggle of rising wobbly zaps
    gig = np.zeros(1)
    for k in range(5): gig = place(gig, zap(600 + 120 * k, 900 + 160 * k, 0.12) * 0.25, 3.3 + k * 0.11)
    return reverb(mix(box, gig), 0.3)
def coffer_open():
    n = int(0.9 * SR)
    creak = np.sin(2 * np.pi * np.cumsum(140 + 60 * np.sin(np.linspace(0, 9, n)) + 30 * noise(n)) / SR) * env(n, 0.05, 0.3, 0.5, 0.3)
    creak = lp_fast(creak * (1 + 0.5 * noise(n)), 1500, 2) * 0.6
    chime = mix(bell(hz('A5'), 1.2), place(np.zeros(1), bell(hz('E6'), 1.2), 0.12))
    return reverb(mix(creak, place(np.zeros(1), chime * 0.5, 0.5)), 0.3)
def coffer_cheese():
    sad = melody(brass, [('G3', 0.75), ('F#3', 0.75), ('F3', 0.75), ('E3', 2.5)], 100, bright=0.7)
    tt = t_(2.6); wob = np.sin(2 * np.pi * np.cumsum(500 + 180 * np.sin(2 * np.pi * 9 * tt) * np.exp(-tt)) / SR) * expdec(len(tt), 0.7) * 0.25
    return reverb(mix(sad, place(np.zeros(1), wob, 0.4)), 0.25)

# ---------- nights ----------
def night_start():
    n = int(3.5 * SR); tt = t_(3.5)
    horn = brass(hz('D2'), 3.2, bright=0.35) + brass(hz('A2'), 3.2, bright=0.3) * 0.6
    dr = np.zeros(1)
    for k, at in enumerate((0, 0.75, 1.5, 1.9, 2.3)): dr = place(dr, taiko() * (1.0 if k < 3 else 0.8), at)
    rumble = lp_fast(noise(n), 120, 2) * env(n, 1.0, 0.5, 0.6, 1.5) * 1.5
    return reverb(mix(horn, dr, rumble), 0.35, 1.3)
def night_end():
    ch = mix(choir(hz('D3'), 3.5), choir(hz('F3'), 3.5) * 0.8, choir(hz('A3'), 3.5) * 0.7,
             place(np.zeros(1), mix(choir(hz('D3'), 2.5), choir(hz('F#3'), 2.5) * 0.8, choir(hz('A3'), 2.5) * 0.7), 2.2))
    return reverb(mix(ch, place(np.zeros(1), bell(hz('D5'), 2.5) * 0.3, 2.2)), 0.4, 1.4)
def hound_night():
    tt = t_(2.4); howl = np.sin(2 * np.pi * np.cumsum(300 + 250 * np.sin(np.pi * tt / 2.4) + 8 * np.sin(2 * np.pi * 6 * tt)) / SR)
    howl = lp_fast(howl + 0.3 * noise(len(tt)) * 0.2, 2500, 1) * env(len(tt), 0.3, 0.3, 0.8, 0.8) * 0.5
    dr = np.zeros(1)
    for k in range(6): dr = place(dr, taiko() * 0.8, 1.2 + k * 0.28)
    return reverb(mix(howl, dr), 0.4, 1.4)
def night_falls():
    return mix(night_start() * 0.8, place(np.zeros(1), choir(hz('D3'), 3.0) * 0.4, 0.5))

# ---------- weapons ----------
def gunshot(body_hz, crack, tail, sub=1.0, d=0.6):
    n = int(d * SR)
    cr = hp(noise(n), 1500) * expdec(n, crack)
    bod = lp_fast(noise(n), body_hz, 2) * expdec(n, tail) * 2.5
    thump = kick(d, 150, 50) * sub * 0.8
    return norm(np.tanh(1.8 * (cr + bod + thump)))
def shot_pistol(): return reverb(gunshot(1800, 0.006, 0.05, 0.6, 0.45), 0.15)
def shot_smg(): return reverb(gunshot(2200, 0.004, 0.03, 0.5, 0.3), 0.12)
def shot_ar(): return reverb(gunshot(1500, 0.006, 0.06, 0.9, 0.5), 0.15)
def shot_lmg(): return reverb(gunshot(1100, 0.007, 0.07, 1.2, 0.55), 0.15)
def shot_shotgun(): return reverb(gunshot(900, 0.01, 0.12, 1.4, 0.9), 0.2)
def shot_sniper():
    x = gunshot(1300, 0.008, 0.1, 1.5, 1.0); return reverb(x, 0.35, 1.5)
def shot_ray():
    return reverb(mix(zap(2400, 300, 0.35), zap(1200, 150, 0.4) * 0.6, bell(hz('E6'), 0.4) * 0.3), 0.2)
def click(f=3000, d=0.03):
    n = int(d * SR); return hp(noise(n), f) * expdec(n, 0.004) + np.sin(2 * np.pi * f / 3 * t_(d)) * expdec(n, 0.006) * 0.5
def reload_mag():
    x = np.zeros(1)
    x = place(x, click(2500), 0.0); x = place(x, lp_fast(noise(int(0.12 * SR)), 1800, 1) * expdec(int(0.12 * SR), 0.03) * 0.5, 0.05)
    x = place(x, click(1800) * 1.2, 0.55); x = place(x, click(3200), 0.62)
    slide = lp_fast(noise(int(0.15 * SR)), 2500, 1) * env(int(0.15 * SR), 0.02, 0.05, 0.6, 0.05) * 0.6
    x = place(x, slide, 0.9); x = place(x, click(2200) * 1.3, 1.06)
    return norm(x, 0.7)
def reload_shell():
    x = np.zeros(1)
    for k in range(2): x = place(x, click(1500) * 1.2, k * 0.3); x = place(x, click(2600) * 0.8, k * 0.3 + 0.08)
    pump = lp_fast(noise(int(0.25 * SR)), 1200, 1) * env(int(0.25 * SR), 0.01, 0.05, 0.7, 0.1) * 0.8
    x = place(x, pump, 0.7); x = place(x, click(1300) * 1.5, 0.95)
    return norm(x, 0.75)
def dry_click(): return norm(click(3500, 0.05), 0.5)
def buy():
    x = np.zeros(1)
    for k, f in enumerate((2637, 3136, 3951, 3520, 4186)):
        x = place(x, bell(f, 0.5, ratio=1.41, idx=1.0), k * 0.06)
    return reverb(norm(x, 0.7), 0.2)
def hitmarker(): return norm(click(4200, 0.06), 0.45)
def headshot(): return norm(mix(click(2000, 0.06) * 1.3, lp_fast(noise(int(0.08 * SR)), 900, 2) * expdec(int(0.08 * SR), 0.02)), 0.6)
def drink():
    x = np.zeros(1)
    for k in range(4):
        n = int(0.14 * SR); tt = t_(0.14)
        g = np.sin(2 * np.pi * np.cumsum(180 + 220 * tt / 0.14) / SR) * env(n, 0.01, 0.04, 0.5, 0.05)
        x = place(x, lp_fast(g, 800, 1), k * 0.22)
    x = place(x, mix(bell(hz('C6'), 0.8), bell(hz('G6'), 0.8) * 0.6), 0.95)
    return reverb(norm(x, 0.7), 0.25)
def knife():
    n = int(0.25 * SR); sweep = np.linspace(500, 4000, n)
    nz = noise(n); w = np.concatenate([lp_fast(c, f, 1) for c, f in zip(np.array_split(nz, 32), np.linspace(500, 4000, 32))])
    return norm(w * env(n, 0.05, 0.05, 0.8, 0.12), 0.6)
def rise():
    n = int(1.3 * SR)
    dirt = lp_fast(noise(n), 400, 2) * env(n, 0.2, 0.3, 0.7, 0.5) * 2
    crumble = np.zeros(n)
    for k in range(14): crumble = place(crumble, lp_fast(noise(int(0.05 * SR)), 1500, 1) * expdec(int(0.05 * SR), 0.01) * 0.4, rng.uniform(0.1, 1.1))[:n]
    groan = brass(hz('A1'), 1.0, bright=0.1) * 0.6
    return reverb(norm(mix(dirt, crumble, place(np.zeros(1), groan, 0.3))), 0.2)

SOUNDS = {
    'brew_trollshide': j_trollshide, 'brew_quicksilver': j_quicksilver, 'brew_twinstrike': j_twinstrike,
    'brew_hearthfire': j_hearthfire, 'brew_windrunner': j_windrunner, 'brew_hawkeye': j_hawkeye,
    'brew_mammoth': j_mammoth, 'brew_frostbite': j_frostbite, 'brew_stormcrown': j_stormcrown,
    'coffer_spin': coffer_spin, 'coffer_open': coffer_open, 'coffer_cheese': coffer_cheese,
    'night_start': night_start, 'night_end': night_end, 'hound_night': hound_night, 'night_falls': night_falls,
    'shot_pistol': shot_pistol, 'shot_smg': shot_smg, 'shot_ar': shot_ar, 'shot_lmg': shot_lmg,
    'shot_shotgun': shot_shotgun, 'shot_sniper': shot_sniper, 'shot_ray': shot_ray,
    'reload_mag': reload_mag, 'reload_shell': reload_shell, 'dry_click': dry_click,
    'buy': buy, 'hitmarker': hitmarker, 'headshot': headshot, 'drink': drink, 'knife': knife, 'rise': rise,
}

def write(path, x):
    x = fade_tail(norm(x))
    pcm = (np.clip(x, -1, 1) * 32767).astype('<i2')
    with wave.open(str(path), 'wb') as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(SR); w.writeframes(pcm.tobytes())

if __name__ == '__main__':
    out = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else 'build/sound') / 'fx' / 'UndyingNights'
    out.mkdir(parents=True, exist_ok=True)
    only = set(sys.argv[2:])
    for name, fn in SOUNDS.items():
        if only and name not in only: continue
        x = fn(); write(out / f'{name}.wav', x); print(f'{name:18s} {len(x) / SR:5.2f}s')
