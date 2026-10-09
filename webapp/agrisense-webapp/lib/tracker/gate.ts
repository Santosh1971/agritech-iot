// Loads what the release gate needs from the database (rules: ./rules.ts).
import { prisma } from "@/lib/prisma";
import { releaseGate, type ChecklistItem } from "./rules";

export async function loadGate(releaseId: string) {
  const release = await prisma.trackRelease.findUnique({
    where: { id: releaseId },
    include: { candidateBuild: { include: { issues: true } } },
  });
  if (!release) return null;
  const issues = await prisma.trackIssue.findMany({
    where: { productId: release.productId },
    select: { id: true, releaseId: true, status: true, severity: true, proposal: true },
  });
  const c = release.candidateBuild;
  const gate = releaseGate({
    issues,
    releaseId,
    candidate: c ? { checklist: c.checklist as ChecklistItem[], verdicts: c.issues } : null,
    signedOff: release.status !== "OPEN",
  });
  return { release, gate };
}
