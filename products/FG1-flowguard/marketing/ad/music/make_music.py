"""Synthesises the 30 s background track for the FG1 ad -> public/fg1-music.wav.

Lighter and friendlier than the WPC track, following the FG1 storyboard:
  0-9 s   morning chore: gentle D-minor-ish sway (Dm - Bb - C - A), plodding footsteps, sigh
  9 s     soft chime + whoosh, switch to D major
  9-25 s  happy D - A - Bm - G at 100 BPM: marimba plucks, bass; light drums + whistle-like
          melody from 13 s; little "drip" blips during the watering scene (13-20 s)
  25-30 s final D major chord with bells, fade out
"""
import os, wave
import numpy as np

SR = 44100
DUR = 30.0
N = int(SR * DUR)
t = np.arange(N) / SR
L = np.zeros(N)
R = np.zeros(N)
rng = np.random.default_rng(7)


def hz(note):
    names = {"C": 0, "C#": 1, "D": 2, "D#": 3, "E": 4, "F": 5, "F#": 6, "G": 7, "G#": 8, "A": 9, "A#": 10, "B": 11}
    n, o = note[:-1], int(note[-1])
    return 440.0 * 2 ** ((names[n] + 12 * (o + 1) - 69) / 12)


def lowpass(x, cutoff):
    a = np.exp(-2 * np.pi * cutoff / SR)
    try:
        from scipy.signal import lfilter
        return lfilter([1 - a], [1, -a], x)
    except ImportError:
        y = np.empty_like(x); acc = 0.0
        for i, v in enumerate(x):
            acc = (1 - a) * v + a * acc; y[i] = acc
        return y


def add(sig, start, pan=0.0, gain=1.0):
    i = int(start * SR)
    if i >= N:
        return
    sig = sig[: N - i] * gain
    L[i:i + len(sig)] += sig * np.sqrt(0.5 * (1 - pan))
    R[i:i + len(sig)] += sig * np.sqrt(0.5 * (1 + pan))


def env_adsr(n, a, d, s, r_):
    e = np.ones(n) * s
    na, nd, nr = int(a * SR), int(d * SR), int(r_ * SR)
    e[:na] = np.linspace(0, 1, na)
    e[na:na + nd] = np.linspace(1, s, len(e[na:na + nd]))
    if nr:
        e[-nr:] *= np.linspace(1, 0, nr)
    return e


def pad(notes, start, length, gain=0.12, cutoff=900):
    n = int(length * SR); tt = np.arange(n) / SR
    s = np.zeros(n)
    for note in notes:
        f = hz(note)
        for det in (-0.12, 0.0, 0.12):
            ff = f * 2 ** (det / 12)
            for h in range(1, 6):  # soft saw
                s += np.sin(2 * np.pi * ff * h * tt + h) / h
    s = lowpass(s, cutoff) * env_adsr(n, min(1.2, length / 3), 0.3, 0.9, min(1.0, length / 3))
    add(s / (len(notes) * 3), start, 0, gain)


def pluck(note, start, length=0.45, gain=0.22, pan=0.0):
    n = int(length * SR); tt = np.arange(n) / SR; f = hz(note)
    s = sum(np.sin(2 * np.pi * f * h * tt) * np.exp(-tt * (6 + 4 * h)) / h for h in range(1, 6))
    s *= np.minimum(1, tt / 0.003)
    add(s, start, pan, gain)


def bell(note, start, length=1.6, gain=0.16, pan=0.0):
    n = int(length * SR); tt = np.arange(n) / SR; f = hz(note)
    mod = np.sin(2 * np.pi * f * 3.5 * tt) * 2.0 * np.exp(-tt * 4)
    s = np.sin(2 * np.pi * f * tt + mod) * np.exp(-tt * 2.8) * np.minimum(1, tt / 0.002)
    add(s, start, pan, gain)


def bass(note, start, length, gain=0.28):
    n = int(length * SR); tt = np.arange(n) / SR; f = hz(note)
    s = (np.sin(2 * np.pi * f * tt) + 0.3 * np.sin(4 * np.pi * f * tt)) * env_adsr(n, 0.01, 0.1, 0.8, 0.08)
    add(s, start, 0, gain)


def kick(start, gain=0.55):
    n = int(0.35 * SR); tt = np.arange(n) / SR
    f = 45 + 90 * np.exp(-tt * 30)
    s = np.sin(2 * np.pi * np.cumsum(f) / SR) * np.exp(-tt * 9)
    add(s, start, 0, gain)


def noise_hit(start, length, decay, cutoff, gain, pan=0.0, hp=False):
    n = int(length * SR); tt = np.arange(n) / SR
    x = rng.standard_normal(n)
    x = x - lowpass(x, cutoff) if hp else lowpass(x, cutoff)
    add(x * np.exp(-tt * decay), start, pan, gain)


def tick(start):
    noise_hit(start, 0.04, 120, 3000, 0.35, 0.3, hp=True)
    n = int(0.03 * SR); tt = np.arange(n) / SR
    add(np.sin(2 * np.pi * 2200 * tt) * np.exp(-tt * 150), start, 0.3, 0.12)


def heartbeat(start):
    for off, g in ((0, 0.5), (0.22, 0.35)):
        n = int(0.25 * SR); tt = np.arange(n) / SR
        add(np.sin(2 * np.pi * 52 * tt) * np.exp(-tt * 14), start + off, 0, g)


def marimba(note, start, length=0.5, gain=0.2, pan=0.0):
    n = int(length * SR); tt = np.arange(n) / SR; f = hz(note)
    s = (np.sin(2 * np.pi * f * tt) * np.exp(-tt * 9) + 0.35 * np.sin(2 * np.pi * f * 4 * tt) * np.exp(-tt * 30))
    add(s * np.minimum(1, tt / 0.002), start, pan, gain)


def whistle(note, start, length, gain=0.09, pan=0.15):
    n = int(length * SR); tt = np.arange(n) / SR; f = hz(note)
    vib = 0.004 * np.sin(2 * np.pi * 5.5 * tt) * np.minimum(1, tt / 0.15)
    s = np.sin(2 * np.pi * f * np.cumsum(1 + vib) / SR) * env_adsr(n, 0.04, 0.05, 0.85, min(0.12, length / 3))
    add(s, start, pan, gain)


def drip(start, gain=0.10, pan=0.0):
    n = int(0.12 * SR); tt = np.arange(n) / SR
    f = 900 + 1400 * (tt / tt[-1])
    add(np.sin(2 * np.pi * np.cumsum(f) / SR) * np.exp(-tt * 40), start, pan, gain)


def step(start):
    n = int(0.12 * SR); tt = np.arange(n) / SR
    add(np.sin(2 * np.pi * 80 * tt) * np.exp(-tt * 30), start, 0, 0.25)
    noise_hit(start, 0.06, 60, 900, 0.10)


# ---------- Part A: 0-9 s, the daily chore ----------
for i, (ch, t0) in enumerate([(["D3", "F3", "A3"], 0.0), (["A#2", "D3", "F3"], 2.25), (["C3", "E3", "G3"], 4.5), (["A2", "C#3", "E3"], 6.75)]):
    pad(ch, t0, 2.4, gain=0.34, cutoff=900)
    for k, note in enumerate([ch[0], ch[1], ch[2], ch[1]]):
        marimba(note.replace("2", "4").replace("3", "4"), t0 + k * 0.56, 0.5, 0.10, -0.3 + 0.2 * k)
for k in range(10):   # plodding footsteps with the buckets
    step(0.3 + k * 0.5)
# "sigh": a falling whistle at 4.2 s
n = int(0.7 * SR); tt = np.arange(n) / SR
add(np.sin(2 * np.pi * np.cumsum(700 - 300 * tt / tt[-1]) / SR) * env_adsr(n, 0.05, 0.1, 0.7, 0.3), 4.2, 0.2, 0.06)
# soft whoosh into 9 s
n = int(1.4 * SR); tt = np.arange(n) / SR
w = rng.standard_normal(n); w = lowpass(w - lowpass(w, 800), 5000) * (tt / tt[-1]) ** 2
add(w, 7.6, 0, 0.22)

# ---------- 9 s: chime ----------
for i, note in enumerate(["D5", "F#5", "A5", "D6"]):
    bell(note, 9.0 + i * 0.07, 1.8, 0.10, -0.3 + 0.2 * i)
kick(9.0, 0.45)

# ---------- Part B: 9-25 s, happy, 100 BPM ----------
BPM = 100
beat = 60 / BPM
bar = 4 * beat
chords = [(["D2", "F#2", "A2"], ["D4", "F#4", "A4", "D5"]),
          (["A1", "C#2", "E2"], ["A3", "C#4", "E4", "A4"]),
          (["B1", "D2", "F#2"], ["B3", "D4", "F#4", "B4"]),
          (["G1", "B1", "D2"], ["G3", "B3", "D4", "G4"])]
start_b, end_b = 9.0, 25.0
pattern = [0, 2, 1, 3, 2, 1, 3, 2]
melody = {  # bar -> (beat, note, length in beats)
    2: [(0, "F#5", 1), (1, "A5", 1), (2, "B5", 0.5), (2.5, "A5", 0.5), (3, "F#5", 1)],
    3: [(0, "E5", 1.5), (1.5, "D5", 0.5), (2, "E5", 2)],
    4: [(0, "F#5", 1), (1, "A5", 1), (2, "D6", 1), (3, "B5", 1)],
    5: [(0, "A5", 1.5), (1.5, "F#5", 0.5), (2, "E5", 1), (3, "D5", 1)],
    6: [(0, "F#5", 1), (1, "A5", 1), (2, "B5", 0.5), (2.5, "A5", 0.5), (3, "F#5", 1)],
}
b = 0
while start_b + b * bar < end_b:
    bt = start_b + b * bar
    padn, arpn = chords[b % 4]
    pad([p.replace("1", "3").replace("2", "3") for p in padn], bt, min(bar, end_b - bt) + 0.1, gain=0.22, cutoff=1500)
    for i in range(8):
        at = bt + i * beat / 2
        if at < end_b:
            marimba(arpn[pattern[i]], at, 0.45, 0.15, -0.35 if i % 2 else 0.35)
    for i in (0, 2, 3):
        if bt + i * beat < end_b:
            bass(padn[0], bt + i * beat, beat * (0.9 if i == 3 else 1.7), 0.26)
    if bt >= 13.0 - 0.01:
        for i in range(4):
            at = bt + i * beat
            if at >= end_b:
                break
            if i in (0, 2):
                kick(at, 0.38)
            else:
                noise_hit(at, 0.12, 30, 4000, 0.14)          # soft snap
            noise_hit(at + beat / 2, 0.04, 90, 7000, 0.05, 0.3, hp=True)
    for off, note, ln in melody.get(b, []):
        if bt + off * beat < end_b:
            whistle(note, bt + off * beat, ln * beat * 0.95)
    b += 1
# water drips during the watering scene (13-20 s)
for k in range(18):
    drip(13.3 + k * 0.37 + (0.08 if k % 3 == 0 else 0), 0.07, -0.5 + (k % 5) * 0.25)

# ---------- Part C: 25-30 s, resolve ----------
pad(["D3", "A3", "D4", "F#4", "A4"], 25.0, 5.0, gain=0.42, cutoff=1900)
bass("D2", 25.0, 4.0, 0.3)
kick(25.0, 0.45)
for i, note in enumerate(["D5", "F#5", "A5", "D6"]):
    bell(note, 25.0 + i * 0.12, 3.0, 0.11, -0.3 + i * 0.2)

# ---------- master ----------
fade = np.ones(N)
fade[-int(2.5 * SR):] = np.linspace(1, 0, int(2.5 * SR)) ** 1.5
fade[:int(0.05 * SR)] = np.linspace(0, 1, int(0.05 * SR))
mix = np.stack([L, R], axis=1) * fade[:, None]
mix = np.tanh(mix / np.max(np.abs(mix)) * 1.5) / np.tanh(1.5) * 0.85
out = os.path.join(os.path.dirname(__file__), "..", "public", "fg1-music.wav")
with wave.open(out, "wb") as w:
    w.setnchannels(2); w.setsampwidth(2); w.setframerate(SR)
    w.writeframes((mix * 32767).astype("<i2").tobytes())
print("wrote", os.path.abspath(out))
