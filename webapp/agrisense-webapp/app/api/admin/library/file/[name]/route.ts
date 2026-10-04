import { NextRequest, NextResponse } from "next/server";
import { cookies } from "next/headers";
import { createReadStream } from "fs";
import { stat } from "fs/promises";
import { Readable } from "stream";
import { verifySession } from "@/lib/session";
import { libraryFilePath } from "@/lib/library";

// Admins only: one file from the document library. Supports HTTP Range so
// videos can be seeked and play on iPhone; ?download=1 saves instead of viewing.
const TYPES: Record<string, string> = { pdf: "application/pdf", mp4: "video/mp4", mp3: "audio/mpeg", png: "image/png" };

export async function GET(req: NextRequest, { params }: { params: Promise<{ name: string }> }) {
  const token = (await cookies()).get("agrisense_session")?.value;
  const session = token ? await verifySession(token) : null;
  if (!session || session.role !== "ADMIN") return NextResponse.json({ error: "Unauthorized" }, { status: 401 });

  const { name } = await params;
  const path = await libraryFilePath(decodeURIComponent(name));
  if (!path) return NextResponse.json({ error: "Not found" }, { status: 404 });
  let size: number;
  try {
    size = (await stat(path)).size;
  } catch {
    return NextResponse.json({ error: "Not found" }, { status: 404 });
  }

  const ext = path.split(".").pop()?.toLowerCase() ?? "";
  const headers: Record<string, string> = {
    "Content-Type": TYPES[ext] ?? "application/octet-stream",
    "Accept-Ranges": "bytes",
    "Cache-Control": "private, max-age=300",
    "Content-Disposition": `${req.nextUrl.searchParams.get("download") ? "attachment" : "inline"}; filename="${name}"`,
  };

  const range = req.headers.get("range")?.match(/bytes=(\d*)-(\d*)/);
  if (range) {
    const start = range[1] ? parseInt(range[1], 10) : Math.max(0, size - parseInt(range[2], 10));
    const end = range[1] && range[2] ? Math.min(parseInt(range[2], 10), size - 1) : size - 1;
    if (start >= size || start > end) {
      return new NextResponse(null, { status: 416, headers: { "Content-Range": `bytes */${size}` } });
    }
    const body = Readable.toWeb(createReadStream(path, { start, end })) as ReadableStream;
    return new NextResponse(body, {
      status: 206,
      headers: { ...headers, "Content-Range": `bytes ${start}-${end}/${size}`, "Content-Length": String(end - start + 1) },
    });
  }
  const body = Readable.toWeb(createReadStream(path)) as ReadableStream;
  return new NextResponse(body, { status: 200, headers: { ...headers, "Content-Length": String(size) } });
}
