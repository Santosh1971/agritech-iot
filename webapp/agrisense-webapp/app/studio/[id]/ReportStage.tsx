"use client";
import { useEffect, useState } from "react";
import Link from "next/link";
import { outputs, readings } from "@/lib/studio/automation";
import { onBands, onHours, series, summary, type FieldRecord } from "@/lib/studio/field";
import { Eng, Why } from "../EngView";
import FieldChart from "../FieldChart";
import { call } from "./api";
import { useDevice } from "./BuildStage";
import { useSerialSupported } from "./device";
import { GateBar, type StageProps } from "./ProjectClient";

type ReportData = { worked?: string; change?: string; site?: string };

export default function ReportStage(props: StageProps) {
  const { state } = props;
  const dev = useDevice();
  const serialOk = useSerialSupported();
  const design = state.design;
  const st = state.stages.report;
  const editable = st.status === "NOT_STARTED" || st.status === "IN_PROGRESS";
  const stored = (st.data ?? {}) as ReportData;
  const [records, setRecords] = useState<FieldRecord[] | null>(null);
  const [notes, setNotes] = useState<ReportData>({ worked: stored.worked ?? "", change: stored.change ?? "", site: stored.site ?? "" });
  const [dirty, setDirty] = useState(false);
  const [busy, setBusy] = useState(false);
  const [progress, setProgress] = useState("");
  const [error, setError] = useState("");

  const load = () => fetch(`/api/studio/projects/${state.id}/field`).then((r) => r.json()).then((j) => setRecords(j.records ?? [])).catch(() => setRecords([]));
  useEffect(() => { load(); }, []); // eslint-disable-line react-hooks/exhaustive-deps

  if (state.stages.encl.status !== "DONE" || !design) {
    return <div className="card"><p>The field trial starts once your teacher has signed off the enclosure.</p></div>;
  }

  const sum = records ? summary(records) : null;
  const download = async () => {
    setBusy(true); setError(""); setProgress("");
    try {
      if (!dev.connected) await dev.connect();
      const since = sum ? Math.floor(sum.last / 1000) + 1 : 0;
      const recs = await dev.downloadLog(since, (n) => setProgress(`${n} records received…`));
      let added = 0;
      for (let i = 0; i < recs.length; i += 2000) {
        const r = await call(`/api/studio/projects/${state.id}/field`, { records: recs.slice(i, i + 2000) });
        added += r.added;
      }
      setProgress(recs.length ? `${added} new records saved.` : "No new records on the board since the last download.");
      await load();
    } catch (e) { setError((e as Error).message); } finally { setBusy(false); }
  };
  const save = async () => {
    setError("");
    try { await call(`/api/studio/projects/${state.id}/stages/report`, { data: notes }, "PUT"); setDirty(false); props.refresh(); }
    catch (e) { setError((e as Error).message); }
  };
  const field = (k: keyof ReportData, label: string, hint: string) => (
    <label className="field">{label} <span>{hint}</span>
      <textarea id={`report-${k}`} disabled={!editable} value={notes[k] ?? ""} onChange={(e) => { setNotes({ ...notes, [k]: e.target.value }); setDirty(true); }} />
    </label>
  );
  const outs = outputs(design.ports);
  const firstOut = outs[0];
  const ready = !!sum && (notes.worked ?? "").trim().length >= 20 && !dirty;

  return (
    <>
      <div className="two">
        <div className="card grid">
          <div className="eyebrow">1 · Run it in the field</div>
          <p>Install the board at the site and leave it running. Every 10 minutes it saves its readings and the state of each output in its own memory, so it needs no WiFi. That's about two weeks of records.</p>
          <p className="small muted">Before you leave it, connect it to the studio once (step 2) so it gets the correct time. On the stand-in DevKit, which has no clock battery, connect again after any power cut.</p>
          {field("site", "Where is it installed?", "the site, the crop, and the date you started")}
        </div>
        <div className="card grid">
          <div className="eyebrow">2 · Bring the readings home</div>
          <p>Connect the board over USB and download its log. Download as often as you like: records already saved are skipped.</p>
          <div className="row">
            <button className="btn" disabled={busy || !serialOk || !editable} onClick={download}>{busy ? "Downloading…" : dev.connected ? "Download the field log" : "Connect and download the field log"}</button>
          </div>
          {progress && <p className="small">{progress}</p>}
          {records === null ? <p className="small muted">Loading saved records…</p> : sum ? (
            <div className="tbl-wrap"><table><tbody>
              <tr><th>Records</th><td>{sum.count}</td></tr>
              <tr><th>From</th><td>{new Date(sum.first).toLocaleString()}</td></tr>
              <tr><th>To</th><td>{new Date(sum.last).toLocaleString()}</td></tr>
              <tr><th>Trial length</th><td>{sum.hours < 48 ? `${sum.hours} hours` : `${Math.round(sum.hours / 24)} days`}</td></tr>
              {outs.map((o) => <tr key={o.port}><th>{o.label}</th><td>ON for {onHours(records!, o.port)} hours</td></tr>)}
            </tbody></table></div>
          ) : <p className="small muted">No records yet.</p>}
        </div>
      </div>

      {records && records.length > 1 && (
        <div className="card grid">
          <div className="eyebrow">What the field showed</div>
          {readings(design.ports).map((r) => (
            <FieldChart key={r.ref} title={r.label} unit={r.unit} points={series(records, r.ref)}
              bands={firstOut ? onBands(records, firstOut.port) : []} bandLabel={firstOut ? `${firstOut.label} ON` : undefined} />
          ))}
        </div>
      )}

      <div className="two">
        <div className="card grid">
          <div className="eyebrow">3 · What you learned</div>
          {field("worked", "What worked?", "did the rules do what you expected in the field? Use the charts")}
          {field("change", "What would you change?", "thresholds, sensor positions, the box, the app…")}
          {editable && <div className="row"><button className="btn" disabled={!dirty} onClick={save}>{dirty ? "Save" : "Saved"}</button></div>}
          {error && <p className="error">{error}</p>}
        </div>
        <div className="grid">
          <div className="card grid">
            <div className="eyebrow">4 · Your project report</div>
            <p>The studio builds your report from everything you've done: the problem, the spec, the design and rules, test results, the app, the box, and the field charts.</p>
            <div className="row"><Link className="btn" href={`/studio/${state.id}/report`} target="_blank">Open the report</Link></div>
            <p className="small muted">In the report, use your browser&apos;s Print and choose &quot;Save as PDF&quot; to hand it in.</p>
          </div>
          <Why title="Field data is the real test">
            The bench test showed each part works. Only days in the field show whether your thresholds suit the crop, the weather and the soil. That's why the report is built from your own data.
          </Why>
          <Eng title="a field-log record">
            <pre className="code">{records?.length ? JSON.stringify(records[records.length - 1], null, 2) : "No records yet."}</pre>
          </Eng>
        </div>
      </div>
      <GateBar {...props} stage="report" canFinish={ready}
        finishHint={!sum ? "Download the field log first." : dirty ? "Save your notes first." : (notes.worked ?? "").trim().length < 20 ? "Write what worked, in a few sentences." : "Submit the report to your teacher for the final sign-off."} />
    </>
  );
}
