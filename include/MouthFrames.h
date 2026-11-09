#pragma once
#include <Arduino.h>
#include "Config.h"

/*
  Mouth artwork, carried at both widths so that MOUTH_MODULES is the only
  thing that has to change when a module is added or lost.

  Frame indices mean the same thing in both sets, so the mouth state machine
  does not care which is in use.
*/
extern const uint8_t MOUTH_FRAME_COUNT;
extern const uint8_t MOUTH_FRAMES_16[][8][2] PROGMEM;
extern const uint8_t MOUTH_FRAMES_24[][8][3] PROGMEM;

// Phoneme shapes for lip sync. Index order is the channel order; see README.
extern const uint8_t PHONEME_COUNT;
extern const char *const PHONEME_NAMES[];
extern const uint8_t PHONEMES_16[][8][2] PROGMEM;
extern const uint8_t PHONEMES_24[][8][3] PROGMEM;

inline uint8_t mouthFrameRow(uint8_t frame, uint8_t row, uint8_t module) {
    if constexpr (MOUTH_MODULES == 2) {
        return pgm_read_byte(&MOUTH_FRAMES_16[frame][row][module]);
    } else {
        return pgm_read_byte(&MOUTH_FRAMES_24[frame][row][module]);
    }
}

inline uint8_t phonemeRow(uint8_t phoneme, uint8_t row, uint8_t module) {
    if constexpr (MOUTH_MODULES == 2) {
        return pgm_read_byte(&PHONEMES_16[phoneme][row][module]);
    } else {
        return pgm_read_byte(&PHONEMES_24[phoneme][row][module]);
    }
}
