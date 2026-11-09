#include <Wire.h>

#include "Trigger.h"
#include "App.h"
#include "Audio.h"
#include "ClipMode.h"
#include "Config.h"
#include "Settings.h"

Trigger trigger;

// Sensor modules chatter around the edge of detection; ignore anything
// closer together than this.
static constexpr uint32_t EDGE_DEBOUNCE_MS = 250;
static constexpr uint8_t  MAX_CLIPS = 24;

// --- HC-SR04 echo timing, measured in an interrupt ------------------------
static volatile uint32_t echoRise_   = 0;
static volatile uint32_t echoWidth_  = 0;
static volatile bool     echoReady_  = false;

static void IRAM_ATTR echoIsr() {
    if (digitalRead(PIN_SONAR_ECHO)) {
        echoRise_ = micros();
    } else if (echoRise_) {
        echoWidth_ = micros() - echoRise_;
        echoRise_  = 0;
        echoReady_ = true;
    }
}

// Sound travels ~343 m/s: 58 microseconds per centimetre, there and back.
static constexpr float US_PER_CM = 58.0f;

// MB1242 command: take a range reading. The result is readable ~100 ms later
// as two bytes, high first, already in centimetres.
static constexpr uint8_t MB1242_CMD_RANGE = 0x51;

void Trigger::begin() {
    bootAt_ = millis();
    applyPhysInputs();
    applySensor();
}

void Trigger::applyPhysInputs() {
    if (!settings.physInputs) {
        toggleOn_   = true;      // no switch fitted: motion follows the web setting
        buttonDown_ = false;
        return;
    }
    // Switch to GND, internal pull-up: closed is LOW.
    pinMode(PIN_TOGGLE, INPUT_PULLUP);
    pinMode(PIN_BUTTON, INPUT_PULLUP);
    Serial.printf("physical inputs on: toggle gpio%d, button gpio%d\n",
                  PIN_TOGGLE, PIN_BUTTON);
}

bool Trigger::motionArmed() const {
    if (!settings.motionEnabled) {
        return false;            // the web switch can always veto
    }
    return settings.physInputs ? toggleOn_ : true;
}

/*
  Speak the name of a mode.

  Played directly rather than through fire(), so it does not count as a
  trigger, does not become "last clip", and cannot be picked at random. A
  missing announcement file is not an error -- the mode still changes.
*/
void Trigger::announceMode(uint8_t mode) {
    const String path = clipModeAnnouncement(mode);
    if (!audio.play(path.c_str())) {
        Serial.print(F("no announcement clip: "));
        Serial.println(path);
    }
}

void Trigger::setMode(uint8_t mode) {
    if (mode >= CLIP_MODE_COUNT) {
        return;
    }
    settings.clipMode = mode;
    settings.save();
    // The no-repeat index refers to a position in the previous mode's list,
    // which means something different now.
    lastIndex_ = -1;
    Serial.print(F("mode -> "));
    Serial.println(clipModeName(mode));
    announceMode(mode);
}

/*
  Read the toggle, and act on the button.

  A short press makes it talk, and fires on *release* so that a long hold is
  not also heard as a short press. A hold past BUTTON_LONG_MS changes mode at
  the moment the threshold passes, while the button is still down, so the
  spoken mode name is the confirmation.

  The button ignores the cooldown and the motion toggle: a deliberate press
  should always get a response. It does respect playback, so a second press
  cannot cut off the clip it just started.
*/
void Trigger::updatePhysInputs(uint32_t now) {
    if (!settings.physInputs) {
        return;
    }

    const bool closed = digitalRead(PIN_TOGGLE) == LOW;
    toggleOn_ = settings.toggleInvert ? !closed : closed;

    const bool down = digitalRead(PIN_BUTTON) == LOW;

    if (down != buttonDown_ && (uint32_t)(now - btnEdge_) > EDGE_DEBOUNCE_MS) {
        btnEdge_ = now;
        if (down) {
            buttonDown_  = true;
            btnPressAt_  = now;
            longFired_   = false;
        } else {
            buttonDown_ = false;
            // Short press: talk. Suppressed if the hold already changed mode.
            if (!longFired_ && !warmingUp(now) && !audio.playing()) {
                fire();
            }
            longFired_ = false;
        }
        return;
    }

    // Still held: has it crossed into very-long territory?
    if (buttonDown_ && !longFired_ &&
        (uint32_t)(now - btnPressAt_) >= BUTTON_LONG_MS) {
        longFired_ = true;
        setMode((uint8_t)((settings.clipMode + 1) % CLIP_MODE_COUNT));
    }
}

const char *Trigger::sensorName() const {
    switch (type_) {
        case SensorType::Pir:       return "PIR";
        case SensorType::SonarI2C:  return "MaxSonar MB1242 (I2C)";
        case SensorType::SonarPing: return "HC-SR04";
    }
    return "?";
}

void Trigger::applySensor() {
    detachInterrupt(digitalPinToInterrupt(PIN_SONAR_ECHO));
    type_ = (SensorType)settings.sensorType;
    distanceCm_ = -1;
    inRange_ = false;
    motion_ = false;
    for (auto &w : window_) {
        w = -1;
    }

    pending_   = false;
    counted_   = false;
    highSince_ = 0;

    switch (type_) {
        case SensorType::Pir:
            // Pulled down so an unplugged sensor reads clear rather than
            // floating and triggering at random.
            pinMode(PIN_MOTION, INPUT_PULLDOWN);
            break;

        case SensorType::SonarI2C:
            Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, (uint32_t)settings.i2cKHz * 1000UL);
            recoverBus();
            i2cFails_ = 0;
            i2cDead_  = false;
            break;

        case SensorType::SonarPing:
            pinMode(PIN_MOTION, OUTPUT);
            digitalWrite(PIN_MOTION, LOW);
            pinMode(PIN_SONAR_ECHO, INPUT);
            attachInterrupt(digitalPinToInterrupt(PIN_SONAR_ECHO), echoIsr, CHANGE);
            break;
    }
    Serial.print(F("sensor: "));
    Serial.println(sensorName());
}

uint32_t Trigger::cooldownLeftMs(uint32_t now) const {
    const uint32_t cd = (uint32_t)settings.cooldownSec * 1000UL;
    if (!lastFire_) {
        return 0;
    }
    const uint32_t since = now - lastFire_;
    return (since >= cd) ? 0 : (cd - since);
}

/*
  Choose a clip at random but never the same one twice running -- hearing the
  same line twice in a row is what makes a prop feel broken rather than alive.
*/
bool Trigger::pickClip(String &out) {
    String all[MAX_CLIPS];
    const uint8_t total = audio.listClips(clipModeDir(settings.clipMode),
                                          all, MAX_CLIPS);
    // Keep only the clips this mode allows motion to use.
    String names[MAX_CLIPS];
    uint8_t n = 0;
    for (uint8_t i = 0; i < total; i++) {
        if (settings.clipAllowedForMotion(settings.clipMode, all[i])) {
            names[n++] = all[i];
        }
    }
    if (n == 0) {
        return false;
    }
    if (n == 1) {
        out = names[0];
        lastIndex_ = 0;
        return true;
    }
    int8_t pick;
    do {
        pick = (int8_t)random(0, n);
    } while (pick == lastIndex_ && n > 1);
    lastIndex_ = pick;
    out = names[pick];
    return true;
}

bool Trigger::fire(const char *path) {
    String clip;
    if (path != nullptr) {
        clip = path;
    } else if (!pickClip(clip)) {
        return false;
    }
    if (!app::powered()) {
        app::setPowered(true);
    }
    if (!audio.play(clip.c_str())) {
        return false;
    }
    lastClip_ = clip;
    lastFire_ = millis();
    fires_++;
    return true;
}

void Trigger::onDetected(uint32_t now) {
    if (warmingUp(now)) {
        return;     // sensor still settling; anything it reports is noise
    }
    if (motionArmed() && !audio.playing() && cooldownLeftMs(now) == 0) {
        fire();
    }
}

/*
  A PIR has to stay high for pirHoldMs before it counts.

  The sensitivity pot sets detection range; it does nothing about the brief
  spikes these modules emit from draughts, sunlight and warm air currents.
  Requiring the signal to persist filters those out, and costs only a short
  delay before the prop speaks.
*/
void Trigger::updatePir(uint32_t now) {
    const bool level = digitalRead(PIN_MOTION) == HIGH;

    /*
      While the prop is talking -- and for a moment after -- discard whatever
      the sensor says and clear the latch. Otherwise a glitch caused by the
      amp is remembered and fired as soon as the cooldown allows, which makes
      the prop talk to an empty room on a loop.
    */
    if (settling(now)) {
        highSince_ = 0;
        counted_   = false;
        motion_    = false;
        return;
    }

    if (!level) {
        if (motion_ && (uint32_t)(now - lastEdge_) > EDGE_DEBOUNCE_MS) {
            lastEdge_ = now;
            motion_   = false;
            counted_  = false;      // re-arm for the next visit
            highSince_ = 0;
        }
        return;
    }

    if (highSince_ == 0) {
        highSince_ = now;           // start of a possible detection
        return;
    }
    if ((uint32_t)(now - highSince_) < settings.pirHoldMs) {
        return;                     // not sustained yet
    }

    motion_ = true;
    if (!counted_) {
        counted_ = true;
        lastEdge_ = now;
        onDetected(now);
    }
}

/*
  Free a bus that a slave is holding low.

  Returns true if SDA came back high. Called before opening the bus and after
  a run of failures, because once a device latches SDA low nothing else on the
  bus can work and no amount of retrying at the protocol level helps.
*/
bool Trigger::recoverBus() {
    Wire.end();
    pinMode(PIN_I2C_SDA, INPUT_PULLUP);
    pinMode(PIN_I2C_SCL, INPUT_PULLUP);
    delayMicroseconds(10);
    if (digitalRead(PIN_I2C_SDA) == HIGH) {
        Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, (uint32_t)settings.i2cKHz * 1000UL);
        return true;
    }

    Serial.println(F("I2C: SDA held low, clocking the bus free"));
    pinMode(PIN_I2C_SCL, OUTPUT);
    for (uint8_t i = 0; i < 9 && digitalRead(PIN_I2C_SDA) == LOW; i++) {
        digitalWrite(PIN_I2C_SCL, LOW);
        delayMicroseconds(5);
        digitalWrite(PIN_I2C_SCL, HIGH);
        delayMicroseconds(5);
    }
    // Then a STOP: SDA released while SCL is high.
    pinMode(PIN_I2C_SDA, OUTPUT);
    digitalWrite(PIN_I2C_SDA, LOW);
    delayMicroseconds(5);
    pinMode(PIN_I2C_SCL, INPUT_PULLUP);
    delayMicroseconds(5);
    pinMode(PIN_I2C_SDA, INPUT_PULLUP);
    delayMicroseconds(10);

    const bool freed = digitalRead(PIN_I2C_SDA) == HIGH;
    Serial.print(F("I2C: recovery "));
    Serial.println(freed ? F("succeeded") : F("FAILED - SDA still low"));
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, (uint32_t)settings.i2cKHz * 1000UL);
    return freed;
}

void Trigger::requestI2cRange() {
    Wire.beginTransmission(MB1242_ADDR);
    Wire.write(MB1242_CMD_RANGE);
    Wire.endTransmission();
}

/*
  Collect the result of the previous range command.

  Returns -1 if the device did not answer, which is the useful signal when
  wiring is wrong -- the distance readout shows n/a rather than a plausible
  but invented number.
*/
int16_t Trigger::readI2cCm() {
    if (Wire.requestFrom((uint8_t)MB1242_ADDR, (uint8_t)2) != 2) {
        return -2;                      // nobody answered
    }
    const uint8_t hi = (uint8_t)Wire.read();
    const uint8_t lo = (uint8_t)Wire.read();
    const int16_t cm = (int16_t)(((uint16_t)hi << 8) | lo);
    if (cm < SONAR_MIN_CM || cm > SONAR_MAX_CM) {
        return -1;                      // answered, but nothing usable
    }
    return cm;
}

// Blocking single reading, for the web diagnostic only.
int16_t Trigger::probeSonar(uint8_t *hi, uint8_t *lo) {
    recoverBus();
    requestI2cRange();
    delay(MB1242_SETTLE_MS + 30);
    if (Wire.requestFrom((uint8_t)MB1242_ADDR, (uint8_t)2) != 2) {
        *hi = *lo = 0;
        return -2;
    }
    *hi = (uint8_t)Wire.read();
    *lo = (uint8_t)Wire.read();
    i2cFails_ = 0;
    i2cDead_  = false;                  // a successful probe clears the latch
    pending_  = false;
    return (int16_t)(((uint16_t)*hi << 8) | *lo);
}

void Trigger::pingStart() {
    digitalWrite(PIN_MOTION, LOW);
    delayMicroseconds(2);
    digitalWrite(PIN_MOTION, HIGH);
    delayMicroseconds(10);
    digitalWrite(PIN_MOTION, LOW);
}

void Trigger::updateSonar(uint32_t now) {
    int16_t cm = -1;

    if (type_ == SensorType::SonarI2C) {
        /*
          Two phases, so nothing blocks: tell the device to range, come back
          100 ms later for the answer. A blocking delay here would stall the
          face, the audio feed and the web server.
        */
        if (!pending_) {
            // Back right off once the device stops answering, otherwise each
            // failed transaction stalls the whole loop for seconds.
            const uint32_t period = i2cDead_ ? I2C_RETRY_MS : SONAR_PERIOD_MS;
            if ((uint32_t)(now - lastSample_) < period) {
                return;
            }
            requestI2cRange();
            cmdAt_      = now;
            lastSample_ = now;   // charge the retry clock from the attempt
            pending_    = true;
            return;
        }
        if ((uint32_t)(now - cmdAt_) < MB1242_SETTLE_MS) {
            return;
        }
        pending_    = false;
        lastSample_ = now;
        cm = readI2cCm();
        if (cm == -2) {
            // Genuinely no answer: back off, or a dead bus stalls the loop.
            distanceCm_ = -1;
            if (i2cFails_ < I2C_FAIL_LIMIT) {
                i2cFails_++;
            }
            if (i2cFails_ >= I2C_FAIL_LIMIT && !i2cDead_) {
                i2cDead_ = true;
                recoverBus();     // it may simply be stuck holding SDA
                Serial.println(F("MB1242 not responding; backing off"));
            }
            return;
        }
        // Answered, so the sensor is alive even if the value is unusable.
        i2cFails_ = 0;
        i2cDead_  = false;
        if (cm < 0) {
            distanceCm_ = -1;
            return;
        }

    } else {
        if ((uint32_t)(now - lastSample_) < SONAR_PERIOD_MS) {
            return;
        }
        lastSample_ = now;
        if (echoReady_) {
            echoReady_ = false;
            const uint32_t w = echoWidth_;
            if (w > 100 && w < 25000) {          // ignore runts and timeouts
                cm = (int16_t)((float)w / US_PER_CM);
            }
        }
        pingStart();                              // kick off the next ping
        if (cm <= 0) {
            return;
        }
    }

    // Ultrasonic readings spike; a median of five is cheap and removes them
    // without the lag of an average.
    window_[windowAt_] = cm;
    windowAt_ = (uint8_t)((windowAt_ + 1) % 5);
    int16_t sorted[5];
    uint8_t n = 0;
    for (int16_t v : window_) {
        if (v > 0) {
            sorted[n++] = v;
        }
    }
    if (n == 0) {
        return;
    }
    for (uint8_t i = 1; i < n; i++) {
        const int16_t k = sorted[i];
        int8_t j = (int8_t)i - 1;
        while (j >= 0 && sorted[j] > k) { sorted[j + 1] = sorted[j]; j--; }
        sorted[j + 1] = k;
    }
    distanceCm_ = sorted[n / 2];

    if (settling(now)) {
        inRange_ = false;          // re-arm once the noise has passed
        motion_  = false;
        return;
    }

    const int16_t threshold = (int16_t)settings.triggerCm;
    if (!inRange_ && distanceCm_ <= threshold) {
        inRange_ = true;
        motion_  = true;
        onDetected(now);
    } else if (inRange_ && distanceCm_ > threshold + SONAR_HYSTERESIS_CM) {
        inRange_ = false;
        motion_  = false;
    }
}

void Trigger::update(uint32_t now) {
    // Keep pushing the settle deadline out for as long as audio is playing.
    if (audio.playing()) {
        settleUntil_ = now + SENSOR_SETTLE_MS;
    }

    updatePhysInputs(now);

    if (type_ == SensorType::Pir) {
        updatePir(now);
    } else {
        updateSonar(now);
    }
}
