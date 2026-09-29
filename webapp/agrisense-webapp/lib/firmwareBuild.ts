import { spawn } from "child_process";
import { randomBytes, timingSafeEqual } from "crypto";
import { mkdir, readFile, rm, writeFile } from "fs/promises";
import { homedir } from "os";
import { join } from "path";

// Compiles ESP32 sketches that workshop students paste from Claude, so they can
// flash them from the browser (/workshop/flash) without installing Arduino.
//
// Security: the sketch is compiled as this server's user, so it could try to pull
// server files into the firmware (#include "/path", asm(".incbin ...")). The
// filter below rejects every way we know of to reference a file outside the
// public core/library include paths, the endpoint needs the workshop access
// code, builds run one at a time with a time limit, and the compiler gets a
// minimal environment (no app secrets).

const TOOLS_BIN = process.env.ARDUINO_CLI_BIN || join(homedir(), "tools", "bin", "arduino-cli");
const BUILDS_DIR = join(process.env.WORKSHOP_SURVEY_DIR || join(homedir(), "agrisense-data"), "builds");
// ESP32 Dev Module with the "Huge APP" partition layout (3 MB app, no OTA), so a
// student's program that combines Bluetooth and WiFi still fits.
const FQBN = "esp32:esp32:esp32:PartitionScheme=huge_app";
const BUILD_TIMEOUT_MS = 4 * 60 * 1000;
const KEEP_JOBS_MS = 2 * 60 * 60 * 1000;
const MAX_SOURCE = 100_000;

export type BuildJob = {
  id: string;
  status: "queued" | "building" | "done" | "error";
  log: string;
  createdAt: number;
  finishedAt?: number;
  binSize?: number;
};

type State = { jobs: Map<string, BuildJob>; queue: string[]; running: boolean };
const g = globalThis as unknown as { __fwBuild?: State };
const state: State = (g.__fwBuild ??= { jobs: new Map(), queue: [], running: false });

export function checkAccess(code: string | null): boolean {
  const expected = process.env.WORKSHOP_BUILD_CODE;
  if (!expected || !code) return false;
  const a = Buffer.from(code.trim().toLowerCase());
  const b = Buffer.from(expected.trim().toLowerCase());
  return a.length === b.length && timingSafeEqual(a, b);
}

// Returns a message explaining why the sketch is refused, or null if it is allowed.
export function checkSketch(source: string): string | null {
  if (source.length > MAX_SOURCE) return "The program is too long (limit 100 KB).";
  if (!/\bsetup\s*\(/.test(source) || !/\bloop\s*\(/.test(source)) {
    return "This does not look like a complete Arduino program: it needs setup() and loop(). Copy the whole code from Claude.";
  }
  const joined = source.replace(/\\\r?\n/g, "");
  const noComments = joined.replace(/\/\*[\s\S]*?\*\//g, " ").replace(/\/\/[^\n]*/g, "");
  for (const text of [joined, noComments]) {
    if (/incbin/i.test(text)) return "Not allowed in the workshop builder: .incbin";
    if (/\b(asm|__asm|__asm__|_Pragma)\b/.test(text)) return "Not allowed in the workshop builder: inline assembly or _Pragma.";
    for (const rawLine of text.split("\n")) {
      const line = rawLine.trim();
      const m = line.match(/^(#|%:|\?\?=)\s*([A-Za-z_]+)(.*)$/);
      if (!m) continue;
      const directive = m[2];
      const rest = m[3];
      if (directive === "include") {
        const inc = rest.match(/^\s*(<([A-Za-z0-9_\-./]+)>|"([A-Za-z0-9_\-./]+)")\s*$/);
        const path = inc && (inc[2] || inc[3]);
        if (!path || path.startsWith("/") || path.includes("..")) {
          return `Not allowed in the workshop builder: ${line.slice(0, 80)} (only library includes like #include <WiFi.h>)`;
        }
      } else if (["include_next", "import", "embed"].includes(directive)) {
        return `Not allowed in the workshop builder: #${directive}`;
      } else if (directive === "pragma" && /dependency|\//.test(rest)) {
        return "Not allowed in the workshop builder: this #pragma";
      }
    }
  }
  return null;
}

export function getJob(id: string): BuildJob | undefined {
  return state.jobs.get(id);
}

export function queuePosition(id: string): number {
  const i = state.queue.indexOf(id);
  return i < 0 ? 0 : i + 1;
}

// The merged image is padded with 0xFF to the full 4 MB flash; drop the padding
// (erased flash already reads 0xFF) so browser flashing takes seconds, not minutes.
export async function readBin(id: string): Promise<Buffer | null> {
  let bin: Buffer;
  try {
    bin = await readFile(join(BUILDS_DIR, id, "out", "sketch.ino.merged.bin"));
  } catch {
    return null;
  }
  let end = bin.length;
  while (end > 0 && bin[end - 1] === 0xff) end--;
  return bin.subarray(0, Math.min(bin.length, Math.ceil(end / 4) * 4));
}

// The app image alone (no bootloader/partition table): what an over-the-air update installs.
export async function readAppBin(id: string): Promise<Buffer | null> {
  try {
    return await readFile(join(BUILDS_DIR, id, "out", "sketch.ino.bin"));
  } catch {
    return null;
  }
}

export async function enqueueBuild(source: string): Promise<BuildJob> {
  prune();
  const id = randomBytes(8).toString("hex");
  const job: BuildJob = { id, status: "queued", log: "", createdAt: Date.now() };
  state.jobs.set(id, job);
  const sketchDir = join(BUILDS_DIR, id, "sketch");
  await mkdir(sketchDir, { recursive: true });
  await writeFile(join(sketchDir, "sketch.ino"), source, "utf8");
  state.queue.push(id);
  void runQueue();
  return job;
}

async function runQueue() {
  if (state.running) return;
  state.running = true;
  try {
    while (state.queue.length) {
      const id = state.queue[0];
      const job = state.jobs.get(id);
      if (job) await compile(job);
      state.queue.shift();
    }
  } finally {
    state.running = false;
  }
}

function compile(job: BuildJob): Promise<void> {
  job.status = "building";
  const dir = join(BUILDS_DIR, job.id);
  return new Promise((resolve) => {
    const child = spawn(
      TOOLS_BIN,
      ["compile", "--fqbn", FQBN, "--jobs", "2", "--output-dir", join(dir, "out"), join(dir, "sketch")],
      {
        env: { PATH: `${join(homedir(), "tools", "bin")}:/usr/bin:/bin`, HOME: homedir(), LANG: "C.UTF-8" } as unknown as NodeJS.ProcessEnv,
        cwd: dir,
      },
    );
    const append = (chunk: Buffer) => {
      job.log = (job.log + chunk.toString()).slice(-20_000);
    };
    child.stdout.on("data", append);
    child.stderr.on("data", append);
    const timer = setTimeout(() => {
      append(Buffer.from("\nBuild took too long and was stopped."));
      child.kill("SIGKILL");
    }, BUILD_TIMEOUT_MS);
    child.on("close", async (code) => {
      clearTimeout(timer);
      // Error messages show paths on our server; show only the file name.
      job.log = job.log.split(join(dir, "sketch") + "/").join("").split(homedir()).join("~");
      const bin = code === 0 ? await readBin(job.id) : null;
      job.status = bin ? "done" : "error";
      job.binSize = bin?.length;
      job.finishedAt = Date.now();
      await rm(join(dir, "sketch"), { recursive: true, force: true });
      resolve();
    });
    child.on("error", (err) => {
      clearTimeout(timer);
      append(Buffer.from(`\nCould not start the compiler: ${err.message}`));
      job.status = "error";
      job.finishedAt = Date.now();
      resolve();
    });
  });
}

function prune() {
  const now = Date.now();
  for (const [id, job] of state.jobs) {
    if (job.finishedAt && now - job.finishedAt > KEEP_JOBS_MS) {
      state.jobs.delete(id);
      void rm(join(BUILDS_DIR, id), { recursive: true, force: true });
    }
  }
}
