import { NextRequest, NextResponse } from "next/server";
import { cookies } from "next/headers";
import { join } from "path";
import { verifySession } from "@/lib/session";
import { clearUpdate, DEVICE_RE, LAB_FIRMWARE_DIR, listDevices, pendingUpdate, queueUpdateFromFile, readInfo } from "@/lib/labStation";
import { readFile } from "fs/promises";

async function admin() {
  const token = (await cookies()).get("agrisense_session")?.value;
  const session = token ? await verifySession(token) : null;
  return session?.role === "ADMIN" ? session : null;
}

// GET: all Lab Stations with status and pending update, plus the latest firmware version.
export async function GET() {
  if (!(await admin())) return NextResponse.json({ error: "Unauthorized" }, { status: 401 });
  const devices = await listDevices();
  const version = (await readFile(join(LAB_FIRMWARE_DIR, "version.txt"), "utf8").catch(() => "")).trim() || null;
  return NextResponse.json({
    latestFirmware: version,
    devices: await Promise.all(devices.map(async (d) => ({ ...(await readInfo(d)), device: d, pending: await pendingUpdate(d) }))),
  });
}

// POST { device, action: "update" | "cancel" }: queue the latest Lab Station firmware (remote FOTA) or cancel.
export async function POST(req: NextRequest) {
  const session = await admin();
  if (!session) return NextResponse.json({ error: "Unauthorized" }, { status: 401 });
  const { device, action } = await req.json().catch(() => ({}));
  if (!DEVICE_RE.test(String(device))) return NextResponse.json({ error: "bad device" }, { status: 400 });
  if (action === "cancel") {
    await clearUpdate(device, "cancelled by admin");
    return NextResponse.json({ ok: true });
  }
  const version = (await readFile(join(LAB_FIRMWARE_DIR, "version.txt"), "utf8").catch(() => "")).trim();
  try {
    const p = await queueUpdateFromFile(device, join(LAB_FIRMWARE_DIR, "labstation.app.bin"), `Lab Station ${version}`, "admin");
    return NextResponse.json({ ok: true, pending: p });
  } catch {
    return NextResponse.json({ error: "Lab Station firmware not built yet." }, { status: 404 });
  }
}
