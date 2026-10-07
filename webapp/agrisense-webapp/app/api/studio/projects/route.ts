import { NextRequest, NextResponse } from "next/server";
import { prisma } from "@/lib/prisma";
import { isCohortMember, studioUser } from "@/lib/studio/access";
import { err } from "@/lib/studio/server";

// Start a project in a cohort. A student who creates one joins its team;
// teammates must already be in the cohort and are added by email.
export async function POST(req: NextRequest) {
  const u = await studioUser();
  if (!u) return err("Log in with a studio account.", 401);
  const body = await req.json();
  const cohortId = String(body.cohortId ?? "");
  const title = String(body.title ?? "").trim();
  const kit = body.kit === "MEGA" ? "MEGA" : "MINI";
  if (!title) return err("Give the project a title.");
  if (u.role !== "ADMIN" && !(await isCohortMember(u.userId, cohortId))) return err("You are not in this cohort.", 403);

  const emails: string[] = Array.isArray(body.teamEmails)
    ? body.teamEmails.map((e: unknown) => String(e).trim().toLowerCase()).filter(Boolean)
    : [];
  const team = emails.length
    ? await prisma.user.findMany({ where: { email: { in: emails }, cohortMembers: { some: { cohortId } } }, select: { id: true, email: true } })
    : [];
  const missing = emails.filter((e) => !team.some((t) => t.email === e));
  if (missing.length) return err(`Not in this cohort yet: ${missing.join(", ")}. Ask your teacher to add them first.`);

  const memberIds = new Set(team.map((t) => t.id));
  if (u.role === "STUDENT") memberIds.add(u.userId);

  const project = await prisma.studioProject.create({
    data: {
      title,
      kit,
      cohortId,
      createdById: u.userId,
      members: { create: [...memberIds].map((userId) => ({ userId })) },
    },
  });
  return NextResponse.json({ project: { id: project.id } });
}
