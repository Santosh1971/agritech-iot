import { NextResponse } from "next/server";
import { cookies } from "next/headers";
import { readFile } from "fs/promises";
import { verifySession } from "@/lib/session";
import { surveyFile } from "@/lib/workshopSurvey";

// Logged-in only: all student questionnaire answers from /workshop/survey.
export async function GET() {
  const token = (await cookies()).get("agrisense_session")?.value;
  const session = token ? await verifySession(token) : null;
  if (!session) return NextResponse.json({ error: "Unauthorized" }, { status: 401 });

  let text = "";
  try {
    text = await readFile(surveyFile(), "utf8");
  } catch {
    // no answers yet
  }
  const entries = text.split("\n").filter(Boolean).map((line) => JSON.parse(line));
  return NextResponse.json({ count: entries.length, entries });
}
