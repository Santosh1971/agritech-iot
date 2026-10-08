import { NextRequest, NextResponse } from "next/server";
import { prisma } from "@/lib/prisma";
import { ActionError, applyIssueAction } from "@/lib/tracker/actions";
import type { ChecklistItem } from "@/lib/tracker/rules";
import { err, notify, requireMember } from "@/lib/tracker/server";

// JSON actions on a test build:
//   { action: "verdict", issueId, verdict: "pass"|"fail", text? }   tester
//   { action: "checklist", index, result: "pass"|"fail"|null, note? } tester
//   { action: "candidate", releaseId }                             developer
export async function POST(req: NextRequest, { params }: { params: Promise<{ id: string }> }) {
  const auth = await requireMember();
  if ("res" in auth) return auth.res;
  const { user } = auth;
  const { id } = await params;
  const body = (await req.json().catch(() => null)) as Record<string, unknown> | null;
  const build = await prisma.trackBuild.findUnique({ where: { id }, include: { product: true, candidateOf: true } });
  if (!build) return err("Build not found", 404);
  if (!body) return err("Bad request");

  if (body.action === "verdict") {
    const issueId = String(body.issueId ?? "");
    const link = await prisma.trackBuildIssue.findUnique({ where: { buildId_issueId: { buildId: id, issueId } } });
    if (!link) return err("This issue is not in this build");
    const newer = await prisma.trackBuildIssue.findFirst({ where: { issueId, build: { number: { gt: build.number } } } });
    if (newer) return err("A newer test build claims this fix; test that one instead");
    if (body.verdict !== "pass" && body.verdict !== "fail") return err("Verdict must be pass or fail");
    try {
      await applyIssueAction(user, issueId, body.verdict === "pass" ? "verify_pass" : "verify_fail", {
        text: typeof body.text === "string" ? body.text : null,
      });
    } catch (e) {
      if (e instanceof ActionError) return err(e.message);
      throw e;
    }
    return NextResponse.json({ ok: true });
  }

  if (body.action === "checklist") {
    if (user.trackRole !== "TESTER") return err("Only the tester ticks the checklist", 403);
    const list = build.checklist as ChecklistItem[];
    const index = Number(body.index);
    if (!Number.isInteger(index) || index < 0 || index >= list.length) return err("Unknown checklist item");
    const result = body.result === "pass" || body.result === "fail" ? body.result : null;
    const note = typeof body.note === "string" ? body.note.trim().slice(0, 500) : "";
    if (result === "fail" && !note) return err("Say what failed");
    list[index] = { ...list[index], result, note: note || undefined };
    await prisma.$transaction([
      prisma.trackBuild.update({ where: { id }, data: { checklist: list } }),
      prisma.trackEvent.create({
        data: { buildId: id, actorId: user.userId, kind: "verdict", text: `Checklist "${list[index].text}": ${result ?? "cleared"}${note ? ` · ${note}` : ""}` },
      }),
    ]);
    if (result === "fail") {
      await notify("DEVELOPER", `${build.product.name} build #${build.number}: checklist failed`, [`"${list[index].text}" failed: ${note}`], `/dashboard/testing/build/${id}`, user.userId);
    }
    return NextResponse.json({ ok: true });
  }

  if (body.action === "candidate") {
    if (user.trackRole !== "DEVELOPER") return err("Only the developer picks the candidate build", 403);
    const releaseId = String(body.releaseId ?? "");
    const release = await prisma.trackRelease.findFirst({ where: { id: releaseId, productId: build.productId, status: { not: "RELEASED" } } });
    if (!release) return err("Pick an unreleased release of this product");
    // A new candidate cancels any earlier sign-off: the tester signs off a build, not a name.
    await prisma.$transaction([
      prisma.trackRelease.updateMany({ where: { candidateBuildId: id, NOT: { id: releaseId } }, data: { candidateBuildId: null } }),
      prisma.trackRelease.update({
        where: { id: releaseId },
        data: { candidateBuildId: id, status: "OPEN", signedOffById: null, signedOffAt: null },
      }),
      prisma.trackBuild.update({ where: { id }, data: { releaseId } }),
      prisma.trackEvent.create({
        data: { buildId: id, releaseId, actorId: user.userId, kind: "status", text: `Build #${build.number} is the release candidate${release.status === "SIGNED_OFF" ? " (earlier sign-off withdrawn)" : ""}` },
      }),
    ]);
    await notify("TESTER", `${release.name}: build #${build.number} is the release candidate`, [`Test build #${build.number} is now the candidate for ${release.name}. Once every fix and checklist item passes, please sign off.`], `/dashboard/testing/release/${releaseId}`, user.userId);
    return NextResponse.json({ ok: true });
  }

  return err("Unknown action");
}
