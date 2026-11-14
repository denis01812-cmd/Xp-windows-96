#!/usr/bin/env python3
"""Serve a real x86 VM preview, exposing only explicitly allowed public files."""
from __future__ import annotations

import argparse
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import sys
from urllib.parse import urlsplit

ROOT = Path(__file__).resolve().parent.parent
ROUTES = {
    "/": ROOT / "web/index.html",
    "/index.html": ROOT / "web/index.html",
    "/style.css": ROOT / "web/style.css",
    "/app.js": ROOT / "web/app.js",
    "/favicon.svg": ROOT / "web/favicon.svg",
    "/xp96.img": ROOT / "build/xp96.img",
    "/manifest.json": ROOT / "build/manifest.json",
    "/vendor/libv86.mjs": ROOT / "node_modules/v86/build/libv86.mjs",
    "/vendor/v86.wasm": ROOT / "node_modules/v86/build/v86.wasm",
    "/vendor/v86-fallback.wasm": ROOT / "node_modules/v86/build/v86-fallback.wasm",
    "/bios/seabios.bin": ROOT / ".cache/bios/seabios.bin",
    "/bios/vgabios.bin": ROOT / ".cache/bios/vgabios.bin",
}


class PreviewHandler(SimpleHTTPRequestHandler):
    extensions_map = {**SimpleHTTPRequestHandler.extensions_map, ".wasm": "application/wasm", ".mjs": "text/javascript", ".img": "application/octet-stream"}

    def do_GET(self) -> None:
        if urlsplit(self.path).path not in ROUTES:
            self.send_error(404, "Not a public preview file")
            return
        super().do_GET()

    def do_HEAD(self) -> None:
        if urlsplit(self.path).path not in ROUTES:
            self.send_error(404, "Not a public preview file")
            return
        super().do_HEAD()

    def translate_path(self, path: str) -> str:
        # Never serve the repository root, .git, local files, or directory listings.
        return str(ROUTES.get(urlsplit(path).path, ROOT / ".cache/nonexistent-preview-route"))

    def end_headers(self) -> None:
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        super().end_headers()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", default=8000, type=int)
    args = parser.parse_args()
    missing = [str(path.relative_to(ROOT)) for path in (
        ROOT / "build/xp96.img", ROOT / "node_modules/v86/build/libv86.mjs",
        ROOT / ".cache/bios/seabios.bin", ROOT / ".cache/bios/vgabios.bin",
    ) if not path.is_file()]
    if missing:
        print("Preview dependencies are missing:\n  " + "\n  ".join(missing), file=sys.stderr)
        print("Run: python3 tools/build.py && npm ci && python3 tools/fetch_bios.py", file=sys.stderr)
        return 1
    try:
        with ThreadingHTTPServer((args.host, args.port), PreviewHandler) as server:
            print(f"XP / 96 preview listening on {args.host}:{args.port}", flush=True)
            server.serve_forever()
    except KeyboardInterrupt:
        return 0
    except OSError as error:
        print(f"Preview failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
