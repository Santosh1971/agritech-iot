// Sales register: who may read and change it, input cleaning, and matching a
// sold unit to the Device the server already knows (for "last seen online").
// The team (testing-tracker members and admins) uses the site session; Claude
// uses the bearer token SALES_TOKEN (tools/sales/sales.py).
import { timingSafeEqual } from "crypto";
import type { NextRequest } from "next/server";
import type { PaymentStatus } from "@prisma/client";
import { prisma } from "@/lib/prisma";
import { trackUser } from "@/lib/tracker/server";

export const PRODUCTS = ["FG1", "FG1-TH", "WPC-GW", "WPC-PN", "WM1-MINI", "WM1-PRO", "WM2", "TH2", "ASC-KIT", "OTHER"] as const;
export const PAYMENTS: PaymentStatus[] = ["PAID", "PARTIAL", "PENDING"];
export const CUSTOMER_TYPES = ["Dealer", "Farmer", "Institution", "Other"];

function bearerOk(req: NextRequest) {
  const want = process.env.SALES_TOKEN;
  const got = req.headers.get("authorization")?.replace(/^Bearer /, "") ?? "";
  if (!want || want.length < 24 || got.length !== want.length) return false;
  return timingSafeEqual(Buffer.from(got), Buffer.from(want));
}

/** Name of whoever is asking (team member or "Claude"), or null. */
export async function salesActor(req?: NextRequest): Promise<string | null> {
  if (req && bearerOk(req)) return "Claude";
  const u = await trackUser();
  if (!u || (!u.trackRole && !u.isAdmin)) return null;
  return u.name;
}

const txt = (v: unknown, max = 300) => (typeof v === "string" && v.trim() ? v.trim().slice(0, max) : null);

/** Sale header fields from a JSON body; only keys present in the body are returned. */
export function saleFields(b: Record<string, unknown>) {
  const out: Record<string, unknown> = {};
  if ("customer" in b) out.customer = txt(b.customer, 200);
  if ("customerType" in b) out.customerType = CUSTOMER_TYPES.includes(String(b.customerType)) ? String(b.customerType) : null;
  if ("location" in b) out.location = txt(b.location, 200);
  if ("invoiceNo" in b) out.invoiceNo = txt(b.invoiceNo, 60);
  if ("notes" in b) out.notes = txt(b.notes, 2000);
  if ("paymentStatus" in b && PAYMENTS.includes(b.paymentStatus as PaymentStatus)) out.paymentStatus = b.paymentStatus;
  if ("dispatchDate" in b) {
    const d = typeof b.dispatchDate === "string" && b.dispatchDate ? new Date(b.dispatchDate) : null;
    out.dispatchDate = d && !Number.isNaN(+d) ? d : null;
  }
  return out;
}

export type UnitIn = { product: string; deviceId: string; warrantyMonths: number; note: string | null };

/** Units from a body: [{product, deviceId, warrantyMonths?, note?}] or {product, deviceIds:"a, b\nc"}. */
export function unitList(b: Record<string, unknown>): UnitIn[] | string {
  const raw: Record<string, unknown>[] = Array.isArray(b.units) ? (b.units as Record<string, unknown>[]) : [];
  if (typeof b.deviceIds === "string") {
    for (const id of b.deviceIds.split(/[\s,;]+/)) if (id) raw.push({ product: b.product, deviceId: id, warrantyMonths: b.warrantyMonths });
  }
  const out: UnitIn[] = [];
  for (const u of raw) {
    const product = String(u.product ?? "").toUpperCase();
    const deviceId = txt(u.deviceId, 64)?.toUpperCase();
    if (!PRODUCTS.includes(product as (typeof PRODUCTS)[number])) return `Unknown product "${u.product ?? ""}"`;
    if (!deviceId) return "Every unit needs a device ID";
    const w = Number(u.warrantyMonths ?? 12);
    out.push({ product, deviceId, warrantyMonths: Number.isFinite(w) && w >= 0 && w <= 120 ? Math.round(w) : 12, note: txt(u.note, 300) });
  }
  if (out.length > 500) return "At most 500 units at a time";
  return out;
}

/** Device IDs already sold (in another sale), to warn about double entries. */
export async function alreadySold(ids: string[], exceptSaleId?: string) {
  if (ids.length === 0) return [];
  const hits = await prisma.saleUnit.findMany({
    where: { deviceId: { in: ids }, ...(exceptSaleId ? { saleId: { not: exceptSaleId } } : {}) },
    select: { deviceId: true },
  });
  return [...new Set(hits.map((h) => h.deviceId))];
}

/** Map of sold device ID -> {fullId, lastSeenAt} from the devices that have reported to the server.
 *  A short ID like "0DA8" matches a full one like "SWC_001_0DA8". */
export async function deviceInfo(ids: string[]) {
  const devices = await prisma.device.findMany({ select: { deviceId: true, lastSeenAt: true } });
  const out: Record<string, { fullId: string; lastSeenAt: string | null }> = {};
  for (const id of new Set(ids)) {
    const hit = devices.find((d) => {
      const full = d.deviceId.toUpperCase();
      return full === id || full.endsWith(`_${id}`) || (id.length >= 4 && full.endsWith(id));
    });
    if (hit) out[id] = { fullId: hit.deviceId, lastSeenAt: hit.lastSeenAt ? hit.lastSeenAt.toISOString() : null };
  }
  return out;
}
