#!/usr/bin/env python3
"""
Test CAN traffic generator using python-can (same code on macOS and Linux).

Sends one frame per FIELDS entry every --period-ms (default 10 ms). Each payload
starts with a per-ID u32 counter (little endian) so a receiver/sink can verify
dirty bits.

Pick the backend with flags or environment variables:
    # macOS / no hardware: two processes talking over UDP multicast (needs msgpack)
    python3 can_feeder.py
    # Linux / Pi with a virtual bus (sudo ip link add dev vcan0 type vcan; sudo ip link set up vcan0)
    CAN_INTERFACE=socketcan CAN_CHANNEL=vcan0 python3 can_feeder.py
    # serial CAN adapter
    python3 can_feeder.py --interface slcan --channel /dev/tty.usbmodem1101 --bitrate 250000

The receiver must open its bus with the same interface/channel.
Requires python-can >= 4.2 (uses the `interface=` argument).

Other options:
    --burst        send every ID at the same instant each period (default: staggered)
    --noise 0.05   extra unknown-ID / remote frames after ~5% of frames
    --duration 30  stop after 30 seconds
"""
import argparse
import heapq
import os
import random
import struct
import time

import can

from can_to_uart import MAC_CHANNEL, MAC_INTERFACE, LINUX_CHANNEL, LINUX_INTERFACE, BITRATE
from frame import FIELDS

UNKNOWN_ID = (0x7F0, False)      # not in FIELDS: should be counted as unknown
OS = "Linux"


def open_bus():
    if OS == "Linux":
        return can.Bus(interface=LINUX_INTERFACE, channel=LINUX_CHANNEL, bitrate=BITRATE)
    else:
        return can.Bus(interface=MAC_INTERFACE, channel=MAC_CHANNEL, bitrate=BITRATE)


def run(bus, args):
    n = len(FIELDS)
    period = args.period_ms / 1000.0
    counters = [0] * n

    start = time.monotonic()
    heap = [(start + (0.0 if args.burst else i * period / n), i) for i in range(n)]
    heapq.heapify(heap)

    sent = dropped = 0
    t_stat, sent_at_stat = start, 0
    end = start + args.duration if args.duration else None

    print(f"feeding {n} IDs, "
          f"period {args.period_ms} ms ({'burst' if args.burst else 'staggered'}), "
          f"~{n / period:.0f} frames/s")

    while True:
        due, i = heap[0]
        now = time.monotonic()
        if end and now >= end:
            break
        if due > now:
            time.sleep(due - now)
        heapq.heapreplace(heap, (due + period, i))      # fixed schedule, no drift

        aid, ln = FIELDS[i]
        cnt = counters[i]
        counters[i] = (cnt + 1) & 0xFFFFFFFF
        payload = (struct.pack("<I", cnt) + bytes([i & 0xFF]) * 4)[:ln]

        try:
            bus.send(can.Message(arbitration_id=aid, is_extended_id=False, data=payload))
            sent += 1
        except can.CanError:
            dropped += 1                                # e.g. tx buffer full
            time.sleep(0.0005)

        if args.noise and random.random() < args.noise:
            try:
                if random.random() < 0.5:
                    # ID not in the table
                    bus.send(can.Message(arbitration_id=UNKNOWN_ID[0],
                                         is_extended_id=UNKNOWN_ID[1],
                                         data=b"\xDE\xAD\xBE\xEF"))
                else:
                    # remote frame for a known ID: should be skipped
                    bus.send(can.Message(arbitration_id=aid, is_extended_id=False,
                                         is_remote_frame=True))
            except can.CanError:
                pass

        now = time.monotonic()
        if now - t_stat >= 1.0:
            print(f"sent={sent} ({(sent - sent_at_stat) / (now - t_stat):.0f}/s) dropped={dropped}")
            t_stat, sent_at_stat = now, sent


def main():
    ap = argparse.ArgumentParser(description="python-can test traffic generator")
    ap.add_argument("--period-ms", type=float, default=50.0, help="per-ID send period")
    ap.add_argument("--burst", action="store_true")
    ap.add_argument("--noise", type=float, default=0.0)
    ap.add_argument("--duration", type=float, default=0.0, help="seconds to run (0 = forever)")
    args = ap.parse_args()

    bus = open_bus()
    try:
        run(bus, args)
    except KeyboardInterrupt:
        pass
    finally:
        bus.shutdown()


if __name__ == "__main__":
    main()