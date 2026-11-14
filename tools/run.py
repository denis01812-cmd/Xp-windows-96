#!/usr/bin/env python3
"""Build XP / 96 and boot its raw IDE image safely in QEMU."""
from __future__ import annotations

import argparse
import os
import shlex
import shutil
import subprocess
import sys

from build import BuildError, ROOT, build


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--headless", action="store_true", help="Serial log only; Ctrl+C exits QEMU")
    args = parser.parse_args()
    qemu = os.environ.get("QEMU", "qemu-system-i386")
    if shutil.which(qemu) is None:
        print("QEMU not found. Install qemu-system-x86, or use the browser preview (see README.md).", file=sys.stderr)
        return 1
    try:
        image = build()
        command = [qemu, "-machine", "pc", "-cpu", "qemu32", "-m", "64M", "-vga", "std",
                   "-drive", f"file={image},format=raw,if=ide", "-boot", "c", "-serial", "stdio",
                   "-monitor", "none"]
        if args.headless:
            command += ["-display", "none"]
        print("\n" + shlex.join(command), flush=True)
        return subprocess.call(command, cwd=ROOT)
    except (BuildError, OSError, subprocess.CalledProcessError) as error:
        print(f"Run failed: {error}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        return 130


if __name__ == "__main__":
    raise SystemExit(main())
