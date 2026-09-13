import { NextResponse } from "next/server";
import { cookies } from "next/headers";
import { verifySession } from "@/lib/session";
import { prisma } from "@/lib/prisma";
import { findActiveGrant } from "@/lib/flasherGrant";
import { readFirmwareBuild } from "@/lib/firmwareStorage";
import { deriveDeviceId } from "@/lib/deviceIdentity";

async function getSession() {
  const token = (await cookies()).get("agrisense_session")?.value;
  return token ? await verifySession(token) : null;
}

// Re-checks the live grant on every download — this, not a cached bin on
// the phone, is what "revoke access" actually controls. The app is expected
// to fetch fresh on every flash attempt rather than reusing a saved copy.
//
// Also gates on the physical device itself: only units already provisioned
// as a Device row (shipped/known hardware) can be flashed by a non-admin
// grantee (dealer/customer) — a fresh/unknown chip's MAC has no matching
// Device, so their download is refused until an admin registers it.
//
// Admins (Santosh, Avinash) are the exception: they're the ones doing
// production, so flashing a brand-new chip's first build IS the
// registration event — auto-create its Device row rather than requiring
// someone to add it by hand first. Dealers still can't touch unregistered
// hardware; only an admin flash (or the existing +Add Device flow) creates
// that first record.
export async function GET(req: Request, { params }: { params: Promise<{ id: string }> }) {
  const session = await getSession();
  if (!session) return NextResponse.json({ error: "Unauthorized" }, { status: 401 });

  const grant = await findActiveGrant(session);
  if (!grant) {
    return NextResponse.json({ error: "No active flasher access for this account" }, { status: 403 });
  }

  const { id } = await params;
  const build = await prisma.firmwareBuild.findUnique({ where: { id } });
  if (!build) return NextResponse.json({ error: "Not found" }, { status: 404 });
  if (!grant.products.includes(build.product)) {
    return NextResponse.json({ error: `Not granted access to ${build.product}` }, { status: 403 });
  }

  const mac = new URL(req.url).searchParams.get("mac");
  if (!mac) {
    return NextResponse.json({ error: "Device MAC is required" }, { status: 400 });
  }
  const deviceId = deriveDeviceId(build.product, mac);
  if (!deviceId) {
    return NextResponse.json({ error: `Cannot identify ${build.product} devices yet` }, { status: 400 });
  }
  const device = await prisma.device.findFirst({ where: { deviceId, product: build.product } });
  if (!device) {
    if (session.role !== "ADMIN") {
      return NextResponse.json(
        { error: `This device (${deviceId}) isn't registered. Ask admin to add it under Devices before flashing.` },
        { status: 403 }
      );
    }
    // First time this unit has ever been flashed — this admin flash is the
    // production record for it, matching the app-facing "+ Add Device" flow.
    await prisma.device.create({
      data: { deviceId, product: build.product, name: deviceId },
    });
  }

  const bytes = await readFirmwareBuild(build.storagePath);

  await prisma.flashEvent.create({
    data: { grantId: grant.id, buildId: build.id, deviceId, result: "downloaded" },
  });

  return new NextResponse(new Uint8Array(bytes), {
    headers: {
      "Content-Type": "application/octet-stream",
      "Content-Disposition": `attachment; filename="${build.product}-${build.version}-${build.variant}.bin"`,
      "X-Firmware-Sha256": build.sha256,
    },
  });
}
