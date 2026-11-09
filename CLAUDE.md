# Working on robo_face

A talking Halloween pumpkin: ESP32-C3, four MAX7219 LED matrices as a face,
PDM audio into an analog amp, PIR motion trigger. Runs standalone, and also
takes E1.31 lip sync from xLights/FPP.

**Read [HANDOFF.md](HANDOFF.md) before changing anything.** It holds the
known-good configuration, the design decisions, and a traps list that has
grown out of real debugging sessions. `docs/` has the rest.

## Build and flash

```sh
~/.platformio/penv/bin/pio run -e c3 -t upload     # firmware
~/.platformio/penv/bin/pio run -e c3 -t uploadfs   # audio clips in data/
```

**Use that path, not the Homebrew `pio`** — the Homebrew one runs on Python
3.9, which current PlatformIO rejects outright.

**`uploadfs` is a separate step.** Changing anything in `data/` needs it;
changing code needs `upload`. Adding a clip needs both.

**Always run a bare `pio run` before finishing.** It builds all four
environments. `esp32dev` silently broke once because a new pin was added only
to the ESP32-C3 branch of `Config.h`, and nothing noticed until a full build.

**The board is often unplugged.** Check `ls /dev/cu.usbmodem*` before
flashing; the port name changes between resets, so let PlatformIO auto-detect
it. There is no OTA (that partition was traded for audio storage).

Serial is USB CDC, so the port only exists after boot and the banner is lost
if you attach afterwards. To catch it, open the port *then* toggle DTR/RTS.
`pio device monitor` needs a real terminal and fails without one.

## Rules of engagement

**Do not change device settings without asking.** The owner tunes volume,
brightness, cooldown and mode by hand. Silently changing them has caused
confusion more than once. If a test needs a setting changed, change it, say
so, and put it back.

**Suspect power before firmware.** Three separate faults on this build were
power: fanning 5 V from the dev board, corroded dupont pins, and running from
USB alone. Each wore a different disguise — dead panels, drifting brightness,
a twitchy motion sensor. Keep the 5 V brick connected while flashing; USB is
for data only.

**Verify a patch applied, not just that it compiled.** Python
`str.replace()` and `sed` fail silently on a near-miss. A one-line change to
call `loadFrames()` did not apply because the file used two-space indent and
the pattern had four; the build and flash both succeeded and a web section
rendered empty. Assert on the *result*.

**Trust the hardware over the tooling.** `Wire.endTransmission()` on a
zero-length write returns success on this core without touching the bus,
which made a scan report all 126 I2C addresses present and sent a debugging
session off after a non-existent fault. When a diagnostic disagrees with an
electrical measurement, suspect the diagnostic.

## Generated files — do not hand-edit

| file | regenerate with |
|------|-----------------|
| `src/MouthFrames.cpp` | `python3 tools/genart.py` |

Artwork lives as ASCII and parameters in `tools/genart.py` and
`tools/phonemes.py`. The generator asserts row widths and that the two mouth
widths agree, so it catches drift — let it.

## Architecture in one breath

`loop()` captures `millis()` **once** and passes it down; mixing time sources
has already caused an unsigned-underflow bug. Nothing blocks: every animation
is a keyframe list stepped against that clock, because WiFi, the web server,
a DMX listener and audio playback all share this loop. The audio task only
reads the file and pushes bytes at I2S — **the matrix SPI bus is touched from
one thread only.**

`App.h` is the only way `Net.cpp` reaches the face. Settings live in NVS and
survive reflashing but not a chip erase, so the compiled defaults are not
necessarily what a given board is running.

## Current hardware

ESP32-C3 (AirM2M CORE). Matrix DIN 3 / CLK 2 / CS 7. Audio PDM out 4, clock 5
(unconnected). PIR 6. Toggle 10, button 0. I2C 0/1 (an MB1242 sonar was
attempted and abandoned — see HANDOFF.md). Only GPIO 1 and 20 are free.

Four matrices: 2 eyes, 2-module mouth. One LED matrix was soldered in the
wrong orientation, which is why it is four and not five.
