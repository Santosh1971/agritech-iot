import Link from "next/link";
import { notFound } from "next/navigation";
import { prisma } from "@/lib/prisma";
import { STATUS_LABEL, allowedActions, editableFields, issueKey } from "@/lib/tracker/rules";
import { fmtDate, trackUser } from "@/lib/tracker/server";
import { AreaPill, Files, NotMember, SevPill, StatusPill, Timeline } from "../../ui";
import { IssueActions, IssueEditor } from "../../IssueForms";
import { ShareButton } from "../../ShareButton";

export const dynamic = "force-dynamic";

export default async function IssuePage({ params }: { params: Promise<{ id: string }> }) {
  const user = await trackUser();
  if (!user) return null;
  if (!user.trackRole && !user.isAdmin) return <NotMember isAdmin={false} />;
  const { id } = await params;
  const issue = await prisma.trackIssue.findUnique({
    where: { id },
    include: {
      product: true,
      release: true,
      reporter: { select: { name: true } },
      files: { where: { eventId: null }, orderBy: { createdAt: "asc" } },
      builds: { include: { build: true }, orderBy: { build: { number: "desc" } } },
      events: { orderBy: { createdAt: "asc" }, include: { actor: { select: { name: true } }, files: true, build: { select: { id: true, number: true } } } },
    },
  });
  if (!issue) notFound();
  const releases = await prisma.trackRelease.findMany({ where: { productId: issue.productId, status: "OPEN" }, orderBy: { createdAt: "desc" }, select: { id: true, name: true } });

  const key = issueKey(issue.product.code, issue.number);
  const role = user.trackRole;
  const actions = role ? allowedActions(issue, role) : [];
  const fields = role ? editableFields(issue, role) : [];
  const latest = issue.builds[0];

  return (
    <>
      <div className="crumbs">
        <Link href="/dashboard/testing">Overview</Link><span>/</span>
        <Link href={`/dashboard/testing/${encodeURIComponent(issue.product.code)}`}>{issue.product.code}</Link><span>/</span><span>{key}</span>
      </div>
      <div className="grid" style={{ gap: 8 }}>
        <div className="row">
          <span className="mono muted">{key}</span>
          <StatusPill s={issue.status} />
          <SevPill s={issue.severity} />
          <AreaPill a={issue.area} />
          {issue.module && <span className="pill">{issue.module}</span>}
        </div>
        <h1>{issue.title}</h1>
      </div>

      {issue.proposal && (
        <div className="note">
          {issue.proposal === "DEFERRED" ? "Defer to a later release" : "Won't fix"} proposed by the developer: {issue.proposalNote}
          {role === "TESTER" ? " · Agree or disagree below." : " · Waiting for the tester."}
        </div>
      )}
      {issue.status === "FIX_READY" && latest && (
        <div className="ok-note">
          Fix is in <Link href={`/dashboard/testing/build/${latest.build.id}`}>test build #{latest.build.number}</Link>.
          {role === "TESTER" ? " Install it, then mark Pass or Fail below." : " Waiting for the tester."}
        </div>
      )}

      <div className="two">
        <div className="grid" style={{ alignContent: "start" }}>
          <section className="card">
            <div className="block"><h3>Steps</h3><p className="prose">{issue.steps}</p></div>
            <div className="block"><h3>What happened</h3><p className="prose">{issue.actual}</p></div>
            {issue.expected && <div className="block"><h3>What should happen</h3><p className="prose">{issue.expected}</p></div>}
            <Files files={issue.files} />
            {role && <div><IssueEditor issue={issue} fields={fields} modules={issue.product.modules} releases={releases} /></div>}
          </section>
          <section className="card">
            <h2>Timeline</h2>
            <Timeline events={issue.events} />
          </section>
          {role && (
            <section className="card">
              <h2>{actions.length > 1 ? "Act on it" : "Comment"}</h2>
              <IssueActions issueId={issue.id} actions={actions} releases={releases} currentReleaseId={issue.releaseId} severity={issue.severity} />
            </section>
          )}
        </div>
        <aside className="grid" style={{ alignContent: "start" }}>
          <section className="card">
            <dl className="facts">
              <dt>Product</dt><dd>{issue.product.name}</dd>
              <dt>Status</dt><dd>{STATUS_LABEL[issue.status]}</dd>
              <dt>Release</dt><dd>{issue.release ? <Link href={`/dashboard/testing/release/${issue.release.id}`}>{issue.release.name}</Link> : "Not set"}</dd>
              {issue.foundFw && (<><dt>Firmware</dt><dd>{issue.foundFw}</dd></>)}
              {issue.foundApp && (<><dt>App</dt><dd>{issue.foundApp}</dd></>)}
              {issue.foundHw && (<><dt>Board</dt><dd>{issue.foundHw}</dd></>)}
              {issue.deviceId && (<><dt>Device</dt><dd className="mono">{issue.deviceId}</dd></>)}
              <dt>Reported</dt><dd>{issue.reporter.name}, {fmtDate(issue.createdAt)}</dd>
            </dl>
            <ShareButton text={`${key} ${issue.title} (${STATUS_LABEL[issue.status]})`} path={`/dashboard/testing/issue/${issue.id}`} />
          </section>
          {issue.builds.length > 0 && (
            <section className="card">
              <h2>Builds that claim this fix</h2>
              <div className="list">
                {issue.builds.map((l) => (
                  <Link key={l.buildId} href={`/dashboard/testing/build/${l.buildId}`} className="item">
                    <span className="key">#{l.build.number}</span>
                    <span className="ttl">{[l.build.fwVersion && `fw ${l.build.fwVersion}`, l.build.appVersion && `app ${l.build.appVersion}`, l.build.hwRev && `board ${l.build.hwRev}`].filter(Boolean).join(" · ")}</span>
                    <span className={`pill ${l.verdict === "pass" ? "good" : l.verdict === "fail" ? "bad" : "lime"}`}>{l.verdict === "pass" ? "Passed" : l.verdict === "fail" ? "Failed" : "To test"}</span>
                    {l.note && <span className="meta">{l.note}</span>}
                  </Link>
                ))}
              </div>
            </section>
          )}
        </aside>
      </div>
    </>
  );
}
