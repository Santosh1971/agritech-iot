import { NextResponse } from "next/server";
import { projectAccess, studioUser } from "@/lib/studio/access";
import { draftSpec, type ProblemData } from "@/lib/studio/spec";
import { blockedBy, err, loadProject, saveStage } from "@/lib/studio/server";

const DRAFTS_PER_DAY = 10;

// Draft (or redraft) the specification from the saved problem.
export async function POST(_req: Request, { params }: { params: Promise<{ id: string }> }) {
  const { id } = await params;
  const u = await studioUser();
  if (!u) return err("Log in with a studio account.", 401);
  if (!(await projectAccess(u, id))) return err("Project not found.", 404);
  const state = await loadProject(id);
  if (!state) return err("Project not found.", 404);

  const blocked = blockedBy(state, "spec");
  if (blocked) return err(blocked, 409);
  const cur = state.stages.spec;
  if (cur.status === "DONE" || cur.status === "SUBMITTED") return err("The spec is finished or waiting for sign-off.", 409);

  // A simple daily limit per project keeps Claude costs predictable.
  const today = new Date().toISOString().slice(0, 10);
  const prev = (cur.data ?? {}) as { drafts?: { day: string; n: number } };
  const n = prev.drafts?.day === today ? prev.drafts.n : 0;
  if (n >= DRAFTS_PER_DAY) return err(`This project has used its ${DRAFTS_PER_DAY} drafts for today. Edit the text by hand, or try again tomorrow.`, 429);

  // Once the board is designed, the spec describes those parts.
  const spec = await draftSpec((state.stages.problem.data ?? {}) as ProblemData, state.kit, state.design?.ports);
  const data = { ...spec, drafts: { day: today, n: n + 1 } };
  await saveStage(id, "spec", { status: "IN_PROGRESS", data });
  return NextResponse.json({ spec: data });
}
