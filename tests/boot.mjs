// Integration test: execute the real disk image on a WebAssembly x86 CPU.
// v86 internals are used only for framebuffer assertions and synthetic mouse input.
import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { deflateSync } from "node:zlib";
import { V86 } from "v86";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const delay = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
const image = (name) => ({
  buffer: Uint8Array.from(fs.readFileSync(path.join(root, name))).buffer,
});
let serial = "";
const watchers = new Set();
const emulator = new V86({
  wasm_path: path.join(root, "node_modules/v86/build/v86.wasm"),
  bios: image(".cache/bios/seabios.bin"),
  vga_bios: image(".cache/bios/vgabios.bin"),
  hda: image("build/xp96.img"),
  memory_size: 64 * 1024 * 1024,
  vga_memory_size: 8 * 1024 * 1024,
  autostart: true,
  log_level: 0,
});
emulator.add_listener("serial0-output-byte", (value) => {
  serial += String.fromCharCode(value);
  for (const watcher of watchers) watcher();
});

function waitFor(expected, offset = 0, timeout = 10000) {
  return new Promise((resolve, reject) => {
    const finish = (error) => {
      clearTimeout(timer);
      watchers.delete(check);
      if (error) reject(error);
      else resolve();
    };
    const check = () => {
      const output = serial.slice(offset);
      if (output.includes("XP96:PANIC")) finish(new Error(output));
      else if (
        typeof expected === "string"
          ? output.includes(expected)
          : expected.test(output)
      )
        finish();
    };
    const timer = setTimeout(
      () =>
        finish(
          new Error(`Timeout waiting for ${expected}\n${serial.slice(-2500)}`),
        ),
      timeout,
    );
    watchers.add(check);
    check();
  });
}

async function expectAfter(action, expected) {
  const pending = waitFor(expected, serial.length);
  await Promise.all([pending, action()]);
}
async function hotkey(scan, app) {
  await expectAfter(
    () => emulator.keyboard_send_scancodes([scan, scan | 0x80]),
    `XP96:OPEN ${app}\n`,
  );
}
async function command(value, expected) {
  await expectAfter(() => emulator.keyboard_send_text(value + "\n"), expected);
}
function vga() {
  return emulator.v86.cpu.devices.vga;
}
function pixel(x, y) {
  const device = vga();
  const index = (y * device.svga_width + x) * (device.svga_bpp / 8);
  const data = device.svga_memory;
  return (data[index + 2] << 16) | (data[index + 1] << 8) | data[index];
}

// A tiny PNG writer keeps the integration test dependency-free beyond v86.
function crc32(buffer) {
  let crc = 0xffffffff;
  for (const byte of buffer) {
    crc ^= byte;
    for (let bit = 0; bit < 8; ++bit)
      crc = (crc >>> 1) ^ (crc & 1 ? 0xedb88320 : 0);
  }
  return (crc ^ 0xffffffff) >>> 0;
}
function chunk(type, data) {
  const name = Buffer.from(type);
  const size = Buffer.alloc(4);
  size.writeUInt32BE(data.length);
  const crc = Buffer.alloc(4);
  crc.writeUInt32BE(crc32(Buffer.concat([name, data])));
  return Buffer.concat([size, name, data, crc]);
}
function screenshot(filename) {
  const device = vga();
  const { svga_width: width, svga_height: height, svga_bpp: bpp } = device;
  const rows = Buffer.alloc(height * (width * 3 + 1));
  for (let y = 0; y < height; ++y) {
    for (let x = 0; x < width; ++x) {
      const source = (y * width + x) * (bpp / 8);
      const target = y * (width * 3 + 1) + 1 + x * 3;
      rows[target] = device.svga_memory[source + 2];
      rows[target + 1] = device.svga_memory[source + 1];
      rows[target + 2] = device.svga_memory[source];
    }
  }
  const header = Buffer.alloc(13);
  header.writeUInt32BE(width, 0);
  header.writeUInt32BE(height, 4);
  header[8] = 8;
  header[9] = 2;
  fs.writeFileSync(
    filename,
    Buffer.concat([
      Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]),
      chunk("IHDR", header),
      chunk("IDAT", deflateSync(rows)),
      chunk("IEND", Buffer.alloc(0)),
    ]),
  );
}

let mouseX = 682;
let mouseY = 450;
async function moveTo(x, y) {
  while (mouseX !== x || mouseY !== y) {
    const dx = Math.max(-100, Math.min(100, x - mouseX));
    const dy = Math.max(-100, Math.min(100, y - mouseY));
    emulator.bus.send("mouse-delta", [dx, -dy]);
    mouseX += dx;
    mouseY += dy;
  }
  await delay(100);
}
async function clickAt(x, y) {
  await moveTo(x, y);
  emulator.bus.send("mouse-click", [true, false, false]);
  await delay(60);
  emulator.bus.send("mouse-click", [false, false, false]);
  await delay(100);
}

try {
  await waitFor("XP96:DESKTOP_READY\n", 0, 25000);
  assert.match(serial, /XP96:INPUT_READY keyboard\+mouse/);
  assert.equal(emulator.v86.cpu.cr[0] & 1, 1, "CPU must be in protected mode");
  assert.equal(vga().svga_width, 800);
  assert.equal(vga().svga_height, 600);
  assert.ok([24, 32].includes(vga().svga_bpp));
  assert.equal(pixel(0, 0), 0x76b9e8, "Sky must be rendered by the kernel");
  assert.equal(pixel(300, 410), 0xf8f7f2, "Welcome window must be rendered");
  screenshot(path.join(root, "build/desktop.png"));
  if (process.argv.includes("--update-screenshot"))
    screenshot(path.join(root, "docs/desktop.png"));
  console.log("PASS BIOS boot -> protected mode -> real framebuffer desktop");

  await clickAt(60, 580);
  assert.equal(pixel(6, 300), 0xffffff, "PS/2 click must open Start");
  screenshot(path.join(root, "build/start-menu.png"));
  await expectAfter(() => clickAt(100, 392), "XP96:OPEN terminal\n");
  console.log("PASS PS/2 mouse -> Start menu -> Terminal");

  await command("help", "XP96:TERM notes    reboot\n");
  await command("about", "Python build tools. Not Microsoft Windows.\n");
  await command(
    "info",
    "No processes, disk filesystem, networking or heap yet.\n",
  );
  await command("uptime", /XP96:TERM Uptime: \d+ seconds\n/);
  await command(
    "ls",
    "These are built-in virtual files, not files on a disk.\n",
  );
  await command("cat SYSTEM.TXT", "Assembly handles boot/IRQs.\n");
  await command(
    "cat missing",
    "File not found. Use ls; names are case-sensitive.\n",
  );
  await command("not-a-command", "Unknown command. Type help.\n");
  await command("echo OOPS\b\b\b\bPASS", "XP96:TERM PASS\n");
  await command(
    "echo " + "x".repeat(160),
    "XP96:TERM " + "x".repeat(122) + "\n",
  );
  for (let i = 0; i < 52; ++i)
    await command(`echo HISTORY_${i}`, `XP96:TERM HISTORY_${i}\n`);
  await expectAfter(
    () =>
      emulator.keyboard_send_scancodes([0xe0, 0x48, 0xe0, 0xc8, 0x1c, 0x9c]),
    "XP96:TERM HISTORY_51\n",
  );
  console.log(
    "PASS shell commands, keyboard translation, backspace, history, bounded input and scrolling",
  );

  await hotkey(0x3e, "notes");
  await emulator.keyboard_send_text("\nBOOT_TEST_NOTE!\b?\n");
  await emulator.keyboard_send_scancodes([0x1d, 0x1f, 0x9f, 0x9d]); // Ctrl+S: honest RAM-only notice.
  await hotkey(0x3d, "terminal");
  await command("cat NOTES.TXT", "BOOT_TEST_NOTE?\n");
  await delay(100);
  assert.equal(pixel(180, 200), 0x121e30);
  await clickAt(730, 125); // Minimize terminal.
  assert.notEqual(pixel(180, 200), 0x121e30);
  await clickAt(345, 580); // Restore terminal from taskbar.
  assert.equal(pixel(180, 200), 0x121e30);
  await expectAfter(
    () => emulator.keyboard_send_scancodes([0x38, 0x3e, 0xbe, 0xb8]),
    "XP96:CLOSE terminal\n",
  );
  console.log("PASS RAM notes, Ctrl+S notice, minimize/restore, Alt+F4");

  await hotkey(0x3c, "files");
  await expectAfter(() => clickAt(340, 301), "XP96:OPEN system\n");
  await hotkey(0x3b, "welcome");
  await hotkey(0x3f, "system");
  await hotkey(0x3d, "terminal");
  await command("clear", "XP96:CMD clear\n");
  await command("echo STILL_ALIVE", "XP96:TERM STILL_ALIVE\n");
  console.log("PASS file browser, every application shortcut and clear");

  await command("reboot", "XP96:DESKTOP_READY\n");
  await hotkey(0x3d, "terminal");
  const offset = serial.length;
  await command("cat NOTES.TXT", "Nothing is saved to disk yet.\n");
  assert.ok(
    !serial.slice(offset).includes("BOOT_TEST_NOTE"),
    "Reboot must clear RAM-only notes",
  );
  assert.ok(!serial.includes("XP96:PANIC"));
  console.log("PASS reboot -> second clean boot; RAM changes do not persist");
} catch (error) {
  console.error(error);
  process.exitCode = 1;
} finally {
  fs.writeFileSync(path.join(root, "build/boot-test.log"), serial);
  await emulator.destroy();
}
