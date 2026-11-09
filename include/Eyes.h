#pragma once
#include <Arduino.h>
#include "Config.h"

/*
  The two eye matrices.

  Each effect is compiled into a list of keyframes that update() steps through
  against millis(). Nothing blocks and nothing recurses.

  Two sequences are in play at once:

    the main sequence  drift, pause, and the periodic effects
    an interjection    a blink or a twitch

  An interjection preempts the main sequence wherever it happens to be, down
  to the millisecond, and the main step resumes afterwards with its remaining
  time intact. That is what lets a blink land at any moment instead of
  queueing behind a three second pause.
*/

enum EyeOp : uint8_t {
    OP_WAIT,       // hold the current picture
    OP_POS,        // a = x offset, b = y offset
    OP_PUPIL,      // a = left-eye pupil row, b = right-eye pupil row
    OP_LID_CLOSE,  // a = lid step 0..3, b = eye bitmask
    OP_LID_OPEN,   // a = lid step 0..3, b = eye bitmask
    OP_LAZY_DOWN,  // a = step 0..2
    OP_LAZY_UP,    // a = step 0..2
    OP_INTENSITY,  // a = brightness
};

static constexpr uint8_t EYE_MASK_LEFT  = 0x01;
static constexpr uint8_t EYE_MASK_RIGHT = 0x02;
static constexpr uint8_t EYE_MASK_BOTH  = 0x03;

struct EyeStep {
    uint8_t  op;
    int16_t  a;
    int16_t  b;
    uint16_t ms;
};

static constexpr uint8_t EYE_MAX_STEPS = 96;  // main sequence
static constexpr uint8_t EYE_MAX_INTER = 32;  // blink / twitch

static constexpr int8_t EYE_POS_MIN = -2;
static constexpr int8_t EYE_POS_MAX = 2;

// Run one of the periodic effects every N drift iterations; 0 disables them.
static constexpr uint8_t EFFECT_ITERATION = 4;

// --- blink and twitch tuning -------------------------------------------
// Gap between involuntary movements. A real blink lands every few seconds,
// so these are deliberately shorter than the original sketch managed.
static constexpr uint16_t BLINK_GAP_MIN_MS = 1500;
static constexpr uint16_t BLINK_GAP_MAX_MS = 5000;
// Of those, this percentage become a one-eye twitch rather than a blink...
static constexpr uint8_t  TWITCH_PERCENT = 18;
// ...and this percentage of the blinks are a quick double blink.
static constexpr uint8_t  DOUBLE_BLINK_PERCENT = 25;

class Eyes {
public:
    void begin();

    // facePowered: false while the face is off (eyes hold still and dark).
    // The eyes run their own schedule and no longer care what the mouth does.
    void update(uint32_t now, bool facePowered);

    void setIntensity(uint8_t value);
    void blank();

private:
    void displayEyes(int offsetX, int offsetY);
    void applyLid(int step, uint8_t mask, bool opening);
    void applyLazy(int step, bool down);
    static int clampPos(int value);

    // --- sequence building ---
    // push() appends to whichever buffer is currently being built.
    void buildInto(EyeStep *buf, uint8_t cap, uint8_t *len);
    void push(uint8_t op, int16_t a, int16_t b, uint16_t ms);
    void pushWait(uint16_t ms) { push(OP_WAIT, 0, 0, ms); }
    void pushMove(int newX, int newY, uint16_t stepDelay);
    void pushBlink(bool left, bool right);
    void pushTwitch();
    void pushCrossEyes();
    void pushMethEyes();
    void pushCrazySpin(uint8_t times);
    void pushRoundSpin(uint8_t times);
    void pushLazyEye();
    void pushGlow(uint8_t times);
    void buildIteration();
    void buildInterjection();

    void execute(const EyeStep &step);
    void scheduleNextBlink(uint32_t now);
    void startStep(uint32_t now, const EyeStep &step);

    EyeStep  seq_[EYE_MAX_STEPS];
    uint8_t  seqLen_   = 0;
    uint8_t  seqIndex_ = 0;
    bool     running_  = false;

    EyeStep  inter_[EYE_MAX_INTER];
    uint8_t  interLen_   = 0;
    uint8_t  interIndex_ = 0;
    bool     interActive_ = false;
    // What was left of the interrupted main step, restored on resume.
    uint16_t mainRemaining_ = 0;

    // Where push() is writing right now.
    EyeStep *buildBuf_ = nullptr;
    uint8_t *buildLen_ = nullptr;
    uint8_t  buildCap_ = 0;

    uint32_t stepStart_ = 0;
    uint16_t curStepMs_ = 0;
    uint32_t nextBlink_ = 0;

    // Position the builder is planning from, which runs ahead of what is drawn.
    int8_t plannedX_ = 0;
    int8_t plannedY_ = 0;

    // What each eye currently shows, so a lid can reopen onto the right
    // picture. Kept per eye because the two differ during cross/meth/lazy.
    uint8_t eyeCurrent_[2][8] = {};
    int8_t  currentX_ = 0;
    int8_t  currentY_ = 0;

    uint8_t cntLoop_   = 0;
    uint8_t cntEffect_ = 0;
    uint8_t intensity_ = INTENSITY_DEFAULT;
};

extern Eyes eyes;
