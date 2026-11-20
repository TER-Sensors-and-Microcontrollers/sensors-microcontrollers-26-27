#!/usr/bin/env python3
"""Creates two linked virtual serial ports, /tmp/ttyA <-> /tmp/ttyB. Ctrl+C to stop."""
import os
import pty
import select
import tty

LINKS = ("/tmp/ttyA", "/tmp/ttyB")

masters, slaves = [], []
for link in LINKS:
    m, s = pty.openpty()
    tty.setraw(s)                        # no echo or newline translation
    name = os.ttyname(s)
    if os.path.lexists(link):
        os.remove(link)
    os.symlink(name, link)               # stable name for the random pty path
    masters.append(m)
    slaves.append(s)                     # keep open so the pty doesn't hang up
    print(f"{link} -> {name}")

a, b = masters
try:
    while True:
        ready, _, _ = select.select([a, b], [], [])
        for fd in ready:
            data = os.read(fd, 4096)
            if data:
                os.write(b if fd == a else a, data)
finally:
    for link in LINKS:
        if os.path.lexists(link):
            os.remove(link)