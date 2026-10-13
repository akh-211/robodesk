#!/usr/bin/env python3
"""Summarise LEV,BEH lines (RoboBehaviorLog.h) from a serial log.

Lines may carry the host timestamp prefix written by tools/serial_monitor.py ("387.8 LEV,BEH,...").
Rates and durations need that prefix; without it only counts are reported.

Usage: python tools/behavior_log_report.py LOG [--json]
"""
import argparse
import json
import math
import re
import sys

LINE = re.compile(r"^\s*(?:(?P<t>\d+(?:\.\d+)?)\s+)?LEV,BEH,(?P<kind>[A-Z]+),(?P<fields>.*?)\s*$")


def parse(lines):
    events = []
    for raw in lines:
        m = LINE.match(raw)
        if not m:
            continue
        fields = {}
        for part in m.group("fields").split(","):
            key, sep, value = part.partition("=")
            if sep:
                fields[key] = value
        t = m.group("t")
        events.append({"t": float(t) if t is not None else None, "kind": m.group("kind"), **fields})
    return events


def p95(values):
    if not values:
        return None
    ordered = sorted(values)
    return ordered[max(0, math.ceil(0.95 * len(ordered)) - 1)]  # nearest rank


def repeats(keys):
    return sum(1 for a, b in zip(keys, keys[1:]) if a == b)


def report(events):
    times = [e["t"] for e in events if e["t"] is not None]
    span_s = (max(times) - min(times)) if len(times) >= 2 else None
    touch = [int(e["latMs"]) for e in events if e["kind"] == "TOUCH" and e.get("latMs", "").isdigit()]
    reacts = [(e.get("id"), e.get("var")) for e in events if e["kind"] == "REACT"]
    idles = [e.get("act") for e in events if e["kind"] == "IDLE"]
    spont = sum(1 for e in events if e["kind"] == "SOUND" and e.get("spont") == "1")
    sounds = sum(1 for e in events if e["kind"] == "SOUND")
    sleep_s = {}
    last = None
    for e in events:
        if e["kind"] != "SLEEP" or e["t"] is None:
            continue
        if last is not None:
            sleep_s[last[0]] = sleep_s.get(last[0], 0.0) + e["t"] - last[1]
        last = (e.get("to"), e["t"])
    if last is not None and times:
        sleep_s[last[0]] = sleep_s.get(last[0], 0.0) + max(times) - last[1]
    return {
        "events": len(events),
        "span_s": round(span_s, 1) if span_s is not None else None,
        "touch_samples": len(touch),
        "touch_p95_ms": p95(touch),
        "touch_max_ms": max(touch) if touch else None,
        "reactions": len(reacts),
        "reaction_repeats": repeats(reacts),
        "idle_actions": len(idles),
        "idle_repeats": repeats(idles),
        "sounds": sounds,
        "spontaneous_sounds": spont,
        "spontaneous_per_hour": round(spont * 3600.0 / span_s, 2) if span_s else None,
        "sleep_state_s": {k: round(v, 1) for k, v in sleep_s.items()},
    }


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("log")
    ap.add_argument("--json", action="store_true")
    args = ap.parse_args(argv)
    with open(args.log, encoding="utf-8", errors="replace") as f:
        result = report(parse(f))
    if args.json:
        print(json.dumps(result, indent=2, sort_keys=True))
    else:
        for key, value in result.items():
            print(f"{key}: {value}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
