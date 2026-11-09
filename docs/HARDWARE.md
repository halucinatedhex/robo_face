# Hardware

Everything here is as built and as the firmware expects. Pin numbers live in
[`include/Config.h`](../include/Config.h) and are the single source of truth —
if you change them there, change them here.

## Bill of materials

| # | Part | Notes |
|---|------|-------|
| 1 | ESP32-C3 board, LuatOS / AirM2M CORE | `board = airm2m_core_esp32c3`. Native USB, no UART bridge. |
| 4 | MAX7219 8x8 LED matrix module | Generic single-matrix modules, not the FC-16 4-in-1 strips. |
| 1 | Analog amp, PAM8403 or similar | Any small class-D with a line input. |
| 1 | Speaker, 4–8 Ω, 3 W | |
| 1 | Motion sensor | RCWL-0516 radar **or** HC-SR501 PIR. See below. |
| 2 | 1 kΩ resistor | Audio filter. |
| 2 | 10 nF capacitor | Audio filter. |
| 1 | 1 µF capacitor | Audio DC block. |
| 1 | 5 V supply, ≥ 2 A | Matrices plus amp. Not the USB port. |
| — | 100 nF ceramic ×4, 470 µF electrolytic | Decoupling. Strongly recommended. |

**Sensor choice matters.** An RCWL-0516 is microwave radar and sees *through*
a plastic pumpkin, so the electronics stay hidden. An HC-SR501 is infrared and
needs a hole with line of sight. The radar module is also far more sensitive —
expect to tune the cooldown up rather than down.

## Pinout

| Signal | GPIO | Goes to |
|--------|------|---------|
| Matrix DIN | **3** | first module `DIN` |
| Matrix CLK | **2** | all modules `CLK` |
| Matrix CS | **7** | all modules `CS`/`LOAD` |
| Audio PDM out | **4** | RC filter → amp input |
| Audio PDM clock | **5** | **nothing — leave unconnected** |
| Motion in | **6** | sensor `OUT`, active high |

### Pins you must not use

| GPIO | Why |
|------|-----|
| 11–17 | SPI flash. Touching these bricks boot until unwired. |
| 18, 19 | Native USB D−/D+. |
| 8, 9 | Boot strapping. 9 is the BOOT button. |
| 20, 21 | UART0. Free in practice (console is over USB) but avoid. |

Free and unused: **0, 1, 10**.

The PDM clock pin is a quirk, not a requirement of the circuit: the ESP-IDF
driver insists on being given a clock pin for PDM transmit, but an RC filter
only needs the data line. Assign it somewhere harmless and ignore it.

## Matrix chain

Only the **data** line chains. CLK and CS are a shared bus — every module
sees the same clock and the same latch pulse, which is what makes the chain
behave as one long shift register.

```
 ESP32-C3            Mod 0        Mod 1        Mod 2        Mod 3
                  (left eye)  (right eye)   (mouth L)    (mouth R)
 GPIO3  DIN ─────────► IN
                     DOUT ────────► IN
                                  DOUT ────────► IN
                                               DOUT ────────► IN
 GPIO2  CLK ─────────►──────────────●────────────●────────────●
 GPIO7  CS  ─────────►──────────────●────────────●────────────●
 5V         ─────────►──────────────●────────────●────────────●
 GND        ─────────►──────────────●────────────●────────────●
```

Device 0 is the module whose `DIN` comes from the ESP32. The firmware assigns
0–1 to the eyes and 2+ to the mouth, so the chain must run **left eye → right
eye → mouth left → mouth right**. A different physical order means editing
`EYE_LEFT`, `EYE_RIGHT` and `MOUTH_FIRST` in `Config.h`; the mouth modules
must stay contiguous and in left-to-right order because `MOUTH_FIRST` indexes
them as a block.

A module mounted upside down is fixed in software — the rotation checkboxes on
the web page, saved to NVS. Do not rewire for that.

### 3.3 V into a 5 V part

The MAX7219 datasheet wants V<sub>IH</sub> ≥ 3.5 V at V<sub>CC</sub> = 5 V.
The ESP32 drives 3.3 V, which is **below spec**. It is not guaranteed; it
usually works. In order of preference when it doesn't:

1. **Drop the module supply.** One silicon diode (1N4001) in series with the
   modules' 5 V gives ~4.3 V, which pulls V<sub>IH</sub> to about 3.0 V. The
   MAX7219 runs from 4.0 V, so this is in spec. Costs a little brightness.
   Cheapest real fix.
2. **Level shift.** A 74AHCT125 on DIN/CLK/CS — a 5 V part with TTL
   thresholds, so 3.3 V is a solid high. Use this if the chain gets long.
   Avoid bidirectional I²C-style shifters; these are push-pull at 4 MHz.

The failure signature is specific: the first module or two behave and the far
end flickers or shows garbage, because the margin erodes along the chain.

## Audio

The C3 has I2S but **no DAC**, so it cannot put analog on a pin. It *can*
transmit PDM — a 1-bit stream whose density encodes the waveform — so one
GPIO plus a passive filter produces line level with no DAC module.

```
            1k        1k
 GPIO4 ──[====]──┬──[====]──┬───||───►  amp input
                 │          │   1µF
               10nF       10nF
                 │          │
                GND        GND
```

**Why these values.** The PDM carrier sits at roughly sample rate × 64 ≈
1 MHz. Speech at a 16 kHz sample rate occupies up to 8 kHz. So the corner goes
between them: 1 kΩ with 10 nF is 15.9 kHz, which passes all the audio and puts
the carrier ~36 dB down. Two stages gets it to ~70 dB.

Getting the capacitor wrong is the easy mistake: 100 nF would put the corner at
1.6 kHz and muffle the speech badly.

The 1 µF blocks DC. The PDM pin swings 0–3.3 V, so it idles around 1.65 V, and
that offset must not reach the amp. Most PAM8403 boards have input caps
already; the series cap costs nothing and removes the doubt.

**Volume is software only.** PDM has no hardware gain, so `Audio::feed()`
scales samples by `settings.volume` (0–100). Start around 30: a PAM8403's
fixed ~24 dB gain clips readily.

## Motion / presence sensor

Three types are supported, selectable from the web page with no reflash. The
firmware reconfigures the pins when you change it.

### MaxSonar MB1242 (I2CXL-MaxSonar-EZ4) — recommended

| sensor pin | goes to |
|------------|---------|
| GND | GND |
| V+ | 3.3 V (it accepts 3.0–5.5 V) |
| SDA | **GPIO 0** |
| SCL | **GPIO 1** |

**Not the board's default SDA/SCL (GPIO 4 and 5)** — those are the audio PDM
output and clock. The C3 routes I2C through its GPIO matrix, so any free pins
work; 0 and 1 are free and neither is a strapping pin.

I2C needs pull-ups, 4.7 kΩ to 3.3 V on each line. Most MaxBotix breakouts
include them; if nothing responds to a bus scan, that is the first thing to
check.

Why this one is the best of the three: it reports **centimetres directly**, so
there is nothing to calibrate; it is 3.3 V native; and because it gives a real
distance the prop can ignore people walking past and speak only to someone who
comes close.

Its stated range is **20–765 cm**, and anything nearer than 20 cm still reads
as 20 — so a trigger threshold below that can never be crossed.

Use **Scan I2C** on the web page to confirm the sensor is seen at `0x70`
before trusting any distance reading. A readout of "n/a" alone cannot tell
you whether the sensor is absent or present-but-silent; the scan can.

### PIR — what this build actually uses

Out to **GPIO 6**, active high, V+ to 5 V, GND common. The pin is
`INPUT_PULLDOWN`, so an unplugged sensor reads clear rather than floating and
firing at random.

No range control: anyone in the cone triggers it. That turned out to be an
acceptable trade, and it worked first time where the I2C sonar did not.

**Tuning an HC-SR501.** Two pots and a jumper, and the non-obvious one matters
most:

- **Tx (time delay) to minimum.** Tx holds the output HIGH for up to five
  minutes after a detection. While it is high the firmware's rising-edge
  detection sees nothing new, so a long Tx starves it completely. Let the
  software cooldown own the hold-off.
- **Jumper to H** (repeat trigger), for one clean rising edge per visit.
- **Sx (sensitivity) down** to shrink the range from ~7 m toward ~3 m.

**Then tune in firmware**, because a pot cannot fix everything:

- **"PIR must hold"** (`settings.pirHoldMs`, default 400 ms) requires the
  signal to persist before it counts. PIRs emit brief spikes from draughts,
  sunlight and warm air; the sensitivity pot does nothing about those. Raise
  toward 800–1000 ms if it is still twitchy.
- **Cooldown** is a different control for a different complaint: it governs
  how often the prop *speaks*, not how easily it *detects*.

**The prop will trigger itself if you let it.** A PIR sharing a supply with
an audio amp false-triggers on the amp's current draw. The firmware therefore
ignores the sensor while a clip plays and for `SENSOR_SETTLE_MS` (4 s)
afterwards, and clears the detection latch — otherwise a glitch during
playback is remembered and fired the instant the cooldown lifts, giving one
clip per cooldown period forever with nobody in the room.

That is suppression, not a cure. The real fix is decoupling the sensor's
power: 100 nF and 10 uF at its own pins, or a supply tap away from the amp.

There is also a **45 s warm-up blanking** period after power-on
(`SENSOR_WARMUP_MS`), because a PIR outputs nonsense while its pyroelectric
element settles. Without it the prop greets an empty room on every reboot.
The status line reads "warming up" during that window.

### HC-SR04

Trigger to **GPIO 6**, echo to **GPIO 10** — but **the echo pin outputs 5 V
and the C3's GPIOs are not 5 V tolerant.** Use a divider (1 kΩ from echo to
the GPIO, 2 kΩ from the GPIO to ground), or an HC-SR04**P**, which is 3.3 V
native.

Echo is timed by interrupt rather than `pulseIn()`, which blocks for up to
38 ms waiting for an echo that may never arrive — unaffordable in a loop that
also runs audio, WiFi and the display.

### Behaviour, both sonar types

They trigger on **approach**: a reading crossing from beyond the threshold to
inside it, not merely being close. 30 cm of hysteresis stops someone standing
near the boundary from retriggering continuously. Readings pass through a
median-of-five filter — a median rather than an average, because it removes
the spikes ultrasonic sensors produce without adding lag to the trigger.

## Old notes on sensor choice

```
 sensor VCC ──── 5V
 sensor GND ──── GND  (common with the ESP32)
 sensor OUT ──── GPIO6
```

Both common modules output 3.3 V logic, so no divider is needed. The firmware
triggers on the **rising** edge, debounced at 250 ms, then holds off for
`cooldownSec` (default 30 s).

HC-SR501 boards have two pots (sensitivity, on-time) and a retrigger jumper.
Set on-time to minimum and let the firmware's cooldown do the work; otherwise
the two timers fight.

## Power

Budget **1–1.5 A at 5 V** for four matrices plus the amp.

- Run the modules and the amp from the external 5 V supply.
- Keep the ESP32 on USB while developing.
- **Tie all grounds together.** Without a common ground the data lines have no
  reference and you get nothing, or garbage.
- 100 nF across VCC/GND at each module, 470 µF at the supply end of the chain.
  The MAX7219 switches all 64 LEDs in bursts; without decoupling the rail sags
  and the symptom looks exactly like a logic-level problem. This is the most
  common cause of "works with two modules, flickers with four".

Do **not** power the modules from the board's `5V` pin: on these boards that
comes from USB, which gives you 500 mA. And never from `3V3` — it both
starves the MAX7219, which needs 4.0 V minimum, and loads the rail the MCU's
own flash depends on.

### Power distribution

Fanning 5 V out from the dev board does not work, and the failure is
confusing rather than obvious. Use a distribution point fed directly from the
supply:

```
 5V supply ──┬── 1000µF ──┬─────────────── amp V+      (its own run)
             │            │
             ├────────────┴─── 470µF ───── matrix chain V+
             │
 GND ────────┴──────┬──────────────────── amp GND
                    ├──────────────────── matrix chain GND
                    └──────────────────── ESP32 GND
```

Points that matter:

- **Star, not daisy-chain.** The amp and the matrices each get their own run
  back to the supply. Chained through one another, the amp's current bursts
  show up as voltage dips at the matrices.
- **Bulk capacitance at each load**, not just at the supply. The cap has to be
  near what it is feeding or the wiring inductance defeats it.
- **Grounds meet at one point.** A shared ground that doubles as a current
  path puts the amp's return current through the matrices' reference.
- Use fat wire for the 5 V runs — 20 AWG or better. At 1.5 A, thin hookup wire
  drops enough to matter when the MAX7219's V<sub>IH</sub> margin is already
  only 0.2 V.
- The ESP32 can stay on USB during development. Keep its ground tied to the
  supply ground.

### Run it from the 5 V brick, never USB alone

**USB cannot power this prop.** 500 mA does not cover four matrices, the amp,
the sensor and the ESP32. On USB alone the rail sags and the symptoms are
confusing rather than obvious -- the one that cost a session was a **PIR
false-triggering roughly every cooldown period**, which looks exactly like a
sensor problem and sent the hunt toward convection inside the enclosure and
noise pickup on the signal line. It was the supply.

On the 5 V brick, the same hardware behaves perfectly.

When flashing or debugging, **leave the brick connected** and use USB only for
the data link. Pulling the brick to plug in USB reproduces the fault, and you
will then be debugging a power problem while believing you are debugging
firmware.

Loads belong on the brick directly, not on the board's `5V` pin -- on these
boards that pin is fed from USB.

### Connectors before circuits

**Check the dupont pins first.** On this build, weeks of intermittent faults —
panels going dark under load, brightness drifting, flicker that came and went —
came down to corroded male pins and thin cable. Replacing the leads with
thicker ones and cleaning the pins fixed what no amount of firmware could.

Corroded contacts are resistive and intermittent, which is what makes them so
hard to chase:

- On **5 V**, the extra resistance is a voltage drop that only bites under
  load. Idle it measures fine; the moment the amp or all 64 LEDs draw current
  the rail sags and modules reset. Symptom looks exactly like an undersized
  supply.
- On **signals**, contact comes and goes with temperature, vibration and
  anything you nudge. Readings are true when taken and wrong ten minutes
  later.
- It **worsens over time**, so a rig that worked last month can fail now with
  nothing having changed.

Cheap dupont pins are tinned brass and oxidise, especially where they have
been plugged and unplugged repeatedly. Before suspecting the circuit design:

- Reseat every connector; wiggle each one while watching for a glitch.
- Look at the male pins for dullness or green tinge; clean with isopropyl and
  a fibreglass pen, or just replace the lead.
- Prefer thicker gauge for the 5 V and ground runs — less IR drop and a
  larger contact area.
- For anything permanent, crimp properly or solder. Dupont is a prototyping
  connector, not a deployment one.

A multimeter across a connector while the load is **on** finds this; a
continuity test with the power off does not.

### Diagnosing a sagging rail

The symptom is specific and worth recognising: **a panel goes blank rather
than showing garbage.** Below its minimum V<sub>CC</sub> the MAX7219 resets
into shutdown mode, which is a dark display. Garbage points at signal
integrity; blank points at power.

The modules furthest down the chain fail first, so with this layout it is the
mouth that drops out before the eyes.

The firmware re-asserts the whole chain every `MATRIX_RESYNC_MS`, so a module
that drops out comes back by itself — see [THEORY.md](THEORY.md). That hides
the symptom; it does not fix the cause, and the same sag will eventually reset
the MCU too.

A related trap: dropping the module supply to ~4.3 V with a series diode both
improves the 3.3 V logic margin *and* lowers the current draw. If flakiness
survives a proper supply, try that before suspecting anything else.
