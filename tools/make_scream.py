#!/usr/bin/env python3
"""
Synthesise a scream, because text-to-speech cannot do one.

`say` either articulates the letters of "aaahhh" or produces a flat sustained
vowel; neither reads as a scream. This builds one from scratch: a harmonic
stack on a swept fundamental, with vibrato, rising breath noise and formant
peaks around the vowel /a/.

    python3 tools/make_scream.py -o /tmp/scream.wav
    python3 tools/make_scream.py -o /tmp/scream.wav --seconds 2.2 --f0 320 --peak 820

Then turn it into a clip as usual:

    python3 tools/make_clip.py /tmp/scream.wav --loud 3 -o data/halloween/scream.pfc

A recorded human scream will always beat this. The pipeline takes any WAV, so
if someone will yell into a phone, use that instead.
"""
import argparse
import wave

import numpy as np

RATE = 16000


def resonator(x, freq, q, rate=RATE):
    """One-pole-pair resonant bandpass, to stand in for a vocal formant."""
    r = np.exp(-np.pi * freq / (q * rate))
    theta = 2 * np.pi * freq / rate
    a1, a2 = -2 * r * np.cos(theta), r * r
    y = np.zeros_like(x)
    for n in range(2, len(x)):
        y[n] = x[n] - a1 * y[n - 1] - a2 * y[n - 2]
    return y


def scream(seconds, f0_start, f0_peak, harmonics, noise_level, seed):
    rng = np.random.default_rng(seed)
    n = int(seconds * RATE)
    t = np.arange(n) / RATE
    frac = t / seconds

    # Pitch rises fast, then sags as the breath runs out.
    sweep = np.where(frac < 0.25, frac / 0.25, 1.0 - 0.45 * (frac - 0.25) / 0.75)
    f0 = f0_start + (f0_peak - f0_start) * sweep

    # Vibrato, deepening as the voice strains.
    f0 = f0 * (1.0 + 0.035 * (0.3 + 0.7 * frac) * np.sin(2 * np.pi * 5.8 * t))

    phase = 2 * np.pi * np.cumsum(f0) / RATE
    sig = np.zeros(n, dtype=np.float64)
    for h in range(1, harmonics + 1):
        # Slight inharmonicity and a random phase keep it from sounding
        # like an organ.
        detune = 1.0 + rng.uniform(-0.004, 0.004)
        sig += (1.0 / h ** 0.85) * np.sin(phase * h * detune + rng.uniform(0, 6.28))

    # Breathiness grows through the scream.
    noise = rng.normal(0, 1, n) * noise_level * (0.25 + 0.75 * frac)
    sig = sig + noise

    # Formants for an open /a/: a shout is mostly these two peaks.
    sig = 0.6 * resonator(sig, 760, 7) + 0.4 * resonator(sig, 1180, 9) + 0.25 * sig

    # Envelope: near-instant attack, long body, quick release.
    env = np.ones(n)
    atk, rel = int(0.02 * RATE), int(0.18 * RATE)
    env[:atk] = np.linspace(0, 1, atk)
    env[-rel:] = np.linspace(1, 0, rel) ** 1.5
    env *= 1.0 - 0.25 * frac  # running out of air
    sig *= env

    sig = sig / (np.abs(sig).max() + 1e-9) * 0.97
    return (sig * 32767).astype("<i2")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-o", "--out", required=True)
    ap.add_argument("--seconds", type=float, default=1.8)
    ap.add_argument("--f0", type=float, default=300.0, help="starting pitch, Hz")
    ap.add_argument("--peak", type=float, default=760.0, help="peak pitch, Hz")
    ap.add_argument("--harmonics", type=int, default=14)
    ap.add_argument("--noise", type=float, default=0.22)
    ap.add_argument("--seed", type=int, default=7)
    a = ap.parse_args()

    s = scream(a.seconds, a.f0, a.peak, a.harmonics, a.noise, a.seed)
    with wave.open(a.out, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(s.tobytes())
    print(f"{a.out}: {len(s)/RATE:.2f}s")


if __name__ == "__main__":
    main()
