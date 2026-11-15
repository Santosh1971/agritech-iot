// GET /api/app/devices?product=WM2 -> the nodes this user may use, by farm.
import { NextRequest, NextResponse } from "next/server";
import { prisma } from "@/lib/prisma";
import { appSession, deviceScope, isProduct } from "@/lib/appAccess";
import { relay } from "@/lib/mqttRelay";

export const dynamic = "force-dynamic";

export async function GET(req: NextRequest) {
  const s = await appSession(req);
  if (!s) return NextResponse.json({ ok: false, err: "sign in first" }, { status: 401 });
  const p = req.nextUrl.searchParams.get("product");
  if (p && !isProduct(p)) return NextResponse.json({ ok: false, err: "unknown product" }, { status: 400 });
  const product = isProduct(p) ? p : undefined;

  const r = relay(); // start the broker connection early, so live/cmd are ready
  const devices = await prisma.device.findMany({
    where: { AND: [deviceScope(s), product ? { product } : {}] },
    include: { farm: { select: { name: true } } },
    orderBy: [{ farmId: "asc" }, { deviceId: "asc" }],
  });
  return NextResponse.json({
    ok: true,
    user: { email: s.email, role: s.role },
    devices: devices.map((d) => {
      const last = (d.lastStatus ?? {}) as Record<string, unknown>;
      return {
        deviceId: d.deviceId,
        product: d.product,
        name: d.name,
        farm: d.farm?.name ?? null,
        node: typeof last.node === "string" ? last.node : null, // "zone/node" as the node reports it
        online: r.online.get(d.deviceId) ?? (last.online === true),
        lastSeenAt: d.lastSeenAt,
      };
    }),
  });
}
