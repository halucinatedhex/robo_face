#pragma once
#include <Arduino.h>

/*
  Motion / presence detection.

  Three sensor types, chosen at runtime so the hardware can be swapped
  without a reflash:

    Pir          a PIR module on PIN_MOTION, active high. Simplest, but it
                 fires for anything warm in its cone -- no range control.
    SonarI2C     a MaxBotix I2CXL-MaxSonar (MB1242) on I2C. Best of the
                 three here: it reports centimetres directly so there is
                 nothing to calibrate, runs at 3.3 V, and gives a real
                 distance so the prop can ignore people walking past and
                 speak only to someone who comes close.
    SonarPing    an HC-SR04. Trigger pulse plus echo timing, done with an
                 interrupt because pulseIn() blocks for up to 38 ms and this
                 loop cannot afford that.

  The sonar types trigger on *approach*: a reading crossing from beyond the
  threshold to inside it. Hysteresis stops someone hovering at the boundary
  from retriggering continuously.
*/
enum class SensorType : uint8_t {
    Pir         = 0,
    SonarI2C    = 1,
    SonarPing   = 2,
};

/*
  PIR modules output nonsense for the first half-minute or so after power-on
  while their pyroelectric element settles. Without this the prop greets an
  empty room on every reboot.
*/
static constexpr uint32_t SENSOR_WARMUP_MS = 45000;

/*
  Ignore the sensor while a clip plays, and for this long afterwards.

  A PIR on the same supply as an audio amp false-triggers on the amp's
  current draw. Without this the prop triggers itself: a clip glitches the
  sensor, the detection is latched, and it is cashed in the instant the
  cooldown lifts -- one clip per cooldown, forever, with nobody in the room.
*/
static constexpr uint32_t SENSOR_SETTLE_MS = 4000;

/*
  Hold the button this long to change clip mode. Deliberately long -- it has
  to be impossible to hit by accident when you only meant to make it talk.
  The switch happens at the moment the threshold is crossed, while the button
  is still down, so the announcement is the confirmation.
*/
static constexpr uint32_t BUTTON_LONG_MS = 2500;

class Trigger {
public:
    void begin();
    void update(uint32_t now);

    bool fire(const char *path = nullptr);

    bool     motion() const { return motion_; }
    // Latest range in cm, or -1 when the sensor does not measure distance.
    int16_t  distanceCm() const { return distanceCm_; }
    uint32_t cooldownLeftMs(uint32_t now) const;
    uint32_t fireCount() const { return fires_; }
    const char *lastClip() const { return lastClip_.c_str(); }
    const char *sensorName() const;
    bool sensorResponding() const { return !i2cDead_; }
    // True while the sensor is still settling after power-on.
    // True while the sensor is being ignored because the prop is, or has
    // just been, making noise.
    bool settling(uint32_t now) const {
        return settleUntil_ && (int32_t)(now - settleUntil_) < 0;
    }
    bool warmingUp(uint32_t now) const {
        return (uint32_t)(now - bootAt_) < SENSOR_WARMUP_MS;
    }
    // Force one immediate reading, bypassing the backoff. For diagnostics.
    int16_t probeSonar(uint8_t *hi, uint8_t *lo);

    // Re-read the sensor type from settings and reconfigure the pins.
    void applySensor();
    // Configure the toggle and button pins from settings.physInputs.
    void applyPhysInputs();
    // Change clip mode, persist it, and speak the new mode's name. Used by
    // both the button hold and the web buttons, so they behave identically.
    void setMode(uint8_t mode);

    // Live state of the physical controls, for the status page.
    bool toggleOn() const { return toggleOn_; }
    bool buttonDown() const { return buttonDown_; }
    // Motion allowed right now: the web setting, gated by the toggle when
    // one is fitted.
    bool motionArmed() const;

private:
    bool pickClip(String &out);
    void updatePir(uint32_t now);
    void updateSonar(uint32_t now);
    void updatePhysInputs(uint32_t now);
    void announceMode(uint8_t mode);
    // >0 cm, -1 = answered but the value is unusable, -2 = no answer.
    // The distinction matters: an out-of-range reading proves the sensor is
    // alive, so it must not count toward the comms-failure backoff.
    int16_t readI2cCm();
    void    requestI2cRange();
    // Standard I2C recovery: a slave interrupted mid-byte can sit holding
    // SDA low forever, which makes every address appear to ACK. Clocking
    // SCL nine times lets it finish the byte it thinks it is sending.
    bool    recoverBus();
    void    pingStart();
    void    onDetected(uint32_t now);

    SensorType type_     = SensorType::Pir;
    bool     motion_     = false;
    int16_t  distanceCm_ = -1;
    bool     inRange_    = false;    // hysteresis latch for the sonar types
    uint32_t lastEdge_   = 0;
    uint32_t highSince_  = 0;    // when the PIR first went high
    bool     counted_    = false;// this high period already triggered
    uint32_t bootAt_     = 0;
    uint32_t settleUntil_ = 0;
    bool     toggleOn_    = true;
    bool     buttonDown_  = false;
    uint32_t btnEdge_     = 0;
    uint32_t btnPressAt_  = 0;
    bool     longFired_   = false;
    uint32_t lastSample_ = 0;
    uint32_t cmdAt_      = 0;
    bool     pending_    = false;
    uint8_t  i2cFails_   = 0;
    bool     i2cDead_    = false;
    uint32_t lastFire_   = 0;
    uint32_t fires_      = 0;
    int8_t   lastIndex_  = -1;
    String   lastClip_;

    int16_t  window_[5] = {-1, -1, -1, -1, -1};
    uint8_t  windowAt_  = 0;
};

extern Trigger trigger;

// The MB1242 needs about 100 ms between being told to range and the result
// being readable, so that sets the cadence.
static constexpr uint32_t SONAR_PERIOD_MS  = 100;
static constexpr uint32_t MB1242_SETTLE_MS = 120;

// Default I2C address of the I2CXL-MaxSonar family.
static constexpr uint8_t  MB1242_ADDR = 0x70;

// MB1242 (EZ4) specifies 20 cm to 765 cm. Anything nearer than 20 cm reads
// as 20, so a trigger threshold below that can never be crossed.
static constexpr int16_t  SONAR_MIN_CM = 20;
static constexpr int16_t  SONAR_MAX_CM = 765;

/*
  An I2C transaction to a device that is not there costs seconds, because the
  driver waits out an internal timeout on a floating bus. Polling that every
  100 ms would freeze the face, so after this many consecutive failures the
  sensor is treated as absent and retried only occasionally.
*/
static constexpr uint8_t  I2C_FAIL_LIMIT   = 3;
static constexpr uint32_t I2C_RETRY_MS     = 10000;

// Must back off this much beyond the threshold before another approach counts.
static constexpr int16_t SONAR_HYSTERESIS_CM = 30;
