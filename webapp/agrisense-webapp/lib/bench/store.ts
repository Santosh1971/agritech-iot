// Remote bench: relays one board's USB serial between the tester's browser
// (Web Serial on /dashboard/testing/bench) and the developer's command-line
// tool (tools/bench/bench.py, bearer BENCH_TOKEN). Lives in memory: it is a
// live console, nothing here needs to survive a restart. The web app runs as
// a single Next.js process under pm2, so one Map is enough.
import { randomUUID } from "crypto";

export type BenchLine = { seq: number; t: number; text: string };
export type BenchCmd =
  | { id: string; type: "write"; data: string }
  | { id: string; type: "reset" }
  | { id: string; type: "flash"; fwId: string; name: string; offset: number; size: number };
type NoId<T> = T extends unknown ? Omit<T, "id"> : never;
export type FlashStatus = { id: string; state: "waiting" | "flashing" | "done" | "failed"; pct: number; msg: string };
export type PageStatus = { connected: boolean; who: string; chip?: string; flash?: FlashStatus };

type Bench = {
  lines: BenchLine[];
  nextSeq: number;
  queue: BenchCmd[];
  lastSeen: number;
  status: PageStatus;
  firmware: Map<string, { name: string; bytes: Buffer; at: number }>;
};

const MAX_LINES = 5000;
const MAX_FIRMWARE = 4;

const g = globalThis as unknown as { __bench?: Bench };
function bench(): Bench {
  if (!g.__bench) {
    g.__bench = { lines: [], nextSeq: 1, queue: [], lastSeen: 0, status: { connected: false, who: "" }, firmware: new Map() };
  }
  return g.__bench;
}

/** From the bench page: new serial lines and its status in, queued commands out. */
export function pagePoll(lines: string[], status: PageStatus): BenchCmd[] {
  const b = bench();
  const now = Date.now();
  for (const text of lines.slice(0, 2000)) {
    b.lines.push({ seq: b.nextSeq++, t: now, text: text.slice(0, 2000) });
  }
  if (b.lines.length > MAX_LINES) b.lines.splice(0, b.lines.length - MAX_LINES);
  b.lastSeen = now;
  b.status = status;
  const out = b.queue;
  b.queue = [];
  return out;
}

/** A note in the log from the server side (commands sent, flash requests). */
export function note(text: string) {
  const b = bench();
  b.lines.push({ seq: b.nextSeq++, t: Date.now(), text: `[bench] ${text}` });
}

export function enqueue(cmd: NoId<BenchCmd>): BenchCmd {
  const c = { ...cmd, id: randomUUID() } as BenchCmd;
  bench().queue.push(c);
  return c;
}

export function addFirmware(name: string, bytes: Buffer): string {
  const b = bench();
  const id = randomUUID();
  b.firmware.set(id, { name, bytes, at: Date.now() });
  while (b.firmware.size > MAX_FIRMWARE) {
    const oldest = [...b.firmware.entries()].sort((x, y) => x[1].at - y[1].at)[0][0];
    b.firmware.delete(oldest);
  }
  return id;
}

export function getFirmware(id: string) {
  return bench().firmware.get(id) ?? null;
}

/** For the developer's tool: lines after `after`, plus whether the page is live. */
export function snapshot(after: number, limit = 1000) {
  const b = bench();
  const lines = b.lines.filter((l) => l.seq > after).slice(0, limit);
  return {
    lines,
    nextSeq: b.nextSeq,
    online: Date.now() - b.lastSeen < 5000,
    lastSeenS: b.lastSeen ? Math.round((Date.now() - b.lastSeen) / 1000) : null,
    status: b.status,
    queued: b.queue.length,
  };
}
