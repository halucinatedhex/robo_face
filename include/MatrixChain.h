#pragma once
#include <Arduino.h>
#include "Config.h"

/*
  Driver for a daisy-chained string of MAX7219 8x8 modules.

  Replaces the LedControl library from the AVR version: LedControl bit-bangs
  every edge with digitalWrite, which is slow on an ESP32. This uses hardware
  SPI and keeps a shadow copy of every row so that redundant writes are
  dropped -- the animation code calls setRow() far more often than the picture
  actually changes.
*/
class MatrixChain {
public:
    void begin();

    // row is 0..7 counting from the top, value is the 8 column bits.
    // Honours the per-module MATRIX_ROTATED flag.
    void setRow(uint8_t device, uint8_t row, uint8_t value);

    void clear(uint8_t device);
    void repaint(uint8_t device);

    // Re-send the configuration registers and every row, unconditionally.
    // The MAX7219 cannot be read back, so recovery cannot be conditional.
    void resync();

    // Rebuild the SPI settings from settings.spiKHz. Call after changing it.
    void applyClock();

    // Call every loop. Runs resync() on a timer so the display heals itself
    // after a module browns out and loses its registers.
    void tick(uint32_t now);

    uint32_t resyncCount() const { return resyncs_; }
    void clearAll();

    void setIntensity(uint8_t device, uint8_t value);
    void setIntensityAll(uint8_t value);

    void shutdown(uint8_t device, bool sleeping);

    // What is physically lit, post-rotation. Used by the web preview, so
    // what the browser draws is what the LEDs are actually doing.
    uint8_t shadowRow(uint8_t device, uint8_t row) const {
        return (device < MATRIX_COUNT && row < 8) ? shadow_[device][row] : 0;
    }

private:
    void writeRegister(uint8_t device, uint8_t reg, uint8_t value);
    void writeRegisterAll(uint8_t reg, uint8_t value);

    void writeConfig();
    void begin_initIntensity();

    uint8_t  shadow_[MATRIX_COUNT][8] = {};
    // Per device, not global: the eye glow effect ramps the two eyes
    // independently, and resync() has to restore what each module should
    // have rather than stamping one value over all of them.
    uint8_t  intensity_[MATRIX_COUNT];
    uint32_t lastResync_ = 0;
    uint32_t resyncs_ = 0;
};

// Reverse the bit order of a byte. The AVR version did this with inline
// assembly, which does not exist on Xtensa; this is the portable equivalent.
inline uint8_t bitswap(uint8_t x) {
    x = (uint8_t)((x >> 4) | (x << 4));
    x = (uint8_t)(((x & 0xCC) >> 2) | ((x & 0x33) << 2));
    x = (uint8_t)(((x & 0xAA) >> 1) | ((x & 0x55) << 1));
    return x;
}

extern MatrixChain matrix;
