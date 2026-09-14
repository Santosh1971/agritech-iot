import { NextRequest, NextResponse } from "next/server";
import { cookies } from "next/headers";
import { verifySession } from "@/lib/session";
import { prisma } from "@/lib/prisma";
import { findActiveGrant } from "@/lib/flasherGrant";
import { deriveDeviceId } from "@/lib/deviceIdentity";

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

  const { buildId, result, mac, deviceId: providedDeviceId, detail } = await req.json();
  if (typeof buildId !== "string" || !VALID_RESULTS.includes(result)) {
    return NextResponse.json({ error: `buildId and result (${VALID_RESULTS.join(" | ")}) are required` }, { status: 400 });
  }

  const build = await prisma.firmwareBuild.findUnique({ where: { id: buildId } });
  if (!build) return NextResponse.json({ error: "Unknown buildId" }, { status: 404 });

  // deviceId comes pre-derived from the WiFi flow (read off the board's own
  // /status after flashing, since there's no MAC to derive it from there —
  // see the download route's comment); the USB flow still only ever has a
  // raw mac, derived here same as always.
  const deviceId = typeof providedDeviceId === "string" && providedDeviceId.length > 0
    ? providedDeviceId
    : typeof mac === "string" ? deriveDeviceId(build.product, mac) : null;

  const event = await prisma.flashEvent.create({
    data: {
      grantId: grant.id,
      buildId,
      deviceId,
      result,
      detail: typeof detail === "string" ? detail : null,
    },
  });

  return NextResponse.json({ event });
}
