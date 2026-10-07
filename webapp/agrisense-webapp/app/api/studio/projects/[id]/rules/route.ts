import { NextRequest, NextResponse } from "next/server";
import { prisma } from "@/lib/prisma";
import { projectAccess, studioUser } from "@/lib/studio/access";
import { checkRules, cleanRules } from "@/lib/studio/automation";
import { blockedBy, err, loadProject, saveStage } from "@/lib/studio/server";

// Save the automation rules. The ports stay as Architecture left them; the
// rules become a new design version, so the board can tell which it runs.
export async function PUT(req: NextRequest, { params }: { params: Promise<{ id: string }> }) {
  const { id } = await params;
  const u = await studioUser();
  if (!u) return err("Log in with a studio account.", 401);
  if (!(await projectAccess(u, id))) return err("Project not found.", 404);
  const state = await loadProject(id);
  if (!state?.design) return err("Save a design in the Architecture stage first.", 409);
  const blocked = blockedBy(state, "build");
  if (blocked) return err(blocked, 409);
  if (state.stages.build.status === "DONE") return err("Build is finished. Reopen it to change the rules.", 409);

  const { rules: raw } = await req.json();
  const rules = cleanRules(raw);
  const version = state.design.version + 1;
  await prisma.studioDesign.create({ data: { projectId: id, version, ports: state.design.ports, rules, app: state.design.app ?? undefined, createdById: u.userId } });
  if (state.stages.build.status === "NOT_STARTED") await saveStage(id, "build", { status: "IN_PROGRESS" });
  return NextResponse.json({ version, rules, checks: checkRules(state.design.ports, rules) });
}
