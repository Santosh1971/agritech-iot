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

// Admin-only build upload. multipart/form-data: product, version, variant,
// notes (optional), file. The checksum is computed here, server-side, from
// the bytes actually received — never trusted from the client.
export async function POST(req: NextRequest) {
  const session = await getSession();
  if (!session || session.role !== "ADMIN") {
    return NextResponse.json({ error: "Forbidden" }, { status: 403 });
  }

  const form = await req.formData();
  const product = form.get("product");
  const version = form.get("version");
  const variant = form.get("variant") || "esp32dev";
  const notes = form.get("notes");
  const file = form.get("file");

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

  const build = await prisma.firmwareBuild.create({
    data: {
      product: product as Product,
      version,
      variant: variant as string,
      storagePath,
      sha256,
      sizeBytes,
      notes: typeof notes === "string" && notes.length > 0 ? notes : null,
      uploadedById: session.userId,
    },
  });

  return NextResponse.json({ build });
}
