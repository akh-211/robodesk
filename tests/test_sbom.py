import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import sbom  # noqa: E402


class SbomTests(unittest.TestCase):
    def test_build_libraries_resolve_core_first_with_licenses(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            hardware = root / "esp32" / "3.3.11"
            core_wifi = hardware / "libraries" / "WiFi"
            core_wifi.mkdir(parents=True)
            (core_wifi / "library.properties").write_text("name=WiFi\nversion=3.3.11\n", encoding="utf-8")
            sketchbook = root / "sketchbook"
            for name, version, licence in (("WiFi", "1.2.7", "MIT License"), ("ArduinoJson", "7.4.3", "MIT License")):
                lib = sketchbook / name
                lib.mkdir(parents=True)
                (lib / "library.properties").write_text(f"name={name}\nversion={version}\n", encoding="utf-8")
                (lib / "LICENSE.txt").write_text(licence, encoding="utf-8")
            build = root / "build"
            for name in ("WiFi", "ArduinoJson", "Mystery"):
                (build / "libraries" / name).mkdir(parents=True)
            (build / "build.options.json").write_text(json.dumps({
                "hardwareFolders": str(hardware), "otherLibrariesFolders": str(sketchbook), "fqbn": "esp32:esp32:esp32s3"}), encoding="utf-8")
            components = {c["name"]: c for c in sbom.firmware_components(build)}
            self.assertEqual(components["arduino-esp32"]["version"], "3.3.11")
            self.assertEqual(components["WiFi"]["version"], "3.3.11")  # core wins over the sketchbook copy
            self.assertEqual(components["WiFi"]["licenses"][0]["license"]["id"], "LGPL-2.1-or-later")
            self.assertEqual(components["ArduinoJson"]["licenses"][0]["license"]["id"], "MIT")
            self.assertEqual(components["Mystery"]["licenses"][0]["license"]["name"], "NOASSERTION")

    def test_android_dependencies_are_listed(self):
        names = {c["name"] for c in sbom.android_components()}
        self.assertIn("activity-compose", names)


if __name__ == "__main__":
    unittest.main()
