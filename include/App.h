#pragma once
#include <Arduino.h>
#include "Face.h"

/*
  What the network layer is allowed to do to the face.

  Net.cpp talks to the face only through this, so the web handlers and the
  E1.31 receiver cannot reach past it into the animation state.
*/
namespace app {
void     setPhoneme(uint8_t index, uint32_t now);
void     setMode(FaceMode mode);
void     setPowered(bool on);
bool     powered();
void     setIntensity(uint8_t value);
uint8_t  intensity();
FaceMode mode();
bool     externalMouth();
uint8_t  phoneme();
bool     speaking();
bool     playClip(const char *path);   // nullptr = pick one at random
void     stopClip();
}  // namespace app
