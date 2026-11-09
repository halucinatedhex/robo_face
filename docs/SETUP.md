# Setup

From a bare board to a talking pumpkin.

## 0. Toolchain

PlatformIO, and **specifically the bundled core**:

```sh
~/.platformio/penv/bin/pio run -e c3
```

The Homebrew `pio` on this machine runs on Python 3.9, which current
PlatformIO rejects outright (`Python version must be 3.10, 3.11, ...`). The
virtualenv core has Python 3.12. Everything below assumes that path; a shell
alias saves typing:

```sh
alias pio=~/.platformio/penv/bin/pio
```

### Build targets

| env | what it builds |
|-----|----------------|
| `c3` | the firmware (ESP32-C3) |
| `c3-diag` | hardware bring-up, bit-banged, no SPI peripheral |
| `esp32dev`, `diag` | the same two for a classic ESP32 |

## 1. Wire it

Follow [HARDWARE.md](HARDWARE.md). Build it up in stages — a fault in a
four-module chain plus an amp is hard to localise, a fault you just created is
not:

1. One matrix. Run `c3-diag`, confirm an eye.
2. Add the rest one at a time, re-running `c3-diag` after each.
3. Flash the real firmware, confirm the face animates.
4. Add the audio filter and amp.
5. Add the motion sensor.

## 2. Flash

```sh
pio run -e c3 -t upload          # firmware
pio run -e c3 -t uploadfs        # audio clips from data/
```

Both are needed on a fresh board. `uploadfs` writes the whole 2.3 MB
filesystem, so it takes about 11 seconds.

The port re-enumerates on reset and its name changes between
`usbmodem101` and `usbmodem2101`, so let PlatformIO auto-detect rather than
pinning `--upload-port`.

### Reading the console

The C3 has no USB-UART bridge — it enumerates as the chip's own USB
Serial/JTAG unit. Two consequences:

- `-DARDUINO_USB_CDC_ON_BOOT=1` is required, and is already set in
  `platformio.ini`. Without it `Serial` goes to UART0 on GPIO 20/21 and
  vanishes.
- The port only exists *after* boot, so **the boot banner is lost if you
  connect afterwards.** To catch it, open the port first and *then* reset.

`pio device monitor` needs a real terminal (it calls `termios.tcgetattr` on
stdin) and fails without one. In a plain shell:

```sh
python3 - <<'EOF'
import serial, time, glob
s = serial.Serial(sorted(glob.glob('/dev/cu.usbmodem*'))[0], 115200, timeout=0.3)
time.sleep(0.3); s.reset_input_buffer()
s.setDTR(False); s.setRTS(True); time.sleep(0.2)   # reset, port already open
s.setRTS(False); s.setDTR(True)
t0 = time.time()
while time.time() - t0 < 20:
    d = s.read(4096)
    if d: print(d.decode('utf-8', 'replace'), end='', flush=True)
EOF
```

A healthy boot:

```
rst:0x15 (USB_UART_CHIP_RESET),boot:0xc (SPI_FAST_FLASH_BOOT)
audio ready, 4 clip(s) on flash
wifi connected, ip 192.168.x.x
robo_face ready
http://robo-face.local
E1.31 listening: universe 1, channels 1-10
```

### Live show wiring

Verified end-to-end against a real show: FPP → E1.31 → this prop, lip-syncing
in time with a larger pumpkin prop. Substitute your own addresses and show
folder below.

| | |
|---|---|
| Prop | `<prop-ip>` / `robo-face.local`, universe **1**, channels **1–10** |
| xLights controller | `PumpkinLED`, type E131, MaxChannels 10 |
| xLights model | `LED Pumpkin` — **Custom 10×1 grid**, `StringType="Single Color White"` |
| Face definition | `Name="Mouth"`, `Type="NodeRange"`, Mouth-AI=1 … Mouth-WQ=10, eyes/outline empty |
| Absolute show channels | ten consecutive channels in your layout, e.g. `N … N+9` |
| FPP output | E1.31 unicast (`type:1`) → `<prop-ip>`, universe 1, start `N`, count 10 |

**Do not use `Single Color Intensity`** for the model — in xLights that is a single dimmer
channel for the entire model, and the prop will only ever see one channel. A `Single Line`
model with `NodesPerString="10"` fails the same way. The Custom-grid form above is what
allocates ten separate channels.

Effects are made by mirroring the big pumpkin's Faces spans onto this model with face `Mouth`
and a **pure-white palette** (single-colour model ⇒ palette sets intensity ⇒ clears
`PHONEME_THRESHOLD`). Rendered output is one channel at 255 at a time, never two.

Health check while a show runs:

```sh
curl -s http://<prop-ip>/status    # src should be "external (show)", pk climbing, dc 0
```


## 3. WiFi

Three modes, set on the web page under Settings, stored in NVS and applied at
the next boot:

| mode | behaviour |
|------|-----------|
| **auto** (default) | Join a saved network. If none is reachable within ~10 s, host its own instead. |
| **ap** | Always host its own network. No join attempt, so it boots straight up. |
| **off** | Radio disabled entirely. No web page, no E1.31 — just motion, audio and the idle face. |

### Hosting its own network

When there is no network to join — a desk with no infrastructure, a venue, a
colleague's office — the prop brings up an open access point:

- **SSID `robo_face`**, no password
- Browse to **http://192.168.4.1**

This serves the **full control page**, not just a WiFi setup form: modes,
clips, volume, the live panel preview, everything. E1.31 runs here too, so a
laptop joined to the AP can drive the mouth.

That is the difference from the old behaviour, where no network meant only
WiFiManager's setup portal and no control at all.

### Joining a network

On the control page there is a **WiFi** section: press **Scan**, pick a
network, enter the password, press **Save & restart**.

The prop then drops its own AP and joins — so the phone you were using will
lose the `robo_face` network. That is expected. Reconnect it to your
normal WiFi and the prop is at **http://robo-face.local**.

Both endpoints work directly if the UI ever misbehaves:

```
curl "http://192.168.4.1/scan"
curl "http://192.168.4.1/wifi?ssid=MyNetwork&pass=secret"
```

### A warning about credentials

Credentials live in our own NVS namespace, not the IDF's wifi config. Do not
"simplify" this by calling `WiFi.begin()` with no arguments to reuse stored
credentials — persistence is on by default, so it writes an empty config over
them and the device can no longer join anything. That cost a working network
during development.

## 4. Standalone talking mode

### Make clips

```sh
# from text, using a macOS voice
python3 tools/make_clip.py --say "Nice of you to drop by" --voice Fred -o data/hello.pfc

# from a recording
python3 tools/make_clip.py audio/line.wav -o data/line.pfc

# see what the mouth will do before flashing
python3 tools/make_clip.py audio/line.wav -o data/line.pfc --preview
```

Then `pio run -e c3 -t uploadfs`.

### Clip modes

Clips live in one directory per mode, and the web page switches between them:

```
data/
  halloween/   boo.pfc  scream.pfc  knife.pfc  ...
  custom/      your own lines
  system/      mode_<name>.pfc announcements
```

Motion picks at random from **the active mode only**. The selected mode
persists in NVS, and Halloween is the default because it is first in
`CLIP_MODE_NAMES`.

### Choosing which clips motion may use

Every clip in the Speak section has a **motion** checkbox, stored separately
per mode. Untick a clip and it can still be played by hand from the web page,
but motion will never choose it — useful when some lines work for a passer-by
and others only make sense when you are there to set them up.

Ticking everything is stored as an empty list, meaning "allow all". That is
deliberate: a clip uploaded later is then eligible by default rather than
silently excluded because it was not in a saved list.

Lists hold basenames, not indexes (`boo,scream,happy`), so adding or removing
a clip cannot quietly re-point a selection at a different file.

`custom/` ships empty — drop your own clips in and they appear in the web UI.
Adding a *further* mode takes three steps and no new logic: add a name and a
directory to `include/ClipMode.h` and `src/Settings.cpp`, create the folder
under `data/`, and run `uploadfs`. The web page builds its mode buttons from
whatever `/info` reports.

Keep the total under ~74 s of audio across all modes — they share the one
2.31 MB filesystem. `say -v '?'` lists the available voices; using a different
voice per mode is a cheap way to give each one a character.

### Tune it

On the web page:

| setting | start at | why |
|---------|----------|-----|
| Volume | **30** | A PAM8403's fixed ~24 dB gain clips easily. Work up. |
| Cooldown | 30–60 s | Radar sensors are sensitive; longer is kinder to whoever sits nearby. |
| Brightness | 5 | Higher draws real current. |

## 5. Physical controls

Both switch to **GND** and use the internal pull-ups, so no resistors:

| | GPIO |
|---|---|
| Toggle (SPST) | **10** |
| Button (momentary) | **0** |

Off until **"physical toggle + button fitted"** is ticked in the Motion
section, so the firmware never reads pins that are not wired.

- **Toggle** arms motion. The web checkbox can still veto it, so the toggle
  behaves as a switch in series rather than an override.
- **Short press** plays a clip, ignoring the cooldown — a deliberate press
  should always get a response. It fires on release, so a long hold is not
  also heard as a short press.
- **Hold 2.5 s** switches clip mode and *speaks the new mode's name*. The
  switch happens as the threshold passes, while the button is still down, so
  the announcement is the confirmation.

If the toggle reads backwards from its labelling, tick **"toggle reads
backwards"** rather than rewiring.

Announcements live in `data/system/mode_<name>.pfc`, outside the mode
directories so motion can never pick one. A missing file is not an error —
the mode still changes. Give each mode's announcement the voice used by that mode's clips, or the
switch sounds like a different character introducing it.

## 6. Choosing the idle mouth animation

The **Idle mouth frames** section of the web page draws every frame as a
thumbnail, with the animations first and animating live. Tap a tile to play
it on the prop and tick it into the cycle; shift-tap ticks without playing.
Ticking none restores the default for that mode.

Items play **in the order they were ticked**, not numeric order. To get a
specific sequence, clear and re-tick in the order you want.

Judge choices on the prop, not in the browser — eight pixels of LED behind a
pumpkin shell reads very differently from a crisp canvas.

## 7. Show mode (xLights / FPP)

Define the prop as a **Faces model, single node**. Assign the ten `Mouth-*`
rows to ten consecutive channels — by default universe 1, channels 1–10, both
changeable from the web page — in this order:

```
AI  E  etc  FV  L  MBP  O  rest  U  WQ
```

Leave `Outline`, `Eyes-Open` and `Eyes-Closed` **unassigned**. The eyes run
their own animation; driving them from the show is not implemented.

Both modes coexist. Show frames take the mouth while they arrive and release
it 2 s after they stop, so a prop wired into the show still idles and still
responds to motion between sequences.

See [TESTING.md](TESTING.md) to check the mapping without building a sequence.
