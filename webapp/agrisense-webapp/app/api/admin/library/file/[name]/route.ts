import { NextRequest, NextResponse } from "next/server";
import { cookies } from "next/headers";
import { verifySession } from "@/lib/session";
import { libraryFilePath } from "@/lib/library";
import { serveFile } from "@/lib/serveFile";

// Admins only: one file from the document library.
export async function GET(req: NextRequest, { params }: { params: Promise<{ name: string }> }) {
  const token = (await cookies()).get("agrisense_session")?.value;
  const session = token ? await verifySession(token) : null;
  if (!session || session.role !== "ADMIN") return NextResponse.json({ error: "Unauthorized" }, { status: 401 });

  const { name } = await params;
  const path = await libraryFilePath(decodeURIComponent(name));
  if (!path) return NextResponse.json({ error: "Not found" }, { status: 404 });
  return serveFile(req, path, name, "private, max-age=300");
}
