#include <SPI.h>
#include "MatrixChain.h"
#include "Settings.h"

// MAX7219 register map
static constexpr uint8_t REG_NOOP        = 0x00;
static constexpr uint8_t REG_DIGIT0      = 0x01;
static constexpr uint8_t REG_DECODEMODE  = 0x09;
static constexpr uint8_t REG_INTENSITY   = 0x0A;
static constexpr uint8_t REG_SCANLIMIT   = 0x0B;
static constexpr uint8_t REG_SHUTDOWN    = 0x0C;
static constexpr uint8_t REG_DISPLAYTEST = 0x0F;

/*
  Clock rate is runtime, not compile time.

  The MAX7219 is rated to 10 MHz, but that assumes sane wiring. On hand-wired
  leads without termination, a fast clock rings enough to be miscounted, and
  one extra edge desynchronises the whole chain -- devices then read each
  other's data as register addresses, which shows up as panels changing
  brightness or blinking rather than simply going dark. Being able to drop the
  clock from the web page turns that from a reflash into a dropdown.
*/
static SPISettings kSpi(4000000, MSBFIRST, SPI_MODE0);

MatrixChain matrix;

void MatrixChain::begin_initIntensity() {
    for (uint8_t d = 0; d < MATRIX_COUNT; d++) {
        intensity_[d] = INTENSITY_DEFAULT;
    }
}

void MatrixChain::applyClock() {
    uint32_t hz = (uint32_t)settings.spiKHz * 1000UL;
    if (hz < 100000UL)   hz = 100000UL;
    if (hz > 10000000UL) hz = 10000000UL;
    kSpi = SPISettings(hz, MSBFIRST, SPI_MODE0);
}

void MatrixChain::begin() {
    begin_initIntensity();
    applyClock();
    pinMode(PIN_MATRIX_CS, OUTPUT);
    digitalWrite(PIN_MATRIX_CS, HIGH);
    SPI.begin(PIN_MATRIX_CLK, -1 /* MISO unused */, PIN_MATRIX_DIN, PIN_MATRIX_CS);

    writeConfig();

    for (uint8_t d = 0; d < MATRIX_COUNT; d++) {
        clear(d);
    }
}

void MatrixChain::setRow(uint8_t device, uint8_t row, uint8_t value) {
    if (device >= MATRIX_COUNT || row > 7) {
        return;
    }
    if (matrixRotated(device)) {
        row   = 7 - row;
        value = bitswap(value);
    }
    if (shadow_[device][row] == value) {
        return;
    }
    shadow_[device][row] = value;
    writeRegister(device, REG_DIGIT0 + row, value);
}

/*
  Force every row back out to a module, ignoring the shadow.
  Needed after a rotation flag changes: the logical picture is identical, so
  the de-duplication in setRow() would otherwise drop every write.
*/
void MatrixChain::repaint(uint8_t device) {
    if (device >= MATRIX_COUNT) {
        return;
    }
    for (uint8_t row = 0; row < 8; row++) {
        const uint8_t value = shadow_[device][row];
        writeRegister(device, REG_DIGIT0 + row, value);
    }
}

void MatrixChain::clear(uint8_t device) {
    for (uint8_t row = 0; row < 8; row++) {
        // Bypass setRow so the rotation flag cannot turn this into a no-op.
        shadow_[device][row] = 0;
        writeRegister(device, REG_DIGIT0 + row, 0);
    }
}

void MatrixChain::clearAll() {
    for (uint8_t d = 0; d < MATRIX_COUNT; d++) {
        clear(d);
    }
}

void MatrixChain::setIntensity(uint8_t device, uint8_t value) {
    if (device >= MATRIX_COUNT) {
        return;
    }
    const uint8_t v = constrain(value, INTENSITY_MIN, INTENSITY_MAX);
    intensity_[device] = v;     // remembered, or resync() would undo it
    writeRegister(device, REG_INTENSITY, v);
}

void MatrixChain::setIntensityAll(uint8_t value) {
    const uint8_t v = constrain(value, INTENSITY_MIN, INTENSITY_MAX);
    for (uint8_t d = 0; d < MATRIX_COUNT; d++) {
        intensity_[d] = v;
    }
    writeRegisterAll(REG_INTENSITY, v);
}

/*
  Put the whole chain back into a known state.

  Every control register, then every row from the shadow. Needed because a
  MAX7219 that browns out comes back with undefined registers -- typically
  blank (shutdown) or fully lit (display test) -- while the ESP32 carries on
  unaware. There is no way to read the chip back, so this is unconditional.
*/
void MatrixChain::writeConfig() {
    writeRegisterAll(REG_DISPLAYTEST, 0x00);  // normal operation, not all-on
    writeRegisterAll(REG_DECODEMODE,  0x00);  // raw segments, no BCD decode
    writeRegisterAll(REG_SCANLIMIT,   0x07);  // drive all 8 digits
    // Each device keeps its own brightness, so this cannot be a broadcast.
    for (uint8_t d = 0; d < MATRIX_COUNT; d++) {
        writeRegister(d, REG_INTENSITY, intensity_[d]);
    }
    writeRegisterAll(REG_SHUTDOWN,    0x01);  // normal operation, not asleep
}

void MatrixChain::resync() {
    writeConfig();
    for (uint8_t d = 0; d < MATRIX_COUNT; d++) {
        repaint(d);
    }
    resyncs_++;
}

void MatrixChain::tick(uint32_t now) {
    if ((uint32_t)(now - lastResync_) < MATRIX_RESYNC_MS) {
        return;
    }
    lastResync_ = now;
    resync();
}

void MatrixChain::shutdown(uint8_t device, bool sleeping) {
    writeRegister(device, REG_SHUTDOWN, sleeping ? 0x00 : 0x01);
}

/*
  A chain of N MAX7219s is one long shift register: to address device d we
  clock out N 16-bit words and make all but the d'th a no-op. Device 0 is
  nearest the controller, so its word must be sent last.
*/
void MatrixChain::writeRegister(uint8_t device, uint8_t reg, uint8_t value) {
    SPI.beginTransaction(kSpi);
    digitalWrite(PIN_MATRIX_CS, LOW);
    for (int8_t d = MATRIX_COUNT - 1; d >= 0; d--) {
        if (d == (int8_t)device) {
            SPI.transfer(reg);
            SPI.transfer(value);
        } else {
            SPI.transfer(REG_NOOP);
            SPI.transfer(0x00);
        }
    }
    digitalWrite(PIN_MATRIX_CS, HIGH);
    SPI.endTransaction();
}

void MatrixChain::writeRegisterAll(uint8_t reg, uint8_t value) {
    SPI.beginTransaction(kSpi);
    digitalWrite(PIN_MATRIX_CS, LOW);
    for (uint8_t d = 0; d < MATRIX_COUNT; d++) {
        SPI.transfer(reg);
        SPI.transfer(value);
    }
    digitalWrite(PIN_MATRIX_CS, HIGH);
    SPI.endTransaction();
}
