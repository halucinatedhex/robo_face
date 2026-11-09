# Theory of operation

## The one rule

**Nothing blocks.** Every animation is a list of keyframes stepped against
`millis()`. There is no `delay()` in the face code and no recursion.

This is not stylistic. The original AVR sketch burned time in a
`bgProcessing()` helper that recursed once per 20 ms of sleep, so a 3 s wait
nested about 150 frames deep. That is fine on an ATmega and a bad bet against
the ESP32 Arduino loop task's 8 KB stack. It also has to stay true now that
WiFi, a web server, a DMX listener and audio playback all share this loop.

## Main loop

```c
void loop() {
    const uint32_t now = millis();

    handleCommand(control.poll(now));   // input stub (was IR)
    net.update(now);                    // WiFi, web server, E1.31
    trigger.update(now);                // motion sensor

    if (audio.playing()) {              // lip-sync cues -> mouth
        int ph = -1, got;
        while ((got = audio.nextCue()) >= 0) ph = got;
        app::setPhoneme(ph >= 0 ? ph : mouth.phoneme(), now);
    }

    eyes.update(now, facePowered);      // independent
    mouth.update(now);                  // independent
}
```

`now` is captured **once** and passed down. Mixing time sources here has
already caused one real bug — see [HANDOFF.md](../HANDOFF.md), trap 1.

## Module map

```
 main.cpp ──── namespace app ◄──── Net.cpp ──── E131.cpp
    │           (the only door)      │          (sACN parser)
    │                                └──── WebPage.h (static HTML)
    ├─ Eyes.cpp ──┐
    ├─ Mouth.cpp ─┼── MatrixChain.cpp ── hardware SPI ── MAX7219 x4
    │             │        (shadow buffer)
    ├─ Audio.cpp ─┘── I2S PDM ── RC filter ── amp
    ├─ Trigger.cpp ── GPIO
    └─ Settings.cpp ── NVS
```

`App.h` exists so the network layer cannot reach past it into animation state.
Web handlers and the DMX receiver both go through that one interface.

## Eyes

### Keyframe sequencer

Each effect is *compiled* into a step list rather than executed inline:

```c
struct EyeStep { uint8_t op; int16_t a, b; uint16_t ms; };
```

`pushCrossEyes()` and friends append steps; the runner advances when
`now - stepStart >= step.ms`. Ops are `OP_POS`, `OP_PUPIL`, `OP_LID_CLOSE`,
`OP_LID_OPEN`, `OP_LAZY_DOWN`, `OP_LAZY_UP`, `OP_INTENSITY`, `OP_WAIT`.

One pass of the director (`buildIteration()`) is: drift to a random position
within ±2, pause `random(5,7) × 300` ms, and every 4th pass run the next of
seven effects in rotation — cross eyes, round spin, crazy spin, meth eyes,
lazy eye, double wink, glow.

### Two sequences at once

```
 main sequence   ──────────[ long pause ]───────────────────
                              │        ▲
 interjection                 ▼        │  remaining time resumed
                           [ blink ]───┘
```

A blink or twitch preempts the main sequence **wherever it is**, to the
millisecond, and the interrupted step resumes with its remaining time intact.
That is what lets a blink land naturally instead of queueing behind a three
second pause. Without it, blinks can only fall on step boundaries.

Tuning (`include/Eyes.h`): gap `1500–5000 ms`, of which 18% become a one-eye
twitch and 25% of the blinks are a quick double. Measured result: a blink
about every 3.3 s.

A twitch is a fast double lid-flutter on one eye that never closes fully —
deliberately unlike a wink, so it reads as involuntary.

### Why the shadow is per eye

`eyeCurrent_[2][8]` holds what each eye shows, so a lid can reopen onto the
right picture. It has to be per eye: the two differ during cross eyes, meth
eyes and lazy eye, and with one shared buffer a blink landing mid-droop
reopens onto the wrong image and snaps the drooped eye back.

## Mouth

Three sources, one renderer.

```
            ┌─ idle cycle (autonomous)
 mouth ◄────┼─ E1.31 phonemes      ─┐
            └─ clip cue track       ┴─► both call setPhoneme()
```

`MouthSource` is just `Idle` or `External`; both the show and clip playback use
`External`. It reverts to `Idle` after `EXTERNAL_TIMEOUT_MS` (2000 ms) without
a new phoneme, so the prop idles by itself when a sequence ends, the network
drops, or a clip finishes.

During a clip the main loop re-asserts the current phoneme every pass, because
a long pause in speech can exceed the timeout and would otherwise drop the
mouth back to idling mid-sentence.

### Idle cycle

The cycle is a **list of items**, not of frames. An item is either a still
pose, held for `CYCLE_MS` (10 s), or a whole animation, played at its own
rate until it finishes — after which the cycle resumes at the *next* item.

That matters: animations were originally hard-wired to run after the cycle
ended, so they could only be all-or-nothing. As list items they can be
placed anywhere, used individually, or left out.

| animation | frames | rate | duration |
|-----------|--------|------|----------|
| `chomp` (200) | 27, 28, 29, 30 | 50 ms | 250 ticks ≈ 12.5 s |
| `scream` (201) | 17→22→18 ping-pong | 50 ms | 80 ticks ≈ 4 s |
| `wave` (202) | 31–36 | 90 ms | 60 ticks ≈ 5.5 s |

Ids at or above 200 mean animations; below `MOUTH_FRAME_COUNT` they are
poses. The default full list is `7, 8, 9, 15, 25, chomp, scream`, which is
the original sketch's behaviour expressed as data rather than control flow.

**The wave needs six frames, not four.** Its pattern repeats every six
columns, so the original artwork's four frames would jump half a period at
the loop point and stutter. `tools/genart.py` computes all six from one
formula, and asserts that formula reproduces the four that were drawn by hand
before trusting it for the two it adds.

`MOUTH_ANIMS` in `src/Mouth.cpp` is the single source of truth: the web
picker fetches frame lists and timings from it via `/frames` rather than
duplicating them, so a preview cannot drift from what the device plays.
Adding an animation is a frame list and a rate — it appears in the picker
automatically.

### Per-mode idle

Each clip mode has its own idle list (`settings.idleFrames[]`, comma-separated
item ids) and a `calm` flag. Empty list means "use the built-in default",
which is why an untouched prop behaves as it always did. `calm` selects a
default of human expressions only, with no chomp or scream — intended for a
mode impersonating a person.

## Phonemes

Ten shapes, the Preston Blair set xLights uses, in this index order — it is
the channel order and the array order:

| 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 |
|---|---|---|---|---|---|---|---|---|---|
| AI | E | etc | FV | L | MBP | O | rest | U | WQ |

Shapes are generated, not drawn by hand: `tools/phonemes.py` renders an
elliptical opening with optional teeth and tongue, parameterised per phoneme,
so the 16- and 24-column versions cannot drift apart.

## E1.31 / sACN

Listens on UDP 5568, multicast `239.255.<hi>.<lo>` for the universe plus
unicast on the same port.

The receiver (`src/E131.cpp`) is hand-written — the packet is a fixed layout
and the validation is the part you want to be able to read when a show
misbehaves. It checks the ACN packet identifier, the root, framing and DMP
layer vectors and the DMX start code, drops stragglers by sequence number, and
drains up to 8 queued packets so a backlog does not play back in slow motion.

Ten consecutive channels from `startChannel`. The **highest channel above
threshold 50 wins**, which is what xLights emits — it drives exactly one
phoneme to full at a time.

**An all-zero frame is deliberately not activity.** FPP can keep streaming
zeros between sequences; if that counted, the mouth would hold one shape
forever instead of returning to idle.

## Audio

```
 LittleFS .pfc ──► audio task ──► I2S PDM ──► RC filter ──► amp
       │           (read+write)
       └─ cue track ──► main loop ──► setPhoneme()
```

**Lip sync is computed offline.** `tools/make_clip.py` analyses the audio on a
PC and emits a cue list; the firmware only replays it. That is why sync is
exact and playback costs nearly nothing. Driving the mouth from a live audio
envelope was considered and rejected — it produces a nutcracker, not speech.

**Threading.** The audio task does nothing but read the file and push bytes at
I2S. Cues reach the main loop through `Audio::nextCue()`, because the matrix
SPI bus must only ever be touched from one thread. Do not move phoneme updates
into the audio task.

**Position** comes from samples handed to I2S minus the DMA depth
(`512 × 4` frames ≈ 128 ms at 16 kHz), not from `millis()`. If the task is ever
starved, audio and mouth stall *together* rather than drifting apart.

### Clip format (`.pfc`)

Little endian. Written by `make_clip.py`, parsed by `Audio::play()` — **change
both together.**

```
 offset  size  field
 0       4     magic "PFC1"
 4       4     sample_rate      (uint32)
 8       4     sample_count     (uint32)
 12      2     cue_count        (uint16)
 14      2     reserved
 16      6*n   cues: uint32 ms, uint8 phoneme, uint8 pad
 ...     2*m   int16 PCM samples, mono
```

16 kHz 16-bit mono is about 31 KB/s. The filesystem holds ~74 s in total.

## Display driver

`MatrixChain` talks to the chain over hardware SPI at 4 MHz and keeps a shadow
copy of every row, dropping writes that would not change anything. The
animation code calls `setRow()` far more often than the picture actually
changes, so this is most of the speed.

The shadow has one consequence worth knowing: after a rotation flag changes
the *logical* picture is identical, so every write gets dropped and nothing
appears to happen. Hence `MatrixChain::repaint()`, which pushes all eight rows
out unconditionally.

### Self-healing

The same de-duplication has a worse failure mode. If a module loses power
briefly — which a nearby audio amp on a shared 5 V rail causes easily — it
comes back with undefined registers, typically blank (shutdown) or fully lit
(display test). The ESP32 never noticed, its shadow still says the data was
written, and so **it never writes again**. The display stays wrong forever
while the firmware runs perfectly.

The MAX7219 cannot be read back, so there is no way to detect this. Instead
`MatrixChain::tick()` re-asserts everything on a timer — all five control
registers plus every row, unconditionally, every `MATRIX_RESYNC_MS`. About
300 bytes of SPI, roughly 0.6 ms.

Because it re-asserts hardware registers unconditionally, **any per-device
state the resync does not model will be overwritten twice a second.** That is
why `intensity_` is an array rather than a scalar: the eye glow effect ramps
each eye independently, and a broadcast would fight it. Anything added later
that varies per module — scan limit, decode mode — has to be tracked the same
way.

This masks a hardware fault rather than fixing it. A rail sagging enough to
reset the display will eventually reset the MCU, so treat a climbing
`resyncs` counter alongside a dropping-out panel as a power problem to solve,
not a solved one.

`shadowRow()` is also what the web preview reads, so the browser draws what
the LEDs are actually doing, post-rotation.

## Settings

NVS, namespace `pumpkin`, loaded at boot and written on change:

| key | default | meaning |
|-----|---------|---------|
| `universe` | 1 | E1.31 universe |
| `startch` | 1 | first phoneme channel |
| `intensity` | 5 | brightness, 0–15 |
| `rotated` | 0 | bitmask, one bit per module |
| `cooldown` | 30 | seconds of quiet after a trigger |
| `volume` | 70 | software audio gain, 0–100 |
| `motion` | true | trigger on motion |

WiFi credentials are in the same namespace, as `ssid` and `pass`. They are
deliberately ours rather than the IDF's wifi config -- see
[HANDOFF.md](../HANDOFF.md) for why.
