import { NextRequest, NextResponse } from "next/server";
import { prisma } from "@/lib/prisma";
import { AREAS, AREA_LABEL, SEVERITIES, SEVERITY_LABEL, editableFields, type Area, type Severity } from "@/lib/tracker/rules";
import { err, requireMember } from "@/lib/tracker/server";

const LABEL: Record<string, string> = {
  title: "Title", steps: "Steps", expected: "Expected", actual: "What happened", foundFw: "Firmware version",
  foundApp: "App version", foundHw: "Board revision", deviceId: "Device ID", module: "Module", area: "Area",
  severity: "Severity", releaseId: "Target release",
};

// JSON: { field: value, ..., note? } — only the fields this role may edit now.
export async function POST(req: NextRequest, { params }: { params: Promise<{ id: string }> }) {
  const auth = await requireMember();
  if ("res" in auth) return auth.res;
  const { user } = auth;
  const { id } = await params;
  const body = (await req.json().catch(() => null)) as Record<string, unknown> | null;
  if (!body) return err("Bad request");

  const issue = await prisma.trackIssue.findUnique({ where: { id }, include: { product: true, release: true } });
  if (!issue) return err("Issue not found", 404);
  const allowed = editableFields(issue, user.trackRole);

  const data: Record<string, string | null> = {};
  const changes: string[] = [];
  for (const [k, raw] of Object.entries(body)) {
    if (k === "note") continue;
    if (!allowed.includes(k)) return err(`You can't change ${LABEL[k] ?? k} now`, 403);
    const v = typeof raw === "string" ? raw.trim().slice(0, 5000) || null : null;
    if ((k === "title" || k === "steps" || k === "actual") && !v) return err(`${LABEL[k]} can't be empty`);
    if (k === "area" && !AREAS.includes(v as Area)) return err("Unknown area");
    if (k === "severity" && !SEVERITIES.includes(v as Severity)) return err("Unknown severity");
    if (k === "module" && v && !issue.product.modules.includes(v)) return err("Unknown module");
    if (k === "releaseId" && v) {
      const r = await prisma.trackRelease.findFirst({ where: { id: v, productId: issue.productId, status: "OPEN" } });
      if (!r) return err("Pick an open release of this product");
    }
    const old = (issue as unknown as Record<string, string | null>)[k];
    if (old === v) continue;
    data[k] = v;
    const show = (x: string | null) =>
      k === "area" ? AREA_LABEL[x as Area] : k === "severity" ? SEVERITY_LABEL[x as Severity] : x ?? "none";
    changes.push(k === "releaseId" ? `Target release changed` : ["steps", "expected", "actual"].includes(k) ? `${LABEL[k]} edited` : `${LABEL[k]}: ${show(old)} → ${show(v)}`);
  }
  if (!changes.length) return NextResponse.json({ ok: true });
  const note = typeof body.note === "string" ? body.note : null;
  await prisma.$transaction([
    prisma.trackIssue.update({ where: { id }, data }),
    prisma.trackEvent.create({ data: { issueId: id, actorId: user.userId, kind: "edit", text: changes.join("; ") + (note ? ` · ${note}` : "") } }),
  ]);
  return NextResponse.json({ ok: true });
}
