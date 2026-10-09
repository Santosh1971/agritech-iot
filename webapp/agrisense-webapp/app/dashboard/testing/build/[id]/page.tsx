import Link from "next/link";
import { notFound } from "next/navigation";
import { prisma } from "@/lib/prisma";
import { issueKey, type ChecklistItem } from "@/lib/tracker/rules";
import { fmtDate, trackUser } from "@/lib/tracker/server";
import { Files, NotMember, SevPill, StatusPill, Timeline } from "../../ui";
import { CandidatePicker, ChecklistRow, VerdictButtons } from "../../BuildForms";
import { ShareButton } from "../../ShareButton";

export const dynamic = "force-dynamic";

export default async function BuildPage({ params }: { params: Promise<{ id: string }> }) {
  const user = await trackUser();
  if (!user) return null;
  if (!user.trackRole && !user.isAdmin) return <NotMember isAdmin={false} />;
  const { id } = await params;
  const build = await prisma.trackBuild.findUnique({
    where: { id },
    include: {
      product: true,
      release: true,
      candidateOf: true,
      createdBy: { select: { name: true } },
      firmwareBuild: true,
      appBuild: true,
      files: { orderBy: { createdAt: "asc" } },
      issues: { include: { issue: true } },
      events: { where: { issueId: null }, orderBy: { createdAt: "asc" }, include: { actor: { select: { name: true } }, files: true } },
    },
  });
  if (!build) notFound();
  const [newest, releases] = await Promise.all([
    prisma.trackBuild.findFirst({ where: { productId: build.productId }, orderBy: { number: "desc" }, select: { number: true } }),
    prisma.trackRelease.findMany({ where: { productId: build.productId, status: { not: "RELEASED" } }, orderBy: { createdAt: "desc" } }),
  ]);
  // A fix can be judged here only while this is the newest build that claims it.
  const newerClaims = await prisma.trackBuildIssue.findMany({
    where: { issueId: { in: build.issues.map((l) => l.issueId) }, build: { number: { gt: build.number } } },
    select: { issueId: true },
  });
  const superseded = new Set(newerClaims.map((c) => c.issueId));

  const role = user.trackRole;
  const checklist = build.checklist as ChecklistItem[];
  const versions = [build.fwVersion && `firmware ${build.fwVersion}`, build.appVersion && `app ${build.appVersion}`, build.hwRev && `board ${build.hwRev}`].filter(Boolean).join(" · ");
  const base = `/dashboard/testing/${encodeURIComponent(build.product.code)}`;
  const byKind = (k: string) => build.files.filter((f) => f.kind === k);

  return (
    <>
      <div className="crumbs">
        <Link href="/dashboard/testing">Overview</Link><span>/</span>
        <Link href={base}>{build.product.code}</Link><span>/</span><span>Build #{build.number}</span>
      </div>
      <div className="grid" style={{ gap: 6 }}>
        <div className="row">
          <span className="eyebrow">{build.product.name}</span>
          {build.candidateOf && <span className="pill lime">Release candidate: {build.candidateOf.name}</span>}
          {newest && newest.number > build.number && <span className="pill">Superseded by #{newest.number}</span>}
        </div>
        <h1>Test build #{build.number}</h1>
        <p className="muted">{versions}</p>
      </div>

      <div className="two">
        <div className="grid" style={{ alignContent: "start" }}>
          <section className="card">
            <h2>Fixes to check ({build.issues.length})</h2>
            {build.issues.length === 0 && <p className="small muted">No fixes listed: regression check only.</p>}
            <div className="list">
              {build.issues.map((l) => (
                <div key={l.issueId} className="item" style={{ gridTemplateColumns: "auto 1fr" }}>
                  <span className="key">{issueKey(build.product.code, l.issue.number)}</span>
                  <div className="grid" style={{ gap: 6 }}>
                    <Link href={`/dashboard/testing/issue/${l.issueId}`} className="ttl">{l.issue.title}</Link>
                    <div className="row small">
                      <StatusPill s={l.issue.status} />
                      <SevPill s={l.issue.severity} />
                      {l.verdict && <span className={`pill ${l.verdict === "pass" ? "good" : "bad"}`}>{l.verdict === "pass" ? "Passed here" : "Failed here"}</span>}
                      {l.note && <span className="muted">{l.note}</span>}
                    </div>
                    {role === "TESTER" && l.issue.status === "FIX_READY" && !superseded.has(l.issueId) && <VerdictButtons buildId={build.id} issueId={l.issueId} />}
                    {superseded.has(l.issueId) && !l.verdict && <span className="small muted">A newer build claims this fix.</span>}
                  </div>
                </div>
              ))}
            </div>
          </section>

          <section className="card">
            <div className="row between">
              <h2>Regression checklist</h2>
              <span className="small muted">{checklist.filter((c) => c.result === "pass").length} / {checklist.length} passed</span>
            </div>
            {checklist.length === 0 && <p className="small muted">No checklist on this build.</p>}
            <div>
              {checklist.map((c, i) => (
                <ChecklistRow
                  key={i}
                  buildId={build.id}
                  index={i}
                  item={c}
                  canTick={role === "TESTER"}
                  reportHref={`${base}/new?build=${build.id}&title=${encodeURIComponent(`Regression: ${c.text}`)}&steps=${encodeURIComponent(`Build #${build.number} checklist item: ${c.text}\n${c.note ?? ""}`)}`}
                />
              ))}
            </div>
          </section>

          <section className="card">
            <h2>Build history</h2>
            <Timeline events={build.events} />
          </section>
        </div>

        <aside className="grid" style={{ alignContent: "start" }}>
          <section className="card">
            <h2>Install</h2>
            {byKind("APK").length > 0 && <div className="grid" style={{ gap: 4 }}><span className="small muted">App</span><Files files={byKind("APK")} /></div>}
            {build.appBuild && (
              <p className="small">App from CI: <a href={`/api/admin/app-builds/${build.appBuild.id}`}>{build.appBuild.versionName} ({build.appBuild.buildType})</a></p>
            )}
            {byKind("FIRMWARE").length > 0 && <div className="grid" style={{ gap: 4 }}><span className="small muted">Firmware</span><Files files={byKind("FIRMWARE")} /></div>}
            {build.firmwareBuild && (
              <p className="small">Firmware {build.firmwareBuild.version} ({build.firmwareBuild.variant}) is in NB Agri Flasher: flash it from the app as usual.</p>
            )}
            {byKind("GERBER").length > 0 && <div className="grid" style={{ gap: 4 }}><span className="small muted">Gerber</span><Files files={byKind("GERBER")} /></div>}
            {byKind("ATTACHMENT").length > 0 && <div className="grid" style={{ gap: 4 }}><span className="small muted">Other files</span><Files files={byKind("ATTACHMENT")} /></div>}
            {build.files.length === 0 && !build.appBuild && !build.firmwareBuild && <p className="small muted">No files attached.</p>}
          </section>
          <section className="card">
            <dl className="facts">
              <dt>Posted</dt><dd>{build.createdBy.name}, {fmtDate(build.createdAt)}</dd>
              <dt>Release</dt><dd>{build.release ? <Link href={`/dashboard/testing/release/${build.release.id}`}>{build.release.name}</Link> : "Not set"}</dd>
            </dl>
            {build.notes && <div className="block"><h3>Notes</h3><p className="prose">{build.notes}</p></div>}
            <ShareButton text={`${build.product.name} test build #${build.number} (${versions}) is ready to test`} path={`/dashboard/testing/build/${build.id}`} />
          </section>
          {role === "DEVELOPER" && (
            <section className="card">
              <h2>Release candidate</h2>
              <CandidatePicker buildId={build.id} releases={releases.map((r) => ({ id: r.id, label: r.name }))} current={build.releaseId} />
            </section>
          )}
        </aside>
      </div>
    </>
  );
}
