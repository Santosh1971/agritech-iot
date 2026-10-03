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

// The companion-app counterpart to /api/flasher/builds (firmware) — "which
// product apps does this logged-in account currently have access to, and
// what's the latest real build of each". Reuses the exact same
// FlasherGrant concept: a grant that covers flashing a product's firmware
// also covers downloading that product's own app, since both are really
// "this person works with this product". No ?product= filter here (unlike
// /api/flasher/builds) — this always returns every granted product's
// latest app at once, since there's no phone-app product picker driving
// this the way there is for firmware.
export async function GET() {
  const session = await getSession();
  if (!session) return NextResponse.json({ error: "Unauthorized" }, { status: 401 });

  const grant = await findActiveGrant(session);
  if (!grant) {
    return NextResponse.json({ error: "No active flasher access for this account" }, { status: 403 });
  }

  const rows = await prisma.mobileAppBuild.findMany({
    where: { product: { in: grant.products as Product[] } },
    orderBy: { createdAt: "desc" },
    select: { id: true, product: true, versionName: true, buildType: true, sizeBytes: true, sha256: true, notes: true, createdAt: true },
  });

  // Same "latest real release per product, never a dev-<sha> CI build"
  // rule as firmware's /api/flasher/builds — admins get the same filtered
  // view here too (unlike that route), since this is "what's the current
  // app for each product I work with", not a full build history browser;
  // admins already have that via /dashboard/flasher's app-builds table.
  const latestByProduct = new Map<string, (typeof rows)[number]>();
  for (const row of rows) {
    if (row.versionName.startsWith("dev-")) continue;
    if (row.product && !latestByProduct.has(row.product)) latestByProduct.set(row.product, row);
  }

  return NextResponse.json({ apps: Array.from(latestByProduct.values()) });
}
