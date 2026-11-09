# Testing

Ordered so each step only depends on the ones before it. When something is
broken, start at the top — the point of this order is that a failure localises
itself.

## 1. Is the board alive?

```sh
~/.platformio/penv/bin/python \
  ~/.platformio/packages/tool-esptoolpy/esptool.py \
  --port /dev/cu.usbmodem101 flash-id
```

**Pass:** a real manufacturer and size, e.g. `Manufacturer: cd  Device: 6016
Detected flash size: 4MB`.

**Fail:** `Manufacturer: ff  Device: ffff  Unknown` means the MCU cannot reach
its own flash. The chip still answers over USB, so it will report its type and
MAC and look healthy — it is not. Causes: a wire on GPIO 11–17, or a sagging
3.3 V rail.

This test is cheap and settles "is it the board or is it my wiring", so it is
worth running whenever behaviour is baffling. Unplug the harness entirely and
re-run; if it recovers, the fault is in the harness.

## 2. Do the matrices work?

Flash the diagnostic, which shares nothing with the real firmware — no SPI
peripheral, no driver code, every edge a `digitalWrite` with microseconds of
slack:

```sh
pio run -e c3-diag -t upload
```

It loops five stages, ~40 s per cycle, narrating over serial.

| stage | what it does | what it tells you |
|-------|--------------|-------------------|
| 1 | toggles each pin at 2 Hz for 6 s | the GPIO is alive — meter it, expect ~1.65 V averaged |
| 2 | **DISPLAY TEST** | all 64 LEDs lit *from inside the chip*, bypassing the digit registers |
| 3 | normal init, all digits `0xFF` | register programming works |
| 4 | walking row | row wiring and order |
| 5 | walking column | column wiring and bit order |

**Stage 2 is the important one.** It is the clean split:

- **2 lights, 3 lights** → wiring and power are good.
- **2 lights, 3 dark** → wiring is fine, the fault is in register setup.
- **2 dark** → power or one of the four connections. No firmware change will
  help. Go back to stage 1 and meter the pins.
- **4 marches across instead of down** → the matrix is soldered 90° out. That
  is a different module orientation, not a wiring error.

`DEVICES` in `src/diag/diag.cpp` tracks `MATRIX_COUNT`. Drop it to 1 to bring
up a single module in isolation.

If the far modules misbehave only once the whole chain is connected, drop
`MATRIX_SPI_HZ` to 1 MHz before suspecting the wiring — that one edit
separates signal integrity from a miswire.

## 3. Does the face run?

```sh
pio run -e c3 -t upload
```

Watch the console (see [SETUP.md](SETUP.md) for catching the boot banner).

**Pass:** exactly **one** `robo_face ready`.

More than one banner means a reset loop. An ESP32 panic, watchdog timeout or
brownout all announce themselves by rebooting and reprinting, so a single
banner across a full minute is a real result, not an absence of evidence.

Then just watch it: the mouth holds each idle expression for **10 seconds**,
so give it a moment before concluding a module is dead. The first chomp lands
around 50 s.

## 4. Does the web page work?

```sh
curl -s http://robo-face.local/status
```

**`HTTP 000` with a time under ~10 ms is not a firmware problem.** That is
connection-refused, i.e. nothing listening. Check the obvious first: is the
board powered and enumerated?

```sh
ls /dev/cu.usbmodem*          # no match = not connected at all
```

Only once the board is up and on the network is an empty response worth
investigating.

```json
{"src":"idle","pn":"rest","uni":1,"ch0":1,"ch1":10,"br":5,"rot":0,
 "pk":0,"dc":0,"spk":0,"mot":0,"men":1,"cd":30,"cdl":0,"vol":70,
 "fire":0,"up":12,"hp":189456,"ip":"<prop-ip>"}
```

| field | meaning |
|-------|---------|
| `src` | mouth source: `idle`, `external (show)` or `off` |
| `pn` | current phoneme name |
| `pk` / `dc` | E1.31 packets received / discarded |
| `spk` | a clip is playing |
| `mot` / `men` | sensor reading now / motion triggering enabled |
| `cdl` | cooldown seconds remaining |
| `fire` | how many times motion has triggered |
| `hp` | free heap — watch for a downward trend over hours |

### Is the preview honest?

`/panels` returns what is physically lit, post-rotation, as hex. Decode it and
compare against the artwork:

```sh
curl -s http://robo-face.local/panels | python3 -c "
import sys
h = sys.stdin.read().strip()
r = [int(h[i*2:i*2+2], 16) for i in range(len(h)//2)]
b = lambda v: ''.join('#' if v >> (7-i) & 1 else '.' for i in range(8))
print('eyes:')
for i in range(8): print('  ' + b(r[i]) + '  ' + b(r[8+i]))
print('mouth:')
for i in range(8): print('  ' + b(r[16+i]) + b(r[24+i]))
"
```

A centred pupil looks like `###..###` on rows 3 and 4; the pupil wanders by
±2 as the eyes drift, so sample a few times. This is the quickest way to
confirm orientation and bit order without squinting at the prop.

The same snippet works on saved output, which is handy when the board is
elsewhere:

```sh
echo "3c72f3ffffff7e3c3c72f3ffffff7e3c000000003f00000000000418e0000000" | python3 -c '...'
```

## 5. Does E1.31 work?

No xLights or FPP needed:

```sh
python3 tools/sacn_test.py --iface <your-local-ip>
```

While it runs, watch `/status`:

- `src` → `external (show)`
- `pk` climbing at your `--fps`
- **`dc` staying at 0**
- `src` → `idle` about 2 s after the sender stops

**Test multicast, not just unicast.** Multicast is what FPP uses by default,
and it is the path that can fail on its own.

The `pk`/`dc` pair is the diagnostic that matters later:

| `pk` | `dc` | meaning |
|------|------|---------|
| 0 | 0 | nothing arriving — network, universe, or firewall |
| climbing | 0 | healthy |
| climbing | climbing | arriving but rejected — wrong universe is *not* counted as a discard, so this means malformed packets |
| climbing | 0, but mouth still idle | packets fine, phoneme channels wrong or below threshold 50 |

What this does **not** prove: your xLights channel assignment. The script
sends where you tell it; it cannot confirm you mapped the `Mouth-*` rows the
same way. Use the web page's phoneme buttons against a real sequence for that.

## 6. Does the mouth art look right?

The ten phoneme buttons on the web page fire each shape directly. Each takes
the mouth for 2 s then releases. Use these to check the artwork and, later,
your channel mapping.

## 7. Does audio work?

Firmware side first, no amp needed:

```sh
curl -s "http://robo-face.local/play?c=/hello.pfc"
```

Watch `/status`: `spk` goes to 1, `pn` steps through the cue track, `spk`
returns to 0, and `src` falls back to `idle` ~2 s later. That confirms
playback, cue timing and the handback — everything except the sound.

Then with the amp wired:

| symptom | likely cause |
|---------|--------------|
| nothing audible | filter not connected to GPIO 4; or the PDM *clock* pin wired to the amp by mistake |
| very quiet, muffled | filter capacitor too large — 100 nF puts the corner at 1.6 kHz; use 10 nF |
| loud hiss / whine | not enough carrier rejection — add the second RC stage |
| distorted, crackly | volume too high into the amp's fixed ~24 dB gain; drop to 30 |
| thump at clip start/end | DC blocking cap missing |
| buzz that tracks the LEDs | share a star ground; keep the audio run away from the matrix wiring |

## 8. Does motion work?

`/status` → `mot` should read `motion` when you wave and `clear` a moment
later. If it never changes, the sensor is not reaching GPIO 6 or has no common
ground.

Once it reads correctly, enable triggering and walk past. `fire` increments
and `cdl` counts down.

On an HC-SR501, set its on-time pot to minimum — otherwise its hardware timer
fights the firmware cooldown.

## Host simulation

`Eyes.cpp`, `Mouth.cpp` and `MouthFrames.cpp` compile on a PC against a stub
`MatrixChain` and a fake `millis()`, which is how the blink rate (~1 per
3.3 s) and the idle cycle (50 s / 12.5 s / 4 s) were measured.

**Know the limit.** `millis()` is a variable you control in a simulation.
The one genuine timing bug in this project — an unsigned underflow from
stamping a timestamp with a second, later clock read — was invisible to the
simulation and obvious within seconds on hardware. Simulate animation logic;
test time handling on the device.
