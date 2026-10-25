#!/usr/bin/env python3
"""Set the DS3231 to the laptop's clock, through a board running the rtc_set firmware.

    python3 tools/set_rtc.py                   # finds /dev/cu.usbmodem* on a Mac
    python3 tools/set_rtc.py /dev/ttyACM0      # or name the port

Waits for the top of the next second, sends the Unix time (UTC), then shows the board's output
for a few seconds so you can see the RTC took it. Standard library only, no pyserial.
"""
import glob
import os
import select
import sys
import termios
import time
import tty


def find_port():
    ports = sorted(glob.glob("/dev/cu.usbmodem*") + glob.glob("/dev/ttyACM*"))
    if not ports:
        sys.exit("No board found. Is it plugged in and running rtc_set?")
    if len(ports) > 1:
        sys.exit("More than one port, pick one: " + " ".join(ports))
    return ports[0]


def main():
    port = sys.argv[1] if len(sys.argv) > 1 else find_port()
    fd = os.open(port, os.O_RDWR | os.O_NOCTTY)
    try:
        tty.setraw(fd)
        termios.tcflush(fd, termios.TCIOFLUSH)

        # The DS3231 only keeps whole seconds, so send on the second boundary.
        now = time.time()
        time.sleep(1 - (now - int(now)))
        epoch = int(time.time())
        os.write(fd, f"{epoch}\n".encode())
        print(f"{port}: sent {epoch} ({time.strftime('%Y-%m-%d %H:%M:%S', time.gmtime(epoch))} UTC)")

        end = time.time() + 3.5
        buf = b""
        while time.time() < end:
            ready, _, _ = select.select([fd], [], [], 0.1)
            if ready:
                buf += os.read(fd, 256)
                *lines, buf = buf.split(b"\n")
                for line in lines:
                    print("  board:", line.decode(errors="replace").rstrip())
    finally:
        os.close(fd)


if __name__ == "__main__":
    main()
