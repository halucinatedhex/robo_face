/*
  robo_face -- ESP32 robot face.

  Grown out of an earlier AVR sketch.

  Five chained MAX7219 8x8 matrices: device 0 and 1 are the eyes, devices
  2..4 form a 24-column mouth.

  The loop never blocks. Eyes and mouth each own a state machine clocked off
  millis(), so adding network or sensor work later is a matter of calling it
  from loop() rather than threading it through a recursive delay helper.
*/
#include <Arduino.h>
#include "App.h"
#include "Audio.h"
#include "Config.h"
#include "Control.h"
#include "Eyes.h"
#include "Face.h"
#include "MatrixChain.h"
#include "Mouth.h"
#include "Net.h"
#include "Settings.h"
#include "Trigger.h"

/*
  Per-module 180 degree flip.

  Backed by Settings::rotated in NVS so a module mounted upside down can be
  corrected from the web page rather than by reflashing a prop that is
  already inside a pumpkin.
*/
bool matrixRotated(uint8_t device) {
    return settings.moduleRotated(device);
}

static uint8_t gIntensity = INTENSITY_DEFAULT;
static bool    facePowered = true;

static void applyIntensity(uint8_t value) {
    gIntensity = constrain(value, INTENSITY_MIN, INTENSITY_MAX);
    matrix.setIntensityAll(gIntensity);
    eyes.setIntensity(gIntensity);
}

static void handleCommand(Command cmd) {
    switch (cmd) {
        case Command::None:
            return;

        case Command::PowerToggle:
            facePowered = !facePowered;
            if (facePowered) {
                mouth.setMode(FaceMode::Cycle);
                applyIntensity(gIntensity);
            } else {
                mouth.setMode(FaceMode::Off);
                eyes.blank();
            }
            break;

        case Command::FaceOff:
            facePowered = false;
            mouth.setMode(FaceMode::Off);
            eyes.blank();
            break;

        case Command::FaceCycle:  facePowered = true; mouth.setMode(FaceMode::Cycle);  break;
        case Command::FaceScream: mouth.setMode(FaceMode::Scream); break;
        case Command::FaceChomp:  mouth.setMode(FaceMode::Chomp);  break;

        case Command::BrightnessUp:
            applyIntensity(gIntensity >= INTENSITY_MAX ? INTENSITY_MAX
                                                       : gIntensity + 1);
            break;
        case Command::BrightnessDown:
            applyIntensity(gIntensity <= INTENSITY_MIN ? INTENSITY_MIN
                                                       : gIntensity - 1);
            break;
    }
}

// --- the interface Net.cpp drives the face through ---
namespace app {
void setPhoneme(uint8_t index, uint32_t now) {
    if (!facePowered) {
        return;
    }
    mouth.setPhoneme(index, now);
}
void setMode(FaceMode m)      { handleCommand(m == FaceMode::Off ? Command::FaceOff
                                             : m == FaceMode::Chomp ? Command::FaceChomp
                                             : m == FaceMode::Scream ? Command::FaceScream
                                             : Command::FaceCycle); }
void setPowered(bool on)      { if (on != facePowered) handleCommand(Command::PowerToggle); }
bool powered()                { return facePowered; }
void setIntensity(uint8_t v)  { applyIntensity(v); settings.intensity = gIntensity;
                                settings.save(); }
uint8_t intensity()           { return gIntensity; }
FaceMode mode()               { return mouth.mode(); }
bool externalMouth()          { return mouth.source() == MouthSource::External; }
uint8_t phoneme()             { return mouth.phoneme(); }
bool speaking()               { return audio.playing(); }
bool playClip(const char *p)  { return trigger.fire(p); }
void stopClip()               { audio.stop(); }
}  // namespace app

void setup() {
    Serial.begin(115200);
    delay(200);
    settings.load();

    // esp_random() is seeded by hardware entropy, so the eye wander differs
    // each boot. A0 is not defined on every variant, so do not read it.
    randomSeed(esp_random());

    matrix.begin();
    applyIntensity(settings.intensity);

    eyes.begin();
    mouth.begin();
    control.begin();
    if (!audio.begin()) {
        Serial.print(F("audio unavailable: "));
        Serial.println(audio.lastError());
    } else {
        Serial.printf("audio ready, %lu clip(s) on flash\n", (unsigned long)audio.clipCount());
    }
    trigger.begin();
    net.begin();

    Serial.println(F("robo_face ready"));
}

void loop() {
    const uint32_t now = millis();

    handleCommand(control.poll(now));
    net.update(now);
    trigger.update(now);

    // While a clip plays, the newest due cue drives the mouth. Re-asserting
    // it every pass keeps the external-source timeout from expiring during a
    // long pause in the speech.
    if (audio.playing()) {
        int ph = -1, got;
        while ((got = audio.nextCue()) >= 0) {
            ph = got;
        }
        app::setPhoneme(ph >= 0 ? (uint8_t)ph : mouth.phoneme(), now);
    }

    // Eyes and mouth are independent: each runs its own schedule, and the
    // only thing they share is whether the face is powered at all.
    eyes.update(now, facePowered);
    mouth.update(now);

    // Periodically re-assert the whole chain, so a module that lost its
    // registers to a power sag recovers without a reboot.
    matrix.tick(now);
}
