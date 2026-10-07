// Server-side helpers shared by the studio pages and API routes.
import { NextResponse } from "next/server";
import { Prisma } from "@prisma/client";
import { prisma } from "@/lib/prisma";
import { STAGES, currentStageIndex, stageDef, type StageKey, type StageStatus } from "./stages";
import type { Ports } from "./rules";
import type { Rule } from "./automation";

export function err(message: string, status = 400) {
  return NextResponse.json({ error: message }, { status });
}

export async function loadProject(projectId: string) {
  const p = await prisma.studioProject.findUnique({
    where: { id: projectId },
    include: {
      cohort: { select: { id: true, name: true, institution: true, session: true } },
      members: { include: { user: { select: { id: true, name: true, email: true } } } },
      stages: { include: { signedOffBy: { select: { name: true } } } },
      designs: { orderBy: { version: "desc" }, take: 1 },
    },
  });
  if (!p) return null;
  const byKey = Object.fromEntries(p.stages.map((s) => [s.stage, s]));
  const status = Object.fromEntries(STAGES.map((s) => [s.key, (byKey[s.key]?.status ?? "NOT_STARTED") as StageStatus]));
  return {
    id: p.id,
    title: p.title,
    kit: p.kit,
    cohort: p.cohort,
    members: p.members.map((m) => m.user),
    stages: Object.fromEntries(
      STAGES.map((s) => {
        const row = byKey[s.key];
        return [s.key, {
          status: status[s.key],
          data: (row?.data ?? null) as Record<string, unknown> | null,
          signedOffBy: row?.signedOffBy?.name ?? null,
          signedOffAt: row?.signedOffAt?.toISOString() ?? null,
        }];
      }),
    ) as Record<StageKey, { status: StageStatus; data: Record<string, unknown> | null; signedOffBy: string | null; signedOffAt: string | null }>,
    design: p.designs[0]
      ? { version: p.designs[0].version, ports: p.designs[0].ports as Ports, rules: (p.designs[0].rules ?? null) as Rule[] | null }
      : null,
    current: currentStageIndex(status),
  };
}

export type ProjectState = NonNullable<Awaited<ReturnType<typeof loadProject>>>;

export async function saveStage(projectId: string, stage: StageKey, patch: { status?: StageStatus; data?: unknown; signedOffById?: string | null; signedOffAt?: Date | null }) {
  const data = patch.data === undefined ? undefined : (patch.data as Prisma.InputJsonValue);
  return prisma.studioStage.upsert({
    where: { projectId_stage: { projectId, stage } },
    create: { projectId, stage, status: patch.status ?? "IN_PROGRESS", data, signedOffById: patch.signedOffById ?? null, signedOffAt: patch.signedOffAt ?? null },
    update: { status: patch.status, data, signedOffById: patch.signedOffById, signedOffAt: patch.signedOffAt },
  });
}

// A stage can only be finished once every required stage before it is done.
export function blockedBy(state: ProjectState, stage: StageKey): string | null {
  const idx = STAGES.findIndex((s) => s.key === stage);
  const open = STAGES.slice(0, idx).find((s) => s.gate !== "optional" && state.stages[s.key].status !== "DONE");
  return open ? `Finish "${open.title}" first.` : null;
}

export function isStageKey(k: string): k is StageKey {
  return !!stageDef(k);
}
