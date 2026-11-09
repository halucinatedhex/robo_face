#pragma once
#include <Arduino.h>

// ---------------------------------------------------------------------------
// Hardware configuration -- classic ESP32 (WROOM-32) and ESP32-C3
// ---------------------------------------------------------------------------

// MAX7219 chain, driven over hardware SPI. CS is toggled by hand so that one
// latch pulse commits the whole chain at once.
//
// Pins differ per target. On the ESP32-C3, GPIO18 and GPIO19 are the native USB
// D-/D+ lines and GPIO11..17 belong to the SPI flash, so the classic ESP32
// numbering is not merely inconvenient there -- it is unusable. The values
// below are the SCK/MOSI/SS pins the board's own Arduino variant declares.
#if defined(CONFIG_IDF_TARGET_ESP32C3)
static constexpr int PIN_MATRIX_DIN = 3;   // FSPI MOSI -> module DIN
static constexpr int PIN_MATRIX_CLK = 2;   // FSPI SCK  -> module CLK
static constexpr int PIN_MATRIX_CS  = 7;   // FSPI SS   -> module CS/LOAD
#else  // classic ESP32 (WROOM-32), VSPI
static constexpr int PIN_MATRIX_DIN = 23;  // VSPI MOSI -> module DIN
static constexpr int PIN_MATRIX_CLK = 18;  // VSPI SCK  -> module CLK
static constexpr int PIN_MATRIX_CS  = 5;   // VSPI SS   -> module CS/LOAD
#endif
static constexpr uint32_t MATRIX_SPI_HZ = 4000000;  // MAX7219 tops out at 10 MHz

// How many modules the mouth spans. Two is the original AVR layout (16
// columns); three gives the wider mouth (24 columns). Artwork exists for both
// in MouthFrames.cpp -- changing this number is all that is needed.
#define MOUTH_MODULES 2

// Modules are wired as one chain. Device 0 is the one nearest the MCU (the
// module whose DIN is connected to PIN_MATRIX_DIN).
static constexpr uint8_t EYE_LEFT      = 0;
static constexpr uint8_t EYE_RIGHT     = 1;
static constexpr uint8_t MOUTH_FIRST   = 2;
static constexpr uint8_t MOUTH_COLS    = MOUTH_MODULES * 8;
static constexpr uint8_t MATRIX_COUNT  = MOUTH_FIRST + MOUTH_MODULES;

// Flip an individual module 180 degrees, for when it is mounted upside down.
// Backed by saved settings, so it can change at runtime.
bool matrixRotated(uint8_t device);

// --- audio and motion ---------------------------------------------------
// The C3 has no DAC. Audio leaves as a 1-bit PDM stream on one pin and needs
// an RC low-pass to become line level. The PDM carrier sits around 1 MHz
// (sample rate x 64), and the audio we care about is under 8 kHz, so aim the
// corner near 16 kHz: 1k + 10nF per stage, two stages for ~70 dB of carrier
// rejection. Then block DC with ~1uF into the amp, because this pin idles
// around 1.65 V.
//
//   GPIO --[1k]--+--[1k]--+--||--> amp in
//                |        |   1uF
//              10nF     10nF
//                |        |
//               GND      GND
//
// The PDM clock pin is required by the driver but need not be connected.
#if defined(CONFIG_IDF_TARGET_ESP32C3)
static constexpr int PIN_AUDIO_PDM_DOUT = 4;   // -> RC filter -> amp input
static constexpr int PIN_AUDIO_PDM_CLK  = 5;   // leave unconnected
static constexpr int PIN_MOTION         = 6;   // PIR out, or HC-SR04 trigger
// I2C for the MaxSonar MB1242. NOT the board's default SDA/SCL (GPIO 4 and
// 5) -- those are the audio PDM output and clock. The C3 routes I2C through
// the GPIO matrix, so any free pins work; 0 and 1 are free and neither is a
// strapping pin.
static constexpr int PIN_I2C_SDA        = 0;
static constexpr int PIN_I2C_SCL        = 1;
static constexpr int PIN_SONAR_ECHO     = 10;  // HC-SR04 echo, VIA A DIVIDER

// Physical controls. Both switch to GND and use the internal pull-ups, so
// closed reads LOW and no external resistors are needed.
// GPIO 10 is shared with the (unused) HC-SR04 echo; they cannot both be
// wired. After these, only GPIO 1 and 20 remain free.
static constexpr int PIN_TOGGLE         = 10;  // SPST: motion on / off
static constexpr int PIN_BUTTON         = 0;   // momentary: talk now
#else
static constexpr int PIN_AUDIO_PDM_DOUT = 25;
static constexpr int PIN_AUDIO_PDM_CLK  = 26;
static constexpr int PIN_MOTION         = 27;
static constexpr int PIN_I2C_SDA        = 21;
static constexpr int PIN_I2C_SCL        = 22;
static constexpr int PIN_SONAR_ECHO     = 35;
static constexpr int PIN_TOGGLE         = 32;  // SPST: motion on / off
static constexpr int PIN_BUTTON         = 33;  // momentary: talk now
#endif

// How often the whole chain is rewritten from scratch.
//
// A MAX7219 that browns out -- which a nearby audio amp can easily cause on a
// shared 5V rail -- comes back with undefined registers and stays wrong,
// because setRow() suppresses writes that match its shadow copy. There is no
// way to read the chip back, so the display is re-asserted on a timer
// instead.
//
// It costs about 300 bytes of SPI, roughly 0.6 ms, so it can run often.
// At 500 ms a module that drops out recovers before it reads as a fault
// rather than a flicker. Lower it further if a rail is really marginal --
// the ceiling is SPI time, and at 4 MHz that is nowhere near reached.
static constexpr uint32_t MATRIX_RESYNC_MS = 500;

static constexpr uint8_t INTENSITY_DEFAULT = 5;
static constexpr uint8_t INTENSITY_MIN     = 0;
static constexpr uint8_t INTENSITY_MAX     = 15;  // MAX7219 intensity is a 4-bit register
