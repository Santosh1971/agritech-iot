import { NextRequest, NextResponse } from "next/server";
import { cookies } from "next/headers";
import { verifySession } from "@/lib/session";
import { prisma } from "@/lib/prisma";
import { deleteFirmwareBuild } from "@/lib/firmwareStorage";

async function getSession() {
  const token = (await cookies()).get("agrisense_session")?.value;
  return token ? await verifySession(token) : null;
}

// Admin-only (human session — CI never needs to delete a build it just
// uploaded). A build with real download/flash history can't be silently
// erased: FlashEvent.buildId is a required relation (default RESTRICT), so
// that surfaces as a 409 rather than either deleting the activity log or
// failing as a generic 500.
export async function DELETE(_req: NextRequest, { params }: { params: Promise<{ id: string }> }) {
  const session = await getSession();
  if (!session || session.role !== "ADMIN") {
    return NextResponse.json({ error: "Forbidden" }, { status: 403 });
  }

  const { id } = await params;
  const build = await prisma.firmwareBuild.findUnique({ where: { id } });
  if (!build) return NextResponse.json({ error: "Not found" }, { status: 404 });

  try {
    await prisma.firmwareBuild.delete({ where: { id } });
  } catch {
    return NextResponse.json(
      { error: "Can't delete — this build has download/flash history." },
      { status: 409 }
    );
  }

  await deleteFirmwareBuild(build.storagePath);
  if (build.bootloaderPath) await deleteFirmwareBuild(build.bootloaderPath);
  if (build.partitionsPath) await deleteFirmwareBuild(build.partitionsPath);
  return NextResponse.json({ ok: true });
}
