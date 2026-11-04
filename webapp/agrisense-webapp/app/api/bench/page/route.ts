import { NextRequest, NextResponse } from "next/server";
import { pagePoll, type PageStatus } from "@/lib/bench/store";
import { err, requireMember } from "@/lib/tracker/server";

// The bench page (tester's browser, USB board attached): posts new serial
// lines and its status, gets back the commands queued for the board.
export async function POST(req: NextRequest) {
  const auth = await requireMember();
  if ("res" in auth) return auth.res;
  let body: { lines?: unknown; status?: Partial<PageStatus> };
  try {
    body = await req.json();
  } catch {
    return err("Bad JSON");
  }
  const lines = Array.isArray(body.lines) ? body.lines.filter((l): l is string => typeof l === "string") : [];
  const s = body.status ?? {};
  const status: PageStatus = {
    connected: s.connected === true,
    who: auth.user.name,
    chip: typeof s.chip === "string" ? s.chip.slice(0, 60) : undefined,
    flash: s.flash && typeof s.flash === "object" ? s.flash : undefined,
  };
  return NextResponse.json({ cmds: pagePoll(lines, status) });
}
