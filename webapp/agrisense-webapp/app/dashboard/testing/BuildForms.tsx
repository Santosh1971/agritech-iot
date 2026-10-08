"use client";
import { useState } from "react";
import Link from "next/link";
import { SEVERITY_LABEL, type ChecklistItem, type Severity } from "@/lib/tracker/rules";
import { useSubmit } from "./useSubmit";

type Opt = { id: string; label: string };
type BuildableIssue = { id: string; key: string; title: string; severity: Severity; status: string };

export function NewBuildForm({
  product, issues, releases, checklist, ciFirmware, ciApps,
}: {
  product: { id: string; hasFirmware: boolean; hasApp: boolean; hasHardware: boolean };
  issues: BuildableIssue[];
  releases: Opt[];
  checklist: string;
  ciFirmware: Opt[];
  ciApps: Opt[];
}) {
  const { submit, busy, error, warning } = useSubmit();
  const [picked, setPicked] = useState<Set<string>>(new Set(issues.filter((i) => i.status === "IN_PROGRESS").map((i) => i.id)));
  const toggle = (id: string) => setPicked((s) => { const n = new Set(s); n.has(id) ? n.delete(id) : n.add(id); return n; });

  return (
    <form
      onSubmit={(e) => {
        e.preventDefault();
        const fd = new FormData(e.currentTarget);
        fd.set("productId", product.id);
        fd.delete("issueIds");
        picked.forEach((id) => fd.append("issueIds", id));
        submit("/api/testing/builds", fd, (j) => `/dashboard/testing/build/${j.build.id}`);
      }}
    >
      <label>
        Release this build is for
        <select name="releaseId" defaultValue={releases[0]?.id ?? ""}>
          <option value="">None yet</option>
          {releases.map((r) => <option key={r.id} value={r.id}>{r.label}</option>)}
        </select>
      </label>
      <div className="fields">
        {product.hasFirmware && <label>Firmware version<input name="fwVersion" placeholder="1.5.0-rc1" /></label>}
        {product.hasApp && <label>App version<input name="appVersion" placeholder="2.4.0 (37)" /></label>}
        {product.hasHardware && <label>Board revision<input name="hwRev" placeholder="Rev C" /></label>}
      </div>
      <div className="fields">
        {product.hasFirmware && <label>Firmware .bin<input type="file" name="firmware" accept=".bin" /></label>}
        {product.hasApp && <label>App .apk <span className="hint">debug-signed is fine</span><input type="file" name="apk" accept=".apk" /></label>}
        {product.hasHardware && <label>Gerber .zip<input type="file" name="gerber" accept=".zip,.rar,.7z" /></label>}
      </div>
      {(ciFirmware.length > 0 || ciApps.length > 0) && (
        <div className="fields">
          {ciFirmware.length > 0 && (
            <label>
              Or link a firmware already uploaded <span className="hint">flasher builds</span>
              <select name="firmwareBuildId" defaultValue=""><option value="">None</option>{ciFirmware.map((b) => <option key={b.id} value={b.id}>{b.label}</option>)}</select>
            </label>
          )}
          {ciApps.length > 0 && (
            <label>
              Or link an app already uploaded <span className="hint">CI builds</span>
              <select name="appBuildId" defaultValue=""><option value="">None</option>{ciApps.map((b) => <option key={b.id} value={b.id}>{b.label}</option>)}</select>
            </label>
          )}
        </div>
      )}
      <div className="grid" style={{ gap: 6 }}>
        <span className="small" style={{ fontWeight: 600 }}>Fixes in this build ({picked.size})</span>
        {issues.length === 0 ? (
          <p className="small muted">No accepted, in-progress or reopened issues. You can still post a build for a regression check.</p>
        ) : (
          issues.map((i) => (
            <label key={i.id} style={{ display: "flex", gap: 8, alignItems: "flex-start", fontWeight: 400 }}>
              <input type="checkbox" checked={picked.has(i.id)} onChange={() => toggle(i.id)} style={{ marginTop: 4 }} />
              <span><span className="mono muted">{i.key}</span> {i.title} <span className="small muted">· {SEVERITY_LABEL[i.severity]}</span></span>
            </label>
          ))
        )}
      </div>
      <label>
        Release notes for the tester
        <textarea name="notes" placeholder="What changed, anything to set up before testing" />
      </label>
      <label>
        Regression checklist <span className="hint">one check per line; the tester ticks each</span>
        <textarea name="checklist" defaultValue={checklist} style={{ minHeight: 140 }} />
      </label>
      <label>
        Other files <span className="hint">optional: logs, screenshots, BOM</span>
        <input type="file" name="attachments" multiple />
      </label>
      {error && <div className="error">{error}</div>}
      {warning && <div className="note">{warning}</div>}
      <div className="row"><button className="btn" disabled={busy}>{busy ? "Uploading…" : "Post test build and email tester"}</button></div>
    </form>
  );
}

export function VerdictButtons({ buildId, issueId }: { buildId: string; issueId: string }) {
  const { submit, busy, error } = useSubmit();
  const [failing, setFailing] = useState(false);
  const [note, setNote] = useState("");
  return (
    <div className="grid" style={{ gap: 6 }}>
      <div className="row">
        <button className="btn small" disabled={busy} onClick={() => submit(`/api/testing/builds/${buildId}/action`, { action: "verdict", issueId, verdict: "pass" })}>Pass</button>
        <button className="btn small bad" disabled={busy} onClick={() => setFailing(!failing)}>Fail</button>
      </div>
      {failing && (
        <div className="row">
          <input value={note} onChange={(e) => setNote(e.target.value)} placeholder="What still goes wrong?" style={{ flex: 1, minWidth: 180 }} />
          <button className="btn small bad" disabled={busy || !note.trim()} onClick={() => submit(`/api/testing/builds/${buildId}/action`, { action: "verdict", issueId, verdict: "fail", text: note })}>Reopen</button>
        </div>
      )}
      {error && <div className="error">{error}</div>}
    </div>
  );
}

export function ChecklistRow({ buildId, index, item, canTick, reportHref }: { buildId: string; index: number; item: ChecklistItem; canTick: boolean; reportHref: string }) {
  const { submit, busy, error } = useSubmit();
  const [failing, setFailing] = useState(false);
  const [note, setNote] = useState("");
  const send = (result: "pass" | "fail" | null, n?: string) => submit(`/api/testing/builds/${buildId}/action`, { action: "checklist", index, result, note: n });
  return (
    <div className="check">
      <span className={`mark ${item.result === "pass" ? "ok" : item.result === "fail" ? "no" : ""}`} aria-label={item.result ?? "not tested"}>
        {item.result === "pass" ? "✓" : item.result === "fail" ? "✕" : index + 1}
      </span>
      <div className="grid" style={{ gap: 6 }}>
        <span>{item.text}</span>
        {item.note && <span className="small muted">{item.note}</span>}
        {item.result === "fail" && <Link className="small" href={reportHref}>Report as issue →</Link>}
        {canTick && (
          <div className="row">
            {item.result !== "pass" && <button className="btn small" disabled={busy} onClick={() => send("pass")}>Pass</button>}
            {item.result !== "fail" && <button className="btn small bad" disabled={busy} onClick={() => setFailing(!failing)}>Fail</button>}
            {item.result && <button className="btn small ghost" disabled={busy} onClick={() => send(null)}>Clear</button>}
          </div>
        )}
        {failing && (
          <div className="row">
            <input value={note} onChange={(e) => setNote(e.target.value)} placeholder="What failed?" style={{ flex: 1, minWidth: 180 }} />
            <button className="btn small bad" disabled={busy || !note.trim()} onClick={async () => { if (await send("fail", note)) setFailing(false); }}>Save fail</button>
          </div>
        )}
        {error && <div className="error">{error}</div>}
      </div>
    </div>
  );
}

export function CandidatePicker({ buildId, releases, current }: { buildId: string; releases: Opt[]; current: string | null }) {
  const { submit, busy, error } = useSubmit();
  const [releaseId, setReleaseId] = useState(current ?? releases[0]?.id ?? "");
  if (!releases.length) return <p className="small muted">Open a release on the product page to make this a candidate.</p>;
  return (
    <div className="grid" style={{ gap: 8 }}>
      <label>
        Make this the release candidate for
        <select value={releaseId} onChange={(e) => setReleaseId(e.target.value)}>
          {releases.map((r) => <option key={r.id} value={r.id}>{r.label}</option>)}
        </select>
      </label>
      <div className="row">
        <button className="btn small" disabled={busy || !releaseId} onClick={() => submit(`/api/testing/builds/${buildId}/action`, { action: "candidate", releaseId })}>
          {busy ? "Saving…" : "Set as candidate"}
        </button>
      </div>
      <p className="small muted">Changing the candidate withdraws any earlier sign-off.</p>
      {error && <div className="error">{error}</div>}
    </div>
  );
}
