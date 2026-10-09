import { NextResponse } from "next/server";
import { prisma } from "@/lib/prisma";
import { studioUser } from "@/lib/studio/access";
import { err } from "@/lib/studio/server";

// The ASC kit firmware the Build stage flashes: the newest release for
// everyone, or the newest build of any kind for admins (CI's dev-<sha> builds
// from main), the same rule as the NB Agri Flasher.
export async function GET() {
  const u = await studioUser();
  if (!u) return err("Log in with a studio account.", 401);
  const builds = await prisma.firmwareBuild.findMany({
    where: { product: "ASC_KIT" },
    orderBy: { createdAt: "desc" },
    take: 20,
    select: { id: true, version: true, notes: true, createdAt: true, sizeBytes: true, bootloaderPath: true, partitionsPath: true },
  });
  const pick = builds.find((b) => u.role === "ADMIN" || !b.version.startsWith("dev-"));
  if (!pick) return NextResponse.json({ build: null });
  return NextResponse.json({
    build: {
      id: pick.id,
      version: pick.version,
      notes: pick.notes,
      createdAt: pick.createdAt,
      sizeBytes: pick.sizeBytes,
      full: !!(pick.bootloaderPath && pick.partitionsPath),
    },
  });
}
