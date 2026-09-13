import { NextResponse } from "next/server";
import { cookies } from "next/headers";
import { verifySession } from "@/lib/session";
import { prisma } from "@/lib/prisma";

async function getSession() {
  const token = (await cookies()).get("agrisense_session")?.value;
  return token ? await verifySession(token) : null;
}

// The audit trail: who downloaded/flashed what, and when — so a revoke is
// verifiable, not just trusted. Most recent 200; this is a bench-scale tool,
// not built for high volume yet.
export async function GET() {
  const session = await getSession();
  if (!session || session.role !== "ADMIN") {
    return NextResponse.json({ error: "Forbidden" }, { status: 403 });
  }

  const events = await prisma.flashEvent.findMany({
    orderBy: { occurredAt: "desc" },
    take: 200,
    include: {
      grant: { select: { label: true, phone: true, email: true } },
      build: { select: { product: true, version: true, variant: true } },
    },
  });

  return NextResponse.json({ events });
}
