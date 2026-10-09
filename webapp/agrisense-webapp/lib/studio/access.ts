// Who can see and change what in the studio.
//   ADMIN   — everything; works as the ASC designer.
//   TEACHER — cohorts they belong to: every project in them, can sign off gates.
//   STUDENT — only projects they are a team member of; no sign-off.
// Dealers and customers have no studio access.
import { cookies } from "next/headers";
import { prisma } from "@/lib/prisma";
import { verifySession, type SessionPayload } from "@/lib/session";

export type StudioUser = SessionPayload;

export async function studioUser(): Promise<StudioUser | null> {
  const token = (await cookies()).get("agrisense_session")?.value;
  const s = token ? await verifySession(token) : null;
  if (!s || !["ADMIN", "TEACHER", "STUDENT"].includes(s.role)) return null;
  return s;
}

export async function isCohortMember(userId: string, cohortId: string): Promise<boolean> {
  const m = await prisma.studioCohortMember.findUnique({ where: { cohortId_userId: { cohortId, userId } } });
  return !!m;
}

// Cohorts this user may open.
export function cohortFilter(u: StudioUser) {
  return u.role === "ADMIN" ? {} : { members: { some: { userId: u.userId } } };
}

// Projects this user may open.
export function projectFilter(u: StudioUser) {
  if (u.role === "ADMIN") return {};
  if (u.role === "TEACHER") return { cohort: { members: { some: { userId: u.userId } } } };
  return { members: { some: { userId: u.userId } } };
}

export type ProjectAccess = { canEdit: boolean; canSignOff: boolean };

export async function projectAccess(u: StudioUser, projectId: string): Promise<ProjectAccess | null> {
  const p = await prisma.studioProject.findFirst({ where: { id: projectId, ...projectFilter(u) }, select: { id: true } });
  if (!p) return null;
  return { canEdit: true, canSignOff: u.role === "ADMIN" || u.role === "TEACHER" };
}
