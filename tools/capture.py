#!/usr/bin/env python3
"""Capture the Uno's serial output to a CSV file.

Works with both sketches:
  odax_atomizer.ino  -> its own CSV (ms,counts,mv_est,sig_fast,...); lines pass through unchanged
  odax_probe.ino     -> "counts:<value>" lines; written as "ms,counts" using the PC clock

Usage:
  python3 capture.py                       # auto-detect port, write eda_<timestamp>.csv
  python3 capture.py --port /dev/cu.usbmodem1101 --out rest_run.csv
  python3 capture.py --list                # show serial ports

While capturing you can type a command letter and press Return to send it to the
sketch (a auto, o on, f off, t test burst, c recalibrate). Ctrl+C stops and saves.
Close the Arduino IDE Serial Monitor/Plotter first: only one program can use the port.
Opening the port resets the Uno, so the sketch restarts (and recalibrates).
"""
import argparse
import datetime as dt
import sys
import threading
import time

import serial
from serial.tools import list_ports


def find_port():
    ports = list(list_ports.comports())
    for p in ports:
        text = f"{p.device} {p.description} {p.manufacturer or ''}".lower()
        if "usbmodem" in text or "arduino" in text or "ttyacm" in text or "usbserial" in text:
            return p.device
    return ports[0].device if len(ports) == 1 else None


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", help="serial port (default: auto-detect)")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--out", help="output CSV (default: eda_YYYYmmdd_HHMMSS.csv)")
    ap.add_argument("--list", action="store_true", help="list serial ports and exit")
    ap.add_argument("--quiet", action="store_true", help="do not echo lines to the terminal")
    args = ap.parse_args()

    if args.list:
        for p in list_ports.comports():
            print(f"{p.device}\t{p.description}")
        return 0

    port = args.port or find_port()
    if not port:
        print("No Arduino port found. Plug in the Uno, or use --list and --port.", file=sys.stderr)
        return 1
    out = args.out or dt.datetime.now().strftime("eda_%Y%m%d_%H%M%S.csv")

    try:
        ser = serial.Serial(port, args.baud, timeout=0.2)
    except serial.SerialException as e:
        print(f"Could not open {port}: {e}\nIs the Serial Monitor/Plotter still open?", file=sys.stderr)
        return 1

    stop = threading.Event()

    def send_commands():
        while not stop.is_set():
            try:
                line = sys.stdin.readline()
            except Exception:
                return
            if not line:
                return
            cmd = line.strip()[:1]
            if cmd:
                ser.write(cmd.encode())

    threading.Thread(target=send_commands, daemon=True).start()

    print(f"Capturing {port} @ {args.baud} -> {out}  (Ctrl+C to stop; type a/o/f/t/c + Return to send a command)")
    t0 = time.monotonic()
    wrote_probe_header = False
    n = 0
    buf = b""
    with open(out, "w", newline="") as f:
        f.write(f"# captured {dt.datetime.now().isoformat(timespec='seconds')} from {port}\n")
        try:
            while True:
                buf += ser.read(256)
                while b"\n" in buf:
                    raw, buf = buf.split(b"\n", 1)
                    line = raw.decode("utf-8", errors="replace").strip()
                    if not line:
                        continue
                    if line.lower().startswith("counts:"):   # probe sketch
                        try:
                            val = float(line.split(":", 1)[1])
                        except ValueError:
                            continue
                        if not wrote_probe_header:
                            f.write("ms,counts\n")
                            wrote_probe_header = True
                        line = f"{int((time.monotonic() - t0) * 1000)},{val}"
                    f.write(line + "\n")
                    n += 1
                    if n % 50 == 0:
                        f.flush()
                    if not args.quiet:
                        print(line)
        except KeyboardInterrupt:
            pass
        finally:
            stop.set()
            f.flush()
            ser.close()
    print(f"\nSaved {n} lines to {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
