import { NextResponse } from "next/server";
import { prisma } from "@/lib/prisma";
import { studioUser } from "@/lib/studio/access";
import { err } from "@/lib/studio/server";

// The ASC Studio phone app students install in the App stage: the newest
// release (asc-app-v* tag), or for admins the newest build of any kind.
export async function GET() {
  const u = await studioUser();
  if (!u) return err("Log in with a studio account.", 401);
  const builds = await prisma.mobileAppBuild.findMany({
    where: { product: "ASC_KIT" },
    orderBy: { createdAt: "desc" },
    take: 20,
    select: { id: true, versionName: true, createdAt: true, sizeBytes: true },
  });
  const pick = builds.find((b) => u.role === "ADMIN" || !b.versionName.startsWith("dev-"));
  return NextResponse.json({ app: pick ?? null });
}
