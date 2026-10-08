import Link from "next/link";
import { notFound } from "next/navigation";
import { prisma } from "@/lib/prisma";
import { AREAS, AREA_LABEL, OPEN_STATUSES, SEVERITIES, SEVERITY_LABEL, STATUSES, STATUS_LABEL, issueKey, type Area, type Severity, type Status } from "@/lib/tracker/rules";
import { fmtDate, fmtDay, trackUser } from "@/lib/tracker/server";
import { NotMember, SevPill, StatusPill, AreaPill } from "../ui";
import { NewReleaseForm } from "../ReleaseForms";

export const dynamic = "force-dynamic";

type SP = { status?: string; sev?: string; area?: string; release?: string };

export default async function ProductPage({ params, searchParams }: { params: Promise<{ code: string }>; searchParams: Promise<SP> }) {
  const user = await trackUser();
  if (!user) return null;
  if (!user.trackRole && !user.isAdmin) return <NotMember isAdmin={false} />;
  const { code } = await params;
  const sp = await searchParams;
  const product = await prisma.trackProduct.findUnique({ where: { code: decodeURIComponent(code) } });
  if (!product) notFound();

  const statusFilter: Status[] | null = sp.status === "all" ? null : sp.status && STATUSES.includes(sp.status as Status) ? [sp.status as Status] : OPEN_STATUSES;
  const [issues, releases, builds] = await Promise.all([
    prisma.trackIssue.findMany({
      where: {
        productId: product.id,
        ...(statusFilter ? { status: { in: statusFilter } } : {}),
        ...(sp.sev && SEVERITIES.includes(sp.sev as Severity) ? { severity: sp.sev as Severity } : {}),
        ...(sp.area && AREAS.includes(sp.area as Area) ? { area: sp.area as Area } : {}),
        ...(sp.release ? { releaseId: sp.release === "none" ? null : sp.release } : {}),
      },
      include: { release: { select: { name: true } }, _count: { select: { files: true } } },
      orderBy: [{ number: "desc" }],
    }),
    prisma.trackRelease.findMany({ where: { productId: product.id }, orderBy: { createdAt: "desc" } }),
    prisma.trackBuild.findMany({ where: { productId: product.id }, orderBy: { number: "desc" }, take: 8, include: { _count: { select: { issues: true } } } }),
  ]);
  const sevOrder = (s: Severity) => SEVERITIES.indexOf(s);
  issues.sort((a, b) => sevOrder(a.severity) - sevOrder(b.severity) || b.number - a.number);

  const base = `/dashboard/testing/${encodeURIComponent(product.code)}`;
  const link = (patch: Partial<SP>) => {
    const q = new URLSearchParams(Object.entries({ ...sp, ...patch }).filter(([, v]) => v) as [string, string][]);
    const s = q.toString();
    return s ? `${base}?${s}` : base;
  };
  const statusKey = sp.status ?? "";

  return (
    <>
      <div className="crumbs"><Link href="/dashboard/testing">Overview</Link><span>/</span><span>{product.code}</span></div>
      <div className="row between">
        <div>
          <span className="eyebrow">{product.modules.join(" · ")}</span>
          <h1>{product.name}</h1>
        </div>
        <div className="row">
          {user.trackRole && <Link className="btn" href={`${base}/new`}>Report issue</Link>}
          {user.trackRole === "DEVELOPER" && <Link className="btn ghost" href={`${base}/build`}>Post test build</Link>}
        </div>
      </div>

      <div className="two">
        <section className="card">
          <div className="row between"><h2>Issues</h2><span className="small muted">{issues.length} shown</span></div>
          <div className="filters" aria-label="Status filter">
            <Link className={statusKey === "" ? "on" : ""} href={link({ status: undefined })}>Open</Link>
            {(["FIX_READY", "REOPENED", "VERIFIED", "DEFERRED", "WONT_FIX"] as Status[]).map((s) => (
              <Link key={s} className={statusKey === s ? "on" : ""} href={link({ status: s })}>{STATUS_LABEL[s]}</Link>
            ))}
            <Link className={statusKey === "all" ? "on" : ""} href={link({ status: "all" })}>All</Link>
          </div>
          <div className="filters" aria-label="Severity and area filter">
            {SEVERITIES.map((s) => (
              <Link key={s} className={sp.sev === s ? "on" : ""} href={link({ sev: sp.sev === s ? undefined : s })}>{SEVERITY_LABEL[s]}</Link>
            ))}
            {AREAS.map((a) => (
              <Link key={a} className={sp.area === a ? "on" : ""} href={link({ area: sp.area === a ? undefined : a })}>{AREA_LABEL[a]}</Link>
            ))}
          </div>
          {issues.length === 0 ? (
            <p className="muted small">No issues match.</p>
          ) : (
            <div className="list">
              {issues.map((i) => (
                <Link key={i.id} href={`/dashboard/testing/issue/${i.id}`} className="item">
                  <span className="key">{issueKey(product.code, i.number)}</span>
                  <span className="ttl">{i.title}</span>
                  <StatusPill s={i.status} />
                  <span className="meta">
                    <SevPill s={i.severity} />
                    <AreaPill a={i.area} />
                    {i.module && <span>{i.module}</span>}
                    {i.release && <span>· {i.release.name}</span>}
                    {i.proposal && <span className="pill warn">{i.proposal === "DEFERRED" ? "Defer" : "Won't fix"} proposed</span>}
                    {i._count.files > 0 && <span>· {i._count.files} file(s)</span>}
                    <span>· {fmtDate(i.updatedAt)}</span>
                  </span>
                </Link>
              ))}
            </div>
          )}
        </section>

        <div className="grid" style={{ alignContent: "start" }}>
          <section className="card">
            <h2>Releases</h2>
            {releases.length === 0 && <p className="muted small">No release yet. The developer opens one to target fixes at.</p>}
            <div className="list">
              {releases.map((r) => (
                <Link key={r.id} href={`/dashboard/testing/release/${r.id}`} className="item">
                  <span className="key">{r.status === "RELEASED" ? "✓" : "•"}</span>
                  <span className="ttl">{r.name}</span>
                  <span className={`pill ${r.status === "RELEASED" ? "good" : r.status === "SIGNED_OFF" ? "lime" : "info"}`}>{r.status === "OPEN" ? "In test" : r.status === "SIGNED_OFF" ? "Signed off" : "Released"}</span>
                  <span className="meta">{r.releasedAt ? `Released ${fmtDay(r.releasedAt)}` : `Opened ${fmtDay(r.createdAt)}`}</span>
                </Link>
              ))}
            </div>
            {user.trackRole === "DEVELOPER" && <NewReleaseForm productId={product.id} productName={product.name} />}
          </section>
          <section className="card">
            <h2>Test builds</h2>
            {builds.length === 0 && <p className="muted small">None yet.</p>}
            <div className="list">
              {builds.map((b) => (
                <Link key={b.id} href={`/dashboard/testing/build/${b.id}`} className="item">
                  <span className="key">#{b.number}</span>
                  <span className="ttl">{[b.fwVersion && `fw ${b.fwVersion}`, b.appVersion && `app ${b.appVersion}`, b.hwRev && `board ${b.hwRev}`].filter(Boolean).join(" · ")}</span>
                  <span className="small muted">{b._count.issues} fix(es)</span>
                  <span className="meta">{fmtDate(b.createdAt)}</span>
                </Link>
              ))}
            </div>
          </section>
        </div>
      </div>
    </>
  );
}
