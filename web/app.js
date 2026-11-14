import { V86 } from "/vendor/libv86.mjs";

const screen = document.getElementById("screen_container");
const overlay = document.getElementById("boot-overlay");
const status = document.getElementById("status");
const statusText = document.getElementById("status-text");
const pauseButton = document.getElementById("pause");
const restartButton = document.getElementById("restart");
const captureButton = document.getElementById("capture");
const appButtons = [...document.querySelectorAll("[data-key]")];
let emulator;
let ready = false;
let paused = false;
let bootTimeout;
let serial = "";

function setStatus(message, error = false) {
  statusText.textContent = message;
  status.classList.toggle("error", error);
}

function fail(message) {
  clearTimeout(bootTimeout);
  ready = false;
  overlay.hidden = false;
  overlay.classList.add("failed");
  document.getElementById("boot-title").textContent = "Не удалось запустить ОС";
  document.getElementById("boot-message").textContent = message;
  setStatus("ОШИБКА ЗАГРУЗКИ", true);
  pauseButton.disabled = true;
  captureButton.disabled = true;
  appButtons.forEach((button) => {
    button.disabled = true;
  });
}

function watchBoot() {
  clearTimeout(bootTimeout);
  bootTimeout = setTimeout(
    () => fail("Обнови страницу или запусти образ в QEMU."),
    45000,
  );
}

function onReady() {
  clearTimeout(bootTimeout);
  ready = true;
  emulator.keyboard_set_enabled(document.activeElement === screen);
  paused = false;
  overlay.hidden = true;
  overlay.classList.remove("failed");
  setStatus("ЯДРО РАБОТАЕТ");
  pauseButton.disabled = false;
  restartButton.disabled = false;
  captureButton.disabled = false;
  appButtons.forEach((button) => {
    button.disabled = false;
  });
  pauseButton.querySelector("span").textContent = "Пауза";
}

async function boot() {
  try {
    // Fail visibly if a local dependency is missing, instead of leaving a black screen.
    const paths = [
      "/xp96.img",
      "/bios/seabios.bin",
      "/bios/vgabios.bin",
      "/vendor/v86.wasm",
    ];
    await Promise.all(
      paths.map(async (path) => {
        const response = await fetch(path, { method: "HEAD" });
        if (!response.ok)
          throw new Error(`Не найден файл ${path}. См. инструкцию в README.`);
      }),
    );
    emulator = new V86({
      wasm_path: "/vendor/v86.wasm",
      bios: { url: "/bios/seabios.bin" },
      vga_bios: { url: "/bios/vgabios.bin" },
      hda: { url: "/xp96.img", async: false },
      memory_size: 64 * 1024 * 1024,
      vga_memory_size: 8 * 1024 * 1024,
      screen_container: screen,
      autostart: true,
      disable_speaker: true,
      log_level: 0,
    });
    emulator.keyboard_set_enabled(false);
    emulator.add_listener("serial0-output-byte", (byte) => {
      serial = (serial + String.fromCharCode(byte)).slice(-4096);
      if (serial.endsWith("XP96:DESKTOP_READY\n")) onReady();
      if (serial.includes("XP96:PANIC"))
        fail("Ядро остановлено. Перезагрузи эмулятор.");
    });
    watchBoot();
  } catch (error) {
    fail(error.message || "Браузер не поддерживает WebAssembly.");
  }
}

screen.addEventListener("pointerdown", () => {
  if (!emulator || !ready) return;
  emulator.keyboard_set_enabled(true);
  screen.focus({ preventScroll: true });
});
// Keyboard navigation to the VM should work without requiring a mouse click.
screen.addEventListener("focus", () => {
  if (ready) emulator?.keyboard_set_enabled(true);
});
screen.addEventListener("blur", () => {
  emulator?.keyboard_set_enabled(false);
});
document.addEventListener("pointerdown", (event) => {
  if (!screen.contains(event.target)) emulator?.keyboard_set_enabled(false);
});

for (const button of appButtons) {
  button.addEventListener("click", () => {
    if (!ready || paused) return;
    const scan = Number(button.dataset.key);
    emulator.keyboard_send_scancodes([scan, scan | 0x80]);
    screen.focus({ preventScroll: true });
  });
}

pauseButton.addEventListener("click", async () => {
  if (!ready) return;
  pauseButton.disabled = true;
  try {
    if (paused) await emulator.run();
    else await emulator.stop();
    paused = !paused;
    pauseButton.querySelector("span").textContent = paused
      ? "Продолжить"
      : "Пауза";
    pauseButton.setAttribute(
      "aria-label",
      paused ? "Продолжить эмуляцию" : "Приостановить эмулятор",
    );
    setStatus(paused ? "ЭМУЛЯЦИЯ НА ПАУЗЕ" : "ЯДРО РАБОТАЕТ");
    appButtons.forEach((button) => {
      button.disabled = paused;
    });
    captureButton.disabled = paused;
  } catch (error) {
    fail(error.message);
  } finally {
    if (ready) pauseButton.disabled = false;
  }
});

restartButton.addEventListener("click", async () => {
  if (!emulator || !confirm("Заметки в ОЗУ будут потеряны. Перезагрузить ОС?"))
    return;
  ready = false;
  serial = "";
  overlay.hidden = false;
  overlay.classList.remove("failed");
  document.getElementById("boot-title").textContent = "Начнём ещё раз.";
  document.getElementById("boot-message").textContent =
    "BIOS → загрузчик → твоё ядро";
  setStatus("ПЕРЕЗАГРУЗКА");
  pauseButton.disabled = true;
  captureButton.disabled = true;
  appButtons.forEach((button) => {
    button.disabled = true;
  });
  emulator.restart();
  if (paused) await emulator.run();
  watchBoot();
});

captureButton.addEventListener("click", async () => {
  if (!ready || paused) return;
  screen.focus({ preventScroll: true });
  try {
    await emulator.lock_mouse();
  } catch {
    setStatus("ЗАХВАТ НЕДОСТУПЕН — МЫШЬ РАБОТАЕТ И БЕЗ НЕГО");
  }
});

document.getElementById("fullscreen").addEventListener("click", async () => {
  try {
    if (document.fullscreenElement) await document.exitFullscreen();
    else await document.getElementById("vm-shell").requestFullscreen();
  } catch {
    setStatus("ПОЛНЫЙ ЭКРАН НЕДОСТУПЕН В ЭТОМ БРАУЗЕРЕ");
  }
});

window.addEventListener("beforeunload", () => {
  emulator?.stop();
});
boot();
