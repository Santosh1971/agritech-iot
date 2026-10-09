import { NextRequest, NextResponse } from "next/server";
import { prisma } from "@/lib/prisma";
import { projectAccess, studioUser } from "@/lib/studio/access";
import { checkLayout, cleanLayout } from "@/lib/studio/appLayout";
import { blockedBy, err, loadProject, saveStage } from "@/lib/studio/server";

// Save the phone screen layout as a new design version (ports and rules carried over).
export async function PUT(req: NextRequest, { params }: { params: Promise<{ id: string }> }) {
  const { id } = await params;
  const u = await studioUser();
  if (!u) return err("Log in with a studio account.", 401);
  if (!(await projectAccess(u, id))) return err("Project not found.", 404);
  const state = await loadProject(id);
  if (!state?.design) return err("Save a design in the Architecture stage first.", 409);
  const blocked = blockedBy(state, "app");
  if (blocked) return err(blocked, 409);
  if (state.stages.app.status === "DONE") return err("The App stage is finished. Reopen it to change the layout.", 409);

  const { app: raw } = await req.json();
  const app = cleanLayout(raw, state.title);
  if (checkLayout(state.design.ports, app).some((c) => c.level === "bad")) return err("Fix the red checks first.");
  const version = state.design.version + 1;
  await prisma.studioDesign.create({
    data: { projectId: id, version, ports: state.design.ports, rules: state.design.rules ?? undefined, app, createdById: u.userId },
  });
  if (state.stages.app.status === "NOT_STARTED") await saveStage(id, "app", { status: "IN_PROGRESS" });
  return NextResponse.json({ version, app });
}
