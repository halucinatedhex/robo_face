#!/usr/bin/env python3
"""
Send E1.31 (sACN) frames at the face, to check the channel mapping without
standing up a full xLights sequence.

    python3 tools/sacn_test.py                 # walk every phoneme
    python3 tools/sacn_test.py --hold AI       # hold one shape
    python3 tools/sacn_test.py --host 10.0.0.5 # unicast instead of multicast

The packet layout is ANSI E1.31-2018; see src/E131.cpp for the receiving end.
"""
import argparse
import socket
import struct
import time
import uuid

PHONEMES = ["AI", "E", "etc", "FV", "L", "MBP", "O", "rest", "U", "WQ"]
PORT = 5568


def packet(universe, seq, data, cid, source="robo_face test"):
    n = len(data)
    p = bytearray()
    # --- root layer ---
    p += struct.pack("!HH", 0x0010, 0x0000)          # preamble / postamble
    p += b"ASC-E1.17\x00\x00\x00"                    # ACN packet identifier
    p += struct.pack("!H", 0x7000 | (109 + n))       # flags + length
    p += struct.pack("!I", 0x00000004)               # VECTOR_ROOT_E131_DATA
    p += cid
    # --- framing layer ---
    p += struct.pack("!H", 0x7000 | (87 + n))
    p += struct.pack("!I", 0x00000002)               # VECTOR_E131_DATA_PACKET
    p += source.encode()[:63].ljust(64, b"\x00")
    p += bytes([100])                                # priority
    p += struct.pack("!H", 0)                        # sync address
    p += bytes([seq & 0xFF, 0])                      # sequence, options
    p += struct.pack("!H", universe)
    # --- DMP layer ---
    p += struct.pack("!H", 0x7000 | (10 + n))
    p += bytes([0x02, 0xA1])                         # vector, address+data type
    p += struct.pack("!HH", 0x0000, 0x0001)          # first address, increment
    p += struct.pack("!H", n + 1)                    # property count (incl. start code)
    p += bytes([0x00])                               # DMX start code
    p += bytes(data)
    return bytes(p)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--universe", type=int, default=1)
    ap.add_argument("--start", type=int, default=1, help="first phoneme channel")
    ap.add_argument("--host", help="unicast target; default is multicast")
    ap.add_argument("--iface", help="local IP to send multicast from; needed on a machine with more than one network interface")
    ap.add_argument("--hold", help="hold one phoneme by name")
    ap.add_argument("--fps", type=float, default=10.0)
    ap.add_argument("--seconds", type=float, default=12.0)
    a = ap.parse_args()

    dest = a.host or f"239.255.{(a.universe >> 8) & 0xFF}.{a.universe & 0xFF}"
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 2)
    if a.iface and not a.host:
        # Without this a multi-homed host picks an arbitrary interface and
        # the send fails with "No route to host".
        sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_IF,
                        socket.inet_aton(a.iface))
    cid = uuid.uuid4().bytes

    print(f"-> {dest}:{PORT}  universe {a.universe}  "
          f"channels {a.start}-{a.start + len(PHONEMES) - 1}")

    hold = PHONEMES.index(a.hold) if a.hold else None
    seq, t0, frame = 0, time.time(), 0
    while time.time() - t0 < a.seconds:
        idx = hold if hold is not None else (frame // max(1, int(a.fps / 2))) % len(PHONEMES)
        data = [0] * (a.start + len(PHONEMES))
        data[a.start - 1 + idx] = 255
        seq = (seq + 1) & 0xFF
        sock.sendto(packet(a.universe, seq, data, cid), (dest, PORT))
        print(f"\r  {PHONEMES[idx]:<5}", end="", flush=True)
        frame += 1
        time.sleep(1.0 / a.fps)
    print("\ndone -- the face should fall back to idle after ~2 s")


if __name__ == "__main__":
    main()
