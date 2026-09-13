import { NextRequest, NextResponse } from "next/server";
import { cookies } from "next/headers";
import { verifySession } from "@/lib/session";
import { prisma } from "@/lib/prisma";

async function getSession() {
  const token = (await cookies()).get("agrisense_session")?.value;
  return token ? await verifySession(token) : null;
}

// The entire revoke/restore action: flip `active`. Checked live by the app
// on every request (see /api/flasher/*), never cached client-side, so this
// takes effect immediately — it just can't delete a bin already sitting on
// someone's phone from an earlier, still-valid download.
export async function PATCH(req: NextRequest, { params }: { params: Promise<{ id: string }> }) {
  const session = await getSession();
  if (!session || session.role !== "ADMIN") {
    return NextResponse.json({ error: "Forbidden" }, { status: 403 });
  }

  const { id } = await params;
  const { active } = await req.json();
  if (typeof active !== "boolean") {
    return NextResponse.json({ error: "active (boolean) is required" }, { status: 400 });
  }

  const grant = await prisma.flasherGrant.findUnique({ where: { id } });
  if (!grant) return NextResponse.json({ error: "Not found" }, { status: 404 });

  const updated = await prisma.flasherGrant.update({
    where: { id },
    data: { active, revokedAt: active ? null : new Date() },
  });

  return NextResponse.json({ grant: updated });
}
