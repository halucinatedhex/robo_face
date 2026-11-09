#pragma once
#include <Arduino.h>
#include "ClipMode.h"
#include "Config.h"

/*
  Values that survive a reboot, in NVS.

  Anything here can be changed from the web page without reflashing, which
  matters for a prop that is already mounted inside a pumpkin.
*/
struct Settings {
    uint16_t universe     = 1;   // E1.31 universe to listen on
    uint16_t startChannel = 1;   // first of the 10 phoneme channels
    uint8_t  intensity    = INTENSITY_DEFAULT;
    uint8_t  rotated      = 0;   // bitmask, one bit per module
    uint16_t cooldownSec  = 30;  // quiet period after a motion trigger
    uint8_t  volume       = 70;  // 0..100, applied in software
    bool     motionEnabled = true;
    uint8_t  sensorType   = 0;   // 0 PIR, 1 analog sonar, 2 HC-SR04
    // A PIR must stay high this long before it counts. Filters the brief
    // spikes they emit from draughts and warm air, which a pot cannot.
    // Physical toggle and button on PIN_TOGGLE / PIN_BUTTON. Off by default,
    // so nothing changes until they are actually wired.
    bool     physInputs   = false;
    // Set if the toggle reads backwards from how it is labelled.
    bool     toggleInvert = false;
    uint16_t pirHoldMs    = 400;
    uint16_t i2cKHz       = 100; // MB1242 bus speed; drop it on long leads
    uint16_t triggerCm    = 120; // sonar: speak when something is closer
    uint8_t  clipMode     = 0;   // index into CLIP_MODE_NAMES
    /*
      Bitmask, one bit per mode. Set means "calm idle": the mouth cycles
      human expressions only, skipping the chomping-teeth and screaming
      phases. Suits a mode that is meant to read as a person rather than
      a monster.
    */
    uint8_t  idleCalm     = 0;
    bool idleIsCalm(uint8_t mode) const { return (idleCalm >> mode) & 1; }

    /*
      Which mouth frames the idle cycle walks, per mode: comma-separated
      frame indices, e.g. "9,7,24,4". Empty means use the built-in default
      for that mode's style, so an untouched prop behaves as before.
    */
    String   idleFrames[CLIP_MODE_COUNT];
    // 0 = auto (join a known network, else host our own)
    // 1 = ap   (always host our own; no join attempt, fastest boot)
    // 2 = off  (radio off entirely: no web page, no E1.31)
    uint8_t  wifiMode     = 0;
    // Credentials are ours, in our own NVS namespace. Previously these lived
    // in the IDF's wifi config via WiFiManager, which proved easy to corrupt.
    /*
      Which clips motion may play, one comma-separated list of basenames per
      mode, e.g. "boo,scream,happy".

      An EMPTY list means every clip in that mode is eligible. That keeps the
      default behaviour and means a freshly uploaded set works without having
      to tick anything.
    */
    String   motionClips[CLIP_MODE_COUNT];

    // True if this clip may be chosen by a motion trigger.
    bool clipAllowedForMotion(uint8_t mode, const String &path) const;
    // Basename with directory and .pfc stripped, as stored in the lists.
    static String clipKey(const String &path);

    String   wifiSsid;
    String   wifiPass;
    uint16_t spiKHz       = 4000; // MAX7219 clock; lower it if the chain is
                                  // flaky on long or unterminated wiring

    void load();
    void save() const;

    bool moduleRotated(uint8_t device) const {
        return (rotated >> device) & 1;
    }
    void setModuleRotated(uint8_t device, bool on) {
        if (on) rotated |= (uint8_t)(1u << device);
        else    rotated &= (uint8_t)~(1u << device);
    }
};

extern Settings settings;
