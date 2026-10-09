import { NextRequest, NextResponse } from "next/server";
import { prisma } from "@/lib/prisma";
import { projectAccess, studioUser } from "@/lib/studio/access";
import { err } from "@/lib/studio/server";

// Field-log records for a project: GET them (oldest first), or POST a batch
// downloaded from the board. Each record is kept once, by its time.
export async function GET(_req: NextRequest, { params }: { params: Promise<{ id: string }> }) {
  const { id } = await params;
  const u = await studioUser();
  if (!u) return err("Log in with a studio account.", 401);
  if (!(await projectAccess(u, id))) return err("Project not found.", 404);
  const rows = await prisma.studioFieldRecord.findMany({ where: { projectId: id }, orderBy: { t: "asc" }, take: 10000 });
  return NextResponse.json({ records: rows.map((r) => ({ t: r.t.toISOString(), design: r.design, values: r.values, outputs: r.outputs })) });
}

export async function POST(req: NextRequest, { params }: { params: Promise<{ id: string }> }) {
  const { id } = await params;
  const u = await studioUser();
  if (!u) return err("Log in with a studio account.", 401);
  if (!(await projectAccess(u, id))) return err("Project not found.", 404);
  const { records } = await req.json();
  if (!Array.isArray(records) || records.length > 5000) return err("Send up to 5000 records at a time.");
  const data = records
    .map((r: Record<string, unknown>) => ({
      projectId: id,
      t: new Date(Number(r.t) * 1000),
      design: Number(r.d) || 0,
      values: (r.v && typeof r.v === "object" ? r.v : {}) as object,
      outputs: (r.o && typeof r.o === "object" ? r.o : {}) as object,
    }))
    // A record needs a real time: from 2024 on, and not in the future.
    .filter((r) => r.t.getTime() > Date.UTC(2024, 0, 1) && r.t.getTime() < Date.now() + 864e5);
  const res = await prisma.studioFieldRecord.createMany({ data, skipDuplicates: true });
  const total = await prisma.studioFieldRecord.count({ where: { projectId: id } });
  return NextResponse.json({ added: res.count, total });
}
