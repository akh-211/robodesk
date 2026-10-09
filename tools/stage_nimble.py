"""Stage pinned host sources without touching unchanged files (incremental builds)."""
import hashlib
import json
from pathlib import Path
import sys
import zipfile

root = Path(__file__).resolve().parents[1]
dest = Path(sys.argv[1]).resolve()
if not dest.is_relative_to(root):
    raise SystemExit('NimBLE staging must remain inside workspace')
pin = json.loads((root / 'tools/nimble-pin.json').read_text())
archive = root / f'.verification/dependencies/NimBLE-Arduino-{pin["version"]}.zip'
if hashlib.sha256(archive.read_bytes()).hexdigest().upper() != pin['sha256'].upper():
    raise SystemExit('NimBLE archive fingerprint mismatch')
library = dest / f'NimBLE-Arduino-{pin["version"]}'
expected = {}
with zipfile.ZipFile(archive) as source:
    for entry in source.infolist():
        if entry.is_dir():
            continue
        path = (dest / entry.filename).resolve()
        if not path.is_relative_to(library):
            raise SystemExit('Unexpected archive path')
        content = source.read(entry)
        if path == library / 'src/nimconfig.h':
            content += b'\n#include "robodesk_gateway_config.h"\n'
        expected[path] = content
expected[library / 'src/robodesk_gateway_config.h'] = (root / 'tools/nimble_gateway_config.h').read_bytes()
if library.exists():
    extras = [p for p in library.rglob('*') if p.is_file() and p.resolve() not in expected]
    if extras:
        raise SystemExit('Unpinned files in staged NimBLE library')
for path, content in expected.items():
    if not path.exists() or path.read_bytes() != content:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(content)
print(library)
