# robo_face — project context

Written for picking this up cold, later. This covers *why it is the way it
is*, what is verified, and the traps. For everything else:

- [README.md](README.md) — what it is, quick start, layout
- [docs/SETUP.md](docs/SETUP.md) — toolchain, flashing, WiFi, xLights
- [docs/HARDWARE.md](docs/HARDWARE.md) — BOM, pinout, wiring, audio filter
- [docs/THEORY.md](docs/THEORY.md) — how it works internally
- [docs/TESTING.md](docs/TESTING.md) — test procedures, troubleshooting
- [docs/TOOLS.md](docs/TOOLS.md) — the host scripts

Last updated 2026-10-23.

## Known-good configuration

Running stably as of 2026-10-23, after rewiring the matrix leads:

| setting | value | note |
|---------|-------|------|
| SPI clock | **4 MHz** | runtime, `settings.spiKHz`; drop it from the web page if a rebuild is flaky |
| resync | 500 ms | `MATRIX_RESYNC_MS`, compile time |
| brightness | 15 | max |
| volume | 100 | software gain |
| cooldown | 30 s | |
| clip mode | halloween | |
| universe / start channel | 1 / 1 | |

Settings live in NVS, so reflashing keeps them. A full NVS erase drops back
to the compiled defaults, which are **not** the same (brightness 5, volume
70) — worth knowing if the prop suddenly seems quiet and dim.

## What it is

A singing Halloween prop. An ESP32-C3 drives four chained MAX7219 8x8 LED
matrices as a face: two eyes and a two-module mouth. The eyes animate
autonomously forever. The mouth lip-syncs to xLights sequences played by FPP
over E1.31/sACN, and falls back to its own idle animation between songs.

Grown out of an earlier AVR sketch, kept locally for reference — the mouth
artwork in `MOUTH_FRAMES_16` is extracted from it verbatim.

## Two ways it runs

1. **Show mode** — xLights sequences via FPP over E1.31/sACN drive the mouth.
2. **Standalone** — a motion sensor triggers a stored clip; audio plays over
   PDM and a pre-computed cue track drives the same phoneme shapes. No
   network needed. Built for taking the prop to the office.

Both feed the one `Mouth::setPhoneme()` path, so there is a single mouth
pipeline and the artwork is shared.

## Hardware as built

| | |
|---|---|
| MCU | ESP32-C3, LuatOS / AirM2M CORE board, `board = airm2m_core_esp32c3` |
| Pins | DIN **3**, CLK **2**, CS **7** |
| Modules | 4 chained MAX7219: device 0 left eye, 1 right eye, 2–3 mouth |
| Power | modules from external 5 V, grounds tied; **not** from the board |
| Audio | PDM on GPIO **4**, RC filter, then an analog amp (PAM8403) |
| PDM clk | GPIO **5**, required by the driver, **leave unconnected** |
| Motion | GPIO **6**, active high |

**Why PDM and not a DAC module:** the C3 has I2S but no DAC, so it cannot put
analog on a pin. It *does* support PDM TX, so one GPIO plus two RC stages
feeds an analog amp with no DAC board at all. Filter and reasoning are in
`Config.h` next to the pin definitions; the corner is near 16 kHz because the
PDM carrier is ~1 MHz and speech is under 8 kHz. **An earlier version of that
comment said 1k + 100nF, which is a 1.6 kHz corner and would muffle speech
badly — it is 10nF.**

PDM has no hardware gain, so volume is applied in software in `Audio::feed()`.

**Why the C3 and not the ESP32 DevKitC:** the DevKitC is still a supported
build target (`-e esp32dev`, pins 23/18/5) but its flash stopped responding
once a harness was attached — `flash read err, 1000` in a boot loop and
`flash-id` returning `Manufacturer: ff`. Never diagnosed. The board may be
fine and merely have been miswired onto GPIO 6–11 (the SPI flash lines, which
a 38-pin DevKitC exposes as `SD0 SD1 SD2 SD3 CLK CMD`). **If that board is
ever revisited, run `flash-id` on it bare first** — that one command
distinguishes a damaged board from a wiring fault.

**Why four modules and not five:** one matrix was soldered into its MAX7219
PCB in the wrong orientation. If it is 90° out it is recoverable in software
with a transpose in the driver (write all 8 digits on change instead of one);
180° is already handled by the rotation flag. Not built. `MOUTH_MODULES 3` in
`Config.h` plus the already-generated 24-column artwork is all that is needed
on the software side to go back to a five-module face.

## Build and flash

```
~/.platformio/penv/bin/pio run -e c3 -t upload
```

**Use that path.** The Homebrew `pio` runs on Python 3.9, which current
PlatformIO rejects outright. The bundled virtualenv core has Python 3.12.

| env | what |
|---|---|
| `c3` | the firmware |
| `c3-diag` | hardware bring-up, bit-banged, no SPI peripheral |
| `esp32dev` / `diag` | same two for the classic ESP32 |

`pio device monitor` needs a real terminal (`termios.tcgetattr` on stdin) and
fails without one. Read the port with pyserial instead.

The C3 has no USB-UART bridge: it enumerates as the chip's own USB
Serial/JTAG unit, so `-DARDUINO_USB_CDC_ON_BOOT=1` is required or `Serial`
goes to UART0 on GPIO 20/21 and vanishes. **The port re-enumerates on reset**
and its name changes (`usbmodem101` ↔ `usbmodem2101`), so never hardcode it.
Serial output printed before the host opens the port is lost — to catch the
boot banner, open the port *then* toggle DTR/RTS to reset.

Partition table is `min_spiffs.csv`: WiFi plus the web server does not fit in
the default 1.25 MB app slot. Currently ~57% of 1.9 MB, two OTA slots spare.

## Architecture

```
main.cpp        loop(): control, net, eyes, mouth. Implements namespace app.
App.h           the only way Net.cpp may touch the face.
Eyes.cpp        keyframe sequencer + independent blink/twitch scheduler
Mouth.cpp       idle expression state machine + external phoneme source
MatrixChain.cpp MAX7219 over hardware SPI, with a shadow buffer
E131.cpp        sACN receiver, hand-written
Net.cpp         WiFi state machine, WebServer, E1.31 glue
WebPage.h       the control page, static, in PROGMEM
Settings.cpp    NVS-backed settings
MouthFrames.cpp GENERATED — do not hand-edit
tools/genart.py regenerates the artwork
tools/phonemes.py parametric phoneme shapes
tools/sacn_test.py send real sACN frames for testing
```

**Nothing blocks.** The original AVR sketch burned time in a `bgProcessing()`
helper that recursed once per 20 ms of sleep — a 3 s wait nested ~150 frames
deep, which is fine on an ATmega and a bad bet against the ESP32 loop task's
8 KB stack. Every animation is now a list of keyframes stepped against
`millis()`. Keep it that way: WiFi, the web server and the DMX stream all
share this loop.

**Eyes and mouth are independent.** They share only `facePowered`.

## Audio pipeline

Clips are `.pfc` files on LittleFS, built by `tools/make_clip.py`:
header, lip-sync cue track, then 16 kHz 16-bit mono PCM. Format is documented
in that script's docstring and parsed in `src/Audio.cpp` — **change both
together.**

**Lip sync is computed offline, never at runtime.** The script frames the
audio and classifies each frame from loudness (how open), spectral centroid
(rounded back vowels vs spread front ones) and zero-crossing rate (fricatives),
then mode-filters to kill flicker and emits cues on change with a 60 ms
minimum dwell. The firmware only replays the list, which is why sync is exact
and playback costs almost nothing. Rhubarb Lip Sync is supported via
`--rhubarb` but is not required and was not installed when this was built.

Driving the mouth from a live audio envelope was considered and rejected: it
produces a nutcracker, not speech.

Partition table is now `partitions_audio.csv` — the second OTA slot was
traded for ~2.3 MB of filesystem, about 74 s of speech. App is at ~71% of
1.62 MB.

**Threading:** a background task does nothing but read the file and push bytes
at I2S. Cues reach the main loop through `Audio::nextCue()`, because **the
matrix SPI bus must only ever be touched from one thread.** Do not move
phoneme updates into the audio task.

Playback position comes from samples handed to I2S minus the DMA depth, not
from `millis()`. If the task is ever starved, the audio and the mouth stall
together rather than drifting apart.

## Decisions worth not re-litigating

- **The sACN receiver is hand-written**, not a library. The packet is a fixed
  layout and the validation is the part you want to be able to read when a
  show misbehaves. ~120 lines.
- **An all-zero DMX frame is not activity.** FPP can stream zeros between
  sequences; if that counted as a live show the mouth would hold one shape
  forever instead of idling. Silence and zeros both lead back to idle.
- **Synchronous `WebServer`, not ESPAsyncWebServer.** Async web stacks have a
  patchy history on the C3; the preview polls `/panels` at 8 Hz and the
  payload is 32 bytes.
- **The control page is static, in PROGMEM.** Building a 6 KB String per
  request on a device also receiving a DMX stream fragments the heap.
- **Artwork is generated, both widths at once.** `tools/genart.py` asserts
  every row is exactly the right width, so the 16- and 24-column sets cannot
  drift apart. Edit the ASCII and the parameters in `tools/`, never
  `src/MouthFrames.cpp`.
- **Rotation is runtime, in NVS.** A module mounted upside down should be a
  web toggle, not a reflash of a prop already inside a pumpkin.

## Traps that cost real time

1. **Mixing two time sources.** `loop()` captures `now` once; something then
   stamped a timestamp with its own later `millis()`, and an unsigned
   `now - stamp` underflowed to ~4.29 billion, instantly expiring a 2 s
   timeout. The mouth took the handover and dropped it on the same tick,
   every frame. The timeout compare in `Mouth::update` is **signed** for this
   reason. Do not "tidy" it back.
2. **The shadow buffer suppresses redraws.** `MatrixChain::setRow` drops
   writes that would not change anything, which is what makes it fast. After
   changing a rotation flag the logical picture is identical, so every write
   gets dropped and nothing appears to happen — hence
   `MatrixChain::repaint()`.

   The nastier version of this cost an hour on 2026-10-23. Loud audio sagged
   the shared 5 V rail; the MAX7219 modules browned out and lost their
   registers while the ESP32 carried on, so the shadow kept saying "already
   written" and the display never recovered. Two modules sat solid-on
   (display test), two dark (shutdown), and it presented as "hangs at
   startup" even though the firmware was serving HTTP the whole time.

   Diagnosis came from comparing `/panels` — what the driver *believes* is
   lit — against what the panels were actually doing. When those disagree,
   suspect this — though note the *underlying* cause that time was corroded
   connector pins, not the firmware. `MatrixChain::tick()` now re-asserts the chain every
   `MATRIX_RESYNC_MS`, which heals it; the underlying cause was powering the
   matrices by fanning 5 V out of the dev board, whose 5V pin is fed from
   USB's 500 mA. Fixed with a proper 5 V distribution board.

6. **The resync stomps per-device state.** The flip side of trap 5, and it
   cost another session. `MatrixChain::writeConfig()` re-asserts hardware
   registers on a timer, so **anything the rest of the firmware sets per
   device that the resync does not know about gets overwritten twice a
   second.** It happened with brightness: the eye glow effect ramps each eye
   independently, while `writeConfig()` was broadcasting one global value.
   Measured in simulation: 61 unwanted brightness jumps in 15 minutes.

   `intensity_` is therefore `[MATRIX_COUNT]`, not a scalar, and
   `setIntensity(device, …)` records what it wrote. If anyone later adds
   per-module scan limits or decode modes, the same trap is waiting.

   Note how this one presented: *brightness changing* looks like data
   corruption, because a brownout can only reset registers to defaults, never
   write a new value. That reasoning is sound but incomplete — it ignores the
   firmware as a possible writer. Lead time was wasted chasing signal
   integrity. Check what your own code writes before blaming the wiring.
3. **Lid reopen needs per-eye state.** `eyeCurrent_` is `[2][8]`, not `[8]`.
   With one shared buffer, a blink landing mid-`lazyEye` reopens onto the
   wrong picture and snaps the drooped eye back.
4. **Multicast from a multi-homed Mac** picks an arbitrary interface and
   fails with "No route to host". `tools/sacn_test.py --iface <local-ip>`.
5. **AVR leftovers**, all removed: inline-asm `bitswap`, `B0001` literals,
   and LedControl's `digitalWrite`-per-edge bit-banging.

## Verified on hardware (2026-10-20)

- Boots clean, one banner, no reset loop; survived 80 s through all three
  mouth modes.
- E1.31 **unicast and multicast**, 10 fps, zero discarded packets; mouth
  switches to `external (show)` and falls back to idle ~2 s after the stream
  stops.
- All ten phoneme test buttons, the mode buttons and the brightness slider.
- `/panels` preview decoded by hand and matches the art pixel-for-pixel.
- Universe change re-binds the listener live (tested universe 7, channel 33).
- Settings survive a reboot; WiFi reconnects by itself.

Not verified by hardware, only by host simulation: the long-run eye
animation timings. `millis()` is a variable in the simulation, which is
exactly why trap 1 above got through it.

## Not yet verified on hardware

Display reliability was resolved on 2026-10-23 by two independent fixes: the
per-device brightness bug above, and replacing the matrix leads with thicker
dupont cable after finding the **male pins were corroded**. 4 MHz is stable
with the current harness.

The corrosion is probably the single biggest lesson in this project. It
explains the whole arc: panels dropping out under load (resistive contacts
sagging the 5 V rail only when current was drawn), flicker that came and
went, and symptoms that worsened over weeks. Every measurement taken was
true at the time and wrong later, which is why it survived a power supply
rebuild, a distribution board and several firmware changes.

**USB alone cannot power this prop.** 500 mA does not cover four matrices
plus the amp. On USB the rail sags and the failure wears a disguise: in
2026-10 it presented as a PIR false-triggering every cooldown period, and was
chased through convection-in-the-enclosure and signal-line noise before
turning out to be the supply. Always leave the 5 V brick connected, including
while flashing -- USB is for data only.

That is the third distinct power fault on this build, after the dev-board
5 V fan-out and the corroded connectors. **On this project, suspect power
first.**

**On a hand-wired rig, check connectors before circuits.** Measure across a
connector with the load running, not with a continuity beep and the power
off. See [docs/HARDWARE.md](docs/HARDWARE.md). The leads are long (~20 cm between modules,
~80 cm of shared CLK/CS bus), which is electrically marginal at 4 MHz — if a
future rebuild is flaky, lower the clock from the web page first, then add
100–220 Ω series resistors at the ESP32 end of CLK/CS/DIN.

**Audio works** (confirmed 2026-10-23) but has not been characterised: no
measurement of the filter's output, and volume above ~40 clips the amp, which
roughly doubles current draw and was what first destabilised the 5 V rail.
Keep it at or below 35 until the supply is sorted.

Earlier note, now superseded: Playback, cue timing and the fallback to
idle are all confirmed on the device, but the amp was not wired when this was
built, so nothing confirms the filter values, the volume range or whether PDM
noise is audible through the LED matrices' switching. That is the first thing
to check.

## WiFi

Three modes in `settings.wifiMode`: auto (join, else host), ap (always host),
off. In AP mode the prop hosts the open SSID **`robo_face`** at
192.168.4.1 and serves the **whole control page plus E1.31** — not merely a
setup form. That is what makes it usable standalone.

**WiFiManager was removed**, and should not come back. It caused three
separate failures:

- `getWiFiSSID()` before the driver is fully up returns uninitialised memory,
  so the "saved" SSID came back as garbage.
- **`WiFi.begin()` with no arguments overwrote the stored credentials**,
  because persistence defaults to on. This destroyed the saved network during
  development.
- Its config portal had to take port 80, which meant stopping our own server
  from inside one of its own request handlers. The response never arrived and
  the link silently did nothing.

Credentials now live in our NVS namespace as `settings.wifiSsid` /
`wifiPass`, and the control page has its own WiFi section backed by `/scan`
and `/wifi`. Saving schedules a reboot ~1.2 s out so the HTTP response
flushes before the restart. Removing the library also returned about 75 KB
of flash.

## The MB1242 sonar: unresolved

A MaxBotix I2CXL-MaxSonar MB1242 was wired to I2C on **GPIO 0 (SDA) / GPIO 1
(SCL)** — deliberately not the board's default 4/5, which are the audio PDM
pins. It was never made to work, and the prop uses a PIR instead. The code
path remains (`SensorType::SonarI2C`) if anyone wants to return to it.

What was established, consistently, across several tests:

- The bus is electrically sound: both pull-ups present, idle high, 3 us rise
  time, no short between the lines.
- **Nothing on the bus ever genuinely acknowledged**, including 0x70.

What wasted the most time was **my own diagnostics disagreeing with each
other**, so treat the tooling with suspicion:

- `Wire.endTransmission()` on a **zero-length write returns success on this
  core without putting anything on the wire.** A scan built on it reported
  all 126 addresses as present. An earlier "MB1242 present at 0x70" was a
  false positive from the same bug. `/i2c` was rebuilt on a bit-banged probe,
  which is both truthful and ~50x faster.
- The hand-rolled bit-bang probe is itself not fully trustworthy: a full
  sweep once reported a contiguous block at 0x72-0x7E with 0x70 absent,
  which is not plausible hardware. Its timing needs work before anyone
  relies on it.

Unverified, and the most likely answer: **the voltage at the sensor's own V+
pin was never measured.** An unpowered MB1242 produces exactly the observed
signature. Reversed SDA/SCL produces it too, and was wrongly dismissed early
on the strength of the false ACK above.

Diagnostics left in place, all on the web page or as endpoints:
`/i2c` (scan), `/i2cdiag` (pull-ups, shorts, SDA rise time), `/i2cbb`
(single bit-banged probe), `/sonar` (one blocking read with raw bytes).
`Trigger::recoverBus()` clocks a stuck bus free.

## Idle animation, as it now works

The idle cycle is a list of items per clip mode, where an item is a still
pose or a whole animation (`src/Mouth.cpp`, ids >= 200). The web page draws
every frame and animates the animations, so choices are made by eye.

Two things worth not undoing:

- `MOUTH_ANIMS` is the only definition of what an animation is. The browser
  gets frame lists and timings from `/frames`. Do not hardcode them in the
  page -- the preview would drift from the device.
- The **wave needs six frames** (31-36). The original artwork had four, which
  jump half a period at the loop. `tools/genart.py` asserts its formula
  reproduces the four hand-drawn frames before generating the extra two.

A CSS lesson that cost a round trip: `.ftile.on` and `.ftile.fanim` have
equal specificity, so whichever is declared later wins. The selected style
must come last, or a selected animation tile shows no outline and the UI
looks broken while working perfectly.

## Known gaps / next

- `src/Control.cpp` is still a deliberate stub — the seam where the old IR
  remote was. Buttons or BLE would go there.
- Eye control over E1.31 is not implemented. xLights Faces has `Eyes-Open` /
  `Eyes-Closed` rows; they are currently meant to be left unassigned. Adding
  them needs a forced-closed state in `Eyes`.
- No OTA: the second slot was traded for audio storage.
- `settings.idleCalm` is largely redundant now that an explicit idle list can
  express the same thing. It still picks the default list when none is set.
- The web page's **Save** button only writes the rotation bitmask; every other
  control writes its own field on change. Posting the whole form let one
  unsynced input overwrite unrelated settings.
- Motion-eligible clips are a per-mode subset (`settings.motionClips[]`,
  comma-separated basenames; empty means all). Set from the checkboxes in the
  Speak section.
- The sensor is ignored while audio plays and for 4 s after
  (`SENSOR_SETTLE_MS`), because a PIR on the amp's rail false-triggers on
  current draw and the latched detection would otherwise fire the moment the
  cooldown lifted — a self-sustaining loop of one clip per cooldown.
- Clips are picked at random within the eligible subset. There is no scheduling,
  no time-of-day behaviour and no way to favour particular lines.
- Clip modes (`include/ClipMode.h`) are a directory per mode under `data/`.
  Halloween is the default because it is index 0; `custom` ships empty.
- The fifth module (see above).
