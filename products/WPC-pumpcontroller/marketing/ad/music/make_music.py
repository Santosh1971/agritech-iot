"""Synthesises the 30 s background track for the WPC ad -> public/wpc-music.wav.

Structure follows the storyboard:
  0-9 s   tense: A minor pad (Am - F - E), clock ticks, heartbeat, riser into 9 s
  9 s     impact, switch to A major
  9-25 s  bright: A - E - F#m - D at 112 BPM, pluck arpeggio, bass; drums + bell melody from 13 s
  25-30 s final A major chord, fade out
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


# ---------- Part A: 0-9 s, tense ----------
pad(["A2", "C3", "E3"], 0.0, 3.2, gain=0.5, cutoff=700)
pad(["F2", "A2", "C3"], 3.0, 3.2, gain=0.5, cutoff=750)
pad(["E2", "G#2", "B2"], 6.0, 3.1, gain=0.55, cutoff=900)
for k in range(10):          # clock ticks, 0-5 s
    tick(0.25 + k * 0.5)
for k in range(5):           # heartbeat under the problem scene, 5-9 s
    heartbeat(5.0 + k * 0.8)
for i, note in enumerate(["A3", "C4", "E4", "C4"] * 2):   # lonely motif
    pluck(note, 0.5 + i * 0.55, 0.8, 0.10, -0.3)
# riser 7.3-9 s
n = int(1.7 * SR); tt = np.arange(n) / SR
riser = rng.standard_normal(n)
riser = riser - lowpass(riser, 1500)
riser = lowpass(riser, 6000) * (tt / tt[-1]) ** 2
riser += 0.3 * np.sin(2 * np.pi * np.cumsum(200 + 800 * (tt / tt[-1]) ** 2) / SR) * (tt / tt[-1]) ** 2
add(riser, 7.3, 0, 0.35)

# ---------- impact at 9 s ----------
kick(9.0, 0.8)
noise_hit(9.0, 2.0, 2.5, 7000, 0.18, 0.0, hp=True)
bell("A5", 9.0, 2.0, 0.12, 0.2)
bell("E5", 9.0, 2.0, 0.10, -0.2)

# ---------- Part B: 9-25 s, bright, 112 BPM ----------
BPM = 112
beat = 60 / BPM
bar = 4 * beat
chords = [("A", ["A2", "C#3", "E3"], ["A3", "C#4", "E4", "A4"]),
          ("E", ["E2", "G#2", "B2"], ["E3", "G#3", "B3", "E4"]),
          ("F#m", ["F#2", "A2", "C#3"], ["F#3", "A3", "C#4", "F#4"]),
          ("D", ["D2", "F#2", "A2"], ["D3", "F#3", "A3", "D4"])]
start_b, end_b = 9.0, 25.0
bars = int(np.ceil((end_b - start_b) / bar))
arp = [0, 1, 2, 3, 2, 1, 2, 3]
melody = {  # bar index -> (beat offset, note)
    2: [(0, "E5"), (1, "C#5"), (1.5, "E5"), (2, "F#5"), (3, "E5")],
    3: [(0, "D5"), (1, "C#5"), (2, "B4"), (3, "A4")],
    4: [(0, "E5"), (1, "C#5"), (1.5, "E5"), (2, "A5"), (3, "G#5")],
    5: [(0, "F#5"), (1, "E5"), (2, "C#5"), (3, "E5")],
    6: [(0, "E5"), (1, "C#5"), (1.5, "E5"), (2, "F#5"), (3, "E5")],
}
for b in range(bars):
    bt = start_b + b * bar
    if bt >= end_b:
        break
    name, padn, arpn = chords[b % 4]
    length = min(bar, end_b - bt)
    pad(padn, bt, length + 0.1, gain=0.32, cutoff=1400)
    for i in range(8):
        at = bt + i * beat / 2
        if at < end_b:
            pluck(arpn[arp[i]], at, 0.4, 0.13, -0.35 if i % 2 else 0.35)
    for i in (0, 2):
        if bt + i * beat < end_b:
            bass(padn[0].replace("2", "1") if padn[0][-1] == "2" else padn[0], bt + i * beat, beat * 1.8)
    if bt >= 13.0 - 0.01:  # drums from 13 s (feature scene)
        for i in range(4):
            if bt + i * beat >= end_b:
                break
            if i in (0, 2):
                kick(bt + i * beat, 0.5)
            else:
                noise_hit(bt + i * beat, 0.18, 22, 3500, 0.22, 0.0)  # soft clap
            for s16 in range(4):
                noise_hit(bt + i * beat + s16 * beat / 4, 0.05, 70, 6000, 0.05 if s16 % 2 else 0.08, 0.25, hp=True)
    for off, note in melody.get(b, []):
        if bt + off * beat < end_b:
            bell(note, bt + off * beat, 1.2, 0.13, 0.1)

# ---------- Part C: 25-30 s, resolve ----------
pad(["A2", "E3", "A3", "C#4", "E4"], 25.0, 5.0, gain=0.5, cutoff=1800)
bass("A1", 25.0, 4.0, 0.3)
kick(25.0, 0.6)
for i, note in enumerate(["A4", "C#5", "E5", "A5"]):
    bell(note, 25.0 + i * 0.12, 3.0, 0.12, -0.3 + i * 0.2)
noise_hit(25.0, 2.5, 2.0, 7000, 0.12, 0.0, hp=True)

# ---------- master ----------
fade = np.ones(N)
fade[-int(2.5 * SR):] = np.linspace(1, 0, int(2.5 * SR)) ** 1.5
fade[:int(0.05 * SR)] = np.linspace(0, 1, int(0.05 * SR))
mix = np.stack([L, R], axis=1) * fade[:, None]
mix = np.tanh(mix / np.max(np.abs(mix)) * 1.6) / np.tanh(1.6) * 0.85
out = os.path.join(os.path.dirname(__file__), "..", "public", "wpc-music.wav")
with wave.open(out, "wb") as w:
    w.setnchannels(2); w.setsampwidth(2); w.setframerate(SR)
    w.writeframes((mix * 32767).astype("<i2").tobytes())
print("wrote", os.path.abspath(out))
