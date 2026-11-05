import { NextRequest, NextResponse } from "next/server";
import type { Prisma } from "@prisma/client";
import { prisma } from "@/lib/prisma";
import { err } from "@/lib/tracker/server";
import { STATUS_LABEL, leadFields, leadsActor } from "@/lib/leads/server";

// PATCH {status?, nextStep?, phone?, ...} -> change a lead. A status change
// also leaves a note saying who moved it, so the history reads like a thread.
export async function PATCH(req: NextRequest, { params }: { params: Promise<{ id: string }> }) {
  const actor = await leadsActor(req);
  if (!actor) return err("Not allowed", 401);
  const { id } = await params;
  const lead = await prisma.lead.findUnique({ where: { id } });
  if (!lead) return err("No such lead", 404);

  const f = leadFields((await req.json().catch(() => ({}))) ?? {});
  delete f.handle;
  delete f.platform;
  const updated = await prisma.lead.update({ where: { id }, data: f as Prisma.LeadUncheckedUpdateInput });
  if (f.status && f.status !== lead.status) {
    await prisma.leadNote.create({
      data: { leadId: id, author: actor, text: `Status: ${STATUS_LABEL[lead.status]} → ${STATUS_LABEL[updated.status]}` },
    });
  }
  return NextResponse.json({ lead: updated });
}
