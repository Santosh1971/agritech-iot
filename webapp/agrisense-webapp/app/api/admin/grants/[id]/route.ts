import { NextRequest, NextResponse } from "next/server";
import { cookies } from "next/headers";
import { Product } from "@prisma/client";
import { verifySession } from "@/lib/session";
import { prisma } from "@/lib/prisma";

async function getSession() {
  const token = (await cookies()).get("agrisense_session")?.value;
  return token ? await verifySession(token) : null;
}

// Revoke/restore (`active`) and/or replace which products a grant covers.
// findActiveGrant() (lib/flasherGrant.ts) takes the *first* matching grant
// row for an account, not a union across several — so adding a product to
// someone's access means updating their one existing grant's `products`
// here, not creating a second grant row for them.
//
// Checked live by the app on every request (see /api/flasher/*), never
// cached client-side, so a change here takes effect immediately — it just
// can't delete a bin already sitting on someone's phone from an earlier,
// still-valid download.
export async function PATCH(req: NextRequest, { params }: { params: Promise<{ id: string }> }) {
  const session = await getSession();
  if (!session || session.role !== "ADMIN") {
    return NextResponse.json({ error: "Forbidden" }, { status: 403 });
  }

  const { id } = await params;
  const { active, products } = await req.json();
  if (active === undefined && products === undefined) {
    return NextResponse.json({ error: "active (boolean) and/or products (string[]) required" }, { status: 400 });
  }
  if (products !== undefined) {
    const validProducts = Object.values(Product) as string[];
    if (!Array.isArray(products) || products.length === 0 || !products.every((p) => validProducts.includes(p))) {
      return NextResponse.json({ error: "products must be a non-empty array of valid products" }, { status: 400 });
    }
  }

  const grant = await prisma.flasherGrant.findUnique({ where: { id } });
  if (!grant) return NextResponse.json({ error: "Not found" }, { status: 404 });

  const updated = await prisma.flasherGrant.update({
    where: { id },
    data: {
      ...(active !== undefined ? { active, revokedAt: active ? null : new Date() } : {}),
      ...(products !== undefined ? { products } : {}),
    },
  });

  return NextResponse.json({ grant: updated });
}
