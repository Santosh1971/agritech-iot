import { NextRequest, NextResponse } from "next/server";
import { appendFile, mkdir } from "fs/promises";
import { homedir } from "os";
import { join } from "path";

// Public endpoint for the website contact form at /contact (no login).
// Each enquiry is appended as one JSON line to a file outside the repo, like
// the workshop survey, so a deploy never touches it.

const MAX_BODY = 8_000;
const FIELDS: Record<string, number> = { name: 100, phone: 30, email: 120, role: 60, place: 100, message: 2000 };

function contactFile(): string {
  const dir = process.env.WORKSHOP_SURVEY_DIR || join(homedir(), "agrisense-data");
  return join(dir, "contact-enquiries.jsonl");
}

export async function POST(req: NextRequest) {
  const raw = await req.text();
  if (raw.length > MAX_BODY) return NextResponse.json({ error: "Message is too long" }, { status: 413 });

  let body: unknown;
  try {
    body = JSON.parse(raw);
  } catch {
    return NextResponse.json({ error: "Invalid request" }, { status: 400 });
  }
  if (!body || typeof body !== "object" || Array.isArray(body)) {
    return NextResponse.json({ error: "Invalid request" }, { status: 400 });
  }

  const enquiry: Record<string, string> = {};
  for (const [key, max] of Object.entries(FIELDS)) {
    const v = (body as Record<string, unknown>)[key];
    if (typeof v === "string" && v.trim()) enquiry[key] = v.trim().slice(0, max);
  }
  if (!enquiry.name || !enquiry.phone || !enquiry.message) {
    return NextResponse.json({ error: "Name, phone and message are required" }, { status: 400 });
  }

  const file = contactFile();
  await mkdir(file.substring(0, file.lastIndexOf("/")), { recursive: true });
  await appendFile(file, JSON.stringify({ at: new Date().toISOString(), ...enquiry }) + "\n", "utf8");

  return NextResponse.json({ ok: true });
}
