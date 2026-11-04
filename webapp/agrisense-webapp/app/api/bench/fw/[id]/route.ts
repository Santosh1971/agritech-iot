import { NextRequest, NextResponse } from "next/server";
import { getFirmware } from "@/lib/bench/store";
import { err, requireMember } from "@/lib/tracker/server";

// Firmware image queued by the developer, fetched by the bench page to flash.
export async function GET(_req: NextRequest, { params }: { params: Promise<{ id: string }> }) {
  const auth = await requireMember();
  if ("res" in auth) return auth.res;
  const fw = getFirmware((await params).id);
  if (!fw) return err("Firmware not found (the server may have restarted: send it again)", 404);
  return new NextResponse(new Uint8Array(fw.bytes), {
    headers: { "content-type": "application/octet-stream", "cache-control": "no-store" },
  });
}
