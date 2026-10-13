#!/usr/bin/env python3
"""PD-4.13: CycloneDX 1.5 SBOM from the libraries an actual build compiled, plus the Android dependencies."""

from __future__ import annotations

import argparse
import json
import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LICENSE_HINTS = (("lesser general public", "LGPL-2.1-or-later"), ("apache license", "Apache-2.0"),
                 ("mit license", "MIT"), ("permission is hereby granted", "MIT"),
                 ("redistribution and use in source and binary forms", "BSD-3-Clause"))


def properties(folder: Path) -> dict[str, str]:
    path = folder / "library.properties"
    if not path.is_file():
        return {}
    out = {}
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if "=" in line and not line.startswith("#"):
            key, value = line.split("=", 1)
            out[key.strip()] = value.strip()
    return out


def license_of(folder: Path) -> str:
    for candidate in sorted(folder.glob("*")):
        if candidate.is_file() and re.match(r"(?i)^(licen[sc]e|copying)", candidate.name):
            text = candidate.read_text(encoding="utf-8", errors="replace")[:4000].lower()
            for needle, spdx in LICENSE_HINTS:
                if needle in text:
                    return spdx
            return "NOASSERTION"
    return "NOASSERTION"


def find_library(name: str, search: list[Path]) -> Path | None:
    for base in search:
        for candidate in (base / name, base / name.replace("-", "_")):
            if candidate.is_dir():
                return candidate
    return None


def firmware_components(build: Path) -> list[dict]:
    options = json.loads((build / "build.options.json").read_text(encoding="utf-8"))
    hardware = Path(options["hardwareFolders"].split(",")[0])
    # Core and pinned dependencies first: the sketchbook may hold unrelated libraries with the same folder name.
    search = [hardware / "libraries", ROOT / ".verification" / "dual-board" / "dependencies"]
    search += [Path(p) for p in options.get("otherLibrariesFolders", "").split(",") if p]
    core_version = hardware.name
    components = [{"type": "framework", "name": "arduino-esp32", "version": core_version,
                   "licenses": [{"license": {"id": "LGPL-2.1-or-later"}}],
                   "properties": [{"name": "robodesk:fqbn", "value": options.get("fqbn", "")}]}]
    for lib in sorted(p.name for p in (build / "libraries").iterdir() if p.is_dir()):
        folder = find_library(lib, search)
        props = properties(folder) if folder else {}
        name, version = props.get("name", lib), props.get("version", "")
        match = re.match(r"^(.*)-(\d+\.\d+\.\d+)$", lib)
        if match and not version:
            name, version = match.group(1), match.group(2)
        spdx = license_of(folder) if folder else "NOASSERTION"
        if folder and folder.parent == hardware / "libraries":
            version, spdx = version or core_version, "LGPL-2.1-or-later"
        component = {"type": "library", "name": name, "version": version or "unknown",
                     "licenses": [{"license": {"id": spdx} if spdx != "NOASSERTION" else {"name": "NOASSERTION"}}]}
        if lib == "ESP_SR":
            component["properties"] = [{"name": "robodesk:note", "value": "Arduino wrapper LGPL; esp-sr models/libraries under the Espressif license (Espressif chips only)"}]
        if name == "LivingEyes":
            pin = json.loads((ROOT / "tools" / "livingeyes-pin.json").read_text(encoding="utf-8"))
            component["hashes"] = [{"alg": "SHA-256", "content": pin["sha256"]}]
            if folder:
                rev = subprocess.run(["git", "-C", str(folder), "rev-parse", "--short", "HEAD"], capture_output=True, text=True)
                if rev.returncode == 0:
                    component.setdefault("properties", []).append({"name": "robodesk:git", "value": rev.stdout.strip()})
        components.append(component)
    return components


def android_components() -> list[dict]:
    out = []
    text = (ROOT / "mobile/android/build.gradle.kts").read_text(encoding="utf-8")
    for plugin, version in re.findall(r'id\("([^"]+)"\) version "([^"]+)"', text):
        out.append({"type": "library", "name": plugin, "version": version, "licenses": [{"license": {"id": "Apache-2.0"}}]})
    app = (ROOT / "mobile/android/app/build.gradle.kts").read_text(encoding="utf-8")
    for dep in re.findall(r'implementation\("([^"]+)"\)', app):
        parts = dep.split(":")
        out.append({"type": "library", "group": parts[0], "name": parts[1] if len(parts) > 1 else dep,
                    "version": parts[2] if len(parts) > 2 else "bom-managed", "licenses": [{"license": {"id": "Apache-2.0"}}]})
    bom = re.search(r'platform\("([^"]+)"\)', app)
    if bom:
        g, n, v = (bom.group(1).split(":") + ["", "", ""])[:3]
        out.append({"type": "library", "group": g, "name": n, "version": v, "licenses": [{"license": {"id": "Apache-2.0"}}]})
    return out


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, default=ROOT / ".verification" / "sbom")
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    head = subprocess.run(["git", "-C", str(ROOT), "rev-parse", "--short", "HEAD"], capture_output=True, text=True).stdout.strip()
    targets = {"robot-s3": ROOT / ".verification/dual-board/robot/build", "gateway-c3": ROOT / ".verification/dual-board/gateway/build"}
    for name, build in list(targets.items()) + [("android-app", None)]:
        if build is not None and not (build / "build.options.json").is_file():
            print(f"skip {name}: no build at {build}")
            continue
        components = firmware_components(build) if build is not None else android_components()
        sbom = {"bomFormat": "CycloneDX", "specVersion": "1.5", "version": 1,
                "metadata": {"component": {"type": "firmware" if build is not None else "application",
                                           "name": f"robodesk-{name}", "version": head}},
                "components": components}
        path = args.out / f"sbom-{name}.cdx.json"
        path.write_text(json.dumps(sbom, indent=2) + "\n", encoding="utf-8")
        unknown = sum(1 for c in components if c["licenses"][0]["license"].get("name") == "NOASSERTION")
        print(f"{path.name}: {len(components)} components, {unknown} without a detected license")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
