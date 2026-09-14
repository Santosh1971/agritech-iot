import { createHash, randomUUID } from "crypto";
import { mkdir, readFile, unlink, writeFile } from "fs/promises";
import path from "path";

// Firmware bins live on local disk, same "local on this VPS" pattern as
// Postgres and Mosquitto (see .env.example) — no S3/object storage needed
// for what's a handful of admins uploading builds a few KB to ~1MB each.
const STORAGE_ROOT = process.env.FIRMWARE_STORAGE_DIR || path.join(process.cwd(), "storage", "firmware");

export function firmwareStoragePath(relativePath: string): string {
  return path.join(STORAGE_ROOT, relativePath);
}

/** Saves a build's bytes under a fresh random name and returns (relativePath, sha256, sizeBytes). */
export async function saveFirmwareBuild(
  bytes: Buffer
): Promise<{ storagePath: string; sha256: string; sizeBytes: number }> {
  await mkdir(STORAGE_ROOT, { recursive: true });

  const storagePath = `${randomUUID()}.bin`;
  await writeFile(firmwareStoragePath(storagePath), bytes);

  const sha256 = createHash("sha256").update(bytes).digest("hex");
  return { storagePath, sha256, sizeBytes: bytes.length };
}

export async function readFirmwareBuild(storagePath: string): Promise<Buffer> {
  return readFile(firmwareStoragePath(storagePath));
}

/** Best-effort — a missing file (already gone, or never written) isn't an error here. */
export async function deleteFirmwareBuild(storagePath: string): Promise<void> {
  await unlink(firmwareStoragePath(storagePath)).catch(() => {});
}
