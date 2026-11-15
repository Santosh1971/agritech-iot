// POST /api/app/devices/{deviceId}/cmd  body = the node command, e.g. {"cmd":"identify"}
// Checks the user may use this node and run this command, relays it over
// MQTT, waits for the node's reply and returns it. Every command is logged in
// Command (the bridge marks it ACKED when the reply passes the broker).
import { NextRequest, NextResponse } from "next/server";
import { randomUUID } from "crypto";
import { prisma } from "@/lib/prisma";
import { appSession, deviceFor, mayRun } from "@/lib/appAccess";
import { relay, TOPIC_OF } from "@/lib/mqttRelay";

export const dynamic = "force-dynamic";

// Read-only commands are frequent (the app polls state); don't log them.
const QUIET = new Set(["get_state", "get_config", "get_program", "get_history", "get_days", "get_events", "device_info", "wifi_scan"]);

export async function POST(req: NextRequest, { params }: { params: Promise<{ deviceId: string }> }) {
  const s = await appSession(req);
  if (!s) return NextResponse.json({ ok: false, err: "sign in first" }, { status: 401 });
  const { deviceId } = await params;
  const device = await deviceFor(s, deviceId);
  if (!device) return NextResponse.json({ ok: false, err: "no such node in your farms" }, { status: 404 });
  const seg = TOPIC_OF[device.product];
  if (!seg) return NextResponse.json({ ok: false, err: "this product is not in the app yet" }, { status: 400 });

  let body: Record<string, unknown>;
  try {
    body = await req.json();
  } catch {
    return NextResponse.json({ ok: false, err: "body must be JSON" }, { status: 400 });
  }
  const cmd = typeof body.cmd === "string" ? body.cmd : "";
  if (!cmd) return NextResponse.json({ ok: false, err: "cmd required" }, { status: 400 });
  if (!mayRun(s, device.product, cmd)) {
    return NextResponse.json({ ok: false, cmd, err: "only an admin can do this" }, { status: 403 });
  }
  // Members never move a node off our broker.
  if (s.role !== "ADMIN" && cmd === "set_config" && body.config && typeof body.config === "object") {
    const c = { ...(body.config as Record<string, unknown>) };
    delete c.broker;
    delete c.broker_host;
    body = { ...body, config: c };
  }
  delete body.id;
  delete body.src;
  delete body.ts;

  const id = randomUUID();
  if (!QUIET.has(cmd)) {
    await prisma.command.create({
      data: { cmdId: id, deviceId: device.id, payloadId: cmd, params: body as object, sentById: s.userId },
    });
  }
  const timeout = Math.min(Math.max(Number(req.nextUrl.searchParams.get("timeout") ?? 20), 3), 30) * 1000;
  const reply = await relay().command(seg, deviceId, body, id, timeout);
  if (!QUIET.has(cmd) && reply.ok === false && !("v" in reply)) {
    await prisma.command.updateMany({ where: { cmdId: id }, data: { status: "TIMEOUT" } });
  }
  return NextResponse.json(reply);
}
