import { NextRequest, NextResponse } from "next/server";
import { checkAccess, getJob, readAppBin } from "@/lib/firmwareBuild";
import { DEVICE_RE, queueUpdate, readInfo } from "@/lib/labStation";

// POST { device } with the workshop access code: send a student's built program to
// the Lab Station over WiFi. The station installs it at its next check-in.
export async function POST(req: NextRequest, { params }: { params: Promise<{ id: string }> }) {
  if (!checkAccess(req.headers.get("x-access-code"))) {
    return NextResponse.json({ error: "Wrong or missing workshop access code." }, { status: 401 });
  }
  const { id } = await params;
  const { device } = await req.json().catch(() => ({}));
  const dev = "FarmIoT-" + String(device || "").trim().replace(/^farmiot-?/i, "").toUpperCase();
  if (!DEVICE_RE.test(dev)) return NextResponse.json({ error: "Type the Lab Station name, like FarmIoT-E0E3." }, { status: 400 });
  if (!(await readInfo(dev))) {
    return NextResponse.json({ error: `${dev} has never checked in. Is it running the Lab Station program and online?` }, { status: 404 });
  }
  const job = getJob(id);
  const bin = job?.status === "done" ? await readAppBin(id) : null;
  if (!bin) return NextResponse.json({ error: "Build it first." }, { status: 404 });
  const p = await queueUpdate(dev, bin, "program from Claude", "workshop flasher");
  return NextResponse.json({ ok: true, device: dev, id: p.id, size: p.size });
}
