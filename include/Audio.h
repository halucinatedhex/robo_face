#pragma once
#include <Arduino.h>

/*
  Clip playback with lip sync.

  The ESP32-C3 has I2S but no DAC, so audio leaves as a 1-bit PDM stream on a
  single pin. An RC low-pass turns that into line level for an analog amp --
  no DAC module needed.

  A clip is one .pfc file on LittleFS: a header, a lip-sync cue track, then
  16 kHz mono PCM. The cues are built offline by tools/make_clip.py, so at
  runtime this only has to replay them, which is why the sync is exact and
  costs nothing.

  Threading: a background task does nothing but read the file and push bytes
  at I2S. Cues are handed to the main loop through nextCue(), because the
  matrix SPI bus must only ever be touched from one thread.
*/

static constexpr uint16_t AUDIO_MAX_CUES = 512;

struct AudioCue {
    uint32_t ms;
    uint8_t  phoneme;
};

class Audio {
public:
    bool begin();

    bool play(const char *path);
    void stop();
    bool playing() const { return playing_; }

    // Milliseconds of audio actually heard, DMA latency accounted for.
    uint32_t positionMs() const;

    // Returns the phoneme whose cue has just come due, or -1 for nothing.
    // Call once per loop while playing().
    int nextCue();

    const char *lastError() const { return err_; }
    uint32_t clipCount() const { return clipCount_; }

    // Fills names with up to max clip paths found in dir. Pass "/" for the
    // filesystem root.
    uint8_t listClips(const char *dir, String *names, uint8_t max) const;

private:
    friend void audioTaskEntry(void *);
    void feed();

    AudioCue cues_[AUDIO_MAX_CUES];
    uint16_t cueCount_ = 0;
    uint16_t cueIndex_ = 0;

    volatile bool     playing_   = false;
    volatile bool     stopReq_   = false;
    volatile uint32_t written_   = 0;   // samples handed to I2S
    uint32_t          total_     = 0;   // samples in the clip
    uint32_t          rate_      = 16000;
    uint32_t          clipCount_ = 0;
    const char       *err_       = "";
};

extern Audio audio;
