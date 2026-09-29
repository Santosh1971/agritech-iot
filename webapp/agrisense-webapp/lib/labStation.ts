import { timingSafeEqual } from "crypto";
import { appendFile, mkdir, readFile, rm, stat, writeFile, copyFile } from "fs/promises";
import { homedir } from "os";
import { join } from "path";

// The "Lab Station": one workshop ESP32 gifted to the college. It uploads its
// temperature/humidity/flow log here and asks here for firmware updates (FOTA).
// Everything is stored as files under ~/agrisense-data/lab, outside the repo.
//
// Devices authenticate with LAB_DEVICE_TOKEN (compiled into the Lab Station
// firmware on the server, never committed).

const ROOT = join(process.env.WORKSHOP_SURVEY_DIR || join(homedir(), "agrisense-data"), "lab");

export const DEVICE_RE = /^FarmIoT-[0-9A-F]{4}$/;

export function checkDeviceToken(token: string | null): boolean {
  const expected = process.env.LAB_DEVICE_TOKEN;
  if (!expected || !token) return false;
  const a = Buffer.from(token);
  const b = Buffer.from(expected);
  return a.length === b.length && timingSafeEqual(a, b);
}

const dir = (device: string) => join(ROOT, "devices", device);

export type Reading = { ts: number; t: number | null; h: number | null; f: number | null; l: number | null };
export type DeviceInfo = { device: string; version?: string; ip?: string; ssid?: string; rssi?: number; lastSeen?: string; program?: string };
export type PendingUpdate = { id: string; label: string; size: number; queuedAt: string; queuedBy: string };

export async function saveReadings(device: string, rows: Reading[]): Promise<number> {
  await mkdir(dir(device), { recursive: true });
  const clean = rows
    .filter((r) => Number.isFinite(r.ts) && r.ts > 1_600_000_000 && r.ts < 4_000_000_000)
    .slice(0, 2000)
    .map((r) => ({
      ts: Math.round(r.ts),
      t: Number.isFinite(r.t) ? r.t : null,
      h: Number.isFinite(r.h) ? r.h : null,
      f: Number.isFinite(r.f) ? r.f : null,
      l: Number.isFinite(r.l) ? r.l : null,
    }));
  if (clean.length) await appendFile(join(dir(device), "readings.jsonl"), clean.map((r) => JSON.stringify(r)).join("\n") + "\n");
  return clean.length;
}

export async function readReadings(device: string, sinceTs: number): Promise<Reading[]> {
  let text = "";
  try {
    text = await readFile(join(dir(device), "readings.jsonl"), "utf8");
  } catch {
    return [];
  }
  const out: Reading[] = [];
  for (const line of text.split("\n")) {
    if (!line) continue;
    try {
      const r = JSON.parse(line) as Reading;
      if (r.ts >= sinceTs) out.push(r);
    } catch {
      /* skip damaged line */
    }
  }
  // de-duplicate (a device may resend rows after a failed upload) and sort
  const byTs = new Map(out.map((r) => [r.ts, r]));
  return [...byTs.values()].sort((a, b) => a.ts - b.ts);
}

export async function saveInfo(info: DeviceInfo): Promise<void> {
  await mkdir(dir(info.device), { recursive: true });
  await writeFile(join(dir(info.device), "info.json"), JSON.stringify({ ...info, lastSeen: new Date().toISOString() }));
}

export async function readInfo(device: string): Promise<DeviceInfo | null> {
  try {
    return JSON.parse(await readFile(join(dir(device), "info.json"), "utf8"));
  } catch {
    return null;
  }
}

export async function listDevices(): Promise<string[]> {
  const { readdir } = await import("fs/promises");
  try {
    return (await readdir(join(ROOT, "devices"))).filter((d) => DEVICE_RE.test(d));
  } catch {
    return [];
  }
}

// ---- firmware updates (the app image, not the merged USB image) ----

export async function queueUpdate(device: string, appBin: Buffer, label: string, by: string): Promise<PendingUpdate> {
  const id = Date.now().toString(36);
  await mkdir(dir(device), { recursive: true });
  await writeFile(join(dir(device), "update.bin"), appBin);
  const pending: PendingUpdate = { id, label, size: appBin.length, queuedAt: new Date().toISOString(), queuedBy: by };
  await writeFile(join(dir(device), "update.json"), JSON.stringify(pending));
  return pending;
}

export async function queueUpdateFromFile(device: string, file: string, label: string, by: string): Promise<PendingUpdate> {
  const id = Date.now().toString(36);
  await mkdir(dir(device), { recursive: true });
  await copyFile(file, join(dir(device), "update.bin"));
  const size = (await stat(join(dir(device), "update.bin"))).size;
  const pending: PendingUpdate = { id, label, size, queuedAt: new Date().toISOString(), queuedBy: by };
  await writeFile(join(dir(device), "update.json"), JSON.stringify(pending));
  return pending;
}

export async function pendingUpdate(device: string): Promise<PendingUpdate | null> {
  try {
    return JSON.parse(await readFile(join(dir(device), "update.json"), "utf8"));
  } catch {
    return null;
  }
}

export async function updateBin(device: string): Promise<Buffer | null> {
  try {
    return await readFile(join(dir(device), "update.bin"));
  } catch {
    return null;
  }
}

export async function clearUpdate(device: string, result: string): Promise<void> {
  const p = await pendingUpdate(device);
  await rm(join(dir(device), "update.json"), { force: true });
  await rm(join(dir(device), "update.bin"), { force: true });
  if (p) await appendFile(join(dir(device), "updates.log"), JSON.stringify({ ...p, result, doneAt: new Date().toISOString() }) + "\n");
}

// The Lab Station firmware itself, built on the server with the secrets
// (see LAB_STATION_DIR/build.sh): app image for FOTA, merged image for USB.
export const LAB_FIRMWARE_DIR = join(ROOT, "firmware");
