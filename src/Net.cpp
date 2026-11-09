#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>

#include "Net.h"
#include "App.h"
#include "E131.h"
#include "Settings.h"
#include "MouthFrames.h"
#include "MatrixChain.h"
#include "WebPage.h"
#include "Audio.h"
#include <Wire.h>

#include "Trigger.h"
#include "Mouth.h"
#include "ClipMode.h"

Net net;

static WebServer   server(80);
static E131        e131;


/*
  Bit-banged I2C address probe. Returns true if the address ACKed.

  Hand-driven on purpose: Wire.endTransmission() with a zero-length write
  reports success on this core without putting anything on the wire, which
  made an earlier bus scan claim all 126 addresses were present. Driving the
  lines directly is both truthful and far quicker -- about 200 us per
  address, so a full sweep costs milliseconds rather than ten seconds.
*/
static bool i2cProbeBitbang(uint8_t addr) {
    auto rel  = [](int pin) { pinMode(pin, INPUT_PULLUP); };
    auto low  = [](int pin) { pinMode(pin, OUTPUT); digitalWrite(pin, LOW); };
    auto tick = []() { delayMicroseconds(6); };

    rel(PIN_I2C_SDA); rel(PIN_I2C_SCL); tick();
    low(PIN_I2C_SDA); tick();                 // START
    low(PIN_I2C_SCL); tick();

    const uint8_t byteOut = (uint8_t)(addr << 1);
    for (int8_t b = 7; b >= 0; b--) {
        if ((byteOut >> b) & 1) rel(PIN_I2C_SDA); else low(PIN_I2C_SDA);
        tick();
        rel(PIN_I2C_SCL); tick();
        low(PIN_I2C_SCL); tick();
    }
    rel(PIN_I2C_SDA); tick();
    rel(PIN_I2C_SCL); tick();
    const bool acked = digitalRead(PIN_I2C_SDA) == LOW;
    low(PIN_I2C_SCL); tick();

    low(PIN_I2C_SDA); tick();                 // STOP
    rel(PIN_I2C_SCL); tick();
    rel(PIN_I2C_SDA); tick();
    return acked;
}

// ---------------------------------------------------------------------------
// Web routes. The page itself is static and lives in WebPage.h; everything
// dynamic is fetched as JSON so nothing large is built in RAM.
// ---------------------------------------------------------------------------

void Net::installRoutes() {
    server.on("/", HTTP_GET, []() {
        server.send_P(200, "text/html", PAGE_HTML);
    });

    // Fetched once: things that cannot change without a reflash.
    server.on("/info", HTTP_GET, []() {
        String j = "{\"mods\":";
        j += MATRIX_COUNT;
        j += ",\"mouth\":";
        j += MOUTH_MODULES;
        j += ",\"ph\":[";
        for (uint8_t i = 0; i < PHONEME_COUNT; i++) {
            if (i) j += ',';
            j += '"';
            j += PHONEME_NAMES[i];
            j += '"';
        }
        j += "],\"modes\":[";
        String names[24];
        for (uint8_t m = 0; m < CLIP_MODE_COUNT; m++) {
            if (m) j += ',';
            j += "{\"name\":\"";
            j += clipModeName(m);
            j += "\",\"clips\":[";
            const uint8_t nc = audio.listClips(clipModeDir(m), names, 24);
            for (uint8_t i = 0; i < nc; i++) {
                if (i) j += ',';
                j += '"';
                j += names[i];
                j += '"';
            }
            j += "],\"iframes\":\"";
            j += settings.idleFrames[m];
            j += "\",\"calm\":";
            j += settings.idleIsCalm(m) ? 1 : 0;
            j += ",\"motion\":\"";
            j += settings.motionClips[m];
            j += "\"}";
        }
        j += "]}";
        server.send(200, "application/json", j);
    });

    // What the panels are actually lit with, as hex. One byte per row,
    // MATRIX_COUNT * 8 rows, post-rotation.
    server.on("/panels", HTTP_GET, []() {
        static const char hexd[] = "0123456789abcdef";
        char buf[MATRIX_COUNT * 8 * 2 + 1];
        uint16_t k = 0;
        for (uint8_t d = 0; d < MATRIX_COUNT; d++) {
            for (uint8_t r = 0; r < 8; r++) {
                const uint8_t v = matrix.shadowRow(d, r);
                buf[k++] = hexd[v >> 4];
                buf[k++] = hexd[v & 0x0F];
            }
        }
        buf[k] = '\0';
        server.send(200, "text/plain", buf);
    });

    server.on("/p", HTTP_GET, []() {
        if (server.hasArg("i")) {
            app::setPhoneme((uint8_t)server.arg("i").toInt(), millis());
        }
        server.send(200, "text/plain", "ok");
    });

    server.on("/m", HTTP_GET, []() {
        const String v = server.arg("v");
        if      (v == "cycle")  { app::setPowered(true); app::setMode(FaceMode::Cycle); }
        else if (v == "chomp")  { app::setPowered(true); app::setMode(FaceMode::Chomp); }
        else if (v == "scream") { app::setPowered(true); app::setMode(FaceMode::Scream); }
        else if (v == "off")    { app::setPowered(false); }
        server.send(200, "text/plain", "ok");
    });

    server.on("/b", HTTP_GET, []() {
        if (server.hasArg("v")) {
            app::setIntensity((uint8_t)server.arg("v").toInt());
        }
        server.send(200, "text/plain", "ok");
    });

    server.on("/save", HTTP_GET, []() {
        bool restartE131 = false;
        if (server.hasArg("uni")) {
            const long u = server.arg("uni").toInt();
            if (u >= 1 && u <= 63999 && (uint16_t)u != settings.universe) {
                settings.universe = (uint16_t)u;
                restartE131 = true;
            }
        }
        if (server.hasArg("ch")) {
            const long c = server.arg("ch").toInt();
            if (c >= 1 && c + PHONEME_CHANNELS - 1 <= 512) {
                settings.startChannel = (uint16_t)c;
            }
        }
        if (server.hasArg("cd")) {
            const long c = server.arg("cd").toInt();
            if (c >= 0 && c <= 3600) {
                settings.cooldownSec = (uint16_t)c;
            }
        }
        if (server.hasArg("sensor")) {
            const long t = server.arg("sensor").toInt();
            if (t >= 0 && t <= 2 && (uint8_t)t != settings.sensorType) {
                settings.sensorType = (uint8_t)t;
                trigger.applySensor();      // reconfigure the pins now
            }
        }
        if (server.hasArg("phys")) {
            settings.physInputs = server.arg("phys").toInt() != 0;
            trigger.applyPhysInputs();
        }
        if (server.hasArg("toginv")) {
            settings.toggleInvert = server.arg("toginv").toInt() != 0;
        }
        if (server.hasArg("pirhold")) {
            const long h = server.arg("pirhold").toInt();
            if (h >= 0 && h <= 5000) {
                settings.pirHoldMs = (uint16_t)h;
            }
        }
        if (server.hasArg("i2ckhz")) {
            const long k = server.arg("i2ckhz").toInt();
            if (k >= 10 && k <= 400 && (uint16_t)k != settings.i2cKHz) {
                settings.i2cKHz = (uint16_t)k;
                trigger.applySensor();     // re-open the bus at the new rate
            }
        }
        if (server.hasArg("trigcm")) {
            const long c = server.arg("trigcm").toInt();
            if (c >= 20 && c <= 600) {
                settings.triggerCm = (uint16_t)c;
            }
        }
        if (server.hasArg("mot")) {
            settings.motionEnabled = server.arg("mot").toInt() != 0;
        }
        if (server.hasArg("wmode")) {
            const long w = server.arg("wmode").toInt();
            if (w >= 0 && w <= 2) {
                settings.wifiMode = (uint8_t)w;   // applies on next boot
            }
        }
        if (server.hasArg("spi")) {
            const long k = server.arg("spi").toInt();
            if (k >= 100 && k <= 10000 && (uint16_t)k != settings.spiKHz) {
                settings.spiKHz = (uint16_t)k;
                matrix.applyClock();
                matrix.resync();   // prove the new rate immediately
            }
        }
        if (server.hasArg("rot")) {
            const uint8_t rot = (uint8_t)server.arg("rot").toInt();
            if (rot != settings.rotated) {
                settings.rotated = rot;
                // The logical picture has not changed, so setRow() would drop
                // every write. Push the rows out again under the new mapping.
                for (uint8_t d = 0; d < MATRIX_COUNT; d++) {
                    matrix.repaint(d);
                }
            }
        }
        settings.save();
        if (restartE131) {
            e131.begin(settings.universe);
        }
        server.send(200, "text/plain", "ok");
    });

    server.on("/play", HTTP_GET, []() {
        const String c = server.arg("c");
        const bool ok = app::playClip(c.length() ? c.c_str() : nullptr);
        server.send(200, "text/plain", ok ? "ok" : "no clip");
    });

    server.on("/stop", HTTP_GET, []() {
        app::stopClip();
        server.send(200, "text/plain", "ok");
    });

    server.on("/vol", HTTP_GET, []() {
        if (server.hasArg("v")) {
            long v = server.arg("v").toInt();
            settings.volume = (uint8_t)constrain(v, 0L, 100L);
            settings.save();
        }
        server.send(200, "text/plain", "ok");
    });

    /*
      Set which clips motion may use, for one mode.
      An empty list means every clip in that mode is eligible.
    */
    /*
      Every mouth frame as hex, so the browser can draw them.
      MOUTH_FRAME_COUNT frames, 8 rows each, MOUTH_MODULES bytes per row.
      At two modules that is 560 bytes -> 1120 hex characters, which is small
      enough to fetch once and cache.
    */
    server.on("/frames", HTTP_GET, []() {
        static const char hexd[] = "0123456789abcdef";
        String j = "{\"count\":";
        j += MOUTH_FRAME_COUNT;
        j += ",\"mods\":";
        j += MOUTH_MODULES;
        j += ",\"data\":\"";
        for (uint8_t f = 0; f < MOUTH_FRAME_COUNT; f++) {
            for (uint8_t r = 0; r < 8; r++) {
                for (uint8_t m = 0; m < MOUTH_MODULES; m++) {
                    const uint8_t v = mouthFrameRow(f, r, m);
                    j += hexd[v >> 4];
                    j += hexd[v & 0x0F];
                }
            }
        }
        j += "\",\"anims\":[";
        for (uint8_t a = 0; a < MOUTH_ANIM_COUNT; a++) {
            const MouthAnim &an = MOUTH_ANIMS[a];
            if (a) j += ',';
            j += "{\"id\":";     j += an.id;
            j += ",\"name\":\""; j += an.name;
            j += "\",\"ms\":";   j += an.ms;
            j += ",\"frames\":[";
            for (uint8_t i = 0; i < an.count; i++) {
                if (i) j += ',';
                j += an.frames[i];
            }
            j += "]}";
        }
        j += "]}";
        server.send(200, "application/json", j);
    });

    // Show one frame on the real mouth, so a choice can be judged on the
    // prop rather than in a browser. The idle cycle overwrites it in time.
    server.on("/frame", HTTP_GET, []() {
        if (app::speaking()) {
            server.send(409, "text/plain", "busy speaking");
            return;
        }
        const long i = server.arg("i").toInt();
        if (i == ANIM_CHOMP) {
            mouth.setMode(FaceMode::Chomp);
            server.send(200, "text/plain", "ok");
            return;
        }
        if (i == ANIM_SCREAM) {
            mouth.setMode(FaceMode::Scream);
            server.send(200, "text/plain", "ok");
            return;
        }
        if (i == ANIM_WAVE) {
            mouth.setMode(FaceMode::Wave);
            server.send(200, "text/plain", "ok");
            return;
        }
        if (i < 0 || i >= MOUTH_FRAME_COUNT) {
            server.send(400, "text/plain", "bad frame");
            return;
        }
        mouth.draw((uint8_t)i);
        server.send(200, "text/plain", "ok");
    });

    // Which frames the idle cycle walks, per mode. Empty restores the default.
    server.on("/idleframes", HTTP_GET, []() {
        if (!server.hasArg("mode")) {
            server.send(400, "text/plain", "mode required");
            return;
        }
        const long m = server.arg("mode").toInt();
        if (m < 0 || m >= CLIP_MODE_COUNT) {
            server.send(400, "text/plain", "bad mode");
            return;
        }
        settings.idleFrames[m] = server.arg("list");
        settings.save();
        if ((uint8_t)m == settings.clipMode) {
            mouth.setMode(FaceMode::Cycle);   // show it straight away
        }
        server.send(200, "text/plain", "ok");
    });

    // Per-mode idle style: calm (human expressions only) or full.
    server.on("/idle", HTTP_GET, []() {
        if (!server.hasArg("mode")) {
            server.send(400, "text/plain", "mode required");
            return;
        }
        const long m = server.arg("mode").toInt();
        if (m < 0 || m >= CLIP_MODE_COUNT) {
            server.send(400, "text/plain", "bad mode");
            return;
        }
        const bool calm = server.arg("calm").toInt() != 0;
        if (calm) settings.idleCalm |= (uint8_t)(1u << m);
        else      settings.idleCalm &= (uint8_t)~(1u << m);
        settings.save();
        // Restart the idle cycle so the change is visible immediately.
        if ((uint8_t)m == settings.clipMode) {
            mouth.setMode(FaceMode::Cycle);
        }
        server.send(200, "text/plain", "ok");
    });

    server.on("/mclips", HTTP_GET, []() {
        if (!server.hasArg("mode")) {
            server.send(400, "text/plain", "mode required");
            return;
        }
        const long m = server.arg("mode").toInt();
        if (m < 0 || m >= CLIP_MODE_COUNT) {
            server.send(400, "text/plain", "bad mode");
            return;
        }
        settings.motionClips[m] = server.arg("list");
        settings.save();
        server.send(200, "text/plain", "ok");
    });

    server.on("/mode", HTTP_GET, []() {
        if (server.hasArg("v")) {
            const long v = server.arg("v").toInt();
            if (v >= 0 && v < CLIP_MODE_COUNT) {
                // Same path as the button hold, so both announce.
                trigger.setMode((uint8_t)v);
            }
        }
        server.send(200, "text/plain", "ok");
    });

    server.on("/scan", HTTP_GET, []() {
        const int n = WiFi.scanNetworks();
        String j = "[";
        for (int i = 0; i < n && i < 20; i++) {
            if (i) j += ',';
            j += "{\"s\":\"";
            j += WiFi.SSID(i);
            j += "\",\"r\":";
            j += WiFi.RSSI(i);
            j += ",\"e\":";
            j += (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? 0 : 1;
            j += '}';
        }
        j += ']';
        WiFi.scanDelete();
        server.send(200, "application/json", j);
    });

    server.on("/wifi", HTTP_GET, []() {
        const String ssid = server.arg("ssid");
        if (ssid.length() == 0) {
            server.send(400, "text/plain", "ssid required");
            return;
        }
        server.send(200, "text/plain", "saved; restarting");
        net.saveCredentials(ssid, server.arg("pass"));
    });

    /*
      Probe the I2C bus. Worth having as its own answer: a distance readout
      of "n/a" cannot distinguish "sensor absent" from "sensor present but
      not answering", and this can.
    */
    server.on("/i2c", HTTP_GET, []() {
        if (app::speaking()) {
            server.send(409, "text/plain", "busy playing audio; try again");
            return;
        }
        const bool full = server.hasArg("full");
        Wire.end();

        String j = "{\"found\":[";
        bool first = true;
        uint8_t count = 0;
        const uint8_t lo = full ? 1 : MB1242_ADDR;
        const uint8_t hi = full ? 126 : MB1242_ADDR;
        for (uint8_t a = lo; a <= hi; a++) {
            if (!i2cProbeBitbang(a)) {
                continue;
            }
            count++;
            if (!first) j += ',';
            first = false;
            j += "\"0x";
            if (a < 16) j += '0';
            j += String(a, HEX);
            j += '"';
        }
        Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, (uint32_t)settings.i2cKHz * 1000UL);

        j += "],\"count\":";
        j += count;
        j += ",\"full\":";
        j += full ? 1 : 0;
        j += ",\"sda\":";
        j += PIN_I2C_SDA;
        j += ",\"scl\":";
        j += PIN_I2C_SCL;
        j += ",\"expect\":\"0x70\"}";
        server.send(200, "application/json", j);
    });

    /*
      Electrical check of the I2C lines.

      An idle I2C bus must sit HIGH, held there by pull-up resistors. This
      enables the chip's internal pull-down (~45k) and reads the pin: an
      external 4.7k pull-up easily wins that fight and reads HIGH, while a
      line with no pull-up reads LOW. So it distinguishes "no pull-ups or no
      power" from "bus fine, device not answering" -- which a failed address
      probe cannot.
    */
    server.on("/i2cdiag", HTTP_GET, []() {
        Wire.end();

        pinMode(PIN_I2C_SDA, INPUT_PULLDOWN);
        pinMode(PIN_I2C_SCL, INPUT_PULLDOWN);
        delay(5);
        const bool sdaHigh = digitalRead(PIN_I2C_SDA) == HIGH;
        const bool sclHigh = digitalRead(PIN_I2C_SCL) == HIGH;

        // Sanity-check the pins themselves against the internal pull-up.
        pinMode(PIN_I2C_SDA, INPUT_PULLUP);
        pinMode(PIN_I2C_SCL, INPUT_PULLUP);
        delay(5);
        const bool sdaPin = digitalRead(PIN_I2C_SDA) == HIGH;
        const bool sclPin = digitalRead(PIN_I2C_SCL) == HIGH;

        /*
          Are the two lines shorted together?

          Drive one and read the other. A short makes every I2C address
          appear to ACK, because the ACK slot samples SDA while SCL is low --
          so SDA reads low for every address and the bus looks crowded with
          devices that are not there.
        */
        pinMode(PIN_I2C_SCL, INPUT_PULLUP);
        pinMode(PIN_I2C_SDA, OUTPUT);
        digitalWrite(PIN_I2C_SDA, LOW);
        delay(5);
        const bool sclFollowedLow = digitalRead(PIN_I2C_SCL) == LOW;
        digitalWrite(PIN_I2C_SDA, HIGH);
        delay(5);

        pinMode(PIN_I2C_SDA, INPUT_PULLUP);
        pinMode(PIN_I2C_SCL, OUTPUT);
        digitalWrite(PIN_I2C_SCL, LOW);
        delay(5);
        const bool sdaFollowedLow = digitalRead(PIN_I2C_SDA) == LOW;
        digitalWrite(PIN_I2C_SCL, HIGH);
        delay(5);

        // Also time how long SDA takes to recover once released: a capacitor
        // on a signal line shows up as a slow edge here.
        pinMode(PIN_I2C_SDA, OUTPUT);
        digitalWrite(PIN_I2C_SDA, LOW);
        delayMicroseconds(50);
        pinMode(PIN_I2C_SDA, INPUT);
        uint32_t riseUs = 0;
        const uint32_t t0 = micros();
        while (digitalRead(PIN_I2C_SDA) == LOW && (micros() - t0) < 20000) {
        }
        riseUs = micros() - t0;

        pinMode(PIN_I2C_SDA, INPUT);
        pinMode(PIN_I2C_SCL, INPUT);
        Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, (uint32_t)settings.i2cKHz * 1000UL);

        String j = "{";
        j += "\"shorted\":"; j += (sclFollowedLow && sdaFollowedLow) ? 1 : 0;
        j += ",\"sda_rise_us\":"; j += riseUs;
        j += ",\"sda_pulled_up\":"; j += sdaHigh ? 1 : 0;
        j += ",\"scl_pulled_up\":"; j += sclHigh ? 1 : 0;
        j += ",\"sda_pin_ok\":";   j += sdaPin ? 1 : 0;
        j += ",\"scl_pin_ok\":";   j += sclPin ? 1 : 0;
        j += ",\"sda\":"; j += PIN_I2C_SDA;
        j += ",\"scl\":"; j += PIN_I2C_SCL;
        j += "}";
        server.send(200, "application/json", j);
    });

    // One blocking reading with the raw bytes exposed, so an unexpected
    // value can be told apart from a failed transaction.
    server.on("/sonar", HTTP_GET, []() {
        if (app::speaking()) {
            server.send(409, "text/plain", "busy playing audio; try again");
            return;
        }
        uint8_t hi = 0, lo = 0;
        const int16_t raw = trigger.probeSonar(&hi, &lo);
        String j = "{\"raw\":";
        j += raw;
        j += ",\"hi\":";
        j += hi;
        j += ",\"lo\":";
        j += lo;
        j += ",\"min\":";
        j += SONAR_MIN_CM;
        j += ",\"max\":";
        j += SONAR_MAX_CM;
        j += "}";
        server.send(200, "application/json", j);
    });

    /*
      Bit-banged address probe.

      The ESP-IDF I2C driver has been giving answers that contradict the
      electrical measurements, so this drives the lines by hand and reports
      the ACK bit it actually observes. ?addr= picks the address; probing one
      address that should exist and one that cannot is the useful comparison,
      because a line stuck low ACKs both.
    */
    server.on("/i2cbb", HTTP_GET, []() {
        const uint8_t addr = server.hasArg("addr")
            ? (uint8_t)strtol(server.arg("addr").c_str(), nullptr, 0) : 0x70;
        Wire.end();

        auto rel  = [](int pin) { pinMode(pin, INPUT_PULLUP); };
        auto low  = [](int pin) { pinMode(pin, OUTPUT); digitalWrite(pin, LOW); };
        auto tick = []() { delayMicroseconds(6); };

        rel(PIN_I2C_SDA); rel(PIN_I2C_SCL); tick();
        const bool idleSda = digitalRead(PIN_I2C_SDA);
        const bool idleScl = digitalRead(PIN_I2C_SCL);

        // START: SDA falls while SCL is high.
        low(PIN_I2C_SDA); tick();
        low(PIN_I2C_SCL); tick();

        const uint8_t byteOut = (uint8_t)(addr << 1);   // write bit = 0
        for (int8_t b = 7; b >= 0; b--) {
            if ((byteOut >> b) & 1) rel(PIN_I2C_SDA); else low(PIN_I2C_SDA);
            tick();
            rel(PIN_I2C_SCL); tick();
            low(PIN_I2C_SCL); tick();
        }

        // ACK slot: release SDA, clock high, sample.
        rel(PIN_I2C_SDA); tick();
        rel(PIN_I2C_SCL); tick();
        const bool ackBit = digitalRead(PIN_I2C_SDA);   // LOW means ACK
        low(PIN_I2C_SCL); tick();

        // STOP
        low(PIN_I2C_SDA); tick();
        rel(PIN_I2C_SCL); tick();
        rel(PIN_I2C_SDA); tick();
        const bool restSda = digitalRead(PIN_I2C_SDA);

        Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, (uint32_t)settings.i2cKHz * 1000UL);

        String j = "{\"addr\":";
        j += addr;
        j += ",\"idle_sda\":";  j += idleSda ? 1 : 0;
        j += ",\"idle_scl\":";  j += idleScl ? 1 : 0;
        j += ",\"ack\":";       j += ackBit ? 0 : 1;   // report 1 = ACKed
        j += ",\"sda_after\":"; j += restSda ? 1 : 0;
        j += "}";
        server.send(200, "application/json", j);
    });

    server.on("/resync", HTTP_GET, []() {
        matrix.resync();
        server.send(200, "text/plain", "ok");
    });

    server.on("/status", HTTP_GET, []() {
        String j = "{";
        j += "\"src\":\"";
        j += app::powered() ? (app::externalMouth() ? "external (show)" : "idle") : "off";
        j += "\",\"pn\":\"";
        j += PHONEME_NAMES[app::phoneme() < PHONEME_COUNT ? app::phoneme() : 0];
        j += "\",\"uni\":"; j += settings.universe;
        j += ",\"ch0\":";    j += settings.startChannel;
        j += ",\"ch1\":";    j += (settings.startChannel + PHONEME_CHANNELS - 1);
        j += ",\"br\":";     j += app::intensity();
        j += ",\"rot\":";    j += settings.rotated;
        j += ",\"pk\":";     j += e131.packets();
        j += ",\"dc\":";     j += e131.discarded();
        j += ",\"up\":";     j += (millis() / 1000);
        j += ",\"hp\":";     j += ESP.getFreeHeap();
        j += ",\"spk\":";    j += app::speaking() ? 1 : 0;
        j += ",\"mot\":";    j += trigger.motion() ? 1 : 0;
        j += ",\"sens\":";   j += settings.sensorType;
        j += ",\"sensnm\":\""; j += trigger.sensorName(); j += '"';
        j += ",\"dist\":";   j += trigger.distanceCm();
        j += ",\"sok\":";    j += trigger.sensorResponding() ? 1 : 0;
        j += ",\"trigcm\":"; j += settings.triggerCm;
        j += ",\"i2ckhz\":"; j += settings.i2cKHz;
        j += ",\"pirhold\":"; j += settings.pirHoldMs;
        j += ",\"warm\":";   j += trigger.warmingUp(millis()) ? 1 : 0;
        j += ",\"settle\":"; j += trigger.settling(millis()) ? 1 : 0;
        j += ",\"phys\":";   j += settings.physInputs ? 1 : 0;
        j += ",\"toginv\":"; j += settings.toggleInvert ? 1 : 0;
        j += ",\"tog\":";    j += trigger.toggleOn() ? 1 : 0;
        j += ",\"btn\":";    j += trigger.buttonDown() ? 1 : 0;
        j += ",\"armed\":";  j += trigger.motionArmed() ? 1 : 0;
        j += ",\"men\":";    j += settings.motionEnabled ? 1 : 0;
        j += ",\"cd\":";     j += settings.cooldownSec;
        j += ",\"cdl\":";    j += (trigger.cooldownLeftMs(millis()) / 1000);
        j += ",\"vol\":";    j += settings.volume;
        j += ",\"fire\":";   j += trigger.fireCount();
        j += ",\"clip\":\"";  j += Settings::clipKey(String(trigger.lastClip())); j += '"';
        j += ",\"rsy\":";    j += matrix.resyncCount();
        j += ",\"spi\":";    j += settings.spiKHz;
        j += ",\"wmode\":"; j += settings.wifiMode;
        j += ",\"net\":\"";  j += net.stateName(); j += '"';
        j += ",\"mode\":";   j += settings.clipMode;
        j += ",\"modenm\":\""; j += clipModeName(settings.clipMode); j += '"';
        j += ",\"ip\":\"";  j += net.address();
        j += "\"}";
        server.send(200, "application/json", j);
    });

    server.onNotFound([]() { server.send(404, "text/plain", "not found"); });
}

// ---------------------------------------------------------------------------

/*
  Bring the radio up without blocking.

  The old code called WiFiManager's autoConnect(), which blocks for its
  connect timeout and froze the face for several seconds on any boot where
  the network was absent. Here the join is started and then polled from
  update(), so the face animates throughout.
*/
void Net::begin() {
    installRoutes();

    switch (settings.wifiMode) {
        case 2:
            WiFi.mode(WIFI_OFF);
            state_ = NetState::Off;
            Serial.println(F("wifi off by setting"));
            return;
        case 1:
            startAccessPoint();
            return;
        default:
            startStation();
            return;
    }
}

/*
  Join a saved network, without blocking.

  Credentials live in our own NVS namespace rather than the IDF's wifi
  config. That was a deliberate move away from WiFiManager after two
  failures: reading its stored SSID before the driver was up returned
  uninitialised memory, and a no-argument WiFi.begin() silently overwrote the
  saved network because persistence defaults to on.

  Owning the strings means the connect path is three lines and the failure
  mode is obvious.
*/
void Net::startStation() {
    if (settings.wifiSsid.length() == 0) {
        Serial.println(F("no network configured; hosting our own"));
        startAccessPoint();
        return;
    }
    WiFi.mode(WIFI_STA);
    WiFi.setHostname("robo-face");
    WiFi.begin(settings.wifiSsid.c_str(), settings.wifiPass.c_str());
    state_ = NetState::Connecting;
    connectStart_ = millis();
    Serial.print(F("joining "));
    Serial.println(settings.wifiSsid);
}

void Net::saveCredentials(const String &ssid, const String &pass) {
    settings.wifiSsid = ssid;
    settings.wifiPass = pass;
    settings.wifiMode = 0;          // auto, so the next boot tries to join
    settings.save();
    rebootAt_ = millis() + 1200;    // let the HTTP response flush first
    Serial.print(F("saved network "));
    Serial.println(ssid);
}

/*
  Host a network of our own.

  This is what makes the prop usable standalone: no infrastructure needed,
  connect a phone straight to it. The control page is served here exactly as
  it is over a real network -- unlike WiFiManager's portal, which can only
  configure WiFi.
*/
void Net::startAccessPoint() {
    stopServices();
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID);
    state_ = NetState::AccessPoint;
    Serial.print(F("hosting AP \""));
    Serial.print(AP_SSID);
    Serial.print(F("\" at http://"));
    Serial.println(WiFi.softAPIP());
    startServices();
}


void Net::startServices() {
    if (servicesUp_) {
        return;
    }
    server.begin();
    if (MDNS.begin("robo-face")) {
        MDNS.addService("http", "tcp", 80);
    }
    if (e131.begin(settings.universe)) {
        Serial.printf("E1.31 listening: universe %u, channels %u-%u\n",
                      settings.universe, settings.startChannel,
                      settings.startChannel + PHONEME_CHANNELS - 1);
    }
    servicesUp_ = true;
}

void Net::stopServices() {
    if (!servicesUp_) {
        return;
    }
    e131.end();
    server.stop();
    MDNS.end();
    servicesUp_ = false;
}

bool Net::wifiConnected() const {
    return WiFi.status() == WL_CONNECTED;
}

const char *Net::stateName() const {
    switch (state_) {
        case NetState::Off:         return "off";
        case NetState::Connecting:  return "connecting";
        case NetState::Station:     return "station";
        case NetState::AccessPoint: return "access point";
    }
    return "?";
}

String Net::address() const {
    if (state_ == NetState::AccessPoint) {
        return WiFi.softAPIP().toString();
    }
    if (state_ == NetState::Station) {
        return WiFi.localIP().toString();
    }
    return String("-");
}

/*
  Translate a DMX frame into a phoneme.

  The highest channel above the threshold wins, which is what xLights
  produces -- it drives exactly one phoneme to full at a time.

  An all-zero frame is deliberately NOT treated as activity. FPP may keep
  streaming zeros between sequences, and if that counted the mouth would sit
  on one shape forever instead of going back to idling.
*/
void Net::applyDmx(uint32_t now) {
    uint8_t best = 0;
    int     bestIndex = -1;
    for (uint8_t i = 0; i < PHONEME_CHANNELS && i < PHONEME_COUNT; i++) {
        const uint8_t v = e131.channel(settings.startChannel + i);
        if (v > best) {
            best = v;
            bestIndex = i;
        }
    }
    if (bestIndex < 0 || best < PHONEME_THRESHOLD) {
        return;
    }
    app::setPhoneme((uint8_t)bestIndex, now);
    lastPhonemeMs_ = now;
}

void Net::update(uint32_t now) {
    if (rebootAt_ && (int32_t)(now - rebootAt_) >= 0) {
        Serial.println(F("restarting to apply network settings"));
        delay(50);
        ESP.restart();
    }

    switch (state_) {
        case NetState::Off:
            return;

        case NetState::Connecting:
            if (wifiConnected()) {
                state_ = NetState::Station;
                Serial.print(F("wifi connected, ip "));
                Serial.println(WiFi.localIP());
                startServices();
            } else if ((uint32_t)(now - connectStart_) > STA_CONNECT_TIMEOUT_MS) {
                Serial.println(F("no network in range; hosting our own"));
                startAccessPoint();
            }
            return;

        case NetState::Station:
            if (!wifiConnected()) {
                stopServices();
                state_ = NetState::Connecting;
                connectStart_ = now;
                WiFi.reconnect();
                return;
            }
            break;

        case NetState::AccessPoint:
            break;
    }

    if (!servicesUp_) {
        return;
    }
    server.handleClient();
    if (e131.poll()) {
        applyDmx(now);
    }
}
