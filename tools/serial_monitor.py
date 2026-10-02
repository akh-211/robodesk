"""Read an ESP32 UART log without asserting the adapter's DTR/RTS reset lines."""

import argparse
import sys

try:
    import serial
except ImportError as exc:
    raise SystemExit("pyserial is required; install it with: python -m pip install pyserial") from exc


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port", nargs="?", default="COM9", help="serial port (default: COM9)")
    parser.add_argument("--baud", type=int, default=115200, help="baud rate (default: 115200)")
    args = parser.parse_args()

    port = serial.Serial()
    port.port = args.port
    port.baudrate = args.baud
    port.timeout = 0.2
    # Set these before open(): opening the port with either line asserted may
    # hold an ESP32 EN/GPIO0 auto-reset circuit in reset or download mode.
    port.dtr = False
    port.rts = False
    try:
        port.open()
        print(f"Reading {args.port} at {args.baud} baud with DTR/RTS off. Ctrl+C to close.", file=sys.stderr)
        while True:
            data = port.read(1024)
            if data:
                sys.stdout.buffer.write(data)
                sys.stdout.buffer.flush()
    except KeyboardInterrupt:
        return 0
    finally:
        if port.is_open:
            port.close()


if __name__ == "__main__":
    raise SystemExit(main())
