import { NextRequest, NextResponse } from "next/server";
import { checkDeviceToken, DEVICE_RE, pendingUpdate, saveInfo, saveReadings, type Reading } from "@/lib/labStation";

// Lab Station check-in (header x-device-token). One call does everything the
// device needs: upload logged readings, report its status, and learn whether a
// firmware update is waiting. Reply: { saved, update: {id,size,label}|null, now }.
export async function POST(req: NextRequest) {
  if (!checkDeviceToken(req.headers.get("x-device-token"))) {
    return NextResponse.json({ error: "unauthorized" }, { status: 401 });
  }
  let body: Record<string, unknown>;
  try {
    body = await req.json();
  } catch {
    return NextResponse.json({ error: "invalid json" }, { status: 400 });
  }
  const device = String(body.device || "");
  if (!DEVICE_RE.test(device)) return NextResponse.json({ error: "bad device" }, { status: 400 });

  const str = (v: unknown, n = 40) => (typeof v === "string" ? v.slice(0, n) : undefined);
  await saveInfo({
    device,
    version: str(body.version),
    ip: str(body.ip),
    ssid: str(body.ssid, 32),
    rssi: typeof body.rssi === "number" ? body.rssi : undefined,
    program: str(body.program, 60),
  });
  const rows = Array.isArray(body.rows) ? (body.rows as Reading[]) : [];
  const saved = await saveReadings(device, rows);
  const p = await pendingUpdate(device);
  return NextResponse.json({
    saved,
    update: p ? { id: p.id, size: p.size, label: p.label } : null,
    now: Math.floor(Date.now() / 1000),
  });
}
