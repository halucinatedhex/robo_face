#pragma once
#include <Arduino.h>
#include <WiFiUdp.h>

/*
  Minimal E1.31 (sACN) receiver.

  Written out rather than pulled in as a library: the packet is a fixed
  layout, the parsing is a few dozen lines, and the validation below is the
  part worth being able to read when a show misbehaves.

  Listens on the multicast group for one universe and also accepts unicast
  to the same port, which is how FPP is often configured.
*/
class E131 {
public:
    static constexpr uint16_t PORT = 5568;
    static constexpr uint16_t MAX_SLOTS = 512;

    bool begin(uint16_t universe);
    void end();

    // Reads waiting packets. True if a valid frame for our universe landed.
    bool poll();

    // DMX slots, 1-based: channel() returns channel n as the show numbers it.
    uint8_t channel(uint16_t n) const {
        return (n >= 1 && n <= slots_) ? dmx_[n] : 0;
    }
    uint16_t slots() const { return slots_; }
    uint16_t universe() const { return universe_; }
    uint32_t packets() const { return packets_; }
    uint32_t discarded() const { return discarded_; }
    bool running() const { return started_; }

private:
    WiFiUDP  udp_;
    uint16_t universe_  = 0;
    uint8_t  dmx_[MAX_SLOTS + 1] = {};
    uint16_t slots_     = 0;
    uint32_t packets_   = 0;
    uint32_t discarded_ = 0;
    uint8_t  lastSeq_   = 0;
    bool     haveSeq_   = false;
    bool     started_   = false;
};
