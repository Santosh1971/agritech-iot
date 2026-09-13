import { NextRequest, NextResponse } from "next/server";
import { cookies } from "next/headers";
import { Product } from "@prisma/client";
import { verifySession } from "@/lib/session";
import { prisma } from "@/lib/prisma";
import { findActiveGrant } from "@/lib/flasherGrant";

async function getSession() {
  const token = (await cookies()).get("agrisense_session")?.value;
  return token ? await verifySession(token) : null;
}

// Lists builds for one product — only ever a product the caller's live
// grant currently covers. `?product=FG1`.
export async function GET(req: NextRequest) {
  const session = await getSession();
  if (!session) return NextResponse.json({ error: "Unauthorized" }, { status: 401 });

  const grant = await findActiveGrant(session);
  if (!grant) {
    return NextResponse.json({ error: "No active flasher access for this account" }, { status: 403 });
  }

  const product = req.nextUrl.searchParams.get("product");
  if (!product || !(Object.values(Product) as string[]).includes(product)) {
    return NextResponse.json({ error: "A valid ?product= is required" }, { status: 400 });
  }
  if (!grant.products.includes(product as Product)) {
    return NextResponse.json({ error: `Not granted access to ${product}` }, { status: 403 });
  }

  const builds = await prisma.firmwareBuild.findMany({
    where: { product: product as Product },
    orderBy: { createdAt: "desc" },
    select: { id: true, product: true, version: true, variant: true, sizeBytes: true, sha256: true, notes: true, createdAt: true },
  });

  return NextResponse.json({ builds });
}
