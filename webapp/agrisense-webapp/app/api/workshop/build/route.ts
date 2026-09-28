import { NextRequest, NextResponse } from "next/server";
import { checkAccess, checkSketch, enqueueBuild, queuePosition } from "@/lib/firmwareBuild";

// POST { code } with header x-access-code: compile a student's ESP32 sketch.
// Returns a job id; poll /api/workshop/build/<id>, then download .../bin.
export async function POST(req: NextRequest) {
  if (!checkAccess(req.headers.get("x-access-code"))) {
    return NextResponse.json({ error: "Wrong or missing workshop access code." }, { status: 401 });
  }
  let code: unknown;
  try {
    ({ code } = await req.json());
  } catch {
    return NextResponse.json({ error: "Invalid request." }, { status: 400 });
  }
  if (typeof code !== "string" || !code.trim()) {
    return NextResponse.json({ error: "Paste the program from Claude first." }, { status: 400 });
  }
  const refused = checkSketch(code);
  if (refused) return NextResponse.json({ error: refused }, { status: 400 });

  const job = await enqueueBuild(code);
  return NextResponse.json({ id: job.id, status: job.status, position: queuePosition(job.id) });
}
