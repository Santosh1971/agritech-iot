import { NextRequest, NextResponse } from "next/server";
import { prisma } from "@/lib/prisma";
import { err, trackUser } from "@/lib/tracker/server";

// Admins manage the testing team. JSON POST { email, role: TESTER|DEVELOPER, active? }.
export async function POST(req: NextRequest) {
  const user = await trackUser();
  if (!user?.isAdmin) return err("Admins only", 403);
  const b = (await req.json().catch(() => null)) as { email?: string; role?: string; active?: boolean } | null;
  const email = b?.email?.trim().toLowerCase();
  if (!email) return err("Email is required");
  if (b?.role !== "TESTER" && b?.role !== "DEVELOPER") return err("Role must be TESTER or DEVELOPER");
  const target = await prisma.user.findUnique({ where: { email } });
  if (!target) return err("No account with that email. Create the user first (they need to be able to log in).");
  const member = await prisma.trackMember.upsert({
    where: { userId: target.id },
    create: { userId: target.id, role: b.role },
    update: { role: b.role, active: b.active ?? true },
  });
  return NextResponse.json({ member });
}
