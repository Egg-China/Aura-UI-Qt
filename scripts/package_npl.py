#!/usr/bin/env python3
"""Package the Qt Runtime provider as a deterministic schema-v5 .npl archive."""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import sys
import zipfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
EPOCH = (1980, 1, 1, 0, 0, 0)
PLATFORMS = {
    "windows-x64": "bin/aura-ui-qt.exe",
    "linux-x64": "bin/aura-ui-qt",
    "macos-x64": "bin/aura-ui-qt",
    "macos-arm64": "bin/aura-ui-qt",
}


def digest(path: pathlib.Path) -> str:
    value = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            value.update(block)
    return value.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--platform", required=True, choices=sorted(PLATFORMS))
    parser.add_argument("--binary", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path)
    arguments = parser.parse_args()

    entrypoint = PLATFORMS[arguments.platform]
    if not arguments.binary.is_file():
        print(f"missing Qt runtime binary: {arguments.binary}", file=sys.stderr)
        return 1
    manifest_path = ROOT / "package" / "plugin.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    manifest["entrypoint"] = entrypoint
    manifest["platforms"] = [arguments.platform]

    output = arguments.output or ROOT / "dist" / f"dev.aura.ui-qt.{arguments.platform}.npl"
    output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(output, "w") as archive:
        info = zipfile.ZipInfo("plugin.json", date_time=EPOCH)
        info.compress_type = zipfile.ZIP_DEFLATED
        archive.writestr(info, json.dumps(manifest, indent=2, ensure_ascii=False) + "\n")
        info = zipfile.ZipInfo(entrypoint, date_time=EPOCH)
        info.compress_type = zipfile.ZIP_DEFLATED
        info.external_attr = 0o755 << 16
        archive.writestr(info, arguments.binary.read_bytes())

    checksum = digest(output)
    output.with_suffix(output.suffix + ".sha256").write_text(
        f"{checksum}  {output.name}\n", encoding="utf-8"
    )
    print(f"packaged {output} ({output.stat().st_size} bytes)")
    print(f"sha256 {checksum}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
