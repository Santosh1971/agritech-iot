import Link from "next/link";
import { prisma } from "@/lib/prisma";
import { OPEN_STATUSES, isOpen, issueKey, releaseGate, type ChecklistItem } from "@/lib/tracker/rules";
import { fmtDate, trackUser } from "@/lib/tracker/server";
import { NotMember, SevPill, StatusPill } from "./ui";

export const dynamic = "force-dynamic";

export default async function TestingOverview() {
  const user = await trackUser();
  if (!user) return null;
  if (!user.trackRole && !user.isAdmin) return <NotMember isAdmin={false} />;

  const [products, issues, releases, builds] = await Promise.all([
    prisma.trackProduct.findMany({ where: { active: true }, orderBy: [{ sortOrder: "asc" }, { code: "asc" }] }),
    prisma.trackIssue.findMany({
      select: { id: true, productId: true, number: true, title: true, status: true, severity: true, proposal: true, releaseId: true, updatedAt: true },
    }),
    prisma.trackRelease.findMany({ where: { status: { not: "RELEASED" } }, include: { candidateBuild: { include: { issues: true } } }, orderBy: { createdAt: "desc" } }),
    prisma.trackBuild.findMany({ orderBy: { createdAt: "desc" }, include: { issues: true }, take: 100 }),
  ]);
  const code = Object.fromEntries(products.map((p) => [p.id, p.code]));

  // My queue: what is waiting on me right now.
  const role = user.trackRole;
  const waitingIssues = issues
    .filter((i) =>
      role === "DEVELOPER" ? i.status === "NEW" || i.status === "REOPENED" : role === "TESTER" ? i.status === "NEED_INFO" || !!i.proposal : false,
    )
    .sort((a, b) => +b.updatedAt - +a.updatedAt);
  const buildsToTest =
    role === "TESTER"
      ? builds.filter((b) => {
          const pendingChecks = (b.checklist as ChecklistItem[]).some((c) => c.result === null);
          const pendingFixes = b.issues.some((l) => l.verdict === null && issues.find((i) => i.id === l.issueId)?.status === "FIX_READY");
          return pendingFixes || (pendingChecks && releases.some((r) => r.candidateBuildId === b.id));
        })
      : [];

  return (
    <>
      {!user.trackRole && <NotMember isAdmin={user.isAdmin} />}
      <div className="row between">
        <div>
          <span className="eyebrow">Testing tracker</span>
          <h1>Products under test</h1>
        </div>
      </div>

      {role && (
        <section className="card queue">
          <h2>Waiting on you</h2>
          {waitingIssues.length === 0 && buildsToTest.length === 0 ? (
            <p className="muted small">Nothing right now.</p>
          ) : (
            <div className="list">
              {buildsToTest.map((b) => (
                <Link key={b.id} href={`/dashboard/testing/build/${b.id}`} className="item">
                  <span className="key">{code[b.productId]}</span>
                  <span className="ttl">Test build #{b.number}{b.fwVersion ? ` · fw ${b.fwVersion}` : ""}{b.appVersion ? ` · app ${b.appVersion}` : ""}{b.hwRev ? ` · board ${b.hwRev}` : ""}</span>
                  <span className="pill lime">To test</span>
                  <span className="meta">Posted {fmtDate(b.createdAt)} · {b.issues.length} fix(es)</span>
                </Link>
              ))}
              {waitingIssues.map((i) => (
                <Link key={i.id} href={`/dashboard/testing/issue/${i.id}`} className="item">
                  <span className="key">{issueKey(code[i.productId], i.number)}</span>
                  <span className="ttl">{i.title}</span>
                  <StatusPill s={i.status} />
                  <span className="meta"><SevPill s={i.severity} />{i.proposal && <span className="pill warn">Needs your agreement</span>}</span>
                </Link>
              ))}
            </div>
          )}
        </section>
      )}

      <div className="cards">
        {products.map((p) => {
          const mine = issues.filter((i) => i.productId === p.id);
          const open = mine.filter((i) => isOpen(i.status));
          const count = (s: string) => open.filter((i) => i.severity === s).length;
          const rel = releases.find((r) => r.productId === p.id);
          const gate = rel
            ? releaseGate({
                issues: mine,
                releaseId: rel.id,
                candidate: rel.candidateBuild ? { checklist: rel.candidateBuild.checklist as ChecklistItem[], verdicts: rel.candidateBuild.issues } : null,
                signedOff: rel.status !== "OPEN",
              })
            : null;
          const failing = gate ? gate.checks.filter((c) => !c.ok).length : 0;
          const dot = !gate ? "" : gate.readyToPublish ? "g" : failing <= 2 ? "a" : "r";
          const last = builds.find((b) => b.productId === p.id);
          return (
            <Link key={p.id} href={`/dashboard/testing/${encodeURIComponent(p.code)}`} className="card" style={{ textDecoration: "none", color: "inherit" }}>
              <div className="row between">
                <h2>{p.name}</h2>
                <span className="mono muted">{p.code}</span>
              </div>
              <div className="stat">
                <span><b>{open.length}</b> open</span>
                {count("BLOCKER") > 0 && <span className="pill bad">{count("BLOCKER")} blocker</span>}
                {count("MAJOR") > 0 && <span className="pill warn">{count("MAJOR")} major</span>}
                <span><b>{mine.filter((i) => i.status === "FIX_READY").length}</b> to verify</span>
                <span><b>{mine.filter((i) => i.status === "VERIFIED").length}</b> verified</span>
              </div>
              <div className="small">
                {rel ? (
                  <span className="row" style={{ gap: 6 }}>
                    <span className={`gate-dot ${dot}`} aria-hidden />
                    {rel.name}: {gate!.readyToPublish ? "ready to publish" : rel.status === "SIGNED_OFF" ? "signed off" : `${failing} gate check(s) open`}
                  </span>
                ) : (
                  <span className="muted">No release in progress</span>
                )}
              </div>
              <div className="small muted">{last ? `Latest build #${last.number}, ${fmtDate(last.createdAt)}` : "No test builds yet"}</div>
            </Link>
          );
        })}
      </div>
      <p className="small muted">
        Open = {OPEN_STATUSES.length} states from New to Fix ready. A release can be published only when every Blocker and Major in it is verified, every Minor is closed or deferred, the candidate build passes, and the tester signs off.
      </p>
    </>
  );
}
