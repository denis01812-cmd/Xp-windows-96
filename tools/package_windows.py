#!/usr/bin/env python3
"""Package a verified, prebuilt image with a Windows double-click launcher."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import sys
import zipfile

ROOT = Path(__file__).resolve().parent.parent


class PackageError(Exception):
    """A missing or inconsistent input that must not be distributed."""


def windows_text(path: Path, *, bom: bool = False) -> bytes:
    text = path.read_text(encoding="utf-8-sig").replace("\r\n", "\n")
    return text.replace("\n", "\r\n").encode("utf-8-sig" if bom else "utf-8")


def package(build_dir: Path, destination: Path) -> Path:
    image = (build_dir / "xp96.img").read_bytes()
    manifest_data = (build_dir / "manifest.json").read_bytes()
    try:
        manifest = json.loads(manifest_data)
    except (ValueError, UnicodeError) as error:
        raise PackageError("Invalid build/manifest.json; rebuild the image first.") from error
    if not isinstance(manifest, dict):
        raise PackageError("The build manifest must be an object.")
    if len(image) < 512 or image[510:512] != b"\x55\xaa":
        raise PackageError("Invalid BIOS boot signature; rebuild the image first.")
    if manifest.get("image_bytes") != len(image):
        raise PackageError("Image size does not match the build manifest.")
    if manifest.get("sha256") != hashlib.sha256(image).hexdigest():
        raise PackageError("Image SHA256 does not match the build manifest.")

    files = {
        "xp96.img": image,
        "manifest.json": manifest_data,
        "Start-XP96.bat": windows_text(ROOT / "Start-XP96.bat"),
        "tools/run-windows.ps1": windows_text(ROOT / "tools/run-windows.ps1", bom=True),
        "START-HERE.md": (ROOT / "docs/WINDOWS.md").read_bytes(),
    }
    destination.mkdir(parents=True, exist_ok=True)
    for name, content in files.items():
        target = destination / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(content)

    archive = destination.with_suffix(".zip")
    # Explicit entries only: never include unrelated local files or credentials.
    with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED) as output:
        for name, content in files.items():
            output.writestr(name, content)
    print(f"Windows package: {destination}\nZIP archive: {archive}")
    return archive


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build")
    parser.add_argument("--output-dir", type=Path, default=ROOT / "build/xp96-windows")
    args = parser.parse_args()
    try:
        package(args.build_dir.resolve(), args.output_dir.resolve())
    except (OSError, PackageError) as error:
        print(f"Packaging failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
