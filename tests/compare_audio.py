#!/usr/bin/env python3
"""Compare a recording of the FM-1 with the plugin engine's render of the same note.

    compare_audio.py <hardware.wav> <plugin.wav> [expected_note]

Aligns both on their onset, then reports pitch, level, spectral balance,
amplitude envelope and the rate/depth of amplitude modulation (the LFO),
side by side. Levels are relative: the synth's MASTER knob and output stage
scale its recording.
"""
import sys

import numpy as np
from scipy.io import wavfile
from scipy.signal import find_peaks


def load(path):
    sr, x = wavfile.read(path)
    x = x.astype(np.float64)
    if x.ndim > 1:
        x = x.mean(axis=1)
    x /= float(np.iinfo(np.int32).max) if np.abs(x).max() > 2 else 1.0
    peak = np.abs(x).max()
    return sr, x, peak


def onset(x, sr):
    env = np.abs(x)
    thr = env.max() * 0.05
    i = int(np.argmax(env > thr))
    return max(0, i - int(0.002 * sr))


def f0(seg, sr, fmin=30, fmax=2000):
    seg = seg - seg.mean()
    ac = np.correlate(seg, seg, mode="full")[len(seg) - 1:]
    lo, hi = int(sr / fmax), int(sr / fmin)
    lag = lo + int(np.argmax(ac[lo:hi]))
    # parabolic refinement
    if 1 <= lag < len(ac) - 1:
        a, b, c = ac[lag - 1], ac[lag], ac[lag + 1]
        lag = lag + 0.5 * (a - c) / (a - 2 * b + c)
    return sr / lag


def spectrum_summary(seg, sr, f):
    w = np.hanning(len(seg))
    mag = np.abs(np.fft.rfft(seg * w))
    freqs = np.fft.rfftfreq(len(seg), 1 / sr)
    centroid = float((freqs * mag).sum() / mag.sum())
    harm = []
    for k in range(1, 13):
        band = (freqs > f * k * 0.97) & (freqs < f * k * 1.03)
        harm.append(mag[band].max() if band.any() else 0.0)
    harm = np.array(harm)
    harm_db = 20 * np.log10(harm / harm.max() + 1e-9)
    return centroid, harm_db


def envelope(x, sr, hop=0.01):
    n = int(hop * sr)
    frames = len(x) // n
    return np.array([np.sqrt(np.mean(x[i * n:(i + 1) * n] ** 2)) for i in range(frames)])


def am_rate(env, hop=0.01):
    e = env - np.convolve(env, np.ones(25) / 25, mode="same")   # remove the slow envelope
    e = e[30:-30]
    if len(e) < 50:
        return 0.0, 0.0
    spec = np.abs(np.fft.rfft(e * np.hanning(len(e))))
    freqs = np.fft.rfftfreq(len(e), hop)
    band = (freqs > 0.5) & (freqs < 30)
    k = np.argmax(spec[band])
    depth = float(np.std(e) / (np.mean(env[30:-30]) + 1e-12))
    return float(freqs[band][k]), depth


def main():
    hw_path, pl_path = sys.argv[1], sys.argv[2]
    note = int(sys.argv[3]) if len(sys.argv) > 3 else None
    rows = []
    for label, path in (("FM-1", hw_path), ("plugin", pl_path)):
        sr, x, peak = load(path)
        x = x[onset(x, sr):]
        x = x / (np.abs(x).max() + 1e-12)
        steady = x[int(0.25 * sr):int(0.75 * sr)]
        f = f0(steady, sr)
        centroid, harm = spectrum_summary(steady, sr, f)
        env = envelope(x, sr)
        rate, depth = am_rate(env[: int(2.0 / 0.01)])
        t50 = next((i * 0.01 for i, v in enumerate(env) if i > 10 and v < env.max() * 0.5), None)
        rows.append((label, f, centroid, harm, rate, depth, t50, env))
    expected = 440.0 * 2 ** ((note - 69) / 12) if note is not None else None
    print(f"{'':14s}{'FM-1':>12s}{'plugin':>12s}")
    if expected:
        print(f"{'expected f0':14s}{expected:12.1f}")
    print(f"{'f0 Hz':14s}{rows[0][1]:12.1f}{rows[1][1]:12.1f}   ratio {rows[1][1] / rows[0][1]:.3f}")
    print(f"{'centroid Hz':14s}{rows[0][2]:12.0f}{rows[1][2]:12.0f}")
    print(f"{'AM rate Hz':14s}{rows[0][4]:12.2f}{rows[1][4]:12.2f}")
    print(f"{'AM depth':14s}{rows[0][5]:12.3f}{rows[1][5]:12.3f}")
    print(f"{'-6 dB at s':14s}{(rows[0][6] or 0):12.2f}{(rows[1][6] or 0):12.2f}")
    print("harmonics dB (1..12):")
    for r in rows:
        print(f"  {r[0]:7s}" + " ".join(f"{v:6.1f}" for v in r[3]))
    n = min(len(rows[0][7]), len(rows[1][7]))
    a, b = rows[0][7][:n], rows[1][7][:n]
    print(f"envelope correlation: {np.corrcoef(a, b)[0, 1]:.3f}")


if __name__ == "__main__":
    main()
