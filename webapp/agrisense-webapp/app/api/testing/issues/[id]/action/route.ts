import { NextRequest, NextResponse } from "next/server";
import type { TrackSeverity } from "@prisma/client";
import { ActionError, applyIssueAction } from "@/lib/tracker/actions";
import { SEVERITIES, type IssueAction } from "@/lib/tracker/rules";
import { err, formFiles, formText, requireMember } from "@/lib/tracker/server";

// multipart: action, text?, severity? and releaseId? (accept), files[].
export async function POST(req: NextRequest, { params }: { params: Promise<{ id: string }> }) {
  const auth = await requireMember();
  if ("res" in auth) return auth.res;
  const { id } = await params;
  const form = await req.formData();
  const action = formText(form, "action", 40) as IssueAction | null;
  if (!action) return err("Missing action");
  const severity = formText(form, "severity", 20) as TrackSeverity | null;
  if (severity && !SEVERITIES.includes(severity)) return err("Unknown severity");
  const releaseRaw = form.get("releaseId");
  try {
    const event = await applyIssueAction(auth.user, id, action, {
      text: formText(form, "text"),
      severity,
      releaseId: typeof releaseRaw === "string" ? releaseRaw || null : undefined,
      files: formFiles(form, "files"),
    });
    return NextResponse.json({ event });
  } catch (e) {
    if (e instanceof ActionError) return err(e.message);
    if (e instanceof Error && /limit/.test(e.message)) return err(e.message);
    throw e;
  }
}
