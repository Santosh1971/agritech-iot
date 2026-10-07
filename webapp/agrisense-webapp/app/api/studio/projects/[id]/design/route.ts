import { NextRequest, NextResponse } from "next/server";
import { prisma } from "@/lib/prisma";
import { projectAccess, studioUser } from "@/lib/studio/access";
import { cleanPorts, checks } from "@/lib/studio/rules";
import { blockedBy, err, loadProject, saveStage } from "@/lib/studio/server";

// Save the block-to-port assignment as a new design version.
export async function PUT(req: NextRequest, { params }: { params: Promise<{ id: string }> }) {
  const { id } = await params;
  const u = await studioUser();
  if (!u) return err("Log in with a studio account.", 401);
  const access = await projectAccess(u, id);
  if (!access?.canEdit) return err("Project not found.", 404);

  const state = await loadProject(id);
  if (!state) return err("Project not found.", 404);
  const blocked = blockedBy(state, "arch");
  if (blocked) return err(blocked, 409);
  if (state.stages.arch.status === "DONE") return err("Architecture is finished. Reopen it to change the design.", 409);

  const { ports: raw } = await req.json();
  const ports = cleanPorts(state.kit, raw);
  const version = (state.design?.version ?? 0) + 1;
  await prisma.studioDesign.create({ data: { projectId: id, version, ports, createdById: u.userId } });
  if (state.stages.arch.status === "NOT_STARTED") await saveStage(id, "arch", { status: "IN_PROGRESS" });
  return NextResponse.json({ version, ports, checks: checks(state.kit, ports) });
}
