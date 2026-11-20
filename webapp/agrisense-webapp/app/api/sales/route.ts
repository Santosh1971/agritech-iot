import { NextRequest, NextResponse } from "next/server";
import type { Prisma } from "@prisma/client";
import { prisma } from "@/lib/prisma";
import { err } from "@/lib/tracker/server";
import { alreadySold, deviceInfo, saleFields, salesActor, unitList } from "@/lib/sales/server";

export const dynamic = "force-dynamic";

// GET -> every sale with its units, plus what the server knows about each device.
export async function GET(req: NextRequest) {
  if (!(await salesActor(req))) return err("Not allowed", 401);
  const sales = await prisma.sale.findMany({ include: { units: { orderBy: { createdAt: "asc" } } }, orderBy: [{ dispatchDate: { sort: "desc", nulls: "last" } }, { createdAt: "desc" }] });
  const devices = await deviceInfo(sales.flatMap((s) => s.units.map((u) => u.deviceId)));
  return NextResponse.json({ sales, devices });
}

// POST {customer, ..., units:[...]} or {sales:[...]} (import) -> create.
// A device ID already in another sale is refused unless allowDuplicate is set.
export async function POST(req: NextRequest) {
  const actor = await salesActor(req);
  if (!actor) return err("Not allowed", 401);
  const body = await req.json().catch(() => null);
  if (!body) return err("Bad request");
  const list: Record<string, unknown>[] = Array.isArray(body.sales) ? body.sales : [body];
  if (list.length > 200) return err("At most 200 sales at a time");

  // Check every sale first, so an import is all-or-nothing.
  const ready: { head: Record<string, unknown>; units: Exclude<ReturnType<typeof unitList>, string> }[] = [];
  const seen = new Set<string>();
  for (const raw of list) {
    const head = saleFields(raw);
    if (!head.customer) return err("Customer is required");
    const units = unitList(raw);
    if (typeof units === "string") return err(units);
    if (!raw.allowDuplicate) {
      const ids = units.map((u) => u.deviceId);
      const repeated = ids.filter((id) => seen.has(id));
      if (repeated.length) return err(`Listed twice: ${[...new Set(repeated)].join(", ")}`, 409);
      ids.forEach((id) => seen.add(id));
      const dup = await alreadySold(ids);
      if (dup.length) return err(`Already in another sale: ${dup.join(", ")}. Check, or tick "allow duplicate" for a resale.`, 409);
    }
    ready.push({ head, units });
  }
  const created = await prisma.$transaction(
    ready.map(({ head, units }) =>
      prisma.sale.create({
        data: { ...(head as Prisma.SaleUncheckedCreateInput), customer: head.customer as string, createdBy: actor, units: { create: units } },
      }),
    ),
  );
  return NextResponse.json({ created: created.length, ids: created.map((s) => s.id) });
}
