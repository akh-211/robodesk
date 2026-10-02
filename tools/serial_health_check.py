"""UART open/close and soak evidence, without asserting DTR/RTS. Does not flash firmware."""
import argparse
import json
import time
import re
from pathlib import Path
import serial


def observe(port, seconds):
    link = serial.Serial()
    link.port, link.baudrate, link.timeout = port, 115200, 0.2
    link.dtr = link.rts = False
    counts = {"heartbeats": 0, "boot_messages": 0, "response_timeouts": 0}
    partial = b""
    try:
        link.open()
        until = time.monotonic() + seconds
        while time.monotonic() < until:
            partial += link.read(4096)
            while b"\n" in partial:
                line, partial = partial.split(b"\n", 1)
                counts["heartbeats"] += b"LEV,STAT," in line
                uptime = re.search(rb"LEV,HEARTBEAT,uptime=(\d+)", line)
                if uptime:
                    counts["uptime_ms"] = int(uptime.group(1))
                counts["boot_messages"] += b"LEV,BOOT," in line
                counts["response_timeouts"] += b"RESPONSE_TIMEOUT" in line
            if len(partial) > 8192:
                partial = b""
    finally:
        link.close()
    return counts


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port", default="COM9", nargs="?")
    parser.add_argument("--cycles", type=int, default=20)
    parser.add_argument("--cycle-seconds", type=float, default=7)
    parser.add_argument("--soak-seconds", type=float, default=7200)
    parser.add_argument("--output", type=Path, default=Path(".verification/serial-health.json"))
    args = parser.parse_args()
    if args.cycles < 1 or args.cycle_seconds < 6 or args.soak_seconds < 0:
        parser.error("Use at least one cycle, >=6 seconds per cycle, and a nonnegative soak duration.")
    report = {"port": args.port, "dtr": False, "rts": False, "cycles": []}
    try:
        for cycle in range(args.cycles):
            result = observe(args.port, args.cycle_seconds)
            report["cycles"].append(result)
            print(f"Cycle {cycle + 1}: {json.dumps(result)}", flush=True)
            time.sleep(0.5)
        # Close the port for the entire soak. Compare monotonic firmware uptime
        # before and after, so a reset while nobody drains serial is detected.
        started = time.monotonic()
        time.sleep(args.soak_seconds)
        after = observe(args.port, args.cycle_seconds)
        elapsed = time.monotonic() - started
        before = report["cycles"][-1].get("uptime_ms")
        final = after.get("uptime_ms")
        delta = ((final - before) & 0xFFFFFFFF) if before is not None and final is not None else None
        report["soak"] = {"serial_closed_seconds": args.soak_seconds, "after": after, "uptime_delta_ms": delta}

        report["passed"] = all(c["heartbeats"] > 0 and c["boot_messages"] == 0 for c in report["cycles"])
        if args.soak_seconds:
            report["passed"] &= after["heartbeats"] > 0 and after["boot_messages"] == 0 and delta is not None and abs(delta / 1000 - elapsed) < 15
    except (serial.SerialException, OSError) as error:
        report["error"] = str(error)
        report["passed"] = False
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(f"Evidence: {args.output}", flush=True)
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
