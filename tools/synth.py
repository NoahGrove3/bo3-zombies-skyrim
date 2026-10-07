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

# ---------- Skyrim-style instruments (v2: no chiptune) ----------
from scipy.signal import butter, sosfilt, fftconvolve
def bp(x, lo, hi, order=2):
    return sosfilt(butter(order, [lo / (SR / 2), min(hi, SR / 2 - 100) / (SR / 2)], 'band', output='sos'), x)
def lpb(x, f, order=2): return sosfilt(butter(order, f / (SR / 2), 'low', output='sos'), x)
def hpb(x, f, order=2): return sosfilt(butter(order, f / (SR / 2), 'high', output='sos'), x)
_IR = {}
def hall(x, wet=0.35, secs=2.8):
    """Stone-hall convolution reverb (synthetic impulse: early reflections + dark decaying tail)."""
    key = secs
    if key not in _IR:
        n = int(secs * SR); r = np.random.default_rng(11)
        tail = r.standard_normal(n) * np.exp(-np.arange(n) / (secs / 6.9 * SR))
        tail = lpb(tail, 4500) + 0.5 * lpb(tail, 900)
        for k, (dly, g) in enumerate(((0.011, .5), (0.019, .4), (0.027, .35), (0.041, .3), (0.053, .25))):
            tail[int(dly * SR)] += g * 8
        _IR[key] = tail / np.sqrt(np.sum(tail ** 2))
    y = fftconvolve(x, _IR[key])
    out = np.pad(x, (0, len(y) - len(x)))
    return out * (1 - wet) + y * wet * 0.9
def saw_bl(f, tt, vib=None):
    """band-limited sawtooth (additive), optional vibrato array in ratio."""
    ph = 2 * np.pi * np.cumsum(f * (vib if vib is not None else 1.0) * np.ones_like(tt)) / SR
    x = np.zeros_like(tt); K = int(min(40, (SR / 2.2) / f))
    for k in range(1, K + 1): x += np.sin(k * ph) / k
    return x
def vib_of(tt, rate=5.0, depth=0.006, delay=0.2):
    ramp = np.clip((tt - delay) / 0.4, 0, 1)
    return 1 + depth * ramp * np.sin(2 * np.pi * rate * tt + rng.uniform(0, 6))
VOWELS = {'a': ((700, 1220, 2600), (1, .5, .25)), 'o': ((450, 800, 2830), (1, .35, .15)), 'u': ((325, 700, 2530), (1, .25, .1)), 'e': ((400, 1700, 2600), (1, .4, .25))}
def choir(f, d, vowel='a', voices=7, bright=1.0):
    """Male choir section: glottal sources through vowel formants, detuned voices."""
    tt = t_(d); src = np.zeros(len(tt))
    for v in range(voices):
        det = 2 ** (rng.uniform(-9, 9) / 1200)
        src += saw_bl(f * det, tt, vib_of(tt, rng.uniform(4.6, 5.6), 0.005, rng.uniform(.1, .4))) * rng.uniform(.7, 1)
    src = lpb(src, 3000 * bright) + lpb(noise(len(tt)), 2500) * 0.04 * voices
    fr, gs = VOWELS[vowel]; out = np.zeros(len(tt))
    for F, g in zip(fr, gs): out += bp(src, F * 0.85, F * 1.18) * g
    return out / voices * 3 * env(len(tt), min(0.35, d * 0.3), 0.2, 0.9, min(0.6, d * 0.35))
def horn(f, d, bright=0.6):
    """Nord war horn: brassy saw, filter opens with breath, scoop up into pitch."""
    tt = t_(d); scoop = 1 - 0.03 * np.exp(-tt * 18)
    x = saw_bl(f, tt, scoop * vib_of(tt, 4.5, 0.003, 0.4))
    e = env(len(tt), 0.12, 0.2, 0.85, 0.25)
    cutoff = 300 + 2600 * bright * e
    y = np.zeros_like(x); chunk = 512
    for i in range(0, len(x), chunk):
        y[i:i + chunk] = lpb(x[max(0, i - 2048):i + chunk], max(120, cutoff[i]), 2)[-len(x[i:i + chunk]):]
    breath = bp(noise(len(tt)), 500, 2500) * 0.05
    return (y + breath) * e * 0.6
def fiddle(f, d, bright=0.7, trem=False):
    """Bowed string (nyckelharpa/fiddle): saw through a wooden body, vibrato, bow hiss."""
    tt = t_(d); x = saw_bl(f, tt, vib_of(tt, 5.5, 0.007, 0.15))
    body = bp(x, 250, 600) * 0.8 + bp(x, 900, 1400) * 0.6 + bp(x, 2400, 3400) * 0.35 * bright + lpb(x, 400) * 0.5
    bow = bp(noise(len(tt)), 2000, 6000) * 0.02
    e = env(len(tt), 0.08, 0.1, 0.85, 0.15)
    if trem: e *= 0.6 + 0.4 * np.abs(np.sin(2 * np.pi * 7 * tt))
    return (body + bow) * e * 0.8
def drone(f, d): return fiddle(f, d, 0.4) * 0.6 + fiddle(f * 1.5, d, 0.3) * 0.3
def lute(f, d, bright=0.4):
    x = pluck(f, d, bright=bright, decay=0.997)
    return bp(x, 90, 3500) + bp(x, 200, 400) * 0.6 + bp(x, 1800, 2600) * 0.3
def woodflute(f, d):
    tt = t_(d); vib = vib_of(tt, 5.0, 0.008, 0.15)
    x = np.sin(2 * np.pi * np.cumsum(f * vib) / SR) + 0.12 * np.sin(4 * np.pi * np.cumsum(f * vib) / SR)
    breath = bp(noise(len(tt)), f * 0.8, f * 3) * 0.12
    return (x + breath) * env(len(tt), 0.07, 0.1, 0.8, 0.12) * 0.45
def wardrum(d=1.4, pitch=1.0, hit=1.0):
    """Big skin drum: inharmonic membrane modes with pitch drop, beater thud, long body."""
    tt = t_(d); out = np.zeros(len(tt)); f0 = 62 * pitch
    for ratio, g, tau in ((1.0, 1.0, .45), (1.59, .5, .25), (2.14, .35, .18), (2.30, .3, .15), (2.65, .2, .1)):
        f = f0 * ratio * (1 + 0.25 * np.exp(-tt * 25))
        out += np.sin(2 * np.pi * np.cumsum(f) / SR) * g * np.exp(-tt / tau)
    thud = lpb(noise(len(tt)), 1200) * np.exp(-tt / 0.03) * 0.8
    return np.tanh((out + thud) * hit * 1.2) * 0.8
def framedrum(d=0.35, hit=1.0):
    tt = t_(d); f = 180 * (1 + 0.3 * np.exp(-tt * 40))
    return (np.sin(2 * np.pi * np.cumsum(f) / SR) * np.exp(-tt / 0.08) * 0.6 + bp(noise(len(tt)), 800, 5000) * np.exp(-tt / 0.03) * 0.5) * hit
def thunder(d=3.0):
    tt = t_(d); x = lpb(noise(len(tt)), 260, 4) * 6
    crack = bp(noise(len(tt)), 300, 3000) * np.exp(-tt / 0.15)
    return np.tanh((x * env(len(tt), 0.05, 0.3, 0.5, d * 0.6) + crack) * 1.5) * 0.7

def seq(inst, notes, bpm, legato=1.1, **kw):
    """notes: (note|None, beats[, vel]). Each note lasts beats*legato (lute rings longer)."""
    beat = 60 / bpm; out = np.zeros(1); at = 0.0
    for item in notes:
        n, b = item[0], item[1]; vel = item[2] if len(item) > 2 else 1.0
        if n:
            dur = max(b * beat * legato, 0.9) if inst is lute else b * beat * legato
            out = place(out, inst(hz(n), dur, **kw) * vel, at)
        at += b * beat
    return out
def chord(inst, notes, d, **kw): return mix(*[inst(hz(n), d, **kw) for n in notes]) / len(notes) * 1.6
def drums(pattern, bpm, inst=wardrum, **kw):
    """pattern: list of (beat, velocity)."""
    out = np.zeros(1)
    for b, v in pattern: out = place(out, inst(**kw) * v, b * 60 / bpm)
    return out

# ---------- brew jingles (original, Skyrim-style) ----------
def j_trollshide():   # heavy: war drums, low horns, humming choir, D minor
    bpm = 80
    dr = drums([(k, 1.0 if k % 2 == 0 else 0.65) for k in range(8)] + [(7.5, .7)], bpm)
    hn = seq(horn, [('D3', 1.5), ('F3', .5), ('A3', 1), ('G3', 1), ('F3', 1), ('E3', 1), ('D3', 2.5)], bpm, bright=.5)
    ch = mix(chord(choir, ['D2', 'A2', 'D3'], 4.5, vowel='u'), place(np.zeros(1), chord(choir, ['A1', 'E2', 'A2'], 2.6, vowel='u'), 4.4))
    return hall(mix(dr * .9, hn, ch * .8), .3)
def j_quicksilver():  # nimble: fast lute arpeggios + fiddle line, A dorian
    bpm = 144; notes = []
    for root, ivs in (('A3', (0, 7, 12, 15)), ('G3', (0, 7, 12, 14)), ('F3', (0, 7, 12, 16)), ('E3', (0, 7, 12, 15))):
        for _ in range(2):
            for iv in ivs: notes.append((root, 0.25, iv))
    out = np.zeros(1); at = 0
    for root, b, iv in notes:
        out = place(out, lute(hz(root) * 2 ** (iv / 12), 0.9, .55), at); at += b * 60 / bpm
    fd = seq(fiddle, [('E5', 1), ('D5', .5), ('C5', .5), ('D5', 1), ('B4', 1), ('C5', .5), ('B4', .5), ('A4', 2)], bpm)
    dr = drums([(k * .5, .5) for k in range(16)], bpm, framedrum)
    return hall(mix(out, fd * .7, dr * .5), .25)
def j_twinstrike():   # double hits, fiddle call and horn answer, G minor
    bpm = 116
    dr = drums([(b + o, v) for b in (0, 2, 4, 6) for o, v in ((0, 1), (.33, .8))], bpm)
    fd = seq(fiddle, [('G4', .5), ('G4', .5), (None, 1), ('Bb4', .5), ('Bb4', .5), (None, 1), ('C5', .5), ('D5', .5), ('C5', .5), ('Bb4', .5), ('G4', 2)], bpm)
    hn = seq(horn, [(None, 1), ('D3', 1), (None, 1), ('F3', 1), (None, 2), ('G3', 2)], bpm)
    return hall(mix(dr * .9, fd, hn * .7), .3)
def j_hearthfire():   # warm: lute lullaby, soft choir, F major
    bpm = 72
    lt = seq(lute, [('F3', .5), ('A3', .5), ('C4', .5), ('F4', .5), ('E4', .5), ('C4', .5), ('D4', 1), ('Bb3', .5), ('D4', .5), ('F4', .5), ('A4', .5), ('G4', 1), ('F4', 2)], bpm, bright=.3)
    ch = mix(chord(choir, ['F2', 'C3', 'A3'], 3.4, vowel='o'), place(np.zeros(1), chord(choir, ['Bb2', 'F3', 'D4'], 2.2, vowel='o'), 3.2), place(np.zeros(1), chord(choir, ['C3', 'G3', 'E4'], 1.6, vowel='o'), 5.0), place(np.zeros(1), chord(choir, ['F2', 'C3', 'A3'], 2.5, vowel='o'), 6.4))
    return hall(mix(lt, ch * .6), .38, 3.2)
def j_windrunner():   # gallop: wooden flute run over frame-drum hoofbeats, E mixolydian
    bpm = 138
    fl = seq(woodflute, [('E4', .25), ('F#4', .25), ('G#4', .25), ('A4', .25), ('B4', .25), ('C#5', .25), ('D5', .25), ('E5', .25), ('E5', 1), ('D5', .5), ('B4', .5), ('E5', .5), ('F#5', .5), ('E5', 2)], bpm)
    dr = drums([(k * .5 + o, v) for k in range(14) for o, v in ((0, .9), (.17, .5), (.33, .6))], bpm, framedrum)
    dn = drone(hz('E2'), 5.0)
    return hall(mix(fl, dr * .6, dn * .4), .28)
def j_hawkeye():      # stealth: sparse lute harmonics over a low drone and whispered choir, B minor
    bpm = 90
    lt = seq(lute, [('B3', .5), (None, .5), ('F#4', .5), (None, .5), ('D4', .5), ('E4', .5), ('F#4', 1), (None, .5), ('B3', .5), ('A3', .5), ('B3', 2)], bpm, bright=.8)
    dn = drone(hz('B1'), 5.5)
    wh = chord(choir, ['B2', 'F#3'], 5.0, vowel='u', bright=.5)
    return hall(mix(lt, dn * .5, wh * .35), .4, 3.2)
def j_mammoth():      # giant steps: deep horns, stomping drum, G
    bpm = 96
    dr = drums([(k, 1.0) for k in range(8)], bpm, wardrum, pitch=.75, hit=1.3)
    hn = seq(horn, [('G2', 1), ('B2', 1), ('D3', 1.5), ('B2', .5), ('C3', 1), ('A2', 1), ('G2', 2)], bpm, bright=.35)
    lo = seq(horn, [('G1', 4), ('D2', 2), ('G1', 2)], bpm, bright=.2)
    return hall(mix(dr * .9, hn, lo * .6), .3)
def j_frostbite():    # icy: tremolo high fiddles, low choir, creeping half-steps, E minor
    bpm = 96
    cr = seq(fiddle, [('E5', .25), ('F5', .25), ('E5', .25), ('D#5', .25), ('E5', .5), (None, .5), ('B4', .25), ('C5', .25), ('B4', .25), ('A#4', .25), ('B4', 1), ('G5', .5), ('F#5', .5), ('E5', 2)], bpm, bright=1.0)
    ice = chord(fiddle, ['E5', 'B5'], 5.0, trem=True, bright=1.0) * .35
    ch = chord(choir, ['E2', 'B2'], 5.0, vowel='o')
    return hall(mix(cr, ice, ch * .5), .45, 3.5)
def j_stormcrown():   # storm: thunder, driving drums, horn call, shouted choir chord, C phrygian
    bpm = 128
    dr = drums([(k * .5, 1.0 if k % 2 == 0 else .55) for k in range(16)], bpm)
    hn = seq(horn, [('C3', .5), ('C3', .5), ('Db3', .5), ('C3', .5), ('Bb2', 1), ('C3', 2)], bpm, bright=.9)
    ch = place(np.zeros(1), chord(choir, ['C3', 'G3', 'C4'], 2.2, vowel='a', bright=1.3), 2.9)
    return hall(mix(place(np.zeros(1), thunder(3.5) * .7, 0), dr * .8, hn, ch * .8), .3)

# ---------- the Coffer (Sheogorath: mad, jolly, off-kilter) ----------
def coffer_spin():
    bpm = 150; tune = [('D5', .5), ('F#5', .5), ('A5', .5), ('G5', .5), ('F#5', .5), ('E5', .5), ('D5', .5), ('C#5', .5), ('E5', .5), ('G5', .5), ('F#5', .5), ('D5', .5), ('A4', 1), ('D5', 1)]
    fd = seq(fiddle, tune, bpm, legato=.9, bright=.9)
    lt = seq(lute, [('D3', 1), ('A3', 1), ('A2', 1), ('E3', 1), ('D3', 1), ('A3', 1), ('D3', 2)], bpm)
    dr = drums([(k * 1.0, .6) for k in range(8)], bpm, framedrum)
    # a giggle: little fiddle squeaks bending up
    gig = np.zeros(1)
    for k in range(4): gig = place(gig, fiddle(hz('A5') * (1 + .06 * k), .12, 1.0) * .5, 3.3 + k * .1)
    return hall(mix(fd, lt * .8, dr * .5, gig), .28)
def coffer_open():
    n = int(0.9 * SR)
    creak = np.sin(2 * np.pi * np.cumsum(140 + 60 * np.sin(np.linspace(0, 9, n)) + 30 * noise(n)) / SR) * env(n, 0.05, 0.3, 0.5, 0.3)
    creak = lpb(creak * (1 + 0.5 * noise(n)), 1500) * 0.6
    sigh = place(np.zeros(1), chord(choir, ['D3', 'A3', 'F#4'], 1.8, vowel='a'), 0.35)
    return hall(mix(creak, sigh * .7), .35)
def coffer_cheese():
    sad = seq(fiddle, [('G4', .75), ('F#4', .75), ('F4', .75), ('E4', 2.5)], 100, bright=.8)
    ohh = place(np.zeros(1), chord(choir, ['C3', 'G3', 'E4'], 2.0, vowel='o'), 1.9)
    return hall(mix(sad, ohh * .6), .3)

# ---------- nights ----------
def night_start():
    hn = mix(seq(horn, [('D3', 2.2), ('A3', 1.0), ('D3', 1.8)], 90, bright=.45), seq(horn, [('D2', 5.0)], 90, bright=.25) * .6)
    dr = drums([(0, 1), (1.5, .8), (3, 1), (3.5, .7), (4, 1), (4.5, .8), (5, 1.1)], 90)
    rumble = lpb(noise(int(4 * SR)), 110, 4) * env(int(4 * SR), 1.0, 0.5, 0.6, 1.5) * 4
    return hall(mix(hn, dr, rumble), .35, 3.2)
def night_end():
    ch = mix(chord(choir, ['D2', 'A2', 'F3'], 3.2, vowel='a'), place(np.zeros(1), chord(choir, ['D2', 'A2', 'F#3', 'D4'], 3.0, vowel='a'), 2.6))
    st = place(np.zeros(1), chord(fiddle, ['D4', 'F#4', 'A4'], 3.0, bright=.5) * .4, 2.6)
    return hall(mix(ch, st), .42, 3.5)
def hound_night():
    tt = t_(2.4); howl = np.sin(2 * np.pi * np.cumsum(300 + 250 * np.sin(np.pi * tt / 2.4) + 8 * np.sin(2 * np.pi * 6 * tt)) / SR)
    howl = bp(howl + 0.2 * noise(len(tt)), 200, 2500) * env(len(tt), 0.3, 0.3, 0.8, 0.8) * 0.6
    dr = drums([(1.2 + k * .3, .9) for k in range(6)], 60)
    hn = place(np.zeros(1), horn(hz('C3'), 1.6, .6), 2.0)
    return hall(mix(howl, dr, hn * .8), .4, 3.2)
def night_falls():
    return mix(night_start() * 0.8, place(np.zeros(1), chord(choir, ['D2', 'A2'], 3.0, vowel='u') * 0.5, 0.5))

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
    n = int(0.6 * SR); tt = t_(0.6)
    whoosh = np.concatenate([bp(c, f, f * 2.2) for c, f in zip(np.array_split(noise(n), 24), np.geomspace(3000, 300, 24))]) * env(n, 0.005, 0.1, 0.5, 0.3)
    shimmer = chord(choir, ['A4', 'E5'], 0.5, vowel='e', bright=1.5) * 0.4
    return hall(mix(whoosh * 1.4, shimmer, kick(0.3, 110, 45) * 0.5), .3)
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
    x = np.zeros(1)   # septims spilling into a purse
    for k in range(7):
        n = int(0.25 * SR); tt = t_(0.25); f = rng.uniform(2800, 4800)
        coin = (np.sin(2 * np.pi * f * tt) + .5 * np.sin(2 * np.pi * f * 2.7 * tt)) * np.exp(-tt / 0.05) * 0.4
        x = place(x, coin, k * 0.045 + rng.uniform(0, .02))
    x = place(x, lpb(noise(int(0.2 * SR)), 1500) * env(int(0.2 * SR), .01, .05, .4, .1) * .4, 0.3)
    return hall(norm(x, 0.7), 0.2, 1.5)
def hitmarker(): return norm(click(4200, 0.06), 0.45)
def headshot(): return norm(mix(click(2000, 0.06) * 1.3, lp_fast(noise(int(0.08 * SR)), 900, 2) * expdec(int(0.08 * SR), 0.02)), 0.6)
def drink():
    x = np.zeros(1)
    for k in range(4):
        n = int(0.14 * SR); tt = t_(0.14)
        g = np.sin(2 * np.pi * np.cumsum(180 + 220 * tt / 0.14) / SR) * env(n, 0.01, 0.04, 0.5, 0.05)
        x = place(x, lp_fast(g, 800, 1), k * 0.22)
    x = place(x, chord(choir, ['C3', 'G3', 'E4'], 1.2, vowel='a') * 0.6, 0.95)
    return hall(norm(x, 0.7), 0.3)
def knife():
    n = int(0.25 * SR); sweep = np.linspace(500, 4000, n)
    nz = noise(n); w = np.concatenate([lp_fast(c, f, 1) for c, f in zip(np.array_split(nz, 32), np.linspace(500, 4000, 32))])
    return norm(w * env(n, 0.05, 0.05, 0.8, 0.12), 0.6)
def rise():
    n = int(1.3 * SR)
    dirt = lp_fast(noise(n), 400, 2) * env(n, 0.2, 0.3, 0.7, 0.5) * 2
    crumble = np.zeros(n)
    for k in range(14): crumble = place(crumble, lp_fast(noise(int(0.05 * SR)), 1500, 1) * expdec(int(0.05 * SR), 0.01) * 0.4, rng.uniform(0.1, 1.1))[:n]
    groan = choir(hz('A1'), 1.1, vowel='o', voices=2, bright=.6) * 0.8
    return hall(norm(mix(dirt, crumble, place(np.zeros(1), groan, 0.3))), 0.25)

def forge_upgrade():
    # three anvil strikes, a rising choir and a crackle of fire
    x = np.zeros(1)
    for k, f in enumerate((1180, 1240, 1310)):
        n = int(0.9 * SR); tt = t_(0.9)
        ring = sum(np.sin(2 * np.pi * f * r * tt) * np.exp(-tt / (0.35 / r)) / r for r in (1, 2.76, 5.4, 8.9))
        hit = bp(noise(int(0.04 * SR)), 1500, 6000) * 0.8
        x = place(x, ring * 0.35, k * 0.42); x = place(x, hit, k * 0.42)
    x = place(x, chord(choir, ['D3', 'A3', 'F#4', 'D4'], 2.4, vowel='a', bright=1.2) * 0.7, 1.1)
    crack = bp(noise(int(2.2 * SR)), 800, 6000) * (np.random.default_rng(3).random(int(2.2 * SR)) > 0.996) * 2.0
    x = place(x, crack, 1.0)
    return hall(norm(x), 0.3, 2.5)

# ---- wands: no powder, just magic -- shimmering release, airy whoosh, a soft low push
def _whoosh(d, f_hi, f_lo, bands=20):
    n = int(d * SR)
    return np.concatenate([bp(c, f, f * 1.8) for c, f in zip(np.array_split(noise(n), bands), np.geomspace(f_hi, f_lo, bands))]) * env(n, 0.004, d * 0.3, 0.4, d * 0.5)
def _sparkle(d, f, count=6):
    x = np.zeros(int(d * SR))
    for k in range(count):
        ff = f * rng.uniform(0.8, 1.6); tt = t_(0.12)
        x = place(x, np.sin(2 * np.pi * ff * tt) * np.exp(-tt / 0.03) * 0.3, rng.uniform(0, d * 0.6))[:len(x)]
    return x
def cast_light():   # a quick bolt: bright shimmer + short whoosh
    return hall(norm(mix(_whoosh(0.32, 5000, 700) * 1.2, _sparkle(0.3, 2600), zap(1400, 500, 0.18) * 0.5, kick(0.2, 160, 70) * 0.25), 0.8), 0.25, 1.6)
def cast_rapid():   # tight, short so it can repeat fast
    return hall(norm(mix(_whoosh(0.14, 6000, 1200, 12), zap(1800, 700, 0.09) * 0.6, _sparkle(0.12, 3200, 3)), 0.7), 0.12, 0.9)
def cast_scatter(): # a burst of bone shards: wide whoosh + crackle + thump
    crack = bp(noise(int(0.4 * SR)), 900, 5000) * (np.random.default_rng(7).random(int(0.4 * SR)) > 0.985) * 1.6
    return hall(norm(mix(_whoosh(0.5, 3000, 300) * 1.4, crack, kick(0.4, 130, 45) * 0.8), 0.85), 0.3, 1.8)
def cast_heavy():   # a big release: deep push, choir sting, long tail
    return hall(norm(mix(_whoosh(0.8, 4000, 200) * 1.3, kick(0.6, 110, 38) * 1.1, chord(choir, ['E3', 'B3', 'G4'], 0.7, vowel='a', bright=1.3) * 0.45, _sparkle(0.6, 2000, 10)), 0.9), 0.35, 2.4)
def charge_up():    # power gathering: rising hum and shimmer
    d = 0.9; n = int(d * SR)
    hum = _sweep(70, 210, d, tau=10) * np.linspace(0.2, 1, n)
    sh = bp(noise(n), 1500, 6000) * np.linspace(0, 0.6, n) ** 2
    return hall(norm(mix(hum * 0.7, sh * 0.5, _sweep(400, 1600, d, tau=10) * np.linspace(0, 0.35, n)), 0.6), 0.25, 1.2)
def charge_ready(): # fully charged: a bright ping
    return hall(norm(mix(_tone(1318, 0.5, 0.15), _tone(1976, 0.5, 0.12) * 0.6, _sparkle(0.4, 3000, 5) * 0.4), 0.55), 0.35, 1.5)
def mana_empty():   # magic fizzles out
    n = int(0.35 * SR)
    return hall(norm(mix(bp(noise(n), 2000, 7000) * env(n, 0.005, 0.05, 0.3, 0.25) * 0.5, zap(900, 220, 0.3) * 0.5), 0.5), 0.2, 1.0)

# ---- one signature cast per weapon (sheet: weapons.fireSound -> sound:cast_<id>)
def _tone(f, d, tau=0.08, harm=(1, 2.0, 3.0), amp=(1, .4, .2)):
    tt = t_(d); return sum(a * np.sin(2 * np.pi * f * h * tt) for h, a in zip(harm, amp)) * np.exp(-tt / tau)
def _sweep(f0, f1, d, tau=None):
    tt = t_(d); f = f0 * (f1 / f0) ** (tt / d); return np.sin(2 * np.pi * np.cumsum(f) / SR) * np.exp(-tt / (tau or d / 2))
def _hiss(d, lo, hi, a=0.004, r=None):
    n = int(d * SR); return bp(noise(n), lo, hi) * env(n, a, d * 0.2, 0.5, r or d * 0.6)
def _crackle(d, dens=0.992, lo=900, hi=6000, seed=1):
    n = int(d * SR); return bp(noise(n), lo, hi) * (np.random.default_rng(seed).random(n) > dens) * 2.0
def _pack(x, wet=0.25, size=1.4, peak=0.85): return hall(norm(x, peak), wet, size)

def c_sidearm():    return _pack(mix(_whoosh(0.3, 5000, 900), _sparkle(0.28, 2400, 7), _tone(1320, 0.3, 0.09) * 0.4))               # arcane: chime + glitter
def c_centurion():  return _pack(mix(_hiss(0.35, 2500, 9000) * 1.3, _sweep(500, 180, 0.25) * 0.5, _crackle(0.3, 0.995, 300, 1500, 2)))  # venom: hiss + gurgle
def c_triad():      return _pack(mix(chord(choir, ['E4', 'B4'], 0.35, vowel='o', bright=1.4) * 0.7, _sweep(900, 300, 0.25) * 0.5, _whoosh(0.2, 4000, 800)), .35, 2.0)  # soul wail
def c_spider():     return _pack(mix(lpb(noise(int(0.18 * SR)), 1400) * env(int(0.18 * SR), 0.003, 0.03, 0.4, 0.1) * 1.4, _crackle(0.15, 0.99, 600, 3000, 3)), .12, 0.8)  # spore puff
def c_drill():      return _pack(mix(_sweep(220, 90, 0.16, 0.06) * 1.2, lpb(noise(int(0.12 * SR)), 900) * expdec(int(0.12 * SR), 0.03), kick(0.12, 140, 60) * 0.6), .12, 0.8)  # blood squelch
def c_steampump():  return _pack(mix(_whoosh(0.45, 2500, 300) * 1.2, _crackle(0.4, 0.97, 1200, 5000, 4) * 1.4, kick(0.4, 120, 45) * 0.9), .3, 1.8)  # bone burst
def c_marksman():   return _pack(mix(_tone(2093, 0.5, 0.18) * 0.6, _tone(3136, 0.5, 0.12) * 0.35, _whoosh(0.3, 7000, 1500) * 0.8), .4, 2.2)  # moonshard chime
def c_animunculi(): return _pack(mix(_whoosh(0.16, 900, 3500, 10)[::-1] * 1.2, _sweep(160, 80, 0.15) * 0.6), .15, 0.9)  # shadow: reverse rush
def c_kagrenac():   return _pack(mix(_tone(1760, 0.2, 0.05, (1, 2.76, 5.4), (1, .5, .25)) * 0.7, _crackle(0.15, 0.993, 2000, 8000, 5)), .15, 0.9)  # amber: glassy ping
def c_mammothlmg(): return _pack(mix(_hiss(0.18, 400, 2600) * 1.4, _sweep(700, 1400, 0.12) * 0.3), .12, 0.8)  # gale gust
def c_longshot():   return _pack(mix(_sweep(300, 2400, 0.35, 0.3) * 0.7, chord(choir, ['A4', 'E5', 'A5'], 0.6, vowel='e', bright=1.5) * 0.5, kick(0.4, 100, 40) * 0.8, _sparkle(0.6, 3000, 12)), .45, 2.6)  # starfall
def c_aetherium():  return _pack(mix(chord(horn, ['C4', 'G4', 'E5'], 0.7) * 0.6, _whoosh(0.7, 5000, 400) * 1.2, kick(0.5, 120, 40), _sparkle(0.5, 2600, 10)), .4, 2.4)  # dawn burst
def c_flamerepeater(): return _pack(mix(lpb(noise(int(0.2 * SR)), 2200) * env(int(0.2 * SR), 0.01, 0.05, 0.7, 0.1) * 1.4, _crackle(0.2, 0.985, 800, 4000, 6)), .1, 0.7)  # fire stream
def c_frostlancer(): return _pack(mix(_crackle(0.35, 0.97, 2500, 9000, 7) * 1.5, _tone(2600, 0.3, 0.06, (1, 1.5, 2.2), (1, .5, .3)) * 0.5, _hiss(0.3, 4000, 10000) * 0.6), .35, 1.8)  # ice crack
def c_stormcaller(): return _pack(mix(zap(2400, 300, 0.25) * 1.2, _crackle(0.3, 0.96, 1500, 8000, 8) * 1.4), .25, 1.2)  # lightning
def c_embermortar(): return _pack(mix(kick(0.6, 110, 35) * 1.3, lpb(noise(int(0.6 * SR)), 1200) * env(int(0.6 * SR), 0.01, 0.1, 0.5, 0.4) * 1.2, _crackle(0.6, 0.99, 600, 3000, 9)), .35, 2.0)  # fireball
def c_wabbajack():  tt = t_(0.6); w = np.sin(2 * np.pi * np.cumsum(500 + 300 * np.sin(2 * np.pi * 9 * tt)) / SR) * env(len(tt), 0.01, 0.1, 0.6, 0.3); return _pack(mix(w * 0.8, _sparkle(0.5, 1800, 9), _sweep(1200, 300, 0.4) * 0.4), .35, 1.8)  # madness warble
def c_thuumcannon(): return _pack(mix(chord(choir, ['D2', 'A2', 'D3'], 0.9, vowel='a', bright=0.9) * 1.2, kick(0.8, 90, 30) * 1.3, _whoosh(0.9, 2000, 120) * 1.3), .45, 3.0)  # FUS RO DAH
def c_skyforgearc(): return _pack(mix(thunder(1.2) * 0.9, zap(3000, 400, 0.35) * 1.1, _crackle(0.6, 0.96, 1500, 9000, 10)), .4, 2.4)  # skyforge chain
def c_apprentice(): return c_sidearm()


# ---- energy kit (v0.5): every staff and bow cast layered from a transient, an element body, a sub thump and a tail
def _pad(x, n): return np.pad(x, (0, max(0, n - len(x))))[:n] if n else x
def _lay(*parts):
    n = max(len(p) for p in parts); return sum(_pad(p, n) for p in parts)
def _click(d=0.012, lo=2500, hi=12000): n = int(d * SR); return bp(noise(n), lo, hi) * expdec(n, d / 3) * 1.6
def _fm(fc0, fc1, ratio, index, d, tau=None):
    """FM energy: a swept carrier with a modulator; the index decays so the tone blooms then rings clean"""
    tt = t_(d); fc = fc0 * (fc1 / fc0) ** (tt / d)
    ph = 2 * np.pi * np.cumsum(fc) / SR
    mod = index * np.exp(-tt / (d / 3)) * np.sin(ph * ratio)
    return np.sin(ph + mod) * np.exp(-tt / (tau or d / 2.2))
def _sub(f0=90, f1=38, d=0.35): return kick(d, f0, f1)
def _tail(d, lo, hi, a=0.01): n = int(d * SR); return bp(noise(n), lo, hi) * env(n, a, d * 0.25, 0.35, d * 0.6)
def _sat(x, drive=2.0): return np.tanh(x * drive) / np.tanh(drive)
def _pluck(f, d, damp=0.996):
    """Karplus-Strong string: the bowstring"""
    n = int(d * SR); N = max(2, int(SR / f)); buf = rng.uniform(-1, 1, N); out = np.zeros(n)
    for i in range(n):
        out[i] = buf[i % N]; buf[i % N] = damp * 0.5 * (buf[i % N] + buf[(i + 1) % N])
    return out * np.exp(-np.arange(n) / (0.25 * SR))
def _bubbles(d, count=14, seed=3):
    r = np.random.default_rng(seed); n = int(d * SR); out = np.zeros(n)
    for _ in range(count):
        st = r.integers(0, max(1, n - 4000)); f = r.uniform(300, 900); m = int(r.uniform(0.02, 0.05) * SR)
        tt = np.arange(m) / SR; out[st:st + m] += np.sin(2 * np.pi * (f + 900 * tt / tt[-1]) * tt) * np.exp(-tt / 0.012)
    return out
def _rumble(d, f=55): tt = t_(d); return lpb(noise(len(tt)), 160) * 3 * np.exp(-tt / (d / 2)) + np.sin(2 * np.pi * f * tt) * np.exp(-tt / (d / 2)) * 0.6
def _energy(body, tail, sub=None, click=True, wet=.3, size=1.6, peak=0.9):
    parts = [body, tail] + ([sub] if sub is not None else []) + ([_click()] if click else [])
    return hall(norm(_sat(_lay(*parts), 1.6), peak), wet, size)

# staffs (tap casts)
def e_arcane():  return _energy(_lay(_fm(1400, 900, 3.5, 4, 0.45) * 0.8, _sparkle(0.35, 2600, 9) * 0.5), _tail(0.35, 3000, 9000) * 0.5, _sub(110, 50, 0.25) * 0.7)
def e_holy():    return _energy(_lay(_fm(880, 990, 2.0, 2.5, 0.8, 0.5) * 0.6, chord(choir, ['C5', 'G5', 'E6'], 0.7, vowel='a', bright=1.5) * 0.5, _sparkle(0.6, 3200, 12) * 0.5), _tail(0.6, 2500, 9000) * 0.5, _sub(120, 45, 0.45), wet=.45, size=2.6)
def e_shadow():  return _energy(_lay(_fm(160, 70, 1.5, 6, 0.4) * 1.1, _whoosh(0.3, 900, 4000, 10)[::-1] * 0.9), _tail(0.3, 200, 1500) * 0.6, _sub(80, 30, 0.4) * 1.1, wet=.25, size=1.2)
def e_vortex():  return _energy(_lay(_fm(900, 120, 0.5, 5, 0.6) * 0.9, _whoosh(0.5, 300, 5000, 12)[::-1] * 0.8), _rumble(0.6) * 0.6, _sub(70, 28, 0.6), wet=.35, size=2.0)
def e_blood():   return _energy(_lay(lpb(noise(int(0.2 * SR)), 700) * expdec(int(0.2 * SR), 0.05) * 2.0, _fm(220, 90, 1.0, 3, 0.25) * 0.7), _tail(0.2, 300, 1800) * 0.4, _lay(_sub(130, 50, 0.18), np.pad(_sub(120, 45, 0.2), (int(0.16 * SR), 0)) * 0.7), wet=.15, size=0.9)
def e_soul():    return _energy(_lay(chord(choir, ['E4', 'B4', 'G5'], 0.5, vowel='o', bright=1.4) * 0.7, _fm(1300, 600, 2.01, 3, 0.4) * 0.5), _tail(0.4, 1500, 7000) * 0.4, _sub(100, 40, 0.3) * 0.6, wet=.4, size=2.2)
def e_nature():  return _energy(_lay(_pluck(196, 0.4) * 0.9, _pluck(294, 0.35) * 0.5, _crackle(0.3, 0.985, 800, 5000, 4) * 0.8), _tail(0.3, 1500, 6000) * 0.5, _sub(110, 50, 0.22) * 0.6, wet=.2, size=1.2)
def e_venom():   return _energy(_lay(_bubbles(0.4) * 0.9, _hiss(0.35, 2500, 9000) * 0.9, _fm(500, 180, 1.5, 4, 0.3) * 0.5), _tail(0.3, 3000, 9000) * 0.4, _sub(100, 45, 0.2) * 0.5, wet=.2, size=1.0)
# bows (release: the bowstring snap + the element)
def _snap(): return _lay(_pluck(110, 0.35) * 0.8, _click(0.02, 1200, 6000), _whoosh(0.25, 6000, 900) * 0.7)
def e_fire():    return _energy(_lay(_snap(), lpb(noise(int(0.35 * SR)), 2400) * env(int(0.35 * SR), 0.005, 0.08, 0.5, 0.2) * 1.3, _crackle(0.4, 0.98, 800, 5000, 6) * 1.2), _tail(0.3, 500, 3000) * 0.4, _sub(110, 45, 0.25) * 0.7)
def e_ice():     return _energy(_lay(_snap(), _fm(2600, 2200, 1.41, 3, 0.4, 0.15) * 0.5, _crackle(0.35, 0.97, 2500, 9000, 7) * 1.2), _tail(0.35, 4000, 11000) * 0.5, _sub(120, 55, 0.2) * 0.5, wet=.35, size=1.8)
def e_storm():   return _energy(_lay(_snap(), zap(3200, 400, 0.25) * 1.1, _crackle(0.35, 0.95, 1500, 9000, 8) * 1.4), _tail(0.3, 2000, 9000) * 0.5, _sub(100, 40, 0.25) * 0.7, wet=.3, size=1.5)
def e_wind():    return _energy(_lay(_snap(), _whoosh(0.5, 3000, 300) * 1.4, _hiss(0.45, 600, 3500) * 0.8), _tail(0.4, 300, 2500) * 0.6, _sub(90, 40, 0.3) * 0.6, wet=.3, size=1.6)

# signature-move releases: the same element, bigger: sweeping riser into a detonation with a long tail
def _boom(body, color_lo, color_hi, d=1.4, wet=.45, size=3.0):
    riser = _sweep(180, 1800, 0.25, 0.2) * np.linspace(0.2, 1, int(0.25 * SR)) * 0.6
    det = _lay(kick(0.9, 90, 26) * 1.6, _rumble(d) * 0.9, _tail(d, color_lo, color_hi) * 0.9, body)
    return hall(norm(_sat(np.concatenate([riser, det]), 2.0), 0.95), wet, size)
def u_arcane():  return _boom(_lay(_fm(1200, 400, 3.5, 6, 1.0) * 0.7, _sparkle(1.0, 2600, 18) * 0.6), 2500, 9000)
def u_holy():    return _boom(_lay(chord(choir, ['C4', 'G4', 'E5', 'C6'], 1.4, vowel='a', bright=1.6) * 0.7, chord(horn, ['C3', 'G3'], 1.2) * 0.4, _sparkle(1.2, 3000, 20) * 0.6), 2000, 9000, 1.8)
def u_shadow():  return _boom(_lay(_fm(120, 40, 1.5, 8, 1.2) * 1.0, _whoosh(1.0, 600, 4000, 14)[::-1] * 0.8), 150, 1600)
def u_vortex():  return _boom(_lay(_fm(1400, 60, 0.5, 7, 1.6) * 0.9, _whoosh(1.4, 200, 6000, 16)[::-1] * 1.0, _rumble(1.6, 40)), 80, 1200, 1.8)
def u_blood():   return _boom(_lay(lpb(noise(int(0.8 * SR)), 600) * expdec(int(0.8 * SR), 0.2) * 1.6, chord(choir, ['A2', 'E3'], 1.0, vowel='o', bright=0.8) * 0.5), 200, 2000)
def u_soul():    return _boom(_lay(chord(choir, ['E3', 'B3', 'G4', 'E5'], 1.4, vowel='o', bright=1.4) * 0.8, _fm(900, 300, 2.01, 4, 1.0) * 0.5), 1200, 7000, 1.6)
def u_nature():  return _boom(_lay(_pluck(98, 1.0) * 0.9, _crackle(1.0, 0.97, 600, 4000, 11) * 1.2, _rumble(1.0)), 400, 3500)
def u_venom():   return _boom(_lay(_bubbles(1.2, 40, 5) * 1.0, _hiss(1.2, 2000, 9000) * 1.0), 1500, 9000)
def u_fire():    return _boom(_lay(lpb(noise(int(1.4 * SR)), 1600) * env(int(1.4 * SR), 0.01, 0.3, 0.6, 0.9) * 1.6, _crackle(1.4, 0.97, 600, 5000, 12) * 1.4), 300, 3500, 1.8)
def u_ice():     return _boom(_lay(_crackle(1.2, 0.94, 2500, 10000, 13) * 1.6, _fm(2200, 1500, 1.41, 4, 1.0, 0.4) * 0.6), 3000, 11000)
def u_storm():   return _boom(_lay(thunder(1.6) * 1.1, zap(3600, 300, 0.5) * 1.2, _crackle(1.4, 0.94, 1500, 9000, 14) * 1.4), 800, 9000, 1.8)
def u_wind():    return _boom(_lay(_whoosh(1.6, 4000, 150, 20) * 1.8, _hiss(1.4, 300, 3000) * 1.2), 200, 3000, 1.8)

def _ticks(d, rate=9, seed=2):
    n = int(d * SR); out = np.zeros(n); k = 0
    while True:
        st = int(k / rate * SR)
        if st >= n - 600: break
        m = 600; out[st:st + m] += bp(noise(m), 3000, 9000) * expdec(m, 0.002) * (1.0 if k % 2 else 0.6); k += 1
    return out
def e_time():    return _energy(_lay(_ticks(0.35) * 1.2, _fm(1568, 1568, 2.76, 2, 0.6, 0.25) * 0.6, _whoosh(0.3, 5000, 800)[::-1] * 0.5), _tail(0.3, 2500, 8000) * 0.4, _sub(100, 45, 0.25) * 0.6, wet=.35, size=1.8)
def e_prism():   return _energy(_lay(_fm(2637, 2637, 3.01, 2, 0.5, 0.2) * 0.6, _fm(3951, 3520, 1.5, 2, 0.4, 0.15) * 0.4, _sparkle(0.4, 3500, 10) * 0.6), _tail(0.3, 5000, 12000) * 0.4, _sub(110, 55, 0.2) * 0.5, wet=.4, size=2.0)
def u_time():    return _boom(_lay(_ticks(1.4, 14) * 1.2, chord(choir, ['A3', 'E4', 'A4'], 1.4, vowel='o', bright=1.2) * 0.5, _fm(196, 98, 2.0, 5, 1.4) * 0.6), 1500, 7000, 1.8)
def u_prism():   return _boom(_lay(_fm(2093, 1046, 3.01, 4, 1.2, 0.5) * 0.6, _sparkle(1.4, 3500, 24) * 0.8, chord(choir, ['E5', 'B5'], 1.0, vowel='e', bright=1.6) * 0.4), 4000, 12000, 1.6)

ENERGY = {  # weapon id -> element kit
    'sidearm': 'arcane', 'aetherium': 'holy', 'animunculi': 'shadow', 'mammothlmg': 'vortex', 'triad': 'soul',
    'kagrenac': 'time', 'longshot': 'prism', 'centurion': 'venom', 'flamerepeater': 'fire', 'frostlancer': 'ice', 'stormcaller': 'storm', 'embermortar': 'wind'}

CASTS = {k[2:]: v for k, v in globals().items() if k.startswith('c_') and callable(v)}
CASTS.update({wid: globals()['e_' + el] for wid, el in ENERGY.items()})        # the new kit replaces the old casts
UNLEASH = {wid: globals()['u_' + el] for wid, el in ENERGY.items()}

SOUNDS = {
    'brew_trollshide': j_trollshide, 'brew_quicksilver': j_quicksilver, 'brew_twinstrike': j_twinstrike,
    'brew_hearthfire': j_hearthfire, 'brew_windrunner': j_windrunner, 'brew_hawkeye': j_hawkeye,
    'brew_mammoth': j_mammoth, 'brew_frostbite': j_frostbite, 'brew_stormcrown': j_stormcrown,
    'coffer_spin': coffer_spin, 'coffer_open': coffer_open, 'coffer_cheese': coffer_cheese,
    'night_start': night_start, 'night_end': night_end, 'hound_night': hound_night, 'night_falls': night_falls,
    'shot_pistol': shot_pistol, 'shot_smg': shot_smg, 'shot_ar': shot_ar, 'shot_lmg': shot_lmg,
    'shot_shotgun': shot_shotgun, 'shot_sniper': shot_sniper, 'shot_ray': shot_ray,
    'reload_mag': reload_mag, 'reload_shell': reload_shell, 'dry_click': dry_click,
    'cast_light': cast_light, 'cast_rapid': cast_rapid, 'cast_scatter': cast_scatter, 'cast_heavy': cast_heavy, 'charge_up': charge_up, 'charge_ready': charge_ready, 'mana_empty': mana_empty,
    'buy': buy, 'forge_upgrade': forge_upgrade, 'hitmarker': hitmarker, 'headshot': headshot, 'drink': drink, 'knife': knife, 'rise': rise,
}

SOUNDS.update({f'cast_{k}': v for k, v in CASTS.items()})
SOUNDS.update({f'unleash_{k}': v for k, v in UNLEASH.items()})

def write(path, x):
    x = norm(x - np.mean(x))
    loud = np.nonzero(np.abs(x) > 0.004)[0]
    x = fade_tail(x[:min(len(x), (loud[-1] if len(loud) else len(x)) + int(0.05 * SR))])
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
