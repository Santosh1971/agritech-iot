import Link from "next/link";
import { notFound } from "next/navigation";
import type { Product } from "@prisma/client";
import { prisma } from "@/lib/prisma";
import { BUILDABLE, defaultChecklist, issueKey } from "@/lib/tracker/rules";
import { fmtDay, trackUser } from "@/lib/tracker/server";
import { NotMember } from "../../ui";
import { NewBuildForm } from "../../BuildForms";

export const dynamic = "force-dynamic";

// Tracker product code → the flasher's Product enum, for linking builds CI already uploaded.
const FLASHER: Record<string, Product> = { FG1: "FG1", "WPC-M": "WPC", "WPC-PN": "WPC", "WM1-MINI": "WM1_MINI", "WM1-PRO": "WM1_PRO" };

export default async function NewBuildPage({ params }: { params: Promise<{ code: string }> }) {
  const user = await trackUser();
  if (!user) return null;
  if (user.trackRole !== "DEVELOPER") return <NotMember isAdmin={user.isAdmin} />;
  const { code } = await params;
  const product = await prisma.trackProduct.findUnique({ where: { code: decodeURIComponent(code) } });
  if (!product) notFound();

  const flasher = FLASHER[product.code];
  const [issues, releases, prev, fw, apps] = await Promise.all([
    prisma.trackIssue.findMany({ where: { productId: product.id, status: { in: BUILDABLE } }, orderBy: [{ severity: "asc" }, { number: "asc" }] }),
    prisma.trackRelease.findMany({ where: { productId: product.id, status: { not: "RELEASED" } }, orderBy: { createdAt: "desc" } }),
    prisma.trackBuild.findFirst({ where: { productId: product.id }, orderBy: { number: "desc" } }),
    flasher ? prisma.firmwareBuild.findMany({ where: { product: flasher }, orderBy: { createdAt: "desc" }, take: 15 }) : [],
    flasher ? prisma.mobileAppBuild.findMany({ where: { product: flasher }, orderBy: { createdAt: "desc" }, take: 15 }) : [],
  ]);
  // Start from the last build's checklist (with results cleared), else the default one.
  const lastList = (prev?.checklist as { text: string }[] | undefined)?.map((c) => c.text);
  const checklist = (lastList?.length ? lastList : defaultChecklist(product)).join("\n");

  return (
    <>
      <div className="crumbs">
        <Link href="/dashboard/testing">Overview</Link><span>/</span>
        <Link href={`/dashboard/testing/${encodeURIComponent(product.code)}`}>{product.code}</Link><span>/</span><span>New test build</span>
      </div>
      <section className="card" style={{ maxWidth: 760 }}>
        <h1>Post a test build for {product.name}</h1>
        <p className="small muted">Listed fixes move to Fix ready and the tester gets an email with the download links.</p>
        <NewBuildForm
          product={{ id: product.id, hasFirmware: product.hasFirmware, hasApp: product.hasApp, hasHardware: product.hasHardware }}
          issues={issues.map((i) => ({ id: i.id, key: issueKey(product.code, i.number), title: i.title, severity: i.severity, status: i.status }))}
          releases={releases.map((r) => ({ id: r.id, label: r.name }))}
          checklist={checklist}
          ciFirmware={fw.map((b) => ({ id: b.id, label: `${b.version} (${b.variant}) · ${fmtDay(b.createdAt)}` }))}
          ciApps={apps.map((b) => ({ id: b.id, label: `${b.versionName} ${b.buildType} · ${fmtDay(b.createdAt)}` }))}
        />
      </section>
    </>
  );
}
