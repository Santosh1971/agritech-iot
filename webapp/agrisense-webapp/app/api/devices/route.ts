import { NextRequest, NextResponse } from "next/server";
import { cookies } from "next/headers";
import { verifySession } from "@/lib/session";
import { prisma } from "@/lib/prisma";

async function getSession() {
  const token = (await cookies()).get("agrisense_session")?.value;
  return token ? await verifySession(token) : null;
}

export async function GET() {
  const session = await getSession();
  if (!session) return NextResponse.json({ error: "Unauthorized" }, { status: 401 });

  const where =
    session.role === "ADMIN"
      ? {}
      : session.role === "DEALER"
      ? { dealerId: session.userId }
      : { ownerId: session.userId };

  const devices = await prisma.device.findMany({
    where,
    orderBy: { createdAt: "desc" },
    include: {
      owner: { select: { name: true, phone: true } },
      dealer: { select: { name: true, phone: true } },
    },
  });

  return NextResponse.json({ devices });
}

// Admin/Dealer only — manually register a device before it's ever sent an
// MQTT message (the bridge also auto-creates a placeholder row on first
// message, this covers pre-provisioning before the device is even powered on).
export async function POST(req: NextRequest) {
  const session = await getSession();
  if (!session || (session.role !== "ADMIN" && session.role !== "DEALER")) {
    return NextResponse.json({ error: "Forbidden" }, { status: 403 });
  }

  const { deviceId, product, name } = await req.json();
  if (!deviceId || !product || !name) {
    return NextResponse.json({ error: "deviceId, product, and name are required" }, { status: 400 });
  }

  try {
    const device = await prisma.device.create({
      data: {
        deviceId,
        product,
        name,
        dealerId: session.role === "DEALER" ? session.userId : undefined,
      },
    });
    return NextResponse.json({ device });
  } catch {
    return NextResponse.json({ error: "Device ID already exists" }, { status: 409 });
  }
}
