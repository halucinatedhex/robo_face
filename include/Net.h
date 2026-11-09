#pragma once
#include <Arduino.h>

/*
  WiFi, the E1.31 receiver and the web control page.

  All of it is non-blocking: the captive portal runs in the background so the
  face keeps animating while someone is picking an SSID on their phone.
*/
/*
  How the radio is behaving right now.

  Station needs a network to exist. AccessPoint does not -- the prop hosts
  its own, which is how the control page stays reachable when the thing is
  sitting on a desk with no infrastructure around it.
*/
enum class NetState : uint8_t {
    Off,          // radio disabled
    Connecting,   // trying a saved network
    Station,      // joined a network
    AccessPoint,  // hosting our own network, serving the control page
};

class Net {
public:
    void begin();
    void update(uint32_t now);

    bool wifiConnected() const;
    NetState state() const { return state_; }
    const char *stateName() const;
    String address() const;

    // Save credentials and restart into station mode shortly afterwards,
    // so the HTTP response gets out first.
    void saveCredentials(const String &ssid, const String &pass);

private:
    void startStation();
    void startAccessPoint();
    void startServices();
    void stopServices();
    void applyDmx(uint32_t now);
    void installRoutes();

    NetState state_ = NetState::Off;
    bool     servicesUp_ = false;
    uint32_t connectStart_ = 0;
    uint32_t lastPhonemeMs_ = 0;
    uint32_t rebootAt_ = 0;
};

// How long to wait for a saved network before hosting our own.
static constexpr uint32_t STA_CONNECT_TIMEOUT_MS = 12000;

// SSID the prop hosts when there is no network to join. Open, so there is
// nothing to forget while standing in front of it.
static constexpr const char *AP_SSID = "robo_face";

extern Net net;

// Number of consecutive phoneme channels claimed, starting at
// Settings::startChannel. Order matches PHONEME_NAMES.
static constexpr uint8_t PHONEME_CHANNELS = 10;
// A channel must exceed this to count as the active phoneme.
static constexpr uint8_t PHONEME_THRESHOLD = 50;
