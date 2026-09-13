import { NextResponse } from "next/server";
import { cookies } from "next/headers";
import { verifySession } from "@/lib/session";
import { prisma } from "@/lib/prisma";
import { findActiveGrant } from "@/lib/flasherGrant";
import { readFirmwareBuild } from "@/lib/firmwareStorage";

async function getSession() {
  const token = (await cookies()).get("agrisense_session")?.value;
  return token ? await verifySession(token) : null;
}

// Re-checks the live grant on every download — this, not a cached bin on
// the phone, is what "revoke access" actually controls. The app is expected
// to fetch fresh on every flash attempt rather than reusing a saved copy.
export async function GET(_req: Request, { params }: { params: Promise<{ id: string }> }) {
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

  const bytes = await readFirmwareBuild(build.storagePath);

  await prisma.flashEvent.create({
    data: { grantId: grant.id, buildId: build.id, result: "downloaded" },
  });

  return new NextResponse(new Uint8Array(bytes), {
    headers: {
      "Content-Type": "application/octet-stream",
      "Content-Disposition": `attachment; filename="${build.product}-${build.version}-${build.variant}.bin"`,
      "X-Firmware-Sha256": build.sha256,
    },
  });
}
