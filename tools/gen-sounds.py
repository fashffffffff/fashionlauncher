#!/usr/bin/env python3
# SPDX-License-Identifier: CC0-1.0
"""Synthesize the launcher's UI sounds from scratch (no samples, so the output is CC0 by construction).

Writes 48 kHz 16-bit mono WAV files into launcher/qmlui/sounds/. Standard library only.
Usage: python3 tools/gen-sounds.py [output_dir]
"""
import math
import os
import random
import struct
import sys
import wave

RATE = 48000
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "launcher", "qmlui", "sounds")


def env(t, attack, decay):
    """Fast attack, exponential decay."""
    if t < attack:
        return t / attack
    return math.exp(-(t - attack) / decay)


def tone(freq, t, phase=0.0):
    return math.sin(2 * math.pi * freq * t + phase)


def render(length, fn):
    return [fn(i / RATE) for i in range(int(length * RATE))]


def fade_edges(samples, ms=3):
    n = int(RATE * ms / 1000)
    for i in range(min(n, len(samples))):
        samples[-1 - i] *= i / n
    return samples


def write(name, samples, peak):
    top = max(abs(s) for s in samples) or 1.0
    samples = fade_edges([s / top * peak for s in samples])
    path = os.path.join(OUT, name + ".wav")
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(b"".join(struct.pack("<h", int(max(-1, min(1, s)) * 32767)) for s in samples))
    print(f"{path}: {len(samples) / RATE * 1000:.0f} ms")


def click(t):
    # A soft wooden tick: a short, low-pitched sine with a quick downward chirp.
    f = 1400 - 600 * min(t / 0.012, 1)
    return tone(f, t) * env(t, 0.0008, 0.010) + 0.25 * tone(f * 2.01, t) * env(t, 0.0005, 0.004)


def toggle(t):
    # Two tiny ticks, the second a fifth higher.
    a = tone(900, t) * env(t, 0.001, 0.012)
    t2 = t - 0.045
    b = tone(1350, t2) * env(t2, 0.001, 0.014) if t2 >= 0 else 0.0
    return a + b


def success(t):
    # A gentle rising major third, bell-like (sine + quiet octave partial).
    out = 0.0
    for start, f in ((0.0, 784.0), (0.09, 987.8)):
        u = t - start
        if u >= 0:
            out += (tone(f, u) + 0.2 * tone(f * 2, u)) * env(u, 0.004, 0.16)
    return out


def error(t):
    # Two low, muted bumps a step apart, falling.
    out = 0.0
    for start, f in ((0.0, 330.0), (0.11, 277.2)):
        u = t - start
        if u >= 0:
            out += (tone(f, u) + 0.35 * tone(f * 1.5, u)) * env(u, 0.004, 0.07)
    return out


_rng = random.Random(7)
_lp = [0.0]


def launch(t):
    # Whoosh: filtered noise with a rising cutoff and a swell, plus a quiet rising tone underneath.
    length = 0.55
    x = t / length
    cutoff = 0.02 + 0.25 * x
    _lp[0] += cutoff * (_rng.uniform(-1, 1) - _lp[0])
    swell = math.sin(math.pi * min(x, 1)) ** 2
    rise = tone(180 + 260 * x * x, t) * 0.25 * swell
    return _lp[0] * 3.0 * swell + rise


def main():
    os.makedirs(OUT, exist_ok=True)
    # peaks are deliberately low: these play on every press
    write("click", render(0.05, click), 0.35)
    write("toggle", render(0.10, toggle), 0.30)
    write("success", render(0.45, success), 0.30)
    write("error", render(0.35, error), 0.30)
    write("launch", render(0.55, launch), 0.35)


if __name__ == "__main__":
    main()
