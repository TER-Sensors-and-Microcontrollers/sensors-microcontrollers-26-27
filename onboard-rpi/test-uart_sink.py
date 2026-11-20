#!/usr/bin/env python3
"""
Test receiver for can_to_uart.py packets.

Reads the serial/pty device, resyncs on MAGIC, checks CRC and layout version,
and reports packet stats. Because can_feeder.py puts a counter in the first
4 bytes of every payload, it also verifies the dirty bits:
    dirty bit set   -> that field's counter must have changed since last packet
    dirty bit clear -> that field's counter must be unchanged
(only checked when no packets were lost in between)

Usage:  python3 uart_sink.py [/tmp/ttyB]
"""
import sys
import time

import serial
import struct
import binascii

from can_to_uart import BAUD
from frame import BITMAP_LEN, FRAME_LEN, LAYOUT, MAGIC, LAYOUT_VER

PKT_LEN = len(MAGIC) + 6 + BITMAP_LEN + FRAME_LEN + 2   # magic, ver, seq, timestamp, bitmap, snapshot, crc
CHECKABLE = {key for key, (_, _, ln) in LAYOUT.items() if ln >= 4}

PRINT_INTERVAL_S = 1.0

def decode_packet(pkt):
    """Receiver-side reference decoder. Returns (seq, timestamp, {key: bytes}, {key: dirty})."""
    body, (crc,) = pkt[:-2], struct.unpack("<H", pkt[-2:])
    if pkt[:2] != MAGIC or binascii.crc_hqx(body, 0xFFFF) != crc:
        raise ValueError("bad magic/crc")
    ver, seq, timestamp = struct.unpack("<BBf", body[2:8])
    if ver != LAYOUT_VER:
        raise ValueError("layout mismatch")
    dirty = int.from_bytes(body[8:8 + BITMAP_LEN], "little")
    snap = body[8 + BITMAP_LEN:]
    values, flags = {}, {}
    for key, (idx, off, ln) in LAYOUT.items():
        values[key] = snap[off:off + ln]
        flags[key] = bool(dirty >> idx & 1)
    return seq, timestamp, values, flags

def main():
    dev = sys.argv[1] if len(sys.argv) > 1 else "/tmp/ttyB"
    ser = serial.Serial(dev, BAUD, timeout=0.5)

    buf = bytearray()
    last_seq = None
    prev = None
    good = bad = lost = mismatch = 0
    n_dirty = 0
    t_print = time.monotonic()

    # TESTING
    timeout = num_bytes = 0

    print(f"listening on {dev}, packet size {PKT_LEN} bytes")

    while True:
        buf += ser.read(ser.in_waiting or 1)     # blocks until >=1 byte or timeout

        while True:
            i = buf.find(MAGIC)
            if i < 0:
                del buf[:-1]                    # keep a possible partial magic byte

                # TESTING
                if len(buf) == 0:
                    timeout += 1
                else:
                    num_bytes += len(buf)  
                                      
                break
            del buf[:i]
            if len(buf) < PKT_LEN:
                break
            try:
                seq, timestamp, values, flags = decode_packet(bytes(buf[:PKT_LEN]))
            except ValueError:
                bad += 1
                del buf[:1]                      # false magic or corruption: resync
                continue
            del buf[:PKT_LEN]
            good += 1

            gap = 0 if last_seq is None else (seq - last_seq - 1) & 0xFF
            lost += gap
            last_seq = seq

            if prev is not None and gap == 0:
                for key in CHECKABLE:
                    changed = values[key][:4] != prev[key][:4]
                    if changed != flags[key]:
                        mismatch += 1
            prev = values
            n_dirty = sum(flags.values())

        now = time.monotonic()
        if now - t_print >= PRINT_INTERVAL_S:
            print(f"Time: {now}\n"
                  f"timeouts={timeout} bytes={num_bytes} pkts={good} bad_crc={bad} lost={lost}\n"
                  f"dirty_mismatch={mismatch} last_dirty={n_dirty}/{len(LAYOUT)}\n")
            t_print = now


if __name__ == "__main__":
    main()