import { NextRequest, NextResponse } from "next/server";
import { prisma } from "@/lib/prisma";
import { BUILDABLE, issueKey, type ChecklistItem } from "@/lib/tracker/rules";
import { err, formFiles, formText, notify, requireMember, saveFiles } from "@/lib/tracker/server";

// New test build (developer, multipart): productId, releaseId?, fwVersion?,
// appVersion?, hwRev?, notes?, checklist (one item per line), issueIds[],
// firmwareBuildId?, appBuildId?, files: firmware, apk, gerber, attachments[].
// Every listed issue moves to Fix ready and the tester is emailed.
export async function POST(req: NextRequest) {
  const auth = await requireMember("DEVELOPER");
  if ("res" in auth) return auth.res;
  const { user } = auth;
  const form = await req.formData();

  const productId = formText(form, "productId", 64);
  const product = productId ? await prisma.trackProduct.findUnique({ where: { id: productId } }) : null;
  if (!product) return err("Unknown product");

  const releaseId = formText(form, "releaseId", 64);
  if (releaseId) {
    const r = await prisma.trackRelease.findFirst({ where: { id: releaseId, productId: product.id, status: { not: "RELEASED" } } });
    if (!r) return err("Pick an unreleased release of this product");
  }
  const fwVersion = formText(form, "fwVersion", 40);
  const appVersion = formText(form, "appVersion", 40);
  const hwRev = formText(form, "hwRev", 40);
  if (!fwVersion && !appVersion && !hwRev) return err("Give at least one of firmware version, app version or board revision");

  const firmware = formFiles(form, "firmware");
  const apk = formFiles(form, "apk");
  const gerber = formFiles(form, "gerber");
  if (firmware.some((f) => !f.name.toLowerCase().endsWith(".bin"))) return err("Firmware must be a .bin file");
  if (apk.some((f) => !f.name.toLowerCase().endsWith(".apk"))) return err("App must be an .apk file");
  if (gerber.some((f) => !/\.(zip|rar|7z)$/i.test(f.name))) return err("Gerber must be a .zip file");

  const firmwareBuildId = formText(form, "firmwareBuildId", 64);
  const appBuildId = formText(form, "appBuildId", 64);
  if (firmwareBuildId && !(await prisma.firmwareBuild.findUnique({ where: { id: firmwareBuildId } }))) return err("Unknown firmware build");
  if (appBuildId && !(await prisma.mobileAppBuild.findUnique({ where: { id: appBuildId } }))) return err("Unknown app build");

  const issueIds = form.getAll("issueIds").filter((v): v is string => typeof v === "string" && v.length > 0);
  const issues = await prisma.trackIssue.findMany({ where: { id: { in: issueIds }, productId: product.id } });
  if (issues.length !== issueIds.length) return err("Some issues are not part of this product");
  const notReady = issues.filter((i) => !BUILDABLE.includes(i.status));
  if (notReady.length) return err(`Only accepted, in-progress or reopened issues can go in a build (${notReady.map((i) => issueKey(product.code, i.number)).join(", ")})`);

  const checklist: ChecklistItem[] = (formText(form, "checklist", 8000) ?? "")
    .split("\n")
    .map((t) => t.trim())
    .filter(Boolean)
    .slice(0, 40)
    .map((text) => ({ text: text.slice(0, 200), result: null }));

  const build = await prisma.$transaction(async (tx) => {
    const p = await tx.trackProduct.update({ where: { id: product.id }, data: { nextBuildNo: { increment: 1 } } });
    const b = await tx.trackBuild.create({
      data: {
        productId: product.id,
        number: p.nextBuildNo - 1,
        releaseId,
        fwVersion,
        appVersion,
        hwRev,
        notes: formText(form, "notes", 8000),
        checklist,
        firmwareBuildId,
        appBuildId,
        createdById: user.userId,
      },
    });
    for (const i of issues) {
      await tx.trackBuildIssue.create({ data: { buildId: b.id, issueId: i.id } });
      await tx.trackIssue.update({ where: { id: i.id }, data: { status: "FIX_READY" } });
      await tx.trackEvent.create({
        data: { issueId: i.id, buildId: b.id, actorId: user.userId, kind: "build", fromStatus: i.status, toStatus: "FIX_READY", text: `Fix in test build #${b.number}` },
      });
    }
    await tx.trackEvent.create({ data: { buildId: b.id, releaseId, actorId: user.userId, kind: "build", text: `Test build #${b.number} posted with ${issues.length} fix(es)` } });
    return b;
  });

  try {
    await saveFiles(firmware, "FIRMWARE", { buildId: build.id }, user.userId);
    await saveFiles(apk, "APK", { buildId: build.id }, user.userId);
    await saveFiles(gerber, "GERBER", { buildId: build.id }, user.userId);
    await saveFiles(formFiles(form, "attachments"), "ATTACHMENT", { buildId: build.id }, user.userId);
  } catch (e) {
    return NextResponse.json({ build, warning: (e as Error).message });
  }

  const versions = [fwVersion && `firmware ${fwVersion}`, appVersion && `app ${appVersion}`, hwRev && `board ${hwRev}`].filter(Boolean).join(", ");
  await notify(
    "TESTER",
    `${product.name} test build #${build.number} ready (${versions})`,
    [
      `${user.name} posted test build #${build.number} for ${product.name}: ${versions}.`,
      issues.length ? `Please check these fixes: ${issues.map((i) => `${issueKey(product.code, i.number)} ${i.title}`).join("; ")}.` : "No issue fixes listed; regression check only.",
      ...(checklist.length ? [`Regression checklist: ${checklist.length} item(s).`] : []),
    ],
    `/dashboard/testing/build/${build.id}`,
    user.userId,
  );
  return NextResponse.json({ build });
}
