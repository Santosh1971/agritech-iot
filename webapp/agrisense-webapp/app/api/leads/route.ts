import { NextRequest, NextResponse } from "next/server";
import type { Prisma } from "@prisma/client";
import { prisma } from "@/lib/prisma";
import { err } from "@/lib/tracker/server";
import { leadFields, leadsActor } from "@/lib/leads/server";

export const dynamic = "force-dynamic";

// GET -> all leads with notes, newest first.
export async function GET(req: NextRequest) {
  if (!(await leadsActor(req))) return err("Not allowed", 401);
  const leads = await prisma.lead.findMany({ include: { notes: { orderBy: { createdAt: "asc" } } }, orderBy: { createdAt: "desc" } });
  return NextResponse.json({ leads });
}

// POST {lead} or {leads:[...]} -> add, or update the same platform+handle.
// Status and notes are left alone on an update, so re-sending a lead never
// undoes what the team has done.
export async function POST(req: NextRequest) {
  const actor = await leadsActor(req);
  if (!actor) return err("Not allowed", 401);
  const body = await req.json().catch(() => null);
  const list: Record<string, unknown>[] = Array.isArray(body?.leads) ? body.leads : body ? [body] : [];
  if (list.length === 0 || list.length > 200) return err("Send 1 to 200 leads");

  const saved = [];
  for (const raw of list) {
    const f = leadFields(raw);
    if (!f.handle || !f.said || !f.kind) return err(`Each lead needs handle, kind and what they said (${f.handle ?? "?"})`);
    const platform = f.platform ?? "Instagram";
    const { status: _s, ...rest } = f;
    const data = { ...rest, platform } as Prisma.LeadUncheckedCreateInput;
    saved.push(
      await prisma.lead.upsert({
        where: { platform_handle: { platform, handle: f.handle } },
        create: { ...data, foundBy: actor },
        update: rest as Prisma.LeadUncheckedUpdateInput,
      }),
    );
  }
  return NextResponse.json({ saved: saved.length });
}
