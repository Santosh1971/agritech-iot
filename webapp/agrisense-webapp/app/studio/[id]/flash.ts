"use client";
// Write the ASC kit firmware from the browser with Espressif's esptool-js.
// Offsets are the Arduino-ESP32 layout for the ESP32-S3: bootloader at 0x0,
// partition table at 0x8000, app at 0x10000. The OTA data partition at
// 0xe000 is blanked so the chip boots the app just written, whatever was on it
// before. NVS (the saved design) is left alone.
import { ESPLoader, Transport, type IEspLoaderTerminal } from "esptool-js";

export type FirmwareBuild = { id: string; version: string; notes: string | null; full: boolean; sizeBytes: number };

async function part(buildId: string, p: "app" | "bootloader" | "partitions"): Promise<Uint8Array> {
  const res = await fetch(`/api/studio/firmware/${buildId}?part=${p}`);
  if (!res.ok) throw new Error(`Couldn't download the firmware (${p}).`);
  return new Uint8Array(await res.arrayBuffer());
}

export async function flashFirmware(
  port: SerialPort,
  build: FirmwareBuild,
  onProgress: (percent: number) => void,
  onLog: (line: string) => void,
): Promise<void> {
  onLog(`Downloading firmware ${build.version}…`);
  const app = await part(build.id, "app");
  const files: { data: Uint8Array; address: number }[] = [];
  if (build.full) {
    files.push({ data: await part(build.id, "bootloader"), address: 0x0 });
    files.push({ data: await part(build.id, "partitions"), address: 0x8000 });
    files.push({ data: new Uint8Array(0x2000).fill(0xff), address: 0xe000 });
  }
  files.push({ data: app, address: 0x10000 });
  const total = files.reduce((n, f) => n + f.data.length, 0);
  const before = files.map((_, i) => files.slice(0, i).reduce((n, f) => n + f.data.length, 0));

  const terminal: IEspLoaderTerminal = {
    clean() {},
    writeLine: (d: string) => onLog(d),
    write: () => {},
  };
  const transport = new Transport(port, false);
  try {
    const loader = new ESPLoader({ transport, baudrate: 460800, romBaudrate: 115200, terminal });
    onLog("Putting the board into download mode…");
    const chip = await loader.main();
    onLog(`Connected to ${chip}.`);
    if (!/ESP32-S3/i.test(chip)) throw new Error(`This is a ${chip}. The ASC kits use an ESP32-S3.`);
    await loader.writeFlash({
      fileArray: files,
      flashMode: "keep",
      flashFreq: "keep",
      flashSize: "keep",
      eraseAll: false,
      compress: true,
      reportProgress: (i, written) => onProgress(Math.round(((before[i] + written) / total) * 100)),
    });
    onLog("Written. Restarting the board…");
    await loader.after("hard_reset");
  } finally {
    await transport.disconnect().catch(() => {});
  }
}
