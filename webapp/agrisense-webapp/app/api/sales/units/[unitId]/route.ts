import { NextRequest, NextResponse } from "next/server";
import { prisma } from "@/lib/prisma";
import { err } from "@/lib/tracker/server";
import { salesActor } from "@/lib/sales/server";

// DELETE -> remove one device from its sale.
export async function DELETE(req: NextRequest, { params }: { params: Promise<{ unitId: string }> }) {
  if (!(await salesActor(req))) return err("Not allowed", 401);
  const { unitId } = await params;
  const r = await prisma.saleUnit.deleteMany({ where: { id: unitId } });
  return r.count ? NextResponse.json({ ok: true }) : err("No such device in a sale", 404);
}
