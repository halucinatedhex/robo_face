#pragma once
#include <Arduino.h>
#include "Config.h"
#include "Face.h"

/*
  The mouth matrices, driven as one strip.

  Two sources can drive it:

    Idle      the autonomous expression cycle, as the AVR sketch did
    External  phonemes pushed in from a show controller for lip sync

  External wins while packets keep arriving and lapses back to Idle after
  EXTERNAL_TIMEOUT_MS of silence, so the prop returns to idling by itself when
  a sequence ends or the network drops. The eyes are unaffected either way.
*/
enum class MouthSource : uint8_t { Idle, External };

/*
  The idle cycle is a list of items, not frames.

  An item below MOUTH_FRAME_COUNT is a still pose, held for CYCLE_MS. These
  two ids instead stand for whole animations, played at their own rate until
  they finish, after which the cycle carries on with the next item. That is
  what lets "chomp" be chosen as one thing rather than as four stills that
  would each sit on screen for ten seconds.
*/
static constexpr uint8_t ANIM_CHOMP  = 200;
static constexpr uint8_t ANIM_SCREAM = 201;
static constexpr uint8_t ANIM_WAVE   = 202;

// How long the mouth keeps lip-syncing after the last phoneme arrives.
static constexpr uint32_t EXTERNAL_TIMEOUT_MS = 2000;

class Mouth {
public:
    void begin();
    void update(uint32_t now);

    void setMode(FaceMode mode);
    FaceMode mode() const { return mode_; }

    // Drive the mouth from outside. Index is into PHONEME_NAMES.
    void setPhoneme(uint8_t index, uint32_t now);
    MouthSource source() const { return source_; }
    uint8_t phoneme() const { return phoneme_; }

    void draw(uint8_t frameIndex);
    void drawPhoneme(uint8_t index);
    void blank();

private:
    void advance();
    void startCycleItem();
    void nextCycleItem();
    void enterCycle();
    void enterChomp();
    void enterScream();
    void enterWave();

    FaceMode mode_        = FaceMode::Cycle;
    uint32_t lastUpdate_  = 0;
    uint32_t updateTime_  = 10000;
    uint8_t  frameIndex_  = 7;
    uint16_t tick_        = 0;
    uint8_t  cycleStep_   = 0;
    const uint8_t *cycleList_ = nullptr;
    uint8_t  cycleLen_    = 0;
    bool     calm_        = false;
    // Frames parsed out of settings.idleFrames for the active mode.
    static constexpr uint8_t MAX_CUSTOM = 24;
    uint8_t  custom_[MAX_CUSTOM] = {};
    uint8_t  waveStep_ = 0;
    bool     screamRising_ = true;

    MouthSource source_       = MouthSource::Idle;
    uint8_t     phoneme_      = 0;
    uint32_t    lastExternal_ = 0;
};

extern Mouth mouth;

// Animation composition, for the web picker to render a moving preview.
// Defined where the animations are, so the two cannot disagree.
struct MouthAnim {
    uint8_t        id;
    const char    *name;
    const uint8_t *frames;
    uint8_t        count;
    uint16_t       ms;
};
extern const MouthAnim MOUTH_ANIMS[];
extern const uint8_t   MOUTH_ANIM_COUNT;
