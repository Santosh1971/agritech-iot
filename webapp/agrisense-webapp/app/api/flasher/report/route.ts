import { NextRequest, NextResponse } from "next/server";
import { cookies } from "next/headers";
import { verifySession } from "@/lib/session";
import { prisma } from "@/lib/prisma";
import { findActiveGrant } from "@/lib/flasherGrant";

async function getSession() {
  const token = (await cookies()).get("agrisense_session")?.value;
  return token ? await verifySession(token) : null;
}

const VALID_RESULTS = ["flash_ok", "flash_failed"];

// The app calls this once a flash attempt actually finishes (or fails) —
// this is what makes /dashboard/flasher's activity log show more than just
// downloads, e.g. "downloaded but never confirmed a successful flash".
export async function POST(req: NextRequest) {
  const session = await getSession();
  if (!session) return NextResponse.json({ error: "Unauthorized" }, { status: 401 });

  const grant = await findActiveGrant(session);
  if (!grant) {
    return NextResponse.json({ error: "No active flasher access for this account" }, { status: 403 });
  }

  const { buildId, result, deviceId, detail } = await req.json();
  if (typeof buildId !== "string" || !VALID_RESULTS.includes(result)) {
    return NextResponse.json({ error: `buildId and result (${VALID_RESULTS.join(" | ")}) are required` }, { status: 400 });
  }

  const build = await prisma.firmwareBuild.findUnique({ where: { id: buildId } });
  if (!build) return NextResponse.json({ error: "Unknown buildId" }, { status: 404 });

  const event = await prisma.flashEvent.create({
    data: {
      grantId: grant.id,
      buildId,
      deviceId: typeof deviceId === "string" ? deviceId : null,
      result,
      detail: typeof detail === "string" ? detail : null,
    },
  });

  return NextResponse.json({ event });
}
