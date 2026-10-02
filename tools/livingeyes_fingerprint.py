#!/usr/bin/env python3
"""Fingerprint LivingEyes library.properties and all files beneath src/.

v1 SHA-256 feeds each file in sorted POSIX-relative-path order as:
8-byte big-endian UTF-8 path length, path bytes, 8-byte big-endian file length,
then the file's raw bytes. Directory timestamps and checkout location are ignored.
"""

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path

PIN_FORMAT = "robodesk-livingeyes-sha256-v1"


def fingerprint(library_root: Path):
    metadata = library_root / "library.properties"
    sources = library_root / "src"
    if not metadata.is_file() or not sources.is_dir():
        raise ValueError(f"Not a LivingEyes library (library.properties or src/ missing): {library_root}")

    entries = list(sources.rglob("*"))
    if any(path.is_symlink() for path in entries):
        raise ValueError(f"Symlinks under src/ cannot be fingerprinted: {library_root}")
    if any(not path.is_file() and not path.is_dir() for path in entries):
        raise ValueError(f"Unsupported entry under src/: {library_root}")
    files = [metadata] + [path for path in entries if path.is_file()]
    if len(files) == 1:
        raise ValueError(f"No source files under src/: {library_root}")
    files.sort(key=lambda path: path.relative_to(library_root).as_posix().encode("utf-8"))

    digest = hashlib.sha256()
    for path in files:
        name = path.relative_to(library_root).as_posix().encode("utf-8")
        length = path.stat().st_size
        digest.update(len(name).to_bytes(8, "big"))
        digest.update(name)
        digest.update(length.to_bytes(8, "big"))
        with path.open("rb") as source:
            while chunk := source.read(1024 * 1024):
                digest.update(chunk)
    return digest.hexdigest(), len(files)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("library_root", type=Path, help="LivingEyes checkout root (not its src directory)")
    parser.add_argument("--pin", type=Path, help="Fail unless the fingerprint matches this pin manifest")
    args = parser.parse_args()

    try:
        digest, count = fingerprint(args.library_root)
        print(f"LivingEyes source SHA-256 ({count} files): {digest}")
        if args.pin:
            pin = json.loads(args.pin.read_text(encoding="utf-8"))
            if not isinstance(pin, dict) or pin.get("format") != PIN_FORMAT:
                raise ValueError(f"Invalid LivingEyes pin format: {args.pin}")
            expected = pin.get("sha256")
            if not isinstance(expected, str) or not re.fullmatch(r"[0-9a-f]{64}", expected):
                raise ValueError(f"Missing or invalid LivingEyes SHA-256 pin: {args.pin}")
            if digest != expected:
                raise ValueError(f"LivingEyes pin mismatch: expected {expected}, got {digest}. "
                                 f"Review the approved source before updating {args.pin}.")
            print(f"LivingEyes pin verified: {args.pin}")
    except (OSError, ValueError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
