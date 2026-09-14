import { NextRequest, NextResponse } from "next/server";
import { cookies } from "next/headers";
import { verifySession } from "@/lib/session";
import { prisma } from "@/lib/prisma";
import { readFirmwareBuild, deleteFirmwareBuild } from "@/lib/firmwareStorage";

async function getSession() {
  const token = (await cookies()).get("agrisense_session")?.value;
  return token ? await verifySession(token) : null;
}

// Admin-only, human session — downloading the app itself doesn't need to go
// through the phone app (chicken-and-egg: you'd need the app to get the
// app), just a browser. No CI path here; CI only ever uploads.
export async function GET(_req: NextRequest, { params }: { params: Promise<{ id: string }> }) {
  const session = await getSession();
  if (!session || session.role !== "ADMIN") {
    return NextResponse.json({ error: "Forbidden" }, { status: 403 });
  }

  const { id } = await params;
  const appBuild = await prisma.mobileAppBuild.findUnique({ where: { id } });
  if (!appBuild) return NextResponse.json({ error: "Not found" }, { status: 404 });

  const bytes = await readFirmwareBuild(appBuild.storagePath);
  return new NextResponse(new Uint8Array(bytes), {
    headers: {
      "Content-Type": "application/vnd.android.package-archive",
      "Content-Disposition": `attachment; filename="nb-agri-flasher-${appBuild.versionName}-${appBuild.buildType}.apk"`,
    },
  });
}

// No FK dependents on MobileAppBuild (unlike FirmwareBuild/FlasherGrant),
// so unlike those two, deleting one is never blocked — nothing else in the
// schema references it.
export async function DELETE(_req: NextRequest, { params }: { params: Promise<{ id: string }> }) {
  const session = await getSession();
  if (!session || session.role !== "ADMIN") {
    return NextResponse.json({ error: "Forbidden" }, { status: 403 });
  }

  const { id } = await params;
  const appBuild = await prisma.mobileAppBuild.findUnique({ where: { id } });
  if (!appBuild) return NextResponse.json({ error: "Not found" }, { status: 404 });

  await prisma.mobileAppBuild.delete({ where: { id } });
  await deleteFirmwareBuild(appBuild.storagePath);

  return NextResponse.json({ ok: true });
}
