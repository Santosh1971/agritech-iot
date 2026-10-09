import { NextRequest, NextResponse } from "next/server";
import { prisma } from "@/lib/prisma";
import { readTrackFile, trackUser } from "@/lib/tracker/server";

// Download or view one tracker file. Members only (admins too, read-only).
export async function GET(req: NextRequest, { params }: { params: Promise<{ id: string }> }) {
  const user = await trackUser();
  if (!user || (!user.trackRole && !user.isAdmin)) return NextResponse.json({ error: "Forbidden" }, { status: 403 });
  const { id } = await params;
  const f = await prisma.trackFile.findUnique({ where: { id } });
  if (!f) return NextResponse.json({ error: "Not found" }, { status: 404 });
  let bytes: Buffer;
  try {
    bytes = await readTrackFile(f.storagePath);
  } catch {
    return NextResponse.json({ error: "File missing on disk" }, { status: 410 });
  }
  // Photos and videos open inline; everything else (bin, apk, zip) downloads.
  const inline = /^(image|video)\//.test(f.mime) && req.nextUrl.searchParams.get("download") === null;
  const mime = f.kind === "APK" ? "application/vnd.android.package-archive" : inline ? f.mime : "application/octet-stream";
  return new NextResponse(new Uint8Array(bytes), {
    headers: {
      "Content-Type": mime,
      "Content-Length": String(bytes.length),
      "Content-Disposition": `${inline ? "inline" : "attachment"}; filename="${f.originalName.replace(/["\\\r\n]/g, "_")}"`,
      "X-Content-Type-Options": "nosniff",
      "Cache-Control": "private, max-age=3600",
    },
  });
}
