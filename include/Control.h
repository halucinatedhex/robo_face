#pragma once
#include <Arduino.h>

/*
  Where input comes from.

  The AVR build read a NEC IR remote here. That hardware is gone, so this is a
  seam rather than a driver: poll() always reports Command::None and the face
  runs itself. Wire Wi-Fi, BLE, buttons or a web UI in by filling in begin()
  and poll() -- nothing else in the firmware needs to change.
*/
enum class Command : uint8_t {
    None,
    PowerToggle,     // off <-> cycling
    FaceOff,
    FaceCycle,
    FaceScream,
    FaceChomp,
    BrightnessUp,
    BrightnessDown,
};

class Control {
public:
    void begin();

    // Called once per loop. Returns Command::None when there is nothing to do.
    Command poll(uint32_t now);
};

extern Control control;
