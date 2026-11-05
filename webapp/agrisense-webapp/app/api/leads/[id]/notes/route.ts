import { NextRequest, NextResponse } from "next/server";
import { prisma } from "@/lib/prisma";
import { err } from "@/lib/tracker/server";
import { leadsActor } from "@/lib/leads/server";

// POST {text} -> add a note to a lead.
export async function POST(req: NextRequest, { params }: { params: Promise<{ id: string }> }) {
  const actor = await leadsActor(req);
  if (!actor) return err("Not allowed", 401);
  const { id } = await params;
  const text = String((await req.json().catch(() => ({})))?.text ?? "").trim().slice(0, 4000);
  if (!text) return err("Write something first");
  if (!(await prisma.lead.findUnique({ where: { id }, select: { id: true } }))) return err("No such lead", 404);
  const note = await prisma.leadNote.create({ data: { leadId: id, author: actor, text } });
  return NextResponse.json({ note });
}
