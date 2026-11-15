// Admin: change one farm.
//   PATCH {action:"addMember", email, name?}  -> the email may sign in to the apps and use this farm's nodes
//   PATCH {action:"removeMember", userId}
//   PATCH {action:"addDevice", deviceId}       -> node (e.g. WM2_0D4D4FB0) moves into this farm
//   PATCH {action:"removeDevice", deviceId}
//   PATCH {action:"rename", name}
//   DELETE -> farm removed (its nodes stay, in no farm; members lose access)
import { NextRequest, NextResponse } from "next/server";
import { prisma } from "@/lib/prisma";
import { adminSession } from "@/lib/adminSession";

export const dynamic = "force-dynamic";

const EMAIL = /^[^\s@]+@[^\s@]+\.[^\s@]+$/;

export async function PATCH(req: NextRequest, { params }: { params: Promise<{ id: string }> }) {
  const admin = await adminSession();
  if (!admin) return NextResponse.json({ error: "Forbidden" }, { status: 403 });
  const { id } = await params;
  const farm = await prisma.farm.findUnique({ where: { id } });
  if (!farm) return NextResponse.json({ error: "no such farm" }, { status: 404 });
  const b = await req.json();

  switch (b.action) {
    case "addMember": {
      const email = typeof b.email === "string" ? b.email.trim() : "";
      if (!EMAIL.test(email)) return NextResponse.json({ error: "enter a valid email" }, { status: 400 });
      // Sign-in matches the stored email exactly; reuse a user that differs only in case.
      let user = await prisma.user.findFirst({ where: { email: { equals: email, mode: "insensitive" } } });
      if (!user) {
        const name = typeof b.name === "string" && b.name.trim() ? b.name.trim() : email.split("@")[0];
        user = await prisma.user.create({ data: { email, name, role: "CUSTOMER" } });
      }
      await prisma.farmMember.upsert({
        where: { farmId_userId: { farmId: id, userId: user.id } },
        update: {},
        create: { farmId: id, userId: user.id, addedById: admin.userId },
      });
      return NextResponse.json({ ok: true, user: { id: user.id, email: user.email, role: user.role } });
    }
    case "removeMember": {
      await prisma.farmMember.deleteMany({ where: { farmId: id, userId: String(b.userId ?? "") } });
      return NextResponse.json({ ok: true });
    }
    case "addDevice":
    case "removeDevice": {
      const d = await prisma.device.findUnique({ where: { deviceId: String(b.deviceId ?? "") } });
      if (!d) return NextResponse.json({ error: "no node with that ID has reported to the server yet" }, { status: 404 });
      if (b.action === "removeDevice" && d.farmId !== id) return NextResponse.json({ error: "node is not in this farm" }, { status: 400 });
      await prisma.device.update({ where: { id: d.id }, data: { farmId: b.action === "addDevice" ? id : null } });
      return NextResponse.json({ ok: true });
    }
    case "rename": {
      const n = typeof b.name === "string" ? b.name.trim() : "";
      if (!n) return NextResponse.json({ error: "farm name required" }, { status: 400 });
      await prisma.farm.update({ where: { id }, data: { name: n } });
      return NextResponse.json({ ok: true });
    }
  }
  return NextResponse.json({ error: "unknown action" }, { status: 400 });
}

export async function DELETE(_req: NextRequest, { params }: { params: Promise<{ id: string }> }) {
  if (!(await adminSession())) return NextResponse.json({ error: "Forbidden" }, { status: 403 });
  const { id } = await params;
  await prisma.device.updateMany({ where: { farmId: id }, data: { farmId: null } });
  await prisma.farm.delete({ where: { id } });
  return NextResponse.json({ ok: true });
}
