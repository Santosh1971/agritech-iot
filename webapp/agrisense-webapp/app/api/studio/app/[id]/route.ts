import { prisma } from "@/lib/prisma";
import { readFirmwareBuild } from "@/lib/firmwareStorage";
import { studioUser } from "@/lib/studio/access";
import { err } from "@/lib/studio/server";

export async function GET(_req: Request, { params }: { params: Promise<{ id: string }> }) {
  const u = await studioUser();
  if (!u) return err("Log in with a studio account.", 401);
  const { id } = await params;
  const b = await prisma.mobileAppBuild.findUnique({ where: { id } });
  if (!b || b.product !== "ASC_KIT") return err("App not found.", 404);
  if (b.versionName.startsWith("dev-") && u.role !== "ADMIN") return err("App not found.", 404);
  const bytes = await readFirmwareBuild(b.storagePath);
  return new Response(new Uint8Array(bytes), {
    headers: {
      "Content-Type": "application/vnd.android.package-archive",
      "Content-Disposition": `attachment; filename="asc-studio-${b.versionName}.apk"`,
    },
  });
}
