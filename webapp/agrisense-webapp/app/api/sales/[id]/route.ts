import { NextRequest, NextResponse } from "next/server";
import type { Prisma } from "@prisma/client";
import { prisma } from "@/lib/prisma";
import { err } from "@/lib/tracker/server";
import { saleFields, salesActor } from "@/lib/sales/server";

// PATCH {paymentStatus?, invoiceNo?, dispatchDate?, ...} -> change a sale's details.
export async function PATCH(req: NextRequest, { params }: { params: Promise<{ id: string }> }) {
  if (!(await salesActor(req))) return err("Not allowed", 401);
  const { id } = await params;
  const f = saleFields((await req.json().catch(() => ({}))) ?? {});
  if ("customer" in f && !f.customer) return err("Customer can't be empty");
  try {
    const sale = await prisma.sale.update({ where: { id }, data: f as Prisma.SaleUncheckedUpdateInput });
    return NextResponse.json({ sale });
  } catch {
    return err("No such sale", 404);
  }
}

// DELETE -> remove a sale and its units (entered by mistake).
export async function DELETE(req: NextRequest, { params }: { params: Promise<{ id: string }> }) {
  if (!(await salesActor(req))) return err("Not allowed", 401);
  const { id } = await params;
  const r = await prisma.sale.deleteMany({ where: { id } });
  return r.count ? NextResponse.json({ ok: true }) : err("No such sale", 404);
}
