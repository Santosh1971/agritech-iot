import { NextRequest, NextResponse } from "next/server";
import { cookies } from "next/headers";
import { Product } from "@prisma/client";
import { verifySession } from "@/lib/session";
import { prisma } from "@/lib/prisma";

async function getSession() {
  const token = (await cookies()).get("agrisense_session")?.value;
  return token ? await verifySession(token) : null;
}

export async function GET() {
  const session = await getSession();
  if (!session || session.role !== "ADMIN") {
    return NextResponse.json({ error: "Forbidden" }, { status: 403 });
  }

  const grants = await prisma.flasherGrant.findMany({
    orderBy: { createdAt: "desc" },
    include: { grantedBy: { select: { name: true } } },
  });
  return NextResponse.json({ grants });
}

// Admin-only. Grants by phone or email (at least one required) — a grant,
// not a role: this never touches the person's User/Role record, so it can
// be switched off without affecting their main dashboard account.
export async function POST(req: NextRequest) {
  const session = await getSession();
  if (!session || session.role !== "ADMIN") {
    return NextResponse.json({ error: "Forbidden" }, { status: 403 });
  }

  const { phone, email, label, products } = await req.json();

  if (!phone && !email) {
    return NextResponse.json({ error: "phone or email is required" }, { status: 400 });
  }
  if (typeof label !== "string" || label.trim().length === 0) {
    return NextResponse.json({ error: "label is required" }, { status: 400 });
  }
  if (!Array.isArray(products) || products.length === 0) {
    return NextResponse.json({ error: "At least one product must be selected" }, { status: 400 });
  }
  const validProducts = Object.values(Product) as string[];
  if (!products.every((p) => validProducts.includes(p))) {
    return NextResponse.json({ error: "One or more products are invalid" }, { status: 400 });
  }

  const grant = await prisma.flasherGrant.create({
    data: {
      phone: phone || null,
      email: email || null,
      label,
      products,
      grantedById: session.userId,
    },
  });

  return NextResponse.json({ grant });
}
