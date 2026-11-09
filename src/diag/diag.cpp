/*
  MAX7219 bring-up diagnostic.

  Deliberately shares nothing with the real firmware: no SPI peripheral, no
  MatrixChain, no animation. Every edge is a digitalWrite with microseconds of
  slack, so timing and the SPI peripheral are both off the suspect list.

  The point is to split one question into three:

    Stage 1  Are the ESP32 pins alive?       (measure them)
    Stage 2  Does DISPLAY TEST light the panel?  (power + DIN/CLK/CS good)
    Stage 3  Does normal register programming work?

  DISPLAY TEST is the key: it lights all 64 LEDs from inside the chip,
  ignoring the digit registers entirely. If stage 2 lights up and stage 3 does
  not, the panel and wiring are fine and the bug is in register setup. If
  stage 2 stays dark, no amount of firmware will help.
*/
#include <Arduino.h>
#include "Config.h"

// Follow the same pins as the real firmware, whichever board is selected.
static constexpr int PIN_DIN = PIN_MATRIX_DIN;
static constexpr int PIN_CLK = PIN_MATRIX_CLK;
static constexpr int PIN_CS  = PIN_MATRIX_CS;

// Follows the real chain length. Drop to 1 when bringing up a single module
// in isolation, which is the easiest way to localise a fault.
static constexpr int DEVICES = MATRIX_COUNT;

static constexpr uint8_t REG_NOOP        = 0x00;
static constexpr uint8_t REG_DIGIT0      = 0x01;
static constexpr uint8_t REG_DECODEMODE  = 0x09;
static constexpr uint8_t REG_INTENSITY   = 0x0A;
static constexpr uint8_t REG_SCANLIMIT   = 0x0B;
static constexpr uint8_t REG_SHUTDOWN    = 0x0C;
static constexpr uint8_t REG_DISPLAYTEST = 0x0F;

// Deliberately glacial: ~100 kHz. The MAX7219 handles 10 MHz, so if this
// works and 4 MHz does not, the answer is signal integrity.
static inline void tick() { delayMicroseconds(5); }

static void shiftByte(uint8_t value) {
    for (int8_t b = 7; b >= 0; b--) {
        digitalWrite(PIN_CLK, LOW);
        digitalWrite(PIN_DIN, (value >> b) & 1 ? HIGH : LOW);
        tick();
        digitalWrite(PIN_CLK, HIGH);   // MAX7219 samples DIN on the rising edge
        tick();
    }
}

// Send one register write to every device in the chain.
static void writeAll(uint8_t reg, uint8_t value) {
    digitalWrite(PIN_CS, LOW);
    tick();
    for (int d = 0; d < DEVICES; d++) {
        shiftByte(reg);
        shiftByte(value);
    }
    digitalWrite(PIN_CLK, LOW);
    tick();
    digitalWrite(PIN_CS, HIGH);        // rising CS latches the shifted data
    tick();
}

static void initPanel() {
    writeAll(REG_DISPLAYTEST, 0x00);
    writeAll(REG_DECODEMODE,  0x00);
    writeAll(REG_SCANLIMIT,   0x07);
    writeAll(REG_INTENSITY,   0x08);
    writeAll(REG_SHUTDOWN,    0x01);   // 0x01 = normal operation, 0x00 = sleep
}

static void fill(uint8_t value) {
    for (uint8_t r = 0; r < 8; r++) {
        writeAll(REG_DIGIT0 + r, value);
    }
}

void setup() {
    Serial.begin(115200);
    delay(400);

    pinMode(PIN_DIN, OUTPUT);
    pinMode(PIN_CLK, OUTPUT);
    pinMode(PIN_CS,  OUTPUT);
    digitalWrite(PIN_CS,  HIGH);
    digitalWrite(PIN_CLK, LOW);
    digitalWrite(PIN_DIN, LOW);

    Serial.println();
    Serial.println(F("=== MAX7219 diagnostic ==="));
    Serial.printf("DIN=GPIO%d  CLK=GPIO%d  CS=GPIO%d  devices=%d\n",
                  PIN_DIN, PIN_CLK, PIN_CS, DEVICES);
    Serial.println(F("Bit-banged at ~100 kHz. No SPI peripheral in use."));
}

void loop() {
    // ---------------------------------------------------------------
    Serial.println(F("\n[1] Pin test: each line toggles at 2 Hz for 6 s."));
    Serial.printf("    Measure GPIO%d, %d, %d against GND -- expect ~1.65 V\n",
                  PIN_DIN, PIN_CLK, PIN_CS);
    Serial.println(F("    on a meter (a 3.3 V square wave averaged)."));
    const int pins[3] = {PIN_DIN, PIN_CLK, PIN_CS};
    const char *names[3] = {"DIN", "CLK", "CS"};
    for (int i = 0; i < 3; i++) {
        // Report the real pin number, never a hardcoded one -- it differs per target.
        Serial.printf("    toggling %s / GPIO%d\n", names[i], pins[i]);
        for (int n = 0; n < 12; n++) {
            digitalWrite(pins[i], n & 1 ? HIGH : LOW);
            delay(250);
        }
        digitalWrite(pins[i], i == 2 ? HIGH : LOW);  // leave CS idle high
    }

    // ---------------------------------------------------------------
    Serial.println(F("\n[2] DISPLAY TEST on -- ALL 64 LEDs should light, 5 s."));
    Serial.println(F("    This bypasses the digit registers completely."));
    writeAll(REG_DISPLAYTEST, 0x01);
    delay(5000);
    writeAll(REG_DISPLAYTEST, 0x00);
    Serial.println(F("    DISPLAY TEST off -- panel should go dark."));
    delay(1500);

    // ---------------------------------------------------------------
    Serial.println(F("\n[3] Normal init, then all digits 0xFF -- all on, 5 s."));
    initPanel();
    fill(0xFF);
    delay(5000);

    Serial.println(F("[4] Walking row, 400 ms each. Row 0 is the DIG0 line."));
    fill(0x00);
    for (uint8_t r = 0; r < 8; r++) {
        writeAll(REG_DIGIT0 + r, 0xFF);
        Serial.printf("    row %d lit\n", r);
        delay(400);
        writeAll(REG_DIGIT0 + r, 0x00);
    }

    Serial.println(F("[5] Walking column, 400 ms each."));
    for (uint8_t c = 0; c < 8; c++) {
        fill(1 << c);
        Serial.printf("    column bit %d lit\n", c);
        delay(400);
    }
    fill(0x00);

    Serial.println(F("\n--- cycle complete, repeating ---"));
    delay(2000);
}
