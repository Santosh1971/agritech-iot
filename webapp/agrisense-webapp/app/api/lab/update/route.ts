import { NextRequest, NextResponse } from "next/server";
import { checkDeviceToken, clearUpdate, DEVICE_RE, pendingUpdate, updateBin } from "@/lib/labStation";

// GET ?device=X (header x-device-token): the waiting firmware (app image) for OTA.
export async function GET(req: NextRequest) {
  if (!checkDeviceToken(req.headers.get("x-device-token"))) {
    return NextResponse.json({ error: "unauthorized" }, { status: 401 });
  }
  const device = req.nextUrl.searchParams.get("device") || "";
  if (!DEVICE_RE.test(device)) return NextResponse.json({ error: "bad device" }, { status: 400 });
  const bin = await updateBin(device);
  if (!bin) return NextResponse.json({ error: "no update" }, { status: 404 });
  return new NextResponse(new Uint8Array(bin), {
    headers: { "Content-Type": "application/octet-stream", "Content-Length": String(bin.length), "Cache-Control": "no-store" },
  });
}

// POST { device, id, result } after the device tried the update: clears it.
export async function POST(req: NextRequest) {
  if (!checkDeviceToken(req.headers.get("x-device-token"))) {
    return NextResponse.json({ error: "unauthorized" }, { status: 401 });
  }
  const { device, id, result } = await req.json().catch(() => ({}));
  if (!DEVICE_RE.test(String(device))) return NextResponse.json({ error: "bad device" }, { status: 400 });
  const p = await pendingUpdate(device);
  if (p && p.id === id) await clearUpdate(device, String(result || "done").slice(0, 80));
  return NextResponse.json({ ok: true });
}
