#!/usr/bin/env python3
"""
Reads from CAN using SocketCAN, stores data in a fixed-layout snapshot data 
frame with per-field dirty bits, sends this snapshot over UART at fixed time
intervals.

Run:  python3 can_to_uart.py
Tip:  chrt -f 50 python3 can_to_uart.py   if you see RX overruns.
"""
import binascii
import platform
import can
import struct
import time
import serial

from frame import MAGIC, LAYOUT, FRAME_LEN, BITMAP_LEN, LAYOUT_VER

# ----------------------------------------------------------------- config
MAC_INTERFACE = "udp_multicast"
MAC_CHANNEL = "239.0.0.1"
LINUX_INTERFACE = "socketcan"
LINUX_CHANNEL = "can0"
BITRATE = 250000
UART_DEV = "/dev/ttys004"
BAUD = 115200 # 460800, mac can't handle this
SEND_INTERVAL_S = 0.5          # 2 Hz for now
RCVBUF_BYTES = 1 << 20
OS = platform.system()   # "Linux" on the Pi, "Darwin" on macOS


# ----------------------------------------------------------------- setup
def open_can():
    if OS == "Linux":
        return can.interface.Bus(interface=LINUX_INTERFACE, 
                                 channel=LINUX_CHANNEL, 
                                 bitrate=BITRATE)
    else: # "Mac"
        return can.interface.Bus(interface=MAC_INTERFACE, 
                                 channel=MAC_CHANNEL, 
                                 bitrate=BITRATE)


def open_uart():
    return serial.Serial(UART_DEV, BAUD, timeout=0, write_timeout=1.0)


# ----------------------------------------------------------------- msg functions
def build_packet(seq, dirty, snapshot):
    body = (
        MAGIC
        + struct.pack("<BBf", LAYOUT_VER, seq & 0xFF, time.monotonic())
        + dirty.to_bytes(BITMAP_LEN, "little")
        + bytes(snapshot)
    )
    return body + struct.pack("<H", binascii.crc_hqx(body, 0xFFFF))



# ----------------------------------------------------------------- main loop
def main():
    bus = open_can()
    ser = open_uart()

    snapshot = bytearray(FRAME_LEN)
    dirty = 0                      # bit i set => field i updated since last send
    seq = 0
    rx_total = rx_unknown = rx_err = rx_len_err = rx_succ = tx_fail = 0

    next_tx = time.monotonic() + SEND_INTERVAL_S

    # TESTING
    t_print = time.monotonic()

    try:
        def handle(msg):
            nonlocal dirty, rx_unknown, rx_total, rx_err, rx_len_err, rx_succ

            rx_total += 1

            if not (msg.is_error_frame or msg.is_remote_frame):
                entry = LAYOUT.get(msg.arbitration_id)

                if entry is None: 
                    rx_unknown += 1
                    return
                else:
                    idx, off, ln = entry
                    data = msg.data
                    if len(data) == ln:
                        snapshot[off:off + ln] = data
                        dirty |= 1 << idx
                        rx_succ += 1
                    else:
                        rx_len_err += 1

            else:        # error frame or remote frame
                rx_err += 1

        while True:
            timeout = max(0.0, next_tx - time.monotonic())
            msg = bus.recv(timeout)

            while msg is not None:
                handle(msg)
                if time.monotonic() >= next_tx:
                    break
                msg = bus.recv(0)

                    
            now = time.monotonic()
            if now >= next_tx:
                try:
                    ser.write(build_packet(seq, dirty, snapshot))
                    seq = (seq + 1) & 0xFF
                    dirty = 0                            # only clear after a successful write
                except serial.SerialTimeoutException:
                    tx_fail += 1                         # keep dirty bits, retry next tick

                next_tx += SEND_INTERVAL_S               # no drift
                if next_tx < now:                        # fell far behind: resync
                    next_tx = now + SEND_INTERVAL_S

            # TESTING
            if now - t_print >= 1.0:
                print(f"Time: {now}\nrx fail={rx_err} rx unknown={rx_unknown} "
                      f"rx len_err={rx_len_err} rx succ={rx_succ} "
                      f"rx total={rx_total}\ntx fail={tx_fail}\n")
                t_print = now
    finally:
        bus.shutdown()


if __name__ == "__main__":
    main()