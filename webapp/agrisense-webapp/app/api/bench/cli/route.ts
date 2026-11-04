import { timingSafeEqual } from "crypto";
import { NextRequest, NextResponse } from "next/server";
import { addFirmware, enqueue, note, snapshot } from "@/lib/bench/store";
import { err } from "@/lib/tracker/server";

// The developer's tool (tools/bench/bench.py). No session cookie: a static
// bearer token, BENCH_TOKEN in the server's .env (never in the repo).
function authorized(req: NextRequest) {
  const want = process.env.BENCH_TOKEN;
  const got = req.headers.get("authorization")?.replace(/^Bearer /, "") ?? "";
  if (!want || want.length < 24 || got.length !== want.length) return false;
  return timingSafeEqual(Buffer.from(got), Buffer.from(want));
}

// GET ?after=SEQ -> new serial lines + page status.
export async function GET(req: NextRequest) {
  if (!authorized(req)) return err("Unauthorized", 401);
  const after = Number(req.nextUrl.searchParams.get("after") ?? 0) || 0;
  return NextResponse.json(snapshot(after));
}

// POST JSON {type:"write", data} | {type:"reset"}
// POST multipart file=<image.bin>, offset=0x0 -> queue a flash.
export async function POST(req: NextRequest) {
  if (!authorized(req)) return err("Unauthorized", 401);
  const ct = req.headers.get("content-type") ?? "";
  if (ct.startsWith("multipart/form-data")) {
    const form = await req.formData();
    const file = form.get("file");
    if (!(file instanceof File)) return err("file is required");
    if (file.size > 16 * 1024 * 1024) return err("Firmware larger than 16 MB");
    const offset = Number(form.get("offset") ?? 0);
    if (!Number.isInteger(offset) || offset < 0 || offset > 0x3fffff) return err("Bad offset");
    const bytes = Buffer.from(await file.arrayBuffer());
    const fwId = addFirmware(file.name, bytes);
    const cmd = enqueue({ type: "flash", fwId, name: file.name, offset, size: bytes.length });
    note(`flash requested: ${file.name} (${bytes.length} bytes) at 0x${offset.toString(16)}`);
    return NextResponse.json({ cmd });
  }
  let body: { type?: string; data?: unknown };
  try {
    body = await req.json();
  } catch {
    return err("Bad JSON");
  }
  if (body.type === "write" && typeof body.data === "string" && body.data.length <= 4000) {
    const cmd = enqueue({ type: "write", data: body.data });
    note(`> ${body.data.trimEnd()}`);
    return NextResponse.json({ cmd });
  }
  if (body.type === "reset") {
    const cmd = enqueue({ type: "reset" });
    note("reset requested");
    return NextResponse.json({ cmd });
  }
  return err("Unknown command");
}
