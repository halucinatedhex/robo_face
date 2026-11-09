#include <Preferences.h>
#include "Settings.h"
#include "ClipMode.h"

Settings settings;

// Halloween is first, so it is also the default.
const char *const CLIP_MODE_NAMES[CLIP_MODE_COUNT] = {"halloween", "custom"};
const char *const CLIP_MODE_DIRS[CLIP_MODE_COUNT]  = {"/halloween", "/custom"};

// Deliberately not renamed with the project: changing it orphans every
// saved setting on an existing board, including its WiFi credentials.
static constexpr const char *NS = "pumpkin";

void Settings::load() {
    Preferences p;
    if (!p.begin(NS, true)) {
        return;  // nothing stored yet; defaults stand
    }
    universe     = p.getUShort("universe", universe);
    startChannel = p.getUShort("startch",  startChannel);
    intensity    = p.getUChar("intensity", intensity);
    rotated      = p.getUChar("rotated",   rotated);
    cooldownSec  = p.getUShort("cooldown", cooldownSec);
    volume       = p.getUChar("volume",    volume);
    motionEnabled = p.getBool("motion",    motionEnabled);
    sensorType    = p.getUChar("sensor",   sensorType);
    triggerCm     = p.getUShort("trigcm",  triggerCm);
    i2cKHz        = p.getUShort("i2ckhz",  i2cKHz);
    pirHoldMs     = p.getUShort("pirhold", pirHoldMs);
    physInputs    = p.getBool("phys",      physInputs);
    toggleInvert  = p.getBool("toginv",    toggleInvert);
    clipMode      = p.getUChar("clipmode", clipMode);
    idleCalm      = p.getUChar("idlecalm", idleCalm);
    spiKHz        = p.getUShort("spikhz",  spiKHz);
    wifiMode      = p.getUChar("wifimode", wifiMode);
    for (uint8_t m = 0; m < CLIP_MODE_COUNT; m++) {
        char key[12];
        snprintf(key, sizeof(key), "mclips%u", m);
        motionClips[m] = p.getString(key, motionClips[m]);
        snprintf(key, sizeof(key), "iframes%u", m);
        idleFrames[m] = p.getString(key, idleFrames[m]);
    }
    wifiSsid      = p.getString("ssid", wifiSsid);
    wifiPass      = p.getString("pass", wifiPass);
    p.end();

    if (universe < 1 || universe > 63999) universe = 1;
    if (startChannel < 1 || startChannel > 512) startChannel = 1;
    if (intensity > INTENSITY_MAX) intensity = INTENSITY_MAX;
    if (volume > 100) volume = 100;
    if (cooldownSec > 3600) cooldownSec = 3600;
    if (clipMode >= CLIP_MODE_COUNT) clipMode = 0;
    if (spiKHz < 100 || spiKHz > 10000) spiKHz = 4000;
    if (wifiMode > 2) wifiMode = 0;
    if (sensorType > 2) sensorType = 0;
    if (triggerCm < 20 || triggerCm > 600) triggerCm = 120;
    if (i2cKHz < 10 || i2cKHz > 400) i2cKHz = 100;
    if (pirHoldMs > 5000) pirHoldMs = 400;
}

/*
  Strip "/halloween/boo.pfc" down to "boo".

  The lists store basenames rather than indexes on purpose: an index would
  silently point at a different clip as soon as one is added or removed.
*/
String Settings::clipKey(const String &path) {
    int slash = path.lastIndexOf('/');
    String k = (slash >= 0) ? path.substring(slash + 1) : path;
    if (k.endsWith(".pfc")) {
        k = k.substring(0, k.length() - 4);
    }
    return k;
}

bool Settings::clipAllowedForMotion(uint8_t mode, const String &path) const {
    if (mode >= CLIP_MODE_COUNT) {
        return true;
    }
    const String &list = motionClips[mode];
    if (list.length() == 0) {
        return true;              // nothing selected means everything
    }
    const String key = clipKey(path);
    int from = 0;
    while (from <= (int)list.length()) {
        int comma = list.indexOf(',', from);
        if (comma < 0) {
            comma = list.length();
        }
        if (list.substring(from, comma) == key) {
            return true;
        }
        from = comma + 1;
    }
    return false;
}

void Settings::save() const {
    Preferences p;
    if (!p.begin(NS, false)) {
        return;
    }
    p.putUShort("universe",  universe);
    p.putUShort("startch",   startChannel);
    p.putUChar("intensity",  intensity);
    p.putUChar("rotated",    rotated);
    p.putUShort("cooldown",  cooldownSec);
    p.putUChar("volume",     volume);
    p.putBool("motion",      motionEnabled);
    p.putUChar("sensor",     sensorType);
    p.putUShort("trigcm",    triggerCm);
    p.putUShort("i2ckhz",    i2cKHz);
    p.putUShort("pirhold",   pirHoldMs);
    p.putBool("phys",        physInputs);
    p.putBool("toginv",      toggleInvert);
    p.putUChar("clipmode",   clipMode);
    p.putUChar("idlecalm",   idleCalm);
    p.putUShort("spikhz",    spiKHz);
    p.putUChar("wifimode",   wifiMode);
    for (uint8_t m = 0; m < CLIP_MODE_COUNT; m++) {
        char key[12];
        snprintf(key, sizeof(key), "mclips%u", m);
        p.putString(key, motionClips[m]);
        snprintf(key, sizeof(key), "iframes%u", m);
        p.putString(key, idleFrames[m]);
    }
    p.putString("ssid",      wifiSsid);
    p.putString("pass",      wifiPass);
    p.end();
}
