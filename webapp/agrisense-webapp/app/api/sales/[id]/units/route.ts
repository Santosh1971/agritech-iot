import { NextRequest, NextResponse } from "next/server";
import { prisma } from "@/lib/prisma";
import { err } from "@/lib/tracker/server";
import { alreadySold, salesActor, unitList } from "@/lib/sales/server";

// POST {product, deviceIds:"a, b", warrantyMonths?} or {units:[...]} -> add devices to a sale.
export async function POST(req: NextRequest, { params }: { params: Promise<{ id: string }> }) {
  if (!(await salesActor(req))) return err("Not allowed", 401);
  const { id } = await params;
  const body = (await req.json().catch(() => ({}))) ?? {};
  if (!(await prisma.sale.findUnique({ where: { id }, select: { id: true } }))) return err("No such sale", 404);
  const units = unitList(body);
  if (typeof units === "string") return err(units);
  if (units.length === 0) return err("Type at least one device ID");
  if (!body.allowDuplicate) {
    const dup = await alreadySold(units.map((u) => u.deviceId));
    if (dup.length) return err(`Already in a sale: ${dup.join(", ")}`, 409);
  }
  await prisma.saleUnit.createMany({ data: units.map((u) => ({ ...u, saleId: id })) });
  return NextResponse.json({ added: units.length });
}
