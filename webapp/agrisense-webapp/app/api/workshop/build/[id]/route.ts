import { NextRequest, NextResponse } from "next/server";
import { checkAccess, getJob, queuePosition } from "@/lib/firmwareBuild";

// Build status: queued (with position) / building / done / error, plus the compiler log.
export async function GET(req: NextRequest, { params }: { params: Promise<{ id: string }> }) {
  if (!checkAccess(req.headers.get("x-access-code"))) {
    return NextResponse.json({ error: "Wrong or missing workshop access code." }, { status: 401 });
  }
  const { id } = await params;
  const job = getJob(id);
  if (!job) return NextResponse.json({ error: "Build not found (builds are kept for 2 hours)." }, { status: 404 });
  return NextResponse.json({
    id: job.id,
    status: job.status,
    position: queuePosition(job.id),
    binSize: job.binSize ?? null,
    log: job.status === "error" ? job.log.slice(-6000) : job.log.slice(-1500),
  });
}
