import { NextRequest, NextResponse } from "next/server";
import { holder, knownFlash, pagePoll, type FlashStatus, type PageStatus } from "@/lib/bench/store";
import { err, requireMember } from "@/lib/tracker/server";

const FLASH_STATES = ["waiting", "flashing", "done", "failed"] as const;

function flashStatus(f: unknown): FlashStatus | undefined {
  if (!f || typeof f !== "object") return undefined;
  const o = f as Record<string, unknown>;
  if (typeof o.id !== "string" || !knownFlash(o.id)) return undefined;
  if (!FLASH_STATES.includes(o.state as FlashStatus["state"])) return undefined;
  const pct = typeof o.pct === "number" && Number.isFinite(o.pct) ? Math.min(100, Math.max(0, Math.round(o.pct))) : 0;
  return { id: o.id, state: o.state as FlashStatus["state"], pct, msg: typeof o.msg === "string" ? o.msg.slice(0, 300) : "" };
}

// The bench page (tester's browser, USB board attached): posts new serial
// lines and its status, gets back the commands queued for the board. One
// page instance holds the bench at a time.
export async function POST(req: NextRequest) {
  const auth = await requireMember();
  if ("res" in auth) return auth.res;
  let body: { instance?: unknown; lines?: unknown; status?: Partial<PageStatus> };
  try {
    body = await req.json();
  } catch {
    return err("Bad JSON");
  }
  if (typeof body.instance !== "string" || !/^[A-Za-z0-9-]{8,64}$/.test(body.instance)) return err("Bad page id");
  const lines = Array.isArray(body.lines) ? body.lines.filter((l): l is string => typeof l === "string") : [];
  const s = body.status ?? {};
  const status: PageStatus = {
    connected: s.connected === true,
    who: auth.user.name,
    chip: typeof s.chip === "string" ? s.chip.slice(0, 60) : undefined,
    flash: flashStatus(s.flash),
  };
  const cmds = pagePoll(`${auth.user.userId}:${body.instance}`, lines, status);
  if (!cmds) return err(`The bench is open in another window (${holder() || "someone else"}). Close that one first.`, 409);
  return NextResponse.json({ cmds });
}
