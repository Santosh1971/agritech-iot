import { NextRequest, NextResponse } from "next/server";
import { prisma } from "@/lib/prisma";
import { projectAccess, studioUser } from "@/lib/studio/access";
import { canBuild } from "@/lib/studio/rules";
import { problemComplete, type ProblemData, type SpecData } from "@/lib/studio/spec";
import { stageDef } from "@/lib/studio/stages";
import { blockedBy, err, isStageKey, loadProject, saveStage } from "@/lib/studio/server";

type Ctx = { params: Promise<{ id: string; stage: string }> };

async function setup(ctx: Ctx) {
  const { id, stage } = await ctx.params;
  const u = await studioUser();
  if (!u) return { error: err("Log in with a studio account.", 401) };
  if (!isStageKey(stage)) return { error: err("Unknown stage.", 404) };
  const access = await projectAccess(u, id);
  if (!access) return { error: err("Project not found.", 404) };
  const state = await loadProject(id);
  if (!state) return { error: err("Project not found.", 404) };
  return { u, id, stage, access, state };
}

// Save a stage's working data (problem facts, edited spec text...).
export async function PUT(req: NextRequest, ctx: Ctx) {
  const s = await setup(ctx);
  if ("error" in s) return s.error;
  const { id, stage, state } = s;
  const cur = state.stages[stage];
  if (cur.status === "DONE" || cur.status === "SUBMITTED") {
    return err("This stage is finished or waiting for sign-off. Reopen it to make changes.", 409);
  }
  const body = await req.json();

  if (stage === "problem") {
    const p = body.data as ProblemData;
    const clean: ProblemData = {
      story: String(p?.story ?? "").slice(0, 3000),
      crop: String(p?.crop ?? "").slice(0, 200),
      area: String(p?.area ?? "").slice(0, 200),
      water: String(p?.water ?? "").slice(0, 200),
      climate: String(p?.climate ?? "").slice(0, 300),
      power: ["mains", "solar", "none"].includes(p?.power as string) ? p.power : undefined,
      network: ["wifi", "mobile", "none"].includes(p?.network as string) ? p.network : undefined,
    };
    // The kit can change until a design has been saved for it.
    if ((body.kit === "MINI" || body.kit === "MEGA") && body.kit !== state.kit) {
      if (state.design) return err("A design is already saved for this kit, so the kit can't change now.", 409);
      await prisma.studioProject.update({ where: { id }, data: { kit: body.kit } });
    }
    await saveStage(id, "problem", { status: "IN_PROGRESS", data: clean });
    return NextResponse.json({ ok: true });
  }

  if (stage === "spec") {
    const prev = (cur.data ?? {}) as Partial<SpecData>;
    const d = body.data as Partial<SpecData>;
    const reqs = Array.isArray(d?.requirements)
      ? d.requirements.slice(0, 20).map((r) => ({ id: String(r.id ?? "").slice(0, 12), text: String(r.text ?? "").slice(0, 400) })).filter((r) => r.text)
      : prev.requirements ?? [];
    const next = {
      ...prev,
      what: String(d?.what ?? prev.what ?? "").slice(0, 2000),
      madeOf: String(d?.madeOf ?? prev.madeOf ?? "").slice(0, 1000),
      use: String(d?.use ?? prev.use ?? "").slice(0, 1000),
      requirements: reqs,
    };
    await saveStage(id, "spec", { status: "IN_PROGRESS", data: next });
    return NextResponse.json({ ok: true });
  }

  return err("This stage has no saved data yet.", 400);
}

// Move a stage through its gate.
//   complete — student says it's done: auto gates finish now, mentor gates go to SUBMITTED
//   approve  — mentor signs off a SUBMITTED stage
//   return   — mentor sends it back with a comment
//   reopen   — reopen a finished stage (mentor gates need a mentor)
export async function POST(req: NextRequest, ctx: Ctx) {
  const s = await setup(ctx);
  if ("error" in s) return s.error;
  const { u, id, stage, access, state } = s;
  const def = stageDef(stage)!;
  const cur = state.stages[stage];
  const { action, comment } = await req.json();
  const data = { ...(cur.data ?? {}) };

  if (action === "complete") {
    if (!def.ready) return err("This stage isn't available yet.");
    if (cur.status === "DONE" || cur.status === "SUBMITTED") return err("Already finished or waiting for sign-off.", 409);
    const blocked = blockedBy(state, stage);
    if (blocked) return err(blocked, 409);

    if (stage === "problem") {
      const missing = problemComplete((cur.data ?? {}) as ProblemData);
      if (missing.length) return err(missing.join(" "));
    }
    if (stage === "spec" && !(cur.data as Partial<SpecData> | null)?.what) return err("Draft the specification first.");
    if (stage === "arch" && !(state.design && canBuild(state.kit, state.design.ports))) {
      return err("Fix the red checks and save the design first.");
    }

    delete data.returnedComment;
    const status = def.gate === "mentor" ? "SUBMITTED" : "DONE";
    await saveStage(id, stage, { status, data });
    return NextResponse.json({ status });
  }

  if (action === "approve" || action === "return") {
    if (!access.canSignOff) return err("Only a teacher or an admin can sign off.", 403);
    if (cur.status !== "SUBMITTED") return err("This stage isn't waiting for sign-off.", 409);
    if (action === "approve") {
      delete data.returnedComment;
      await saveStage(id, stage, { status: "DONE", data, signedOffById: u.userId, signedOffAt: new Date() });
      return NextResponse.json({ status: "DONE" });
    }
    data.returnedComment = String(comment ?? "").slice(0, 1000) || "Please revise and submit again.";
    await saveStage(id, stage, { status: "IN_PROGRESS", data });
    return NextResponse.json({ status: "IN_PROGRESS" });
  }

  if (action === "reopen") {
    if (cur.status !== "DONE") return err("Only a finished stage can be reopened.", 409);
    if (def.gate === "mentor" && !access.canSignOff) return err("Ask your teacher to reopen a signed-off stage.", 403);
    await saveStage(id, stage, { status: "IN_PROGRESS", data, signedOffById: null, signedOffAt: null });
    return NextResponse.json({ status: "IN_PROGRESS" });
  }

  return err("Unknown action.");
}
