import { NextRequest, NextResponse } from "next/server";
import type { TrackArea, TrackSeverity } from "@prisma/client";
import { prisma } from "@/lib/prisma";
import { AREAS, SEVERITIES, issueKey, SEVERITY_LABEL } from "@/lib/tracker/rules";
import { err, formFiles, formText, notify, requireMember, saveFiles } from "@/lib/tracker/server";

// New issue (multipart): productId, title, area, severity, module?, steps,
// expected?, actual, foundFw?, foundApp?, foundHw?, deviceId?, files[].
export async function POST(req: NextRequest) {
  const auth = await requireMember();
  if ("res" in auth) return auth.res;
  const { user } = auth;

  const form = await req.formData();
  const productId = formText(form, "productId", 64);
  const title = formText(form, "title", 200);
  const area = formText(form, "area", 20) as TrackArea | null;
  const severity = formText(form, "severity", 20) as TrackSeverity | null;
  const steps = formText(form, "steps");
  const actual = formText(form, "actual");
  if (!productId || !title || !steps || !actual) return err("Title, steps and what happened are required");
  if (!area || !AREAS.includes(area)) return err("Pick an area");
  if (!severity || !SEVERITIES.includes(severity)) return err("Pick a severity");

  const product = await prisma.trackProduct.findUnique({ where: { id: productId } });
  if (!product || !product.active) return err("Unknown product");
  const module = formText(form, "module", 80);
  if (module && !product.modules.includes(module)) return err("Unknown module for this product");

  const files = formFiles(form, "files");
  const issue = await prisma.$transaction(async (tx) => {
    // Per-product numbering: bump the counter and take the old value.
    const p = await tx.trackProduct.update({ where: { id: productId }, data: { nextIssueNo: { increment: 1 } } });
    const created = await tx.trackIssue.create({
      data: {
        productId,
        number: p.nextIssueNo - 1,
        title,
        area,
        severity,
        module,
        steps,
        actual,
        expected: formText(form, "expected"),
        foundFw: formText(form, "foundFw", 40),
        foundApp: formText(form, "foundApp", 40),
        foundHw: formText(form, "foundHw", 40),
        deviceId: formText(form, "deviceId", 80),
        reporterId: user.userId,
      },
    });
    await tx.trackEvent.create({ data: { issueId: created.id, actorId: user.userId, kind: "created", toStatus: "NEW" } });
    return created;
  });

  try {
    await saveFiles(files, "ATTACHMENT", { issueId: issue.id }, user.userId);
  } catch (e) {
    // The issue itself is saved; say which file was refused.
    return NextResponse.json({ issue, warning: (e as Error).message });
  }

  const key = issueKey(product.code, issue.number);
  await notify(
    user.trackRole === "TESTER" ? "DEVELOPER" : "TESTER",
    `New ${SEVERITY_LABEL[severity]} ${key}: ${title}`,
    [`${user.name} reported a ${SEVERITY_LABEL[severity].toLowerCase()} issue on ${product.name}.`, `What happened: ${actual}`],
    `/dashboard/testing/issue/${issue.id}`,
    user.userId,
  );
  return NextResponse.json({ issue });
}
