#include "Mouth.h"
#include "MouthFrames.h"
#include "MatrixChain.h"
#include "ClipMode.h"
#include "Settings.h"

Mouth mouth;

// Resting expressions the face drifts through before it gets bored.
// The AVR version meant to walk 7 -> 8 -> 9 -> 15 -> 25, but a missing `else`
// made state 7 fall straight through to 9, so the 8 frame never appeared.
// Spelling the walk out as a list fixes that and makes it editable.
// Default "full" idle: the expressions, then both animations. Listing the
// animations here rather than hard-wiring them after the cycle is what makes
// them selectable like anything else.
static const uint8_t kCycleFrames[] = {7, 8, 9, 15, 25, ANIM_CHOMP, ANIM_SCREAM};

/*
  Calm idle: expressions that read as a face rather than a creature.

  9  flat        7  smirk       24 open smile   4  rounded "oh"
  8  small frown 16 pursed      25 toothy grin  5  small o     26 open

  Chomping teeth (27-30) and the screaming tongue (17-22) are deliberately
  absent -- they are the monstrous end of the artwork, and a mode meant to
  impersonate a person should not do them.
*/
static const uint8_t kCalmFrames[] = {9, 7, 24, 4, 8, 16, 25, 5, 26};

// Chomp loops these four; scream ping-pongs between the two bounds.
static const uint8_t kChompFrames[] = {27, 28, 29, 30};
static constexpr uint8_t kChompLen = sizeof(kChompFrames);
static constexpr uint8_t kScreamLow  = 17;
static constexpr uint8_t kScreamHigh = 22;

// Scream ping-pongs 17..22 and back; spell the full loop out so the browser
// preview matches what the device plays.
static const uint8_t kScreamLoop[] = {17, 18, 19, 20, 21, 22, 21, 20, 19, 18};

/*
  Scrolling wave. Six frames, because the pattern repeats every six columns
  -- the original artwork only had four, which would jump half a period at
  the loop point and visibly stutter. Slower than the other animations: a
  scroll reads as motion, so it does not need chomp's urgency.
*/
static const uint8_t kWaveFrames[] = {31, 32, 33, 34, 35, 36};
static constexpr uint32_t WAVE_MS    = 90;
static constexpr uint16_t WAVE_TICKS = 60;   // about 5.5 s

const MouthAnim MOUTH_ANIMS[] = {
    {ANIM_CHOMP,  "chomp",  kChompFrames, (uint8_t)sizeof(kChompFrames), 50},
    {ANIM_SCREAM, "scream", kScreamLoop,  (uint8_t)sizeof(kScreamLoop),  50},
    {ANIM_WAVE,   "wave",   kWaveFrames,  (uint8_t)sizeof(kWaveFrames),  WAVE_MS},
};
const uint8_t MOUTH_ANIM_COUNT = (uint8_t)(sizeof(MOUTH_ANIMS) / sizeof(MOUTH_ANIMS[0]));

static constexpr uint32_t CYCLE_MS  = 10000;
static constexpr uint32_t CHOMP_MS  = 50;
static constexpr uint32_t SCREAM_MS = 50;

static constexpr uint16_t CHOMP_TICKS  = 250;  // ~12.5 s of chewing
static constexpr uint16_t SCREAM_TICKS = 80;   // ~4 s of screaming

void Mouth::begin() {
    lastUpdate_ = millis();
    enterCycle();
    draw(frameIndex_);
}

void Mouth::draw(uint8_t frameIndex) {
    if (frameIndex >= MOUTH_FRAME_COUNT) {
        return;
    }
    for (uint8_t r = 0; r < 8; r++) {
        for (uint8_t m = 0; m < MOUTH_MODULES; m++) {
            matrix.setRow(MOUTH_FIRST + m, r, mouthFrameRow(frameIndex, r, m));
        }
    }
}

void Mouth::drawPhoneme(uint8_t index) {
    if (index >= PHONEME_COUNT) {
        return;
    }
    for (uint8_t r = 0; r < 8; r++) {
        for (uint8_t m = 0; m < MOUTH_MODULES; m++) {
            matrix.setRow(MOUTH_FIRST + m, r, phonemeRow(index, r, m));
        }
    }
}

/*
  Hand the mouth to an external source. Repeated calls with the same phoneme
  are cheap -- the matrix driver drops writes that would not change anything.
*/
void Mouth::setPhoneme(uint8_t index, uint32_t now) {
    if (index >= PHONEME_COUNT) {
        return;
    }
    source_       = MouthSource::External;
    phoneme_      = index;
    lastExternal_ = now;
    drawPhoneme(index);
}

void Mouth::blank() {
    for (uint8_t m = 0; m < MOUTH_MODULES; m++) {
        matrix.clear(MOUTH_FIRST + m);
    }
}

void Mouth::enterCycle() {
    // Re-read every time, so editing the frame list or toggling calm idle
    // takes effect at the next cycle without needing a reset.
    calm_ = settings.idleIsCalm(settings.clipMode);

    // A hand-picked list wins over the built-in defaults.
    const String &pick = settings.idleFrames[settings.clipMode];
    uint8_t n = 0;
    if (pick.length() > 0) {
        int from = 0;
        while (from <= (int)pick.length() && n < MAX_CUSTOM) {
            int comma = pick.indexOf(',', from);
            if (comma < 0) {
                comma = pick.length();
            }
            if (comma > from) {
                const int v = pick.substring(from, comma).toInt();
                if ((v >= 0 && v < MOUTH_FRAME_COUNT) ||
                    v == ANIM_CHOMP || v == ANIM_SCREAM ||
                    v == ANIM_WAVE) {
                    custom_[n++] = (uint8_t)v;
                }
            }
            from = comma + 1;
        }
    }

    if (n > 0) {
        cycleList_ = custom_;
        cycleLen_  = n;
    } else if (calm_) {
        cycleList_ = kCalmFrames;
        cycleLen_  = (uint8_t)sizeof(kCalmFrames);
    } else {
        cycleList_ = kCycleFrames;
        cycleLen_  = (uint8_t)sizeof(kCycleFrames);
    }
    cycleStep_ = 0;
    startCycleItem();
}

/*
  Begin whatever the current cycle item is: hold a pose, or hand over to an
  animation. Animations keep cycleStep_ intact so the cycle resumes after
  them rather than restarting.
*/
void Mouth::startCycleItem() {
    const uint8_t id = cycleList_[cycleStep_];
    if (id == ANIM_CHOMP) {
        enterChomp();
    } else if (id == ANIM_SCREAM) {
        enterScream();
    } else if (id == ANIM_WAVE) {
        enterWave();
    } else {
        mode_       = FaceMode::Cycle;
        frameIndex_ = id;
        updateTime_ = CYCLE_MS;
        tick_       = 0;
    }
    draw(frameIndex_);
}

void Mouth::nextCycleItem() {
    cycleStep_++;
    if (cycleStep_ >= cycleLen_) {
        cycleStep_ = 0;
    }
    startCycleItem();
}

void Mouth::enterChomp() {
    // cycleStep_ is deliberately left alone: the cycle resumes after this.
    mode_       = FaceMode::Chomp;
    frameIndex_ = kChompFrames[0];
    updateTime_ = CHOMP_MS;
    tick_       = 0;
}

void Mouth::enterScream() {
    mode_         = FaceMode::Scream;
    frameIndex_   = kScreamHigh;
    screamRising_ = false;
    updateTime_   = SCREAM_MS;
    tick_         = 0;
}

void Mouth::enterWave() {
    mode_       = FaceMode::Wave;
    waveStep_   = 0;
    frameIndex_ = kWaveFrames[0];
    updateTime_ = WAVE_MS;
    tick_       = 0;
}

void Mouth::setMode(FaceMode mode) {
    source_ = MouthSource::Idle;  // an explicit mode change takes the mouth back
    switch (mode) {
        case FaceMode::Off:
            mode_ = FaceMode::Off;
            blank();
            break;
        case FaceMode::Cycle:  enterCycle();  draw(frameIndex_); break;
        case FaceMode::Chomp:  enterChomp();  draw(frameIndex_); break;
        case FaceMode::Scream: enterScream(); draw(frameIndex_); break;
        case FaceMode::Wave:   enterWave();   draw(frameIndex_); break;
    }
    lastUpdate_ = millis();
}

void Mouth::update(uint32_t now) {
    if (mode_ == FaceMode::Off) {
        return;
    }

    if (source_ == MouthSource::External) {
        // Signed: callers may stamp lastExternal_ from a millis() read
        // taken slightly after this loop's `now`, and an unsigned compare
        // would underflow to a huge age and expire the handover instantly.
        if ((int32_t)(now - lastExternal_) < (int32_t)EXTERNAL_TIMEOUT_MS) {
            return;  // the show owns the mouth
        }
        // The stream stopped. Pick the idle cycle back up from the start.
        source_ = MouthSource::Idle;
        enterCycle();
        draw(frameIndex_);
        lastUpdate_ = now;
        return;
    }

    if ((uint32_t)(now - lastUpdate_) < updateTime_) {
        return;
    }
    lastUpdate_ = now;
    advance();
}

void Mouth::advance() {
    tick_++;

    switch (mode_) {
        case FaceMode::Cycle:
            nextCycleItem();
            return;

        case FaceMode::Chomp: {
            if (tick_ >= CHOMP_TICKS) {
                nextCycleItem();
                return;
            }
            uint8_t i = 0;
            while (i < kChompLen && kChompFrames[i] != frameIndex_) {
                i++;
            }
            frameIndex_ = kChompFrames[(i + 1) % kChompLen];
            break;
        }

        case FaceMode::Scream:
            if (tick_ >= SCREAM_TICKS) {
                nextCycleItem();
                return;
            }
            if (screamRising_) {
                if (frameIndex_ >= kScreamHigh) {
                    screamRising_ = false;
                    frameIndex_--;
                } else {
                    frameIndex_++;
                }
            } else {
                if (frameIndex_ <= kScreamLow) {
                    screamRising_ = true;
                    frameIndex_++;
                } else {
                    frameIndex_--;
                }
            }
            break;

        case FaceMode::Wave:
            if (tick_ >= WAVE_TICKS) {
                nextCycleItem();
                return;
            }
            waveStep_ = (uint8_t)((waveStep_ + 1) % sizeof(kWaveFrames));
            frameIndex_ = kWaveFrames[waveStep_];
            break;

        case FaceMode::Off:
            return;
    }

    draw(frameIndex_);
}
