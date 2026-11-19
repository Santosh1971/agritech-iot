"use client";
// Flash a product's released firmware by USB cable, in the browser (Chrome or
// Edge on a laptop: Web Serial), with Espressif's esptool-js. Same builds,
// grant and per-device checks as the NB Agri Flasher app (/api/flasher/*), so
// the flash shows up in the flasher activity log like an app flash.
import { ESPLoader, Transport, type IEspLoaderTerminal } from "esptool-js";
import { USB_PRODUCTS, bootloaderOffset, chipForVariant, deviceIdFor } from "./products";

export type Build = {
  id: string;
  version: string;
  variant: string;
  sizeBytes: number;
  notes: string | null;
  createdAt: string;
  hasFullFlash: boolean;
};

export type FlashOptions = {
  product: string;
  build: Build;
  full: boolean; // also write bootloader + partition table (blank module, admins only)
  eraseAll: boolean; // wipe the whole chip first: node settings, WiFi, history (needs full)
  onProgress: (percent: number) => void;
  onLog: (line: string) => void;
};

export type FlashResult = { mac: string; deviceId: string | null };

async function download(buildId: string, query: string): Promise<Uint8Array> {
  const res = await fetch(`/api/flasher/download/${buildId}?${query}`, { cache: "no-store" });
  if (!res.ok) {
    const j = await res.json().catch(() => ({}));
    throw new Error(j.error ?? `Download failed (${res.status}).`);
  }
  return new Uint8Array(await res.arrayBuffer());
}

export async function report(buildId: string, result: "flash_ok" | "flash_failed", mac: string | null, detail?: string) {
  await fetch("/api/flasher/report", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ buildId, result, mac: mac ?? undefined, detail: detail ? `USB web flasher: ${detail}` : "USB web flasher" }),
  }).catch(() => {});
}

export async function flashOverUsb(port: SerialPort, o: FlashOptions): Promise<FlashResult> {
  const name = USB_PRODUCTS[o.product];
  if (!name) throw new Error(`${o.product} can't be flashed from this page.`);
  const want = chipForVariant(o.build.variant);
  if (o.eraseAll && !o.full) throw new Error("Erase everything also needs the bootloader and partition table.");

  const terminal: IEspLoaderTerminal = { clean() {}, writeLine: (d: string) => o.onLog(d), write: () => {} };
  const transport = new Transport(port, false);
  let mac: string | null = null;
  try {
    const loader = new ESPLoader({ transport, baudrate: 460800, romBaudrate: 115200, terminal });
    o.onLog("Putting the board into download mode…");
    const chip = await loader.main();
    o.onLog(`Connected to ${chip}.`);
    if (loader.chip.CHIP_NAME !== want) {
      throw new Error(
        `This board is an ${loader.chip.CHIP_NAME}, but ${name} ${o.build.variant} is built for an ${want}. Wrong board, product or variant?`,
      );
    }
    mac = await loader.chip.readMac(loader);
    const deviceId = deviceIdFor(o.product, mac);
    o.onLog(`MAC ${mac}${deviceId ? `, device ${deviceId}` : ""}.`);

    // Downloading with the MAC checks this unit is registered (and registers
    // it on an admin's first flash) and logs the download. Products with no
    // MAC-based ID go without it, as the app's WiFi flow does.
    o.onLog(`Downloading firmware ${o.build.version} (${o.build.variant})…`);
    const app = await download(o.build.id, deviceId ? `mac=${encodeURIComponent(mac)}` : "");
    const files: { data: Uint8Array; address: number }[] = [];
    if (o.full) {
      files.push({ data: await download(o.build.id, "part=bootloader"), address: bootloaderOffset(want) });
      files.push({ data: await download(o.build.id, "part=partitions"), address: 0x8000 });
    }
    // Blank OTA data so the chip boots the app written to 0x10000, even if a
    // WiFi update had last switched it to the second slot.
    files.push({ data: new Uint8Array(0x2000).fill(0xff), address: 0xe000 });
    files.push({ data: app, address: 0x10000 });

    if (o.eraseAll) {
      o.onLog("Erasing the whole chip (about 10 s)…");
      await loader.eraseFlash();
    }
    const total = files.reduce((n, f) => n + f.data.length, 0);
    const before = files.map((_, i) => files.slice(0, i).reduce((n, f) => n + f.data.length, 0));
    o.onLog(o.full ? "Writing bootloader, partition table and firmware…" : "Writing firmware (settings are kept)…");
    await loader.writeFlash({
      fileArray: files,
      flashMode: "keep",
      flashFreq: "keep",
      flashSize: "keep",
      eraseAll: false,
      compress: true,
      reportProgress: (i, written) => o.onProgress(Math.round(((before[i] + written) / total) * 100)),
    });
    o.onLog("Written. Restarting the board…");
    await loader.after("hard_reset");
    return { mac, deviceId };
  } catch (e) {
    const err = e as Error & { mac?: string | null };
    err.mac = mac;
    throw err;
  } finally {
    await transport.disconnect().catch(() => {});
  }
}

// Restart the board and read its serial output (115200) for `seconds`, so the
// boot log (firmware version, device ID, SoftAP name and password) shows up.
// A classic ESP32 DevKit's auto-reset: RTS pulls EN low, DTR pulls GPIO0 low,
// so restart with RTS only and leave DTR released (normal boot, not download).
export async function bootLog(
  port: SerialPort,
  onText: (text: string) => void,
  seconds: number,
  stop: { stopped: boolean },
): Promise<void> {
  await port.open({ baudRate: 115200, bufferSize: 16384 });
  try {
    await port.setSignals({ dataTerminalReady: false, requestToSend: true });
    await new Promise((r) => setTimeout(r, 120));
    await port.setSignals({ dataTerminalReady: false, requestToSend: false });
    const reader = port.readable!.getReader();
    const dec = new TextDecoder();
    const timer = setTimeout(() => reader.cancel().catch(() => {}), seconds * 1000);
    const poll = setInterval(() => {
      if (stop.stopped) reader.cancel().catch(() => {});
    }, 200);
    try {
      for (;;) {
        const { value, done } = await reader.read();
        if (done) break;
        if (value) onText(dec.decode(value, { stream: true }));
      }
    } finally {
      clearTimeout(timer);
      clearInterval(poll);
      reader.releaseLock();
    }
  } finally {
    await port.close().catch(() => {});
  }
}
