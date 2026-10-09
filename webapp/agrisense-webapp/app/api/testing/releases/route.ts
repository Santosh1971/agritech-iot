import { NextRequest, NextResponse } from "next/server";
import { prisma } from "@/lib/prisma";
import { err, requireMember } from "@/lib/tracker/server";

// New release target (developer, JSON): productId, name, fwVersion?, appVersion?, hwRev?, notes?
export async function POST(req: NextRequest) {
  const auth = await requireMember("DEVELOPER");
  if ("res" in auth) return auth.res;
  const b = (await req.json().catch(() => null)) as Record<string, unknown> | null;
  const s = (k: string, max = 80) => (typeof b?.[k] === "string" ? (b[k] as string).trim().slice(0, max) || null : null);
  const productId = s("productId");
  const name = s("name", 120);
  if (!productId || !name) return err("Product and name are required");
  const product = await prisma.trackProduct.findUnique({ where: { id: productId } });
  if (!product) return err("Unknown product");
  const release = await prisma.trackRelease.create({
    data: { productId, name, fwVersion: s("fwVersion", 40), appVersion: s("appVersion", 40), hwRev: s("hwRev", 40), notes: s("notes", 4000) },
  });
  await prisma.trackEvent.create({ data: { releaseId: release.id, actorId: auth.user.userId, kind: "release", text: `Release ${name} opened` } });
  return NextResponse.json({ release });
}
