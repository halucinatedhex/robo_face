#include "Eyes.h"
#include "MatrixChain.h"

Eyes eyes;

// Eyeball outline, no pupil.
static const uint8_t kEyeBall[8] = {
    0b00111100,
    0b01111110,
    0b11111111,
    0b11111111,
    0b11111111,
    0b11111111,
    0b01111110,
    0b00111100,
};

// The pupil is the two zero bits in the middle of this mask.
static constexpr uint8_t kEyePupil = 0b11100111;

static constexpr uint16_t DELAY_BLINK = 40;

void Eyes::begin() {
    buildInto(seq_, EYE_MAX_STEPS, &seqLen_);
    displayEyes(0, 0);
    plannedX_ = currentX_;
    plannedY_ = currentY_;
    running_      = false;
    interActive_  = false;
    seqLen_       = 0;
    interLen_     = 0;
    nextBlink_    = 0;
}

void Eyes::setIntensity(uint8_t value) {
    intensity_ = constrain(value, INTENSITY_MIN, INTENSITY_MAX);
    matrix.setIntensity(EYE_LEFT,  intensity_);
    matrix.setIntensity(EYE_RIGHT, intensity_);
}

void Eyes::blank() {
    matrix.clear(EYE_LEFT);
    matrix.clear(EYE_RIGHT);
    running_     = false;
    interActive_ = false;
    seqLen_      = 0;
    interLen_    = 0;
}

int Eyes::clampPos(int value) {
    if (value > EYE_POS_MAX) return EYE_POS_MAX;
    if (value < EYE_POS_MIN) return EYE_POS_MIN;
    return value;
}

/*
  Draw the eyeball with the pupil offset by (x, y) from centre.
  Both matrices show the same eye.
*/
void Eyes::displayEyes(int offsetX, int offsetY) {
    offsetX = clampPos(offsetX);
    offsetY = clampPos(offsetY);

    const int row1 = 3 - offsetY;
    const int row2 = 4 - offsetY;

    uint8_t pupilRow = kEyePupil;
    for (int i = 0; i < offsetX; i++) {
        pupilRow = (uint8_t)((pupilRow >> 1) | 0b10000000);
    }
    for (int i = 0; i > offsetX; i--) {
        pupilRow = (uint8_t)((pupilRow << 1) | 0b00000001);
    }

    // The pupil cannot light pixels that fall outside the eyeball.
    const uint8_t pupilRow1 = pupilRow & kEyeBall[row1];
    const uint8_t pupilRow2 = pupilRow & kEyeBall[row2];

    for (int r = 0; r < 8; r++) {
        uint8_t value;
        if (r == row1)       value = pupilRow1;
        else if (r == row2)  value = pupilRow2;
        else                 value = kEyeBall[r];

        matrix.setRow(EYE_LEFT,  r, value);
        matrix.setRow(EYE_RIGHT, r, value);
        eyeCurrent_[0][r] = value;
        eyeCurrent_[1][r] = value;
    }

    currentX_ = (int8_t)offsetX;
    currentY_ = (int8_t)offsetY;
}

void Eyes::applyLid(int step, uint8_t mask, bool opening) {
    if (mask & EYE_MASK_LEFT) {
        matrix.setRow(EYE_LEFT, step,     opening ? eyeCurrent_[0][step]     : 0);
        matrix.setRow(EYE_LEFT, 7 - step, opening ? eyeCurrent_[0][7 - step] : 0);
    }
    if (mask & EYE_MASK_RIGHT) {
        matrix.setRow(EYE_RIGHT, step,     opening ? eyeCurrent_[1][step]     : 0);
        matrix.setRow(EYE_RIGHT, 7 - step, opening ? eyeCurrent_[1][7 - step] : 0);
    }
}

// Lowers (down) or raises (up) the right pupil only.
void Eyes::applyLazy(int step, bool down) {
    // Mirror every write into the shadow, so a blink landing mid-droop
    // reopens onto the drooped eye rather than snapping it back.
    auto put = [&](int row, uint8_t value) {
        matrix.setRow(EYE_RIGHT, row, value);
        eyeCurrent_[1][row] = value;
    };
    if (down) {
        put(step + 2, kEyeBall[step + 2]);
        put(step + 3, kEyeBall[step + 3] & kEyePupil);
        put(step + 4, kEyeBall[step + 4] & kEyePupil);
    } else {
        put(4 - step, kEyeBall[4 - step] & kEyePupil);
        put(5 - step, kEyeBall[5 - step] & kEyePupil);
        put(6 - step, kEyeBall[6 - step]);
    }
}

// ---------------------------------------------------------------------------
// Sequence building
// ---------------------------------------------------------------------------

void Eyes::buildInto(EyeStep *buf, uint8_t cap, uint8_t *len) {
    buildBuf_ = buf;
    buildCap_ = cap;
    buildLen_ = len;
    *len = 0;
}

void Eyes::push(uint8_t op, int16_t a, int16_t b, uint16_t ms) {
    if (buildLen_ == nullptr || *buildLen_ >= buildCap_) {
        return;  // budget exhausted; drop the tail rather than overrun
    }
    buildBuf_[(*buildLen_)++] = EyeStep{op, a, b, ms};
}

/*
  A twitch: one eye, lids fluttering twice, far faster than a blink and never
  closing all the way. Reads as involuntary rather than deliberate.
*/
void Eyes::pushTwitch() {
    const uint8_t mask = (random(0, 2) == 0) ? EYE_MASK_LEFT : EYE_MASK_RIGHT;
    for (int n = 0; n < 2; n++) {
        push(OP_LID_CLOSE, 0, mask, 25);
        push(OP_LID_CLOSE, 1, mask, 30);
        push(OP_LID_OPEN,  1, mask, 25);
        push(OP_LID_OPEN,  0, mask, 25);
        if (n == 0) {
            pushWait(45);
        }
    }
}

void Eyes::buildInterjection() {
    buildInto(inter_, EYE_MAX_INTER, &interLen_);

    if (random(0, 100) < TWITCH_PERCENT) {
        pushTwitch();
        return;
    }
    pushBlink(true, true);
    if (random(0, 100) < DOUBLE_BLINK_PERCENT) {
        pushWait(70);
        pushBlink(true, true);
    }
}

void Eyes::scheduleNextBlink(uint32_t now) {
    nextBlink_ = now + (uint32_t)random(BLINK_GAP_MIN_MS, BLINK_GAP_MAX_MS + 1);
}

void Eyes::startStep(uint32_t now, const EyeStep &step) {
    stepStart_ = now;
    curStepMs_ = step.ms;
    execute(step);
}

void Eyes::pushMove(int newX, int newY, uint16_t stepDelay) {
    const int startX = plannedX_;
    const int startY = plannedY_;

    newX = clampPos(newX);
    newY = clampPos(newY);

    const int stepsX = abs(startX - newX);
    const int stepsY = abs(startY - newY);
    if (stepsX == 0 && stepsY == 0) {
        return;
    }

    const int dirX  = (newX >= startX) ? 1 : -1;
    const int dirY  = (newY >= startY) ? 1 : -1;
    const int steps = (stepsX > stepsY) ? stepsX : stepsY;

    const float changeX = (float)stepsX / (float)steps;
    const float changeY = (float)stepsY / (float)steps;

    for (int i = 1; i <= steps; i++) {
        const int x = startX + (int)lround(changeX * i * dirX);
        const int y = startY + (int)lround(changeY * i * dirY);
        push(OP_POS, x, y, stepDelay);
    }

    plannedX_ = (int8_t)newX;
    plannedY_ = (int8_t)newY;
}

void Eyes::pushBlink(bool left, bool right) {
    const uint8_t mask = (left ? EYE_MASK_LEFT : 0) | (right ? EYE_MASK_RIGHT : 0);
    if (mask == 0) {
        return;
    }
    for (int i = 0; i <= 3; i++) {
        push(OP_LID_CLOSE, i, mask, DELAY_BLINK);
    }
    for (int i = 3; i >= 0; i--) {
        push(OP_LID_OPEN, i, mask, DELAY_BLINK);
    }
}

void Eyes::pushCrossEyes() {
    pushMove(0, 0, 50);
    pushWait(500);

    uint8_t pupilR = kEyePupil;
    uint8_t pupilL = kEyePupil;

    for (int i = 0; i < 2; i++) {
        pupilR = (uint8_t)((pupilR >> 1) | 0b10000000);
        pupilL = (uint8_t)((pupilL << 1) | 0b00000001);
        push(OP_PUPIL, pupilR, pupilL, 100);
    }
    pushWait(2000);
    for (int i = 0; i < 2; i++) {
        pupilR = (uint8_t)((pupilR << 1) | 0b00000001);
        pupilL = (uint8_t)((pupilL >> 1) | 0b10000000);
        push(OP_PUPIL, pupilR, pupilL, 100);
    }
}

void Eyes::pushMethEyes() {
    pushMove(0, 0, 50);
    pushWait(500);

    uint8_t pupilR = kEyePupil;
    uint8_t pupilL = kEyePupil;

    for (int i = 0; i < 2; i++) {
        pupilR = (uint8_t)((pupilR << 1) | 0b00000001);
        pupilL = (uint8_t)((pupilL >> 1) | 0b10000000);
        push(OP_PUPIL, pupilR, pupilL, 100);
    }
    pushWait(1000);
    for (int i = 0; i < 2; i++) {
        pupilR = (uint8_t)((pupilR >> 1) | 0b10000000);
        pupilL = (uint8_t)((pupilL << 1) | 0b00000001);
        push(OP_PUPIL, pupilR, pupilL, 100);
    }
}

void Eyes::pushCrazySpin(uint8_t times) {
    if (times == 0) {
        return;
    }
    pushMove(0, 0, 50);
    pushWait(500);

    uint8_t row = kEyePupil;
    for (uint8_t t = 0; t < times; t++) {
        // centre out to the left edge
        for (int i = 0; i < 5; i++) {
            row = (uint8_t)((row >> 1) | 0b10000000);
            // first pass ramps the delay down, which reads as speeding up
            push(OP_PUPIL, row, row, 50 + (t == 0 ? (5 - i) * 10 : 0));
        }
        // right edge back to centre
        for (int i = 0; i < 5; i++) {
            row = (uint8_t)(row >> 1);
            if (i >= 2) {
                row = (uint8_t)(row | 0b10000000);
            }
            // last pass ramps the delay up, which reads as slowing down
            push(OP_PUPIL, row, row, 50 + (t == times - 1 ? (i + 1) * 10 : 0));
        }
    }
}

void Eyes::pushRoundSpin(uint8_t times) {
    if (times == 0) {
        return;
    }
    pushMove(2, 0, 50);
    pushWait(500);

    // Twelve positions round the rim; the first and last laps are eased.
    static const int8_t kPath[12][2] = {
        { 2, -1}, { 1, -2}, { 0, -2}, {-1, -2}, {-2, -1}, {-2, 0},
        {-2,  1}, {-1,  2}, { 0,  2}, { 1,  2}, { 2,  1}, { 2, 0},
    };
    static const uint16_t kEaseIn[12]  = {40, 30, 20, 10, 0, 0, 0,  0,  0,  0,  0,  0};
    static const uint16_t kEaseOut[12] = { 0,  0,  0,  0, 0, 0, 10, 20, 30, 40, 50, 0};

    for (uint8_t i = 0; i < times; i++) {
        for (uint8_t s = 0; s < 12; s++) {
            uint16_t ms = 40;
            if (i == 0)         ms += kEaseIn[s];
            if (i == times - 1) ms += kEaseOut[s];
            push(OP_POS, kPath[s][0], kPath[s][1], ms);
        }
    }
    plannedX_ = kPath[11][0];
    plannedY_ = kPath[11][1];
}

void Eyes::pushLazyEye() {
    pushMove(0, 1, 50);
    pushWait(300);

    for (int i = 0; i < 3; i++) {
        push(OP_LAZY_DOWN, i, 0, 150);
    }
    pushWait(700);
    for (int i = 0; i < 3; i++) {
        push(OP_LAZY_UP, i, 0, 25);
    }
}

void Eyes::pushGlow(uint8_t times) {
    for (uint8_t t = 0; t < times; t++) {
        for (int i = 2; i <= 8; i++) {
            push(OP_INTENSITY, i, 0, 50);
        }
        pushWait(250);
        for (int i = 7; i >= 1; i--) {
            push(OP_INTENSITY, i, 0, 25);
        }
        pushWait(150);
    }
    // The AVR version left the eyes stuck at brightness 1 here.
    push(OP_INTENSITY, intensity_, 0, 0);
}

/*
  One pass of the original loop(): drift to a new position, pause, sometimes
  blink, and every EFFECT_ITERATION passes run the next effect in rotation.
*/
void Eyes::buildIteration() {
    buildInto(seq_, EYE_MAX_STEPS, &seqLen_);
    seqIndex_ = 0;
    running_  = false;

    pushMove(random(EYE_POS_MIN, EYE_POS_MAX + 1),
             random(EYE_POS_MIN, EYE_POS_MAX + 1), 50);
    pushWait(random(5, 7) * 300);

    // Blinking is no longer part of this sequence -- it runs on its own
    // schedule and preempts whatever is playing.
    if (EFFECT_ITERATION > 0) {
        cntLoop_++;
        if (cntLoop_ == EFFECT_ITERATION) {
            cntLoop_ = 0;
            if (cntEffect_ > 6) {
                cntEffect_ = 0;
            }
            switch (cntEffect_) {
                case 0: pushCrossEyes();  break;
                case 1: pushRoundSpin(2); break;
                case 2: pushCrazySpin(2); break;
                case 3: pushMethEyes();   break;
                case 4: pushLazyEye();    break;
                case 5: pushBlink(true, false);
                        pushBlink(false, true); break;  // wink, then the other
                case 6: pushGlow(3);      break;
                default: break;
            }
            pushWait(1000);
            cntEffect_++;
        }
    }

    running_ = seqLen_ > 0;
}

// ---------------------------------------------------------------------------

void Eyes::execute(const EyeStep &step) {
    switch (step.op) {
        case OP_WAIT:
            break;
        case OP_POS:
            displayEyes(step.a, step.b);
            break;
        case OP_PUPIL:
            matrix.setRow(EYE_LEFT,  3, (uint8_t)step.a);
            matrix.setRow(EYE_LEFT,  4, (uint8_t)step.a);
            matrix.setRow(EYE_RIGHT, 3, (uint8_t)step.b);
            matrix.setRow(EYE_RIGHT, 4, (uint8_t)step.b);
            eyeCurrent_[0][3] = eyeCurrent_[0][4] = (uint8_t)step.a;
            eyeCurrent_[1][3] = eyeCurrent_[1][4] = (uint8_t)step.b;
            break;
        case OP_LID_CLOSE:
            applyLid(step.a, (uint8_t)step.b, false);
            break;
        case OP_LID_OPEN:
            applyLid(step.a, (uint8_t)step.b, true);
            break;
        case OP_LAZY_DOWN:
            applyLazy(step.a, true);
            break;
        case OP_LAZY_UP:
            applyLazy(step.a, false);
            break;
        case OP_INTENSITY:
            matrix.setIntensity(EYE_LEFT,  (uint8_t)step.a);
            matrix.setIntensity(EYE_RIGHT, (uint8_t)step.a);
            break;
        default:
            break;
    }
}

void Eyes::update(uint32_t now, bool facePowered) {
    if (!facePowered) {
        if (running_ || interActive_) {
            running_ = interActive_ = false;
            seqLen_ = interLen_ = 0;
        }
        return;
    }

    // Start a new drift iteration whenever the last one has played out.
    if (!running_) {
        buildIteration();
        running_ = seqLen_ > 0;
        if (!running_) {
            return;
        }
        seqIndex_ = 0;
        startStep(now, seq_[0]);
        if (nextBlink_ == 0) {
            scheduleNextBlink(now);
        }
        return;
    }

    // A blink or twitch preempts the main sequence wherever it is. The
    // remainder of the interrupted step is kept and resumed afterwards.
    if (!interActive_ && (int32_t)(now - nextBlink_) >= 0) {
        buildInterjection();
        scheduleNextBlink(now);
        if (interLen_ > 0) {
            const uint32_t elapsed = now - stepStart_;
            mainRemaining_ = (elapsed >= curStepMs_)
                                 ? 0
                                 : (uint16_t)(curStepMs_ - elapsed);
            interActive_ = true;
            interIndex_  = 0;
            startStep(now, inter_[0]);
            return;
        }
    }

    if ((uint32_t)(now - stepStart_) < curStepMs_) {
        return;
    }

    if (interActive_) {
        interIndex_++;
        if (interIndex_ >= interLen_) {
            interActive_ = false;
            // Resume the main step with whatever time it had left.
            stepStart_ = now;
            curStepMs_ = mainRemaining_;
            return;
        }
        startStep(now, inter_[interIndex_]);
        return;
    }

    seqIndex_++;
    if (seqIndex_ >= seqLen_) {
        running_ = false;
        return;
    }
    startStep(now, seq_[seqIndex_]);
}
