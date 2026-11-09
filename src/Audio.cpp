#include <ESP_I2S.h>
#include <LittleFS.h>

#include "Audio.h"
#include "Config.h"
#include "Settings.h"
#include "ClipMode.h"

Audio audio;

static I2SClass i2s;
static File     clipFile;
static TaskHandle_t taskHandle = nullptr;
static portMUX_TYPE fileMux = portMUX_INITIALIZER_UNLOCKED;

// 4 x 512 frames of DMA. At 16 kHz that is ~128 ms in flight, which is both
// enough slack to survive a busy loop and little enough that subtracting it
// keeps the mouth visually in step.
static constexpr uint32_t DMA_FRAMES = 512 * 4;
static constexpr size_t   CHUNK      = 1024;   // bytes per read

static constexpr char MAGIC[4] = {'P', 'F', 'C', '1'};

void audioTaskEntry(void *arg) {
    static_cast<Audio *>(arg)->feed();
}

bool Audio::begin() {
    if (!LittleFS.begin(true)) {
        err_ = "littlefs mount failed";
        return false;
    }

    // Count clips across every mode, so the boot log says whether any are
    // installed at all.
    String tmp[24];
    for (uint8_t m = 0; m < CLIP_MODE_COUNT; m++) {
        clipCount_ += listClips(clipModeDir(m), tmp, 24);
    }

    i2s.setPinsPdmTx(PIN_AUDIO_PDM_CLK, PIN_AUDIO_PDM_DOUT);
    if (!i2s.begin(I2S_MODE_PDM_TX, 16000, I2S_DATA_BIT_WIDTH_16BIT,
                   I2S_SLOT_MODE_MONO)) {
        err_ = "i2s begin failed";
        return false;
    }

    xTaskCreate(audioTaskEntry, "audio", 4096, this, 1, &taskHandle);
    return true;
}

uint8_t Audio::listClips(const char *dir, String *names, uint8_t max) const {
    uint8_t n = 0;
    File root = LittleFS.open(dir);
    if (!root || !root.isDirectory()) {
        return 0;
    }
    const String prefix = (strcmp(dir, "/") == 0) ? String("") : String(dir);
    for (File f = root.openNextFile(); f && n < max; f = root.openNextFile()) {
        if (f.isDirectory()) {
            continue;
        }
        String nm = f.name();
        if (!nm.endsWith(".pfc")) {
            continue;
        }
        // name() is a bare filename on some core versions and a full path on
        // others; normalise to an absolute path either way.
        if (!nm.startsWith("/")) {
            nm = prefix + "/" + nm;
        }
        names[n++] = nm;
    }
    return n;
}

bool Audio::play(const char *path) {
    stop();

    File f = LittleFS.open(path, "r");
    if (!f) {
        err_ = "clip not found";
        return false;
    }

    uint8_t head[16];
    if (f.read(head, sizeof(head)) != sizeof(head) ||
        memcmp(head, MAGIC, 4) != 0) {
        f.close();
        err_ = "not a pfc clip";
        return false;
    }
    rate_  = (uint32_t)head[4] | ((uint32_t)head[5] << 8) |
             ((uint32_t)head[6] << 16) | ((uint32_t)head[7] << 24);
    total_ = (uint32_t)head[8] | ((uint32_t)head[9] << 8) |
             ((uint32_t)head[10] << 16) | ((uint32_t)head[11] << 24);
    uint16_t cc = (uint16_t)head[12] | ((uint16_t)head[13] << 8);
    if (cc > AUDIO_MAX_CUES) {
        cc = AUDIO_MAX_CUES;
    }

    for (uint16_t i = 0; i < cc; i++) {
        uint8_t c[6];
        if (f.read(c, 6) != 6) {
            cc = i;
            break;
        }
        cues_[i].ms = (uint32_t)c[0] | ((uint32_t)c[1] << 8) |
                      ((uint32_t)c[2] << 16) | ((uint32_t)c[3] << 24);
        cues_[i].phoneme = c[4];
    }
    cueCount_ = cc;
    cueIndex_ = 0;

    if (rate_ != 16000) {
        i2s.configureTX(rate_, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);
    }

    portENTER_CRITICAL(&fileMux);
    clipFile = f;
    written_ = 0;
    stopReq_ = false;
    playing_ = true;
    portEXIT_CRITICAL(&fileMux);

    err_ = "";
    return true;
}

void Audio::stop() {
    if (!playing_) {
        return;
    }
    stopReq_ = true;
    // Let the task notice and close the file before returning.
    for (int i = 0; i < 50 && playing_; i++) {
        delay(2);
    }
    playing_ = false;
}

uint32_t Audio::positionMs() const {
    const uint32_t w = written_;
    if (w <= DMA_FRAMES) {
        return 0;
    }
    return (uint32_t)(((uint64_t)(w - DMA_FRAMES) * 1000ULL) / rate_);
}

int Audio::nextCue() {
    if (!playing_ || cueIndex_ >= cueCount_) {
        return -1;
    }
    if (positionMs() < cues_[cueIndex_].ms) {
        return -1;
    }
    return (int)cues_[cueIndex_++].phoneme;
}

/*
  Background task: read, write, repeat. Deliberately knows nothing about the
  face -- it must never touch the matrix bus.
*/
void Audio::feed() {
    uint8_t buf[CHUNK];
    for (;;) {
        if (!playing_) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        if (stopReq_) {
            clipFile.close();
            written_ = 0;
            playing_ = false;
            continue;
        }

        const int n = clipFile.read(buf, CHUNK);
        if (n <= 0) {
            // Flush the DMA tail with silence so the amp does not click.
            memset(buf, 0, CHUNK);
            for (int i = 0; i < 4; i++) {
                i2s.write(buf, CHUNK);
            }
            clipFile.close();
            playing_ = false;
            continue;
        }
        // PDM output has no hardware gain, so scale here. Kept integer to
        // stay cheap: this runs on every sample of every clip.
        const uint16_t vol = settings.volume;
        if (vol < 100) {
            int16_t *s16 = reinterpret_cast<int16_t *>(buf);
            for (int i = 0; i < n / 2; i++) {
                s16[i] = (int16_t)(((int32_t)s16[i] * vol) / 100);
            }
        }
        i2s.write(buf, (size_t)n);
        written_ += (uint32_t)(n / 2);
    }
}
