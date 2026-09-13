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
// as a Device row (shipped/known hardware) can be flashed. A fresh/unknown
// chip's MAC has no matching Device, so the download is refused until an
// admin registers it (the same "+ Add Device" flow used for provisioning) —
// this is the "give access from Server" step for brand-new units.
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
    return NextResponse.json(
      { error: `This device (${deviceId}) isn't registered. Ask admin to add it under Devices before flashing.` },
      { status: 403 }
    );
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
