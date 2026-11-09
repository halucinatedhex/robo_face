#!/usr/bin/env python3
"""
Turn speech into a clip the pumpkin can play: 16 kHz mono PCM plus a
lip-sync cue track, in one .pfc file for LittleFS.

    # from text, using the macOS voice
    python3 tools/make_clip.py --say "Nice of you to drop by" -o data/hello.pfc

    # from a recording
    python3 tools/make_clip.py audio/line1.wav -o data/line1.pfc

    # see what the mouth will do
    python3 tools/make_clip.py audio/line1.wav -o data/line1.pfc --preview

Lip sync is estimated here rather than at runtime: the ESP32 just replays a
cue list, which keeps playback cheap and the sync exact. If Rhubarb Lip Sync
is installed, --rhubarb uses it instead of the built-in estimator.

Container format (little endian), matching src/Audio.cpp:

    char     magic[4]   "PFC1"
    uint32   sample_rate
    uint32   sample_count
    uint16   cue_count
    uint16   reserved
    cue[cue_count]:  uint32 ms, uint8 phoneme, uint8 pad
    int16    samples[sample_count]
"""
import argparse
import json
import os
import struct
import subprocess
import sys
import tempfile
import wave

import numpy as np

# Must match PHONEME_NAMES / the generated artwork order.
PHONEMES = ["AI", "E", "etc", "FV", "L", "MBP", "O", "rest", "U", "WQ"]
IDX = {n: i for i, n in enumerate(PHONEMES)}

RATE = 16000
FRAME = 400          # 25 ms analysis window
HOP = 160            # 10 ms hop -> 100 cues/s before smoothing
MIN_DWELL_MS = 60    # shapes shorter than this read as flicker

# Rhubarb's mouth shapes map almost one-to-one onto ours.
RHUBARB = {"A": "MBP", "B": "etc", "C": "E", "D": "AI",
           "E": "O", "F": "U", "G": "FV", "H": "L", "X": "rest"}


def to_pcm16k(path):
    """Resample anything ffmpeg understands to 16 kHz mono 16-bit."""
    out = tempfile.mktemp(suffix=".wav")
    subprocess.run(["ffmpeg", "-v", "error", "-y", "-i", path,
                    "-ac", "1", "-ar", str(RATE), "-sample_fmt", "s16", out],
                   check=True)
    with wave.open(out, "rb") as w:
        assert w.getframerate() == RATE and w.getnchannels() == 1
        data = w.readframes(w.getnframes())
    os.unlink(out)
    return np.frombuffer(data, dtype="<i2")


def say_to_wav(text, voice=None, rate=None, pitch=None):
    """
    Synthesise with `say`.

    Pitch goes through the [[pbas n]] speech-synthesis tag rather than a
    command-line flag, because `say` has no option for it. Values are roughly
    30 (deep) to 70 (high); each voice has its own default, so the useful
    range is found by ear rather than calculated.
    """
    out = tempfile.mktemp(suffix=".aiff")
    cmd = ["say", "-o", out]
    if voice:
        cmd += ["-v", voice]
    if rate:
        cmd += ["-r", str(rate)]
    if pitch:
        text = f"[[pbas {int(pitch)}]] {text}"
    subprocess.run(cmd + [text], check=True)
    return out


def measure(x):
    """Peak and RMS in dBFS, for reporting."""
    peak = float(np.abs(x).max()) / 32768.0
    rms = float(np.sqrt((x.astype(np.float64) / 32768.0) ** 2).mean()) ** 0.5
    to_db = lambda v: -99.0 if v <= 0 else 20.0 * np.log10(v)
    return to_db(peak), to_db(rms)


def loudness(samples, peak_dbfs, drive):
    """
    Raise the level of a clip.

    The device can only attenuate -- its volume control multiplies by
    vol/100 -- so anything quiet has to be fixed here. Two stages:

      drive > 1   push through a tanh soft-knee, which lifts the quiet parts
                  toward the loud ones. Raises perceived loudness far more
                  than peak normalising alone, because speech is mostly well
                  below its peaks. Harmonically gentler than hard clipping.
      peak_dbfs   then scale so the loudest sample lands here.

    Analysis runs on the untouched audio, so adding drive never degrades the
    lip sync -- soft clipping adds harmonics that would otherwise drag the
    spectral centroid upward and skew the mouth shapes.
    """
    x = samples.astype(np.float32) / 32768.0
    x = x - x.mean()                      # strip any DC offset

    if drive > 1.0:
        peak = float(np.abs(x).max()) or 1.0
        x = np.tanh((x / peak) * drive) / np.tanh(drive) * peak

    target = 10.0 ** (peak_dbfs / 20.0)
    peak = float(np.abs(x).max())
    if peak > 0:
        x = x * (target / peak)

    return np.clip(x * 32768.0, -32768, 32767).astype(np.int16)


def analyse(samples):
    """
    Estimate a mouth shape per frame from loudness and spectral balance.

    Loudness picks how open the mouth is; the spectral centroid separates
    rounded back vowels (low) from spread front vowels (high); a high
    zero-crossing rate with little energy is a fricative, which reads as FV.
    """
    x = samples.astype(np.float32) / 32768.0
    if x.size < FRAME:
        return ["rest"]

    n = 1 + (x.size - FRAME) // HOP
    frames = np.lib.stride_tricks.as_strided(
        x, shape=(n, FRAME), strides=(x.strides[0] * HOP, x.strides[0]))

    win = np.hanning(FRAME).astype(np.float32)
    rms = np.sqrt((frames ** 2).mean(axis=1)) + 1e-9
    db = 20 * np.log10(rms / rms.max())

    zcr = (np.diff(np.signbit(frames), axis=1).sum(axis=1) / FRAME)

    spec = np.abs(np.fft.rfft(frames * win, axis=1))
    freqs = np.fft.rfftfreq(FRAME, 1.0 / RATE)
    centroid = (spec * freqs).sum(axis=1) / (spec.sum(axis=1) + 1e-9)

    out = []
    for i in range(n):
        if db[i] < -38:
            out.append("rest")
            continue
        if zcr[i] > 0.22 and db[i] < -14:
            out.append("FV")
            continue
        loud = db[i]
        c = centroid[i]
        if c < 900:                                  # rounded / back
            out.append("O" if loud > -10 else "U" if loud > -20 else "WQ")
        elif c < 1800:                               # central
            out.append("AI" if loud > -9 else "etc" if loud > -20 else "MBP")
        else:                                        # spread / front
            out.append("E" if loud > -11 else "etc" if loud > -20 else "L")
    return out


def smooth(seq, k=5):
    """Mode filter: kills single-frame flicker without blurring transitions."""
    if len(seq) < k:
        return seq
    half = k // 2
    out = list(seq)
    for i in range(half, len(seq) - half):
        window = seq[i - half:i + half + 1]
        out[i] = max(set(window), key=window.count)
    return out


def to_cues(seq, hop_ms):
    cues, last = [], None
    for i, name in enumerate(seq):
        if name == last:
            continue
        ms = int(i * hop_ms)
        if cues and ms - cues[-1][0] < MIN_DWELL_MS:
            # The previous shape was too brief to see: overwrite it rather
            # than appending. If that makes it match the one before, drop it
            # entirely -- two identical cues in a row is a wasted redraw.
            if len(cues) >= 2 and cues[-2][1] == name:
                cues.pop()
            else:
                cues[-1] = (cues[-1][0], name)
        else:
            cues.append((ms, name))
        last = cues[-1][1]
    if not cues:
        cues = [(0, "rest")]
    elif cues[0][0] > 0:
        cues.insert(0, (0, "rest"))
    return cues


def rhubarb_cues(wav):
    out = subprocess.run(["rhubarb", "-f", "json", wav],
                         check=True, capture_output=True, text=True).stdout
    return [(int(c["start"] * 1000), RHUBARB.get(c["value"], "rest"))
            for c in json.loads(out)["mouthCues"]]


def write_pfc(path, samples, cues):
    with open(path, "wb") as f:
        f.write(b"PFC1")
        f.write(struct.pack("<IIHH", RATE, samples.size, len(cues), 0))
        for ms, name in cues:
            f.write(struct.pack("<IBB", ms, IDX[name], 0))
        f.write(samples.astype("<i2").tobytes())
    return os.path.getsize(path)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("input", nargs="?", help="input audio file")
    ap.add_argument("--say", help="synthesise this text instead")
    ap.add_argument("--voice", help="macOS voice for --say")
    ap.add_argument("--rate", type=int, help="words per minute for --say")
    ap.add_argument("--pitch", type=int, metavar="N",
                    help="voice pitch for --say, via the [[pbas n]] tag. "
                         "Roughly 30 (deep) to 70 (high); each voice has its "
                         "own default, so judge by ear")
    ap.add_argument("-o", "--out", required=True)
    ap.add_argument("--rhubarb", action="store_true",
                    help="use Rhubarb Lip Sync if installed")
    ap.add_argument("--preview", action="store_true",
                    help="print the cue track")
    ap.add_argument("--peak", type=float, default=-1.0,
                    help="normalise so the loudest sample sits at this dBFS "
                         "(default -1). Use --peak 0 to disable normalising.")
    ap.add_argument("--loud", type=float, default=1.0, metavar="DRIVE",
                    help="soft-knee drive before normalising; 1 = off, "
                         "2-4 noticeably louder, above 6 starts to sound "
                         "squashed")
    a = ap.parse_args()

    if not a.input and not a.say:
        ap.error("give an input file or --say TEXT")

    tmp = say_to_wav(a.say, a.voice, a.rate, a.pitch) if a.say else None
    src = tmp or a.input
    samples = to_pcm16k(src)

    if a.rhubarb:
        norm = tempfile.mktemp(suffix=".wav")
        with wave.open(norm, "wb") as w:
            w.setnchannels(1); w.setsampwidth(2); w.setframerate(RATE)
            w.writeframes(samples.tobytes())
        cues = rhubarb_cues(norm)
        os.unlink(norm)
    else:
        cues = to_cues(smooth(analyse(samples)), HOP * 1000.0 / RATE)

    if tmp:
        os.unlink(tmp)

    before = measure(samples)
    if a.peak < 0 or a.loud > 1.0:
        # Cues were computed above from the untouched audio, on purpose.
        samples = loudness(samples, a.peak, a.loud)
    after = measure(samples)

    os.makedirs(os.path.dirname(a.out) or ".", exist_ok=True)
    size = write_pfc(a.out, samples, cues)
    dur = samples.size / RATE

    print(f"{a.out}: {dur:.2f}s, {len(cues)} cues, {size/1024:.1f} KB")
    print(f"    level: peak {before[0]:6.1f} -> {after[0]:6.1f} dBFS   "
          f"rms {before[1]:6.1f} -> {after[1]:6.1f} dBFS")
    if a.preview:
        for ms, name in cues:
            print(f"  {ms/1000:6.2f}s  {name}")


if __name__ == "__main__":
    main()
