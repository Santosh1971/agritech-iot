import { NextRequest, NextResponse } from "next/server";
import { prisma } from "@/lib/prisma";
import { studioUser } from "@/lib/studio/access";
import { err } from "@/lib/studio/server";

// Admin only: start a new cohort (one class at one college, or Project #0).
export async function POST(req: NextRequest) {
  const u = await studioUser();
  if (!u || u.role !== "ADMIN") return err("Only an admin can create a cohort.", 403);
  const { name, institution, session } = await req.json();
  if (!name?.trim() || !institution?.trim()) return err("Give the cohort a name and an institution.");
  const cohort = await prisma.studioCohort.create({
    data: {
      name: name.trim(),
      institution: institution.trim(),
      session: session?.trim() || null,
      createdById: u.userId,
      members: { create: { userId: u.userId } },
    },
  });
  return NextResponse.json({ cohort });
}
