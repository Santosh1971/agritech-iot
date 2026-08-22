import { NextRequest, NextResponse } from "next/server";
import { cookies } from "next/headers";
import { verifySession } from "@/lib/session";
import { prisma } from "@/lib/prisma";

async function getSession() {
  const token = (await cookies()).get("agrisense_session")?.value;
  return token ? await verifySession(token) : null;
}

export async function PATCH(req: NextRequest, { params }: { params: Promise<{ id: string }> }) {
  const session = await getSession();
  if (!session) return NextResponse.json({ error: "Unauthorized" }, { status: 401 });

  const { id } = await params;
  const device = await prisma.device.findUnique({ where: { id } });
  if (!device) return NextResponse.json({ error: "Not found" }, { status: 404 });

  // Customers may only rename their own device. Assigning ownerId/dealerId
  // is Admin-only — that's the provisioning step that hands a device to a
  // customer, matches the WM1-Mini access model.
  const body = await req.json();
  const data: Record<string, unknown> = {};

  if (session.role === "ADMIN") {
    if (typeof body.name === "string") data.name = body.name;
    if ("ownerPhone" in body) {
      if (!body.ownerPhone) data.ownerId = null;
      else {
        const owner = await prisma.user.findUnique({ where: { phone: body.ownerPhone } });
        if (!owner) return NextResponse.json({ error: "No user with that phone number" }, { status: 400 });
        data.ownerId = owner.id;
      }
    }
    if ("dealerPhone" in body) {
      if (!body.dealerPhone) data.dealerId = null;
      else {
        const dealer = await prisma.user.findUnique({ where: { phone: body.dealerPhone } });
        if (!dealer) return NextResponse.json({ error: "No user with that phone number" }, { status: 400 });
        data.dealerId = dealer.id;
      }
    }
  } else if (session.role === "DEALER" && device.dealerId === session.userId) {
    if (typeof body.name === "string") data.name = body.name;
    if ("ownerPhone" in body) {
      if (!body.ownerPhone) data.ownerId = null;
      else {
        const owner = await prisma.user.findUnique({ where: { phone: body.ownerPhone } });
        if (!owner) return NextResponse.json({ error: "No user with that phone number" }, { status: 400 });
        data.ownerId = owner.id;
      }
    }
  } else if (session.role === "CUSTOMER" && device.ownerId === session.userId) {
    if (typeof body.name === "string") data.name = body.name;
  } else {
    return NextResponse.json({ error: "Forbidden" }, { status: 403 });
  }

  const updated = await prisma.device.update({ where: { id }, data });
  return NextResponse.json({ device: updated });
}
