import { prisma } from "@/lib/prisma";
import { readFirmwareBuild } from "@/lib/firmwareStorage";
import { studioUser } from "@/lib/studio/access";
import { err } from "@/lib/studio/server";

// The bytes of one ASC kit firmware build: ?part=app (default), bootloader or partitions.
export async function GET(req: Request, { params }: { params: Promise<{ id: string }> }) {
  const u = await studioUser();
  if (!u) return err("Log in with a studio account.", 401);
  const { id } = await params;
  const build = await prisma.firmwareBuild.findUnique({ where: { id } });
  if (!build || build.product !== "ASC_KIT") return err("Firmware not found.", 404);
  if (build.version.startsWith("dev-") && u.role !== "ADMIN") return err("Firmware not found.", 404);

  const part = new URL(req.url).searchParams.get("part") ?? "app";
  const path = part === "bootloader" ? build.bootloaderPath : part === "partitions" ? build.partitionsPath : part === "app" ? build.storagePath : null;
  if (!path) return err("This build has no such part.", 404);
  const bytes = await readFirmwareBuild(path);
  return new Response(new Uint8Array(bytes), {
    headers: { "Content-Type": "application/octet-stream", "Cache-Control": "private, max-age=3600" },
  });
}
