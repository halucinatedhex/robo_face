#include "E131.h"

/*
  Offsets into an E1.31 data packet (ANSI E1.31-2018).

    0    root layer: preamble, ACN packet identifier, root vector, CID
    38   framing layer: source name, priority, sequence, universe
    115  DMP layer: property count, DMX start code, then the slots
*/
static constexpr uint16_t OFF_ACN_ID      = 4;    // 12 bytes
static constexpr uint16_t OFF_ROOT_VECTOR = 18;   // 4 bytes
static constexpr uint16_t OFF_FRAME_VECTOR= 40;   // 4 bytes
static constexpr uint16_t OFF_SEQUENCE    = 111;
static constexpr uint16_t OFF_UNIVERSE    = 113;  // 2 bytes, big endian
static constexpr uint16_t OFF_DMP_VECTOR  = 117;
static constexpr uint16_t OFF_PROP_COUNT  = 123;  // 2 bytes, big endian
static constexpr uint16_t OFF_START_CODE  = 125;
static constexpr uint16_t OFF_DMX         = 126;
static constexpr uint16_t MIN_PACKET      = OFF_DMX;

static const uint8_t ACN_ID[12] = {
    'A', 'S', 'C', '-', 'E', '1', '.', '1', '7', 0x00, 0x00, 0x00
};
static constexpr uint32_t VECTOR_ROOT_DATA  = 0x00000004;
static constexpr uint32_t VECTOR_FRAME_DATA = 0x00000002;
static constexpr uint8_t  VECTOR_DMP_SET    = 0x02;

static inline uint16_t be16(const uint8_t *p) {
    return (uint16_t)((p[0] << 8) | p[1]);
}
static inline uint32_t be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

bool E131::begin(uint16_t universe) {
    end();
    universe_ = universe;

    // Multicast group for a universe is 239.255.<high byte>.<low byte>.
    const IPAddress group(239, 255, (universe >> 8) & 0xFF, universe & 0xFF);
    started_ = udp_.beginMulticast(group, PORT);
    if (!started_) {
        // Fall back to plain unicast on the same port.
        started_ = udp_.begin(PORT);
    }
    return started_;
}

void E131::end() {
    if (started_) {
        udp_.stop();
        started_ = false;
    }
    haveSeq_ = false;
    slots_   = 0;
}

bool E131::poll() {
    if (!started_) {
        return false;
    }
    bool got = false;

    // Drain the queue: during a show several frames can be waiting, and only
    // the newest is worth drawing.
    for (int guard = 0; guard < 8; guard++) {
        const int size = udp_.parsePacket();
        if (size <= 0) {
            break;
        }
        if (size < (int)MIN_PACKET) {
            udp_.clear();
            discarded_++;
            continue;
        }

        static uint8_t buf[OFF_DMX + MAX_SLOTS + 1];
        const int n = udp_.read(buf, sizeof(buf));
        if (n < (int)MIN_PACKET) {
            discarded_++;
            continue;
        }

        if (memcmp(buf + OFF_ACN_ID, ACN_ID, sizeof(ACN_ID)) != 0 ||
            be32(buf + OFF_ROOT_VECTOR)  != VECTOR_ROOT_DATA ||
            be32(buf + OFF_FRAME_VECTOR) != VECTOR_FRAME_DATA ||
            buf[OFF_DMP_VECTOR]          != VECTOR_DMP_SET ||
            buf[OFF_START_CODE]          != 0x00) {
            discarded_++;
            continue;
        }
        if (be16(buf + OFF_UNIVERSE) != universe_) {
            continue;  // another prop's universe, not an error
        }

        // Property count includes the start code byte.
        uint16_t count = be16(buf + OFF_PROP_COUNT);
        if (count == 0) {
            discarded_++;
            continue;
        }
        count -= 1;
        if (count > MAX_SLOTS) {
            count = MAX_SLOTS;
        }
        if ((int)(OFF_DMX + count) > n) {
            count = (uint16_t)(n - OFF_DMX);
        }

        // Sequence numbers wrap; a packet that is behind the last one is a
        // straggler and gets dropped. 0 opts out of sequencing.
        const uint8_t seq = buf[OFF_SEQUENCE];
        if (haveSeq_ && seq != 0) {
            const int8_t delta = (int8_t)(seq - lastSeq_);
            if (delta <= 0 && delta > -20) {
                discarded_++;
                continue;
            }
        }
        lastSeq_ = seq;
        haveSeq_ = true;

        memcpy(dmx_ + 1, buf + OFF_DMX, count);
        slots_ = count;
        packets_++;
        got = true;
    }
    return got;
}
