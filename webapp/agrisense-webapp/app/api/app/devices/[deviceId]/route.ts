// DELETE /api/app/devices/{deviceId}  (admin only) -> the node, its readings
// and its command log are removed from the server. A node that is still
// powered and online reports again and comes back on its own, in no farm.
import { NextRequest, NextResponse } from "next/server";
import { prisma } from "@/lib/prisma";
import { appSession } from "@/lib/appAccess";

export const dynamic = "force-dynamic";

export async function DELETE(req: NextRequest, { params }: { params: Promise<{ deviceId: string }> }) {
  const s = await appSession(req);
  if (!s) return NextResponse.json({ ok: false, err: "sign in first" }, { status: 401 });
  if (s.role !== "ADMIN") return NextResponse.json({ ok: false, err: "only an admin can delete a node" }, { status: 403 });
  const { deviceId } = await params;
  const d = await prisma.device.findUnique({ where: { deviceId } });
  if (!d) return NextResponse.json({ ok: false, err: "no such node" }, { status: 404 });
  const [r, c] = await prisma.$transaction([
    prisma.reading.deleteMany({ where: { deviceId: d.id } }),
    prisma.command.deleteMany({ where: { deviceId: d.id } }),
    prisma.device.delete({ where: { id: d.id } }),
  ]);
  return NextResponse.json({ ok: true, deleted: deviceId, readings: r.count, commands: c.count });
}
