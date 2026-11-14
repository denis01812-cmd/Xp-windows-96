#!/usr/bin/env python3
"""Fetch hash-pinned SeaBIOS firmware for the optional v86 browser emulator."""
from __future__ import annotations

import base64
import hashlib
import json
from pathlib import Path
import sys
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen

ROOT = Path(__file__).resolve().parent.parent
REVISION = "315b2b0b758703ef0e6cc7b4088d6068f83f3c20"
FIRMWARE = {
    "seabios.bin": "73e3f359102e3a9982c35fce98eb7cd08f18303ac7f1ba6ebfbe6cdc1c244d98",
    "vgabios.bin": "a4bc0d80cc3ca028c73dafa8fee396b8d054ce87ebd8abfbd31b06b437607880",
}


def verified(data: bytes, expected: str) -> bool:
    return hashlib.sha256(data).hexdigest() == expected


def fetch() -> None:
    destination = ROOT / ".cache/bios"
    destination.mkdir(parents=True, exist_ok=True)
    for name, digest in FIRMWARE.items():
        path = destination / name
        if path.is_file() and verified(path.read_bytes(), digest):
            print(f"Verified: {name} (cached)")
            continue
        url = f"https://api.github.com/repos/copy/v86/contents/bios/{name}?ref={REVISION}"
        request = Request(url, headers={"User-Agent": "xp96-firmware-setup", "Accept": "application/vnd.github+json"})
        with urlopen(request, timeout=30) as response:
            payload = json.load(response)
        if payload.get("encoding") != "base64":
            raise ValueError(f"Unexpected GitHub response for {name}")
        data = base64.b64decode(payload["content"])
        if not verified(data, digest):
            raise ValueError(f"SHA256 mismatch for {name}; refusing to use this firmware")
        temporary = path.with_suffix(".tmp")
        temporary.write_bytes(data)
        temporary.replace(path)
        print(f"Downloaded and verified: {name}")


def main() -> int:
    try:
        fetch()
    except (HTTPError, URLError, OSError, ValueError, KeyError) as error:
        print(f"Firmware setup failed: {error}\nCheck your network or GitHub API rate limit and try again.", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
