# Tools

Four host-side scripts in `tools/`. All are plain Python 3; only
`make_clip.py` needs anything beyond the standard library (numpy, and ffmpeg
on `PATH`).

## `genart.py` — regenerate the artwork

Emits `src/MouthFrames.cpp`: the idle face frames at both mouth widths plus
the ten phoneme shapes, as C arrays.

```sh
python3 tools/genart.py
```

**Never hand-edit `src/MouthFrames.cpp`.** It is generated, and it carries
both the 16- and 24-column sets.

Two artwork sets live here:

- **Idle face frames** — 35 frames. The 16-column set is extracted *verbatim*
  from the original AVR sketch; the 24-column set was widened by hand and
  lives as ASCII art inside `genart.py`. Edit the ASCII and re-run.
- **Phoneme shapes** — from `phonemes.py`, below.

The script asserts every row is exactly the right width, so the two widths
cannot silently drift apart.

## `phonemes.py` — the ten mouth shapes

Imported by `genart.py`; runnable on its own to preview:

```sh
python3 tools/phonemes.py          # prints all ten at both widths
```

Shapes are drawn parametrically rather than by hand — an elliptical opening
with optional teeth and tongue — so one set of numbers produces both widths
and they stay in the same visual family. The table at the top is the whole
definition:

```python
#  name    style    a     b    teeth tongue
  ("AI",   "open",  0.84, 3.4,  "t",  False),
```

| field | meaning |
|-------|---------|
| `style` | `open` elliptical, `line` lips together, `smile` relaxed, `fv` teeth on lower lip |
| `a` | horizontal semi-axis, fraction of half the canvas |
| `b` | vertical semi-axis in rows: 0.5 shut, 3.5 gaping |
| `teeth` | `t` top, `b` bottom, `tb` both |
| `tongue` | lit lump on the lower lip |

**Array order is channel order.** It matches how the xLights Faces dialog
lists phonemes. Changing the order means re-doing your xLights mapping.

After editing, re-run `genart.py` and rebuild.

## `make_clip.py` — build a speech clip

Turns audio into a `.pfc`: 16 kHz mono PCM plus a lip-sync cue track.

```sh
python3 tools/make_clip.py --say "TEXT" -o data/name.pfc
python3 tools/make_clip.py input.wav   -o data/name.pfc
```

| option | effect |
|--------|--------|
| `--say TEXT` | synthesise with the macOS `say` voice instead of a file |
| `--voice NAME` | pick a voice (`say -v '?'` to list) |
| `--rate WPM` | speaking rate for `--say` |
| `--preview` | print the cue track as timestamps and shapes |
| `--rhubarb` | use Rhubarb Lip Sync instead of the built-in estimator |
| `--peak DBFS` | normalise the loudest sample to this level, default −1. `--peak 0` disables |
| `--loud DRIVE` | soft-knee drive before normalising; 1 = off, 2–4 noticeably louder |
| `-o PATH` | output, required; put it in `data/<mode>/` |

### Making a clip louder

**The device can only attenuate.** Its volume control multiplies samples by
`volume/100`, so 100 is unity gain — if a clip is quiet on flash, no setting
on the web page will fix it. Level has to be built in here.

`--loud` pushes the audio through a tanh soft-knee before normalising, which
lifts the quiet parts toward the peaks. That matters because speech sits well
below its peaks most of the time, so peak-normalising alone buys very little.
Measured on a TTS line:

| | RMS | gain |
|---|-----|------|
| untouched | −13.8 dBFS | — |
| normalise only | −11.3 | +2.5 dB |
| `--loud 2` | −8.5 | +5.3 dB |
| `--loud 3` | −7.2 | +6.6 dB |
| `--loud 5` | −5.6 | +8.2 dB |

3 is a good default. Above about 6 it starts to sound squashed.

The cue track is computed from the **untouched** audio, before any of this.
Soft clipping adds harmonics that would drag the spectral centroid upward and
skew the mouth shapes, so analysing first keeps the lip sync honest however
hard the audio is driven.

Louder clips draw more current through the amp, which is what first
destabilised the 5 V rail on this build — see [HARDWARE.md](HARDWARE.md).

Input can be anything ffmpeg reads; it is resampled to 16 kHz mono 16-bit.

### How the lip sync is estimated

Per 25 ms frame, hopping 10 ms:

- **loudness** → how open the mouth is
- **spectral centroid** → rounded back vowels (low) vs spread front vowels (high)
- **zero-crossing rate, high, with low energy** → a fricative, drawn as `FV`
- below −38 dB from peak → `rest`

Then a 5-frame mode filter kills single-frame flicker, and cues are emitted on
change with a 60 ms minimum dwell. Flicker is the thing to watch for: without
the filter the mouth buzzes.

It is an estimate, not phoneme recognition. At eight rows of LEDs that is
almost certainly indistinguishable, but `--rhubarb` is there if you want real
phoneme alignment — install Rhubarb Lip Sync and pass the flag.

Use `--preview` before flashing. If a line looks wrong, the cue list usually
tells you why: all `rest` means the audio is too quiet, constant `FV` means it
is noisy or clipped.

### Clip format

Documented in the script's docstring and parsed in `src/Audio.cpp`. **If you
change one, change the other.** See [THEORY.md](THEORY.md) for the layout.

## `make_scream.py` — synthesise a scream

Text-to-speech cannot scream. `say` either articulates the letters of
"aaahhh" over several seconds — diction, which is the opposite of a scream —
or gives a flat sustained vowel with a single mouth cue. Neither works.

This builds one instead: a harmonic stack on a swept fundamental, with
vibrato that deepens as the voice strains, rising breath noise, and resonators
tuned to the two formants of an open /a/.

```sh
python3 tools/make_scream.py -o audio/scream.wav --seconds 2.5 --noise 0.3
python3 tools/make_clip.py audio/scream.wav --loud 2 -o data/halloween/scream.pfc
```

Those are the settings actually in use; `audio/scream.wav` is kept in the repo
so the clip is reproducible.

| option | default | effect |
|--------|---------|--------|
| `--seconds` | 1.8 | length |
| `--f0` | 300 | starting pitch, Hz |
| `--peak` | 760 | peak pitch, Hz |
| `--harmonics` | 14 | more is brighter and harsher |
| `--noise` | 0.22 | breathiness; 0.3+ is raspy |
| `--seed` | 7 | changes the detuning and noise |

The pitch sweep is what makes it usable visually: a flat vowel produces one
mouth cue, while a sweep moves the spectral centroid and yields six to eight,
so the mouth animates through the scream.

**A recorded human scream beats this.** The pipeline takes any WAV.

## `sacn_test.py` — send E1.31 without a show controller

Builds genuine ANSI E1.31-2018 packets, so nothing about your xLights or FPP
setup needs to exist or change.

```sh
python3 tools/sacn_test.py                             # multicast, walk all ten
python3 tools/sacn_test.py --iface 192.168.1.5         # pick the local NIC
python3 tools/sacn_test.py --host 192.168.1.50         # unicast
python3 tools/sacn_test.py --hold AI                   # hold one shape
python3 tools/sacn_test.py --universe 7 --start 33     # different mapping
```

| option | default | meaning |
|--------|---------|---------|
| `--universe` | 1 | universe to send to |
| `--start` | 1 | first phoneme channel |
| `--host` | — | unicast target; omit for multicast |
| `--iface` | — | local IP to send multicast *from* |
| `--hold NAME` | — | hold one phoneme |
| `--fps` | 10 | frames per second |
| `--seconds` | 12 | how long to run |

**`--iface` is needed on a machine with more than one network interface.**
Otherwise the OS picks an arbitrary one and the send fails with
`No route to host`. Unicast does not need it.

## Host simulations

Not in `tools/` — they are scratch harnesses, but worth knowing they are
possible. `Eyes.cpp`, `Mouth.cpp` and `MouthFrames.cpp` have no hardware
dependencies beyond `MatrixChain`, so they compile on a PC against a stub
driver and a fake `millis()`. That is how the blink rate and the idle cycle
timings were measured.

The limit is important: **`millis()` is a variable you control in a
simulation**, which is exactly how the one real timing bug in this project got
past it. Simulations check animation logic, not time handling.
