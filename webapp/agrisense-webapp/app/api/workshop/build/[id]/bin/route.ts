import { NextRequest, NextResponse } from "next/server";
import { checkAccess, getJob, readBin } from "@/lib/firmwareBuild";

// The compiled firmware (bootloader + partitions + app merged, flashed at 0x0).
export async function GET(req: NextRequest, { params }: { params: Promise<{ id: string }> }) {
  if (!checkAccess(req.headers.get("x-access-code"))) {
    return NextResponse.json({ error: "Wrong or missing workshop access code." }, { status: 401 });
  }
  const { id } = await params;
  const job = getJob(id);
  const bin = job?.status === "done" ? await readBin(id) : null;
  if (!bin) return NextResponse.json({ error: "Firmware not ready." }, { status: 404 });
  return new NextResponse(new Uint8Array(bin), {
    headers: { "Content-Type": "application/octet-stream", "Cache-Control": "no-store" },
  });
}
