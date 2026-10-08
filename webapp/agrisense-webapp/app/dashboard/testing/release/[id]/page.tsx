import Link from "next/link";
import { notFound } from "next/navigation";
import { prisma } from "@/lib/prisma";
import { loadGate } from "@/lib/tracker/gate";
import { AREAS, AREA_LABEL, issueKey } from "@/lib/tracker/rules";
import { fmtDate, fmtDay, trackUser } from "@/lib/tracker/server";
import { Files, NotMember, SevPill, StatusPill, Timeline } from "../../ui";
import { CopyBox, Publish, SignOff } from "../../ReleaseForms";
import { ShareButton } from "../../ShareButton";

export const dynamic = "force-dynamic";

export default async function ReleasePage({ params }: { params: Promise<{ id: string }> }) {
  const user = await trackUser();
  if (!user) return null;
  if (!user.trackRole && !user.isAdmin) return <NotMember isAdmin={false} />;
  const { id } = await params;
  const loaded = await loadGate(id);
  if (!loaded) notFound();
  const { gate } = loaded;
  const release = await prisma.trackRelease.findUnique({
    where: { id },
    include: {
      product: true,
      signedOffBy: { select: { name: true } },
      releasedBy: { select: { name: true } },
      candidateBuild: true,
      builds: { orderBy: { number: "desc" } },
      issues: { orderBy: { number: "asc" } },
      files: true,
      events: { orderBy: { createdAt: "asc" }, include: { actor: { select: { name: true } }, files: true } },
    },
  });
  if (!release) notFound();
  const p = release.product;
  const role = user.trackRole;
  const key = (n: number) => issueKey(p.code, n);

  const fixed = release.issues.filter((i) => i.status === "VERIFIED");
  const known = await prisma.trackIssue.findMany({ where: { productId: p.id, status: "DEFERRED" }, orderBy: { number: "asc" } });
  const versions = [release.fwVersion && `Firmware ${release.fwVersion}`, release.appVersion && `App ${release.appVersion}`, release.hwRev && `Board ${release.hwRev}`].filter(Boolean).join(" · ");
  const changelog = [
    `${release.name}${release.releasedAt ? ` (${fmtDay(release.releasedAt)})` : ""}`,
    versions,
    "",
    ...AREAS.flatMap((a) => {
      const list = fixed.filter((i) => i.area === a);
      return list.length ? [`${AREA_LABEL[a]} fixes:`, ...list.map((i) => `- ${i.title} (${key(i.number)})`), ""] : [];
    }),
    ...(known.length ? ["Known issues, deferred to a later release:", ...known.map((i) => `- ${i.title} (${key(i.number)})`)] : []),
  ].filter((l, i, arr) => !(l === "" && arr[i - 1] === "")).join("\n").trim();

  return (
    <>
      <div className="crumbs">
        <Link href="/dashboard/testing">Overview</Link><span>/</span>
        <Link href={`/dashboard/testing/${encodeURIComponent(p.code)}`}>{p.code}</Link><span>/</span><span>Release</span>
      </div>
      <div className="grid" style={{ gap: 6 }}>
        <div className="row">
          <span className="eyebrow">{p.name}</span>
          <span className={`pill ${release.status === "RELEASED" ? "good" : release.status === "SIGNED_OFF" ? "lime" : "info"}`}>
            {release.status === "OPEN" ? "In test" : release.status === "SIGNED_OFF" ? "Signed off" : "Released"}
          </span>
        </div>
        <h1>{release.name}</h1>
        {versions && <p className="muted">{versions}</p>}
        {release.notes && <p>{release.notes}</p>}
      </div>

      <div className="two">
        <div className="grid" style={{ alignContent: "start" }}>
          {release.status !== "RELEASED" && (
            <section className="card">
              <h2>Release gate</h2>
              <div>
                {gate.checks.map((c) => (
                  <div key={c.key} className="check">
                    <span className={`mark ${c.ok ? "ok" : "no"}`}>{c.ok ? "✓" : "✕"}</span>
                    <div><b style={{ fontWeight: 600 }}>{c.label}</b><div className="small muted">{c.detail}</div></div>
                  </div>
                ))}
              </div>
              {role === "TESTER" && <SignOff releaseId={release.id} ready={gate.readyForSignOff} signedOff={release.status === "SIGNED_OFF"} />}
              {role === "DEVELOPER" && release.status === "SIGNED_OFF" && gate.readyToPublish && (
                <Publish releaseId={release.id} hasFirmware={p.hasFirmware} hasApp={p.hasApp} hasHardware={p.hasHardware} />
              )}
              {role === "DEVELOPER" && release.status === "OPEN" && <p className="small muted">Publish opens after the tester signs off.</p>}
            </section>
          )}

          <section className="card">
            <h2>Issues in this release ({release.issues.length})</h2>
            {release.issues.length === 0 && <p className="small muted">None targeted yet. Accepting an issue sets its release.</p>}
            <div className="list">
              {release.issues.map((i) => (
                <Link key={i.id} href={`/dashboard/testing/issue/${i.id}`} className="item">
                  <span className="key">{key(i.number)}</span>
                  <span className="ttl">{i.title}</span>
                  <StatusPill s={i.status} />
                  <span className="meta"><SevPill s={i.severity} />{i.proposal && <span className="pill warn">Proposal pending</span>}</span>
                </Link>
              ))}
            </div>
          </section>

          <section className="card">
            <h2>Changelog</h2>
            <p className="small muted">Built from verified issues; deferred ones are listed as known issues.</p>
            <CopyBox text={changelog} />
          </section>
        </div>

        <aside className="grid" style={{ alignContent: "start" }}>
          {release.status === "RELEASED" && (
            <section className="card">
              <h2>Final files</h2>
              <Files files={release.files} />
              {release.files.length === 0 && <p className="small muted">No files attached at publish.</p>}
            </section>
          )}
          <section className="card">
            <dl className="facts">
              <dt>Candidate</dt>
              <dd>{release.candidateBuild ? <Link href={`/dashboard/testing/build/${release.candidateBuild.id}`}>Build #{release.candidateBuild.number}</Link> : "Not chosen"}</dd>
              <dt>Signed off</dt><dd>{release.signedOffBy ? `${release.signedOffBy.name}, ${fmtDate(release.signedOffAt!)}` : "Not yet"}</dd>
              <dt>Published</dt><dd>{release.releasedBy ? `${release.releasedBy.name}, ${fmtDate(release.releasedAt!)}` : "Not yet"}</dd>
            </dl>
            <ShareButton text={`${release.name}: ${release.status === "RELEASED" ? "released" : release.status === "SIGNED_OFF" ? "signed off" : "in test"}`} path={`/dashboard/testing/release/${release.id}`} />
          </section>
          <section className="card">
            <h2>Builds</h2>
            {release.builds.length === 0 && <p className="small muted">None yet.</p>}
            <div className="list">
              {release.builds.map((b) => (
                <Link key={b.id} href={`/dashboard/testing/build/${b.id}`} className="item">
                  <span className="key">#{b.number}</span>
                  <span className="ttl">{[b.fwVersion && `fw ${b.fwVersion}`, b.appVersion && `app ${b.appVersion}`, b.hwRev && `board ${b.hwRev}`].filter(Boolean).join(" · ")}</span>
                  {b.id === release.candidateBuildId ? <span className="pill lime">Candidate</span> : <span />}
                  <span className="meta">{fmtDate(b.createdAt)}</span>
                </Link>
              ))}
            </div>
          </section>
          <section className="card">
            <h2>History</h2>
            <Timeline events={release.events} />
          </section>
        </aside>
      </div>
    </>
  );
}
