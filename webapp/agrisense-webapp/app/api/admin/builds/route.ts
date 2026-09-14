import { NextRequest, NextResponse } from "next/server";
import { cookies } from "next/headers";
import { Product } from "@prisma/client";
import { verifySession } from "@/lib/session";
import { prisma } from "@/lib/prisma";
import { saveFirmwareBuild } from "@/lib/firmwareStorage";

async function getSession() {
  const token = (await cookies()).get("agrisense_session")?.value;
  return token ? await verifySession(token) : null;
}

// Upload accepts two callers: a human admin session (email-OTP, same as
// every other admin route), or CI — GitHub Actions has no inbox to receive
// an OTP in, so it authenticates instead with a static bearer token
// (CI_UPLOAD_TOKEN, set in the server's .env and as a GitHub Actions repo
// secret — never committed). Attributed to a dedicated "CI Bot" User row
// rather than leaving uploadedById null, so the Builds table still shows
// who/what produced each one.
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

  const builds = await prisma.firmwareBuild.findMany({
    orderBy: { createdAt: "desc" },
    include: { uploadedBy: { select: { name: true } } },
  });
  return NextResponse.json({ builds });
}

// multipart/form-data: product, version, variant, notes (optional), file.
// bootloader/partitions are optional companion files — when both are
// present, the app can offer a full (blank-chip) flash for this build, not
// just an app-only update; see /api/flasher/download's ?part= handling.
// The checksum is computed here, server-side, from the bytes actually
// received — never trusted from the client.
export async function POST(req: NextRequest) {
  const authorized = await authorizedUploader(req);
  if (!authorized) {
    return NextResponse.json({ error: "Forbidden" }, { status: 403 });
  }

  const form = await req.formData();
  const product = form.get("product");
  const version = form.get("version");
  const variant = form.get("variant") || "esp32dev";
  const notes = form.get("notes");
  const file = form.get("file");
  const bootloaderFile = form.get("bootloader");
  const partitionsFile = form.get("partitions");

  if (typeof product !== "string" || typeof version !== "string" || !(file instanceof File)) {
    return NextResponse.json({ error: "product, version, and file are required" }, { status: 400 });
  }
  if (!(Object.values(Product) as string[]).includes(product)) {
    return NextResponse.json({ error: `Unknown product "${product}"` }, { status: 400 });
  }

  const bytes = Buffer.from(await file.arrayBuffer());
  if (bytes.length === 0) {
    return NextResponse.json({ error: "Uploaded file is empty" }, { status: 400 });
  }

  const { storagePath, sha256, sizeBytes } = await saveFirmwareBuild(bytes);

  let bootloaderPath: string | null = null;
  let partitionsPath: string | null = null;
  if (bootloaderFile instanceof File && partitionsFile instanceof File) {
    const bootloaderBytes = Buffer.from(await bootloaderFile.arrayBuffer());
    const partitionsBytes = Buffer.from(await partitionsFile.arrayBuffer());
    if (bootloaderBytes.length > 0 && partitionsBytes.length > 0) {
      bootloaderPath = (await saveFirmwareBuild(bootloaderBytes)).storagePath;
      partitionsPath = (await saveFirmwareBuild(partitionsBytes)).storagePath;
    }
  }

  const build = await prisma.firmwareBuild.create({
    data: {
      product: product as Product,
      version,
      variant: variant as string,
      storagePath,
      bootloaderPath,
      partitionsPath,
      sha256,
      sizeBytes,
      notes: typeof notes === "string" && notes.length > 0 ? notes : null,
      uploadedById: authorized.userId,
    },
  });

  return NextResponse.json({ build });
}
