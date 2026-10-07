import { NextRequest, NextResponse } from "next/server";
import { prisma } from "@/lib/prisma";
import { isCohortMember, studioUser } from "@/lib/studio/access";
import { err } from "@/lib/studio/server";

// Add a student (teachers and admins) or a teacher (admins only) to a cohort.
// There is no self-signup, so this is also how studio accounts are created:
// the person then logs in with the email OTP as usual.
export async function POST(req: NextRequest, { params }: { params: Promise<{ id: string }> }) {
  const { id } = await params;
  const u = await studioUser();
  if (!u || u.role === "STUDENT") return err("Only a teacher or an admin can add people.", 403);
  if (u.role === "TEACHER" && !(await isCohortMember(u.userId, id))) return err("You are not a teacher in this cohort.", 403);

  const body = await req.json();
  const email = String(body.email ?? "").trim().toLowerCase();
  const name = String(body.name ?? "").trim();
  const role = body.role === "TEACHER" ? "TEACHER" : "STUDENT";
  if (!/^[^@\s]+@[^@\s]+\.[^@\s]+$/.test(email)) return err("Enter a valid email address.");
  if (role === "TEACHER" && u.role !== "ADMIN") return err("Only an admin can add a teacher.", 403);

  const cohort = await prisma.studioCohort.findUnique({ where: { id } });
  if (!cohort) return err("Cohort not found.", 404);

  let user = await prisma.user.findUnique({ where: { email } });
  if (user && !["STUDENT", "TEACHER", "ADMIN"].includes(user.role)) {
    return err("This email already belongs to a dealer or customer account. Use a different email for the studio.", 409);
  }
  if (!user) {
    if (!name) return err("Enter the person's name.");
    user = await prisma.user.create({ data: { email, name, role } });
  }
  await prisma.studioCohortMember.upsert({
    where: { cohortId_userId: { cohortId: id, userId: user.id } },
    create: { cohortId: id, userId: user.id },
    update: {},
  });
  return NextResponse.json({ member: { id: user.id, name: user.name, email: user.email, role: user.role } });
}
