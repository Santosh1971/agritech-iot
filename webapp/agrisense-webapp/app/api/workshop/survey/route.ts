import { NextRequest, NextResponse } from "next/server";
import { appendFile, mkdir } from "fs/promises";
import { surveyFile } from "@/lib/workshopSurvey";

// Public endpoint for the student questionnaire at /workshop/survey (no login).
// Each submission is appended as one JSON line to a file outside the repo, so a
// deploy (git stash -u / pull) never touches it. Results: /api/admin/workshop-survey.

const MAX_BODY = 32_000;
const MAX_TEXT = 1500;
const MAX_LIST = 20;

function clean(value: unknown): string | string[] | number | null {
  if (typeof value === "string") return value.trim().slice(0, MAX_TEXT);
  if (typeof value === "number" && Number.isFinite(value)) return value;
  if (Array.isArray(value)) {
    return value.filter((v) => typeof v === "string").slice(0, MAX_LIST).map((v) => v.trim().slice(0, 200));
  }
  return null;
}

export async function POST(req: NextRequest) {
  const raw = await req.text();
  if (raw.length > MAX_BODY) return NextResponse.json({ error: "Too long" }, { status: 413 });

  let body: unknown;
  try {
    body = JSON.parse(raw);
  } catch {
    return NextResponse.json({ error: "Invalid JSON" }, { status: 400 });
  }
  if (!body || typeof body !== "object" || Array.isArray(body)) {
    return NextResponse.json({ error: "Expected an object" }, { status: 400 });
  }

  const answers: Record<string, unknown> = {};
  for (const [key, value] of Object.entries(body).slice(0, 60)) {
    if (!/^[a-z0-9_]{1,40}$/.test(key)) continue;
    const v = clean(value);
    if (v !== null && v !== "") answers[key] = v;
  }
  if (typeof answers.name !== "string" || typeof answers.problem !== "string") {
    return NextResponse.json({ error: "Name and one farm problem are required" }, { status: 400 });
  }

  const entry = { at: new Date().toISOString(), survey: "gpsioam-2026", answers };
  const file = surveyFile();
  await mkdir(file.substring(0, file.lastIndexOf("/")), { recursive: true });
  await appendFile(file, JSON.stringify(entry) + "\n", "utf8");

  return NextResponse.json({ ok: true });
}
