import { NextRequest, NextResponse } from "next/server";
import { homedir } from "os";
import { join } from "path";
import { serveFile } from "@/lib/serveFile";

// Public: product videos shown on /products/<slug>. The MP4s live on the server
// in ~/agrisense-data/site-videos (kept out of the public repo because of size);
// tools/site-videos/publish.sh uploads them. Only names listed in a product's
// `videos` are meant to be linked, but any plain file name in that folder is served.
const root = () => join(process.env.WORKSHOP_SURVEY_DIR || join(homedir(), "agrisense-data"), "site-videos");

export async function GET(req: NextRequest, { params }: { params: Promise<{ name: string }> }) {
  const name = decodeURIComponent((await params).name);
  if (!/^[a-z0-9][a-z0-9._-]*\.(mp4|jpg)$/i.test(name)) return NextResponse.json({ error: "Not found" }, { status: 404 });
  return serveFile(req, join(root(), name), name, "public, max-age=86400");
}
