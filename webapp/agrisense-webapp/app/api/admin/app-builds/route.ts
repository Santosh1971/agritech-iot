import { NextRequest, NextResponse } from "next/server";
import { cookies } from "next/headers";
import { verifySession } from "@/lib/session";
import { prisma } from "@/lib/prisma";
import { saveFirmwareBuild } from "@/lib/firmwareStorage";

async function getSession() {
  const token = (await cookies()).get("agrisense_session")?.value;
  return token ? await verifySession(token) : null;
}

// Same CI-or-human auth as /api/admin/builds (see that file's comment) — the
// same CI_UPLOAD_TOKEN covers both, since it's really "is this GitHub
// Actions" rather than anything firmware-specific.
async function authorizedUploader(req: NextRequest): Promise<{ userId: string } | null> {
  const authHeader = req.headers.get("authorization");
  const ciToken = process.env.CI_UPLOAD_TOKEN;
  if (ciToken && authHeader === `Bearer ${ciToken}`) {
    const ciUser = await prisma.user.findUnique({ where: { email: "ci@agrisenseandcontrol.in" } });
    if (ciUser) return { userId: ciUser.id };
  }
  const session = await getSession();
  if (session && session.role === "ADMIN") return { userId: session.userId };
  return null;
}

export async function GET() {
  const session = await getSession();
  if (!session || session.role !== "ADMIN") {
    return NextResponse.json({ error: "Forbidden" }, { status: 403 });
  }

  const appBuilds = await prisma.mobileAppBuild.findMany({
    orderBy: { createdAt: "desc" },
    include: { uploadedBy: { select: { name: true } } },
  });
  return NextResponse.json({ appBuilds });
}

// multipart/form-data: versionName, buildType (optional, "debug"|"release"),
// notes (optional), file (the .apk). Checksum computed server-side, same as
// firmware uploads.
export async function POST(req: NextRequest) {
  const authorized = await authorizedUploader(req);
  if (!authorized) {
    return NextResponse.json({ error: "Forbidden" }, { status: 403 });
  }

  const form = await req.formData();
  const versionName = form.get("versionName");
  const buildType = form.get("buildType") || "debug";
  const notes = form.get("notes");
  const file = form.get("file");

  if (typeof versionName !== "string" || !(file instanceof File)) {
    return NextResponse.json({ error: "versionName and file are required" }, { status: 400 });
  }
  if (buildType !== "debug" && buildType !== "release") {
    return NextResponse.json({ error: 'buildType must be "debug" or "release"' }, { status: 400 });
  }

  const bytes = Buffer.from(await file.arrayBuffer());
  if (bytes.length === 0) {
    return NextResponse.json({ error: "Uploaded file is empty" }, { status: 400 });
  }

  const { storagePath, sha256, sizeBytes } = await saveFirmwareBuild(bytes);

  const appBuild = await prisma.mobileAppBuild.create({
    data: {
      versionName,
      buildType: buildType as string,
      storagePath,
      sha256,
      sizeBytes,
      notes: typeof notes === "string" && notes.length > 0 ? notes : null,
      uploadedById: authorized.userId,
    },
  });

  return NextResponse.json({ appBuild });
}
