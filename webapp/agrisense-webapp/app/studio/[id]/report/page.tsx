import { notFound } from "next/navigation";
import { prisma } from "@/lib/prisma";
import { describeRule, outputs, readings } from "@/lib/studio/automation";
import type { AppLayout } from "@/lib/studio/appLayout";
import { BLOCK_BY_ID } from "@/lib/studio/blocks";
import { plan, type EnclosureChoice } from "@/lib/studio/enclosure";
import { onBands, onHours, series, summary, type FieldRecord } from "@/lib/studio/field";
import { KITS } from "@/lib/studio/kits";
import type { ProblemData, SpecData } from "@/lib/studio/problem";
import { power } from "@/lib/studio/rules";
import { STAGES } from "@/lib/studio/stages";
import { projectAccess, studioUser } from "@/lib/studio/access";
import { loadProject } from "@/lib/studio/server";
import FieldChart from "../../FieldChart";
import PrintButton from "./PrintButton";

export const dynamic = "force-dynamic";

const day = (s?: string | number | null) => (s ? new Date(s).toLocaleDateString("en-IN", { day: "numeric", month: "long", year: "numeric" }) : "—");

export default async function ReportPage({ params }: { params: Promise<{ id: string }> }) {
  const { id } = await params;
  const u = await studioUser();
  if (!u || !(await projectAccess(u, id))) notFound();
  const s = await loadProject(id);
  if (!s) notFound();
  const rows = await prisma.studioFieldRecord.findMany({ where: { projectId: id }, orderBy: { t: "asc" }, take: 10000 });
  const records: FieldRecord[] = rows.map((r) => ({ t: r.t.toISOString(), design: r.design, values: r.values as FieldRecord["values"], outputs: r.outputs as FieldRecord["outputs"] }));

  const problem = (s.stages.problem.data ?? {}) as ProblemData;
  const spec = s.stages.spec.data as SpecData | null;
  const d = s.design;
  const ports = d?.ports ?? {};
  const used = Object.entries(ports).filter(([, b]) => b) as [string, string][];
  const build = s.stages.build.data as { sent?: { design: number; at: string; id: string; fw: string } } | null;
  const test = s.stages.test.data as { items?: { label: string; ok: boolean; at: string; value?: number }[] } | null;
  const app = d?.app as AppLayout | null;
  const encl = s.stages.encl.data as EnclosureChoice | null;
  const encPlan = encl && d ? plan(s.kit, ports, encl) : null;
  const notes = (s.stages.report.data ?? {}) as { worked?: string; change?: string; site?: string };
  const sum = summary(records);
  const pw = power(ports);
  const firstOut = outputs(ports)[0];

  return (
    <article className="report">
      <div className="no-print row between"><span className="small muted">This page is your report. Print it, or save it as a PDF.</span><PrintButton /></div>
      <header>
        <div className="eyebrow">{s.cohort.institution} · {s.cohort.name}{s.cohort.session ? ` · ${s.cohort.session}` : ""}</div>
        <h1>{s.title}</h1>
        <p>Team: {s.members.map((m) => m.name).join(", ") || "ASC team"} · {KITS[s.kit].name} kit · Report built {day(Date.now())}</p>
      </header>

      <section>
        <h2>Progress and sign-offs</h2>
        <table><thead><tr><th>Stage</th><th>Status</th><th>Signed off</th></tr></thead><tbody>
          {STAGES.map((st) => {
            const x = s.stages[st.key];
            return <tr key={st.key}><td>{st.title}</td><td>{x.status === "DONE" ? "Done" : x.status === "SUBMITTED" ? "Waiting for sign-off" : x.status === "IN_PROGRESS" ? "In progress" : st.gate === "optional" ? "Skipped (optional)" : "Not started"}</td><td>{x.signedOffBy ? `${x.signedOffBy}, ${day(x.signedOffAt)}` : st.gate === "mentor" ? "—" : "Automatic"}</td></tr>;
          })}
        </tbody></table>
      </section>

      <section>
        <h2>1. The problem</h2>
        <p>{problem.story}</p>
        <table><tbody>
          {problem.crop && <tr><th>Crop / site</th><td>{problem.crop}</td></tr>}
          {problem.area && <tr><th>Area</th><td>{problem.area}</td></tr>}
          {problem.water && <tr><th>Water</th><td>{problem.water}</td></tr>}
          <tr><th>Power at the site</th><td>{problem.power === "mains" ? "Mains" : problem.power === "solar" ? "Solar only" : "None"}</td></tr>
          <tr><th>Network</th><td>{problem.network === "wifi" ? "WiFi" : problem.network === "mobile" ? "Mobile signal" : "None"}</td></tr>
          {problem.climate && <tr><th>Climate</th><td>{problem.climate}</td></tr>}
        </tbody></table>
      </section>

      {spec && (
        <section>
          <h2>2. Specification</h2>
          <p><b>What it does.</b> {spec.what}</p>
          <p><b>What it is made of.</b> {spec.madeOf}</p>
          <p><b>How it is used.</b> {spec.use}</p>
          <p><b>Safety.</b> {spec.safety}</p>
          <table><tbody>{spec.requirements.map((r) => <tr key={r.id + r.text}><td className="mono">{r.id}</td><td>{r.text}</td></tr>)}</tbody></table>
        </section>
      )}

      {d && (
        <section>
          <h2>3. Design</h2>
          <table><thead><tr><th>Port</th><th>Part</th><th>Supply</th></tr></thead><tbody>
            {used.map(([p, b]) => <tr key={p}><td className="mono">{p}</td><td>{BLOCK_BY_ID[b]?.name ?? b}</td><td>{BLOCK_BY_ID[b]?.supply}</td></tr>)}
          </tbody></table>
          <p>Peak current: {pw.p3} mA on the 3.3 V rail, about {pw.total5} mA from the 5 V input. Design version {d.version}.</p>
          <h3>Rules</h3>
          {(d.rules ?? []).length ? <ul>{(d.rules ?? []).map((r, i) => <li key={i}>{describeRule(ports, r)}</li>)}</ul> : <p>No automatic rules. The outputs are switched by hand.</p>}
        </section>
      )}

      <section>
        <h2>4. Simulation and build</h2>
        <p>Simulation: {s.stages.sim.status === "DONE" ? "tried in Wokwi" : "skipped"}.</p>
        {build?.sent && <p>Design version {build.sent.design} was sent to board {build.sent.id} (firmware {build.sent.fw}) on {day(build.sent.at)}.</p>}
      </section>

      {test?.items && (
        <section>
          <h2>5. Bench test</h2>
          <table><thead><tr><th>Check</th><th>Result</th><th>When</th></tr></thead><tbody>
            {test.items.map((i) => <tr key={i.label}><td>{i.label}</td><td>{i.ok ? `Passed${i.value !== undefined ? ` (read ${i.value})` : ""}` : "Not passed"}</td><td>{day(i.at)}</td></tr>)}
          </tbody></table>
        </section>
      )}

      {app && (
        <section>
          <h2>6. Phone app</h2>
          <p>App name: <b>{app.name}</b>, in {app.lang === "hi" ? "Hindi" : "English"}.</p>
          <table><thead><tr><th>Tile</th><th>Shows</th><th>As</th></tr></thead><tbody>
            {app.tiles.map((t, i) => <tr key={i}><td>{t.label}{t.labelHi ? ` / ${t.labelHi}` : ""}</td><td className="mono">{t.ref}</td><td>{t.kind}</td></tr>)}
          </tbody></table>
          {app.alerts.length > 0 && <ul>{app.alerts.map((a, i) => <li key={i}>Alert &quot;{a.say}&quot; when {a.ref} goes {a.when} {a.value}.</li>)}</ul>}
        </section>
      )}

      {encPlan && (
        <section>
          <h2>7. Enclosure</h2>
          <p>{encPlan.box.name}, {encPlan.box.w} × {encPlan.box.h} × {encPlan.box.d} mm, with {encPlan.holes.length} holes on the bottom wall.</p>
          <table><tbody>{encPlan.holes.map((h) => <tr key={h.id}><td className="mono">{h.id}</td><td>{h.label}</td><td>Ø {h.dia} mm at {h.x} mm</td></tr>)}</tbody></table>
        </section>
      )}

      <section>
        <h2>8. Field trial</h2>
        {notes.site && <p>{notes.site}</p>}
        {sum ? (
          <>
            <p>{sum.count} records from {day(sum.first)} to {day(sum.last)} ({sum.hours < 48 ? `${sum.hours} hours` : `${Math.round(sum.hours / 24)} days`}).
              {outputs(ports).map((o) => ` ${o.label} was ON for ${onHours(records, o.port)} hours.`).join("")}</p>
            {readings(ports).map((r) => (
              <FieldChart key={r.ref} title={r.label} unit={r.unit} points={series(records, r.ref)}
                bands={firstOut ? onBands(records, firstOut.port) : []} bandLabel={firstOut ? `${firstOut.label} ON` : undefined} />
            ))}
          </>
        ) : <p>No field records yet.</p>}
      </section>

      <section>
        <h2>9. What we learned</h2>
        <h3>What worked</h3><p>{notes.worked || "—"}</p>
        <h3>What we would change</h3><p>{notes.change || "—"}</p>
      </section>
      <footer className="small muted">Built with the ASC Product Studio · Agri Sensors and Controls · agrisenseandcontrol.in</footer>
    </article>
  );
}
