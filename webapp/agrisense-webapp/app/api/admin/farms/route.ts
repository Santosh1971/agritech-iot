// Admin: farms, their member emails and their nodes (product-app access).
//   GET  -> every farm with members and devices, plus nodes in no farm
//   POST {name} -> create a farm
import { NextRequest, NextResponse } from "next/server";
import { prisma } from "@/lib/prisma";
import { adminSession } from "@/lib/adminSession";

export const dynamic = "force-dynamic";

const deviceSel = { id: true, deviceId: true, product: true, name: true, lastSeenAt: true, lastStatus: true } as const;

export async function GET() {
  if (!(await adminSession())) return NextResponse.json({ error: "Forbidden" }, { status: 403 });
  const farms = await prisma.farm.findMany({
    orderBy: { name: "asc" },
    include: {
      members: { include: { user: { select: { id: true, email: true, name: true, role: true } } }, orderBy: { createdAt: "asc" } },
      devices: { select: deviceSel, orderBy: { deviceId: "asc" } },
    },
  });
  const unassigned = await prisma.device.findMany({ where: { farmId: null }, select: deviceSel, orderBy: [{ product: "asc" }, { deviceId: "asc" }] });
  return NextResponse.json({ farms, unassigned });
}

export async function POST(req: NextRequest) {
  if (!(await adminSession())) return NextResponse.json({ error: "Forbidden" }, { status: 403 });
  const { name } = await req.json();
  const n = typeof name === "string" ? name.trim() : "";
  if (!n) return NextResponse.json({ error: "farm name required" }, { status: 400 });
  if (await prisma.farm.findUnique({ where: { name: n } })) return NextResponse.json({ error: "a farm with that name exists" }, { status: 409 });
  const farm = await prisma.farm.create({ data: { name: n } });
  return NextResponse.json({ farm });
}
