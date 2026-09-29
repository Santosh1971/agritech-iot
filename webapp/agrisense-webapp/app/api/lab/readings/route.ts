import { NextRequest, NextResponse } from "next/server";
import { DEVICE_RE, listDevices, pendingUpdate, readInfo, readReadings } from "@/lib/labStation";

// Public, read-only: the Lab Station's logged readings for the history graph.
// GET ?device=FarmIoT-XXXX&days=7 (no device: list of known Lab Stations).
export async function GET(req: NextRequest) {
  const device = req.nextUrl.searchParams.get("device") || "";
  if (!device) {
    const devices = await listDevices();
    const infos = await Promise.all(devices.map(async (d) => ({ device: d, lastSeen: (await readInfo(d))?.lastSeen ?? null })));
    return NextResponse.json({ devices: infos });
  }
  if (!DEVICE_RE.test(device)) return NextResponse.json({ error: "bad device" }, { status: 400 });
  const days = Math.min(90, Math.max(1, Number(req.nextUrl.searchParams.get("days")) || 7));
  const since = Math.floor(Date.now() / 1000) - days * 86400;
  const info = await readInfo(device);
  const p = await pendingUpdate(device);
  return NextResponse.json({
    device,
    lastSeen: info?.lastSeen ?? null,
    version: info?.version ?? null,
    program: info?.program ?? null,
    updatePending: p ? p.label : null,
    rows: await readReadings(device, since),
  });
}
