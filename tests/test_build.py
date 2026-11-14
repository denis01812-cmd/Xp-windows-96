"""Fast, dependency-free checks for the disk layout and preview file boundary."""
import hashlib
from http.client import HTTPConnection
from http.server import ThreadingHTTPServer
from pathlib import Path
import struct
import threading
import unittest

from tools.build import BuildError, IMAGE_SIZE, KERNEL_LBA, MAX_KERNEL_SECTORS, create_image, sector_count
from tools.fetch_bios import verified
from tools.preview import PreviewHandler, ROOT, ROUTES


class DiskImageTests(unittest.TestCase):
    @staticmethod
    def boot():
        return b"\x00" * 510 + b"\x55\xaa"

    def test_rounding_sectors(self):
        self.assertEqual(sector_count(1), 1)
        self.assertEqual(sector_count(512), 1)
        self.assertEqual(sector_count(513), 2)

    def test_empty_kernel_is_rejected(self):
        for size in (0, -1):
            with self.assertRaises(BuildError):
                sector_count(size)
        with self.assertRaises(BuildError):
            create_image(self.boot(), b"loader", b"")

    def test_disk_offsets_and_zero_padding(self):
        image = create_image(self.boot(), b"LOADER", b"KERNEL")
        self.assertEqual(len(image), IMAGE_SIZE)
        self.assertEqual(image[510:512], b"\x55\xaa")
        self.assertEqual(image[512:518], b"LOADER")
        self.assertEqual(image[518:KERNEL_LBA * 512], bytes(KERNEL_LBA * 512 - 518))
        self.assertEqual(image[KERNEL_LBA * 512:KERNEL_LBA * 512 + 6], b"KERNEL")
        self.assertEqual(image[-512:], bytes(512))

    def test_bad_boot_sector_is_rejected(self):
        for boot in (b"", bytes(512), self.boot() + b"extra"):
            with self.assertRaises(BuildError):
                create_image(boot, b"loader", b"kernel")

    def test_loader_limits(self):
        for loader in (b"", bytes(4097)):
            with self.assertRaises(BuildError):
                create_image(self.boot(), loader, b"kernel")
        self.assertEqual(len(create_image(self.boot(), bytes(4096), b"kernel")), IMAGE_SIZE)

    def test_kernel_boundary(self):
        largest = bytes(MAX_KERNEL_SECTORS * 512)
        self.assertEqual(len(create_image(self.boot(), b"loader", largest)), IMAGE_SIZE)
        with self.assertRaises(BuildError):
            create_image(self.boot(), b"loader", largest + b"x")

    def test_image_generation_is_deterministic(self):
        a = create_image(self.boot(), b"loader", b"kernel")
        b = create_image(self.boot(), b"loader", b"kernel")
        self.assertEqual(hashlib.sha256(a).digest(), hashlib.sha256(b).digest())

    def test_firmware_hash_validation(self):
        digest = hashlib.sha256(b"firmware").hexdigest()
        self.assertTrue(verified(b"firmware", digest))
        self.assertFalse(verified(b"corrupted", digest))

    def test_built_kernel_is_32_bit_elf(self):
        kernel = ROOT / "build/kernel.elf"
        if not kernel.exists():
            self.skipTest("Run python3 tools/build.py to check the compiled ELF")
        data = kernel.read_bytes()
        self.assertEqual(data[:6], b"\x7fELF\x01\x01")
        self.assertEqual(struct.unpack_from("<H", data, 18)[0], 3)  # EM_386
        self.assertEqual(struct.unpack_from("<I", data, 24)[0], 0x10000)


class QuietHandler(PreviewHandler):
    def log_message(self, *_args):
        pass


class PreviewTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.server = ThreadingHTTPServer(("127.0.0.1", 0), QuietHandler)
        cls.thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.thread.start()

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()
        cls.thread.join()

    def request(self, path, method="GET"):
        connection = HTTPConnection("127.0.0.1", self.server.server_port, timeout=3)
        connection.request(method, path)
        response = connection.getresponse()
        result = response.status, response.getheaders(), response.read()
        connection.close()
        return result

    def test_index_is_served(self):
        status, headers, body = self.request("/?preview=1")
        self.assertEqual(status, 200)
        self.assertIn(b"XP / 96", body)
        self.assertIn(("X-Content-Type-Options", "nosniff"), headers)

    def test_private_files_are_not_served(self):
        for path in ("/.git/config", "/README.md", "/../README.md", "/%2e%2e/README.md", "/bios/", "/.env", "/kernel/main.cpp"):
            with self.subTest(path=path):
                self.assertEqual(self.request(path)[0], 404)
                self.assertEqual(self.request(path, "HEAD")[0], 404)

    def test_no_route_exposes_a_directory(self):
        for path in ROUTES.values():
            self.assertFalse(path.is_dir())
            self.assertTrue(path.is_relative_to(ROOT))

    def test_wasm_has_correct_mime_type(self):
        self.assertEqual(PreviewHandler.extensions_map[".wasm"], "application/wasm")


if __name__ == "__main__":
    unittest.main()
