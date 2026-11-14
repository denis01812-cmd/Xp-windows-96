#!/usr/bin/env python3
"""Build a BIOS/x86 disk image using Python's standard library and GNU tools."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
SECTOR_SIZE = 512
LOADER_SECTORS = 8
KERNEL_LBA = 1 + LOADER_SECTORS
MAX_KERNEL_SECTORS = 127
IMAGE_SIZE = 2 * 1024 * 1024


class BuildError(Exception):
    """An actionable build failure, suitable for display without a traceback."""


def sector_count(size: int) -> int:
    if size <= 0:
        raise BuildError("The kernel binary must not be empty.")
    return (size + SECTOR_SIZE - 1) // SECTOR_SIZE


def create_image(boot: bytes, loader: bytes, kernel: bytes) -> bytes:
    if len(boot) != SECTOR_SIZE or boot[-2:] != b"\x55\xaa":
        raise BuildError("The boot sector must be 512 bytes and end in 55 AA.")
    if not loader or len(loader) > LOADER_SECTORS * SECTOR_SIZE:
        raise BuildError("The second-stage loader must fit in 8 sectors.")
    if sector_count(len(kernel)) > MAX_KERNEL_SECTORS:
        raise BuildError("The kernel exceeds the bootstrap limit of 127 sectors (65,024 bytes).")
    image = bytearray(IMAGE_SIZE)
    image[:SECTOR_SIZE] = boot
    image[SECTOR_SIZE:SECTOR_SIZE + len(loader)] = loader
    start = KERNEL_LBA * SECTOR_SIZE
    image[start:start + len(kernel)] = kernel
    return bytes(image)


def tool(variable: str, default: str) -> list[str]:
    command = shlex.split(os.environ.get(variable, default))
    if not command or shutil.which(command[0]) is None:
        raise BuildError(f"Missing {variable}: install {default}, or set {variable} to its executable path.")
    return command


def run(command: list[str]) -> None:
    print("  " + shlex.join([str(part) for part in command]), flush=True)
    subprocess.run(command, cwd=ROOT, check=True)


def build(output: Path = ROOT / "build") -> Path:
    output = output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    cc, cxx = tool("CC", "gcc"), tool("CXX", "g++")
    ld, objcopy = tool("LD", "ld"), tool("OBJCOPY", "objcopy")
    flags = [
        "-m32", "-std=c++17", "-Os", "-ffreestanding", "-fno-builtin",
        "-fno-pic", "-fno-pie", "-fno-stack-protector", "-fno-exceptions",
        "-fno-rtti", "-fno-threadsafe-statics", "-fno-unwind-tables",
        "-fno-asynchronous-unwind-tables", "-mno-sse", "-mno-sse2",
        "-mno-mmx", "-msoft-float", "-Wall", "-Wextra", "-Werror",
        "-I", str(ROOT / "kernel"),
    ]
    print("\nXP / 96 - building a freestanding 32-bit kernel", flush=True)
    objects = []
    for source in sorted((ROOT / "kernel").glob("*.cpp")):
        target = output / (source.stem + ".o")
        run(cxx + flags + ["-c", str(source), "-o", str(target)])
        objects.append(str(target))
    entry = output / "entry.o"
    run(cc + ["-m32", "-c", str(ROOT / "kernel/entry.S"), "-o", str(entry)])
    elf = output / "kernel.elf"
    run(ld + ["-m", "elf_i386", "--build-id=none", "-T", str(ROOT / "kernel/linker.ld"),
              "-Map", str(output / "kernel.map"), "-o", str(elf), str(entry)] + objects)
    binary = output / "kernel.bin"
    run(objcopy + ["-O", "binary", str(elf), str(binary)])
    kernel = binary.read_bytes()
    sectors = sector_count(len(kernel))
    if sectors > MAX_KERNEL_SECTORS:
        raise BuildError("Kernel is too large for the single-read BIOS loader.")

    for name, address in (("boot", "0x7c00"), ("loader", "0x8000")):
        target = output / (name + ".o")
        run(cc + ["-m32", "-x", "assembler-with-cpp", f"-DKERNEL_SECTORS={sectors}",
                  "-c", str(ROOT / "boot" / (name + ".S")), "-o", str(target)])
        run(ld + ["-m", "elf_i386", "--build-id=none", "--oformat", "binary",
                  "-Ttext", address, "-e", "_start", "-o", str(output / (name + ".bin")), str(target)])

    image = create_image((output / "boot.bin").read_bytes(), (output / "loader.bin").read_bytes(), kernel)
    target = output / "xp96.img"
    target.write_bytes(image)
    report = {
        "version": "0.1.0", "architecture": "i386-bios", "image_bytes": len(image),
        "kernel_bytes": len(kernel), "kernel_sectors": sectors,
        "kernel_lba": KERNEL_LBA, "sha256": hashlib.sha256(image).hexdigest(),
    }
    (output / "manifest.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"\nReady: {target}\nKernel: {len(kernel):,} bytes / {sectors} sectors\nSHA256: {report['sha256']}")
    return target


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, default=ROOT / "build")
    args = parser.parse_args()
    try:
        build(args.output_dir)
    except (BuildError, OSError, subprocess.CalledProcessError) as error:
        print(f"\nBuild failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
