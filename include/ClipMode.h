#pragma once
#include <Arduino.h>

/*
  Which set of phrases the prop draws from.

  Each mode is a directory on LittleFS. Adding a mode means adding a name and
  a directory here, dropping clips into the matching folder under data/, and
  running `pio run -t uploadfs` -- no other code changes.
*/
enum class ClipMode : uint8_t {
    Halloween = 0,
    Custom    = 1,
};

static constexpr uint8_t CLIP_MODE_COUNT = 2;

extern const char *const CLIP_MODE_NAMES[CLIP_MODE_COUNT];
extern const char *const CLIP_MODE_DIRS[CLIP_MODE_COUNT];

inline const char *clipModeName(uint8_t m) {
    return CLIP_MODE_NAMES[m < CLIP_MODE_COUNT ? m : 0];
}
inline const char *clipModeDir(uint8_t m) {
    return CLIP_MODE_DIRS[m < CLIP_MODE_COUNT ? m : 0];
}

/*
  Spoken announcement for a mode, e.g. "/system/mode_halloween.pfc".

  Kept in /system rather than a mode directory so motion can never pick it:
  listClips() only ever looks inside one mode's folder.
*/
inline String clipModeAnnouncement(uint8_t m) {
    return String("/system/mode_") + clipModeName(m) + ".pfc";
}
