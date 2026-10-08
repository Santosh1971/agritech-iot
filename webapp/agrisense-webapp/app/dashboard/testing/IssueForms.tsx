"use client";
import { useState } from "react";
import {
  ACTION_LABEL, AREAS, AREA_LABEL, NEEDS_TEXT, SEVERITIES, SEVERITY_HINT, SEVERITY_LABEL,
  type Area, type IssueAction, type Severity,
} from "@/lib/tracker/rules";
import { useSubmit } from "./useSubmit";

type Product = { id: string; code: string; name: string; modules: string[]; hasFirmware: boolean; hasApp: boolean; hasHardware: boolean };
type Release = { id: string; name: string };

function Choice<T extends string>({ options, value, onChange, label, cls }: { options: T[]; value: T | null; onChange: (v: T) => void; label: (v: T) => string; cls?: (v: T) => string }) {
  return (
    <div className="choice">
      {options.map((o) => (
        <button type="button" key={o} aria-pressed={value === o} className={cls?.(o)} onClick={() => onChange(o)}>
          {label(o)}
        </button>
      ))}
    </div>
  );
}

export function NewIssueForm({ product, defaults }: { product: Product; defaults: { foundFw?: string; foundApp?: string; foundHw?: string; title?: string; steps?: string } }) {
  const { submit, busy, error } = useSubmit();
  const [area, setArea] = useState<Area | null>(product.hasFirmware ? null : "HARDWARE");
  const [severity, setSeverity] = useState<Severity | null>(null);
  const areas = AREAS.filter((a) => (a === "FIRMWARE" ? product.hasFirmware : a === "APP" ? product.hasApp : a === "HARDWARE" ? product.hasHardware : true));

  return (
    <form
      onSubmit={(e) => {
        e.preventDefault();
        const fd = new FormData(e.currentTarget);
        fd.set("productId", product.id);
        if (area) fd.set("area", area);
        if (severity) fd.set("severity", severity);
        submit("/api/testing/issues", fd, (j) => `/dashboard/testing/issue/${j.issue.id}`);
      }}
    >
      <label>
        Short title
        <input name="title" required maxLength={200} defaultValue={defaults.title} placeholder="e.g. Pump does not stop when tank is full" />
      </label>
      <div className="grid" style={{ gap: 6 }}>
        <span className="small" style={{ fontWeight: 600 }}>Area</span>
        <Choice options={areas} value={area} onChange={setArea} label={(a) => AREA_LABEL[a]} />
      </div>
      {product.modules.length > 1 && (
        <label>
          Module
          <select name="module" defaultValue="">
            <option value="">Whole product</option>
            {product.modules.map((m) => <option key={m}>{m}</option>)}
          </select>
        </label>
      )}
      <div className="grid" style={{ gap: 6 }}>
        <span className="small" style={{ fontWeight: 600 }}>Severity</span>
        <Choice options={SEVERITIES} value={severity} onChange={setSeverity} label={(s) => SEVERITY_LABEL[s]} cls={(s) => `sev-${s}`} />
        {severity && <span className="small muted">{SEVERITY_HINT[severity]}</span>}
      </div>
      <label>
        Steps to make it happen
        <textarea name="steps" required defaultValue={defaults.steps} placeholder={"1. Set tank level to full\n2. Start pump from app\n3. ..."} />
      </label>
      <label>
        What happened
        <textarea name="actual" required placeholder="What you saw, with any error text or LED pattern" />
      </label>
      <label>
        What should happen <span className="hint">optional</span>
        <textarea name="expected" style={{ minHeight: 60 }} />
      </label>
      <div className="fields">
        {product.hasFirmware && <label>Firmware version<input name="foundFw" defaultValue={defaults.foundFw} /></label>}
        {product.hasApp && <label>App version<input name="foundApp" defaultValue={defaults.foundApp} /></label>}
        {product.hasHardware && <label>Board revision<input name="foundHw" defaultValue={defaults.foundHw} /></label>}
        <label>Device ID <span className="hint">optional</span><input name="deviceId" /></label>
      </div>
      <label>
        Photos, video or log <span className="hint">up to 25 MB each</span>
        <input type="file" name="files" multiple accept="image/*,video/*,.txt,.log,.csv,.pdf,.zip" />
      </label>
      {error && <div className="error">{error}</div>}
      {!area || !severity ? <p className="small muted">Pick an area and a severity to submit.</p> : null}
      <div className="row">
        <button className="btn" disabled={busy || !area || !severity}>{busy ? "Saving…" : "Report issue"}</button>
      </div>
    </form>
  );
}

const PRIMARY: IssueAction[] = ["accept", "start", "answer", "verify_pass", "agree_proposal"];
const DANGER: IssueAction[] = ["verify_fail", "reopen", "propose_wontfix"];

export function IssueActions({ issueId, actions, releases, currentReleaseId, severity }: { issueId: string; actions: IssueAction[]; releases: Release[]; currentReleaseId: string | null; severity: Severity }) {
  const { submit, busy, error, warning } = useSubmit();
  const [action, setAction] = useState<IssueAction>("comment");
  const [text, setText] = useState("");
  const [sev, setSev] = useState<Severity>(severity);
  const [releaseId, setReleaseId] = useState(currentReleaseId ?? releases[0]?.id ?? "");
  const [fileKey, setFileKey] = useState(0);

  const needsText = NEEDS_TEXT.includes(action);
  return (
    <form
      onSubmit={async (e) => {
        e.preventDefault();
        const fd = new FormData(e.currentTarget);
        fd.set("action", action);
        fd.set("text", text);
        if (action === "accept") {
          fd.set("severity", sev);
          fd.set("releaseId", releaseId);
        }
        if (await submit(`/api/testing/issues/${issueId}/action`, fd)) {
          setText("");
          setAction("comment");
          setFileKey((k) => k + 1);
        }
      }}
    >
      <div className="row">
        {actions.filter((a) => a !== "comment").map((a) => (
          <button
            type="button"
            key={a}
            className={`btn small ${PRIMARY.includes(a) ? "" : DANGER.includes(a) ? "bad" : "ghost"} ${action === a ? "on" : ""}`}
            aria-pressed={action === a}
            onClick={() => setAction(action === a ? "comment" : a)}
          >
            {ACTION_LABEL[a]}
          </button>
        ))}
      </div>
      {action === "accept" && (
        <div className="fields">
          <label>
            Severity
            <select value={sev} onChange={(e) => setSev(e.target.value as Severity)}>
              {SEVERITIES.map((s) => <option key={s} value={s}>{SEVERITY_LABEL[s]}</option>)}
            </select>
          </label>
          <label>
            Target release
            <select value={releaseId} onChange={(e) => setReleaseId(e.target.value)}>
              <option value="">Not yet decided</option>
              {releases.map((r) => <option key={r.id} value={r.id}>{r.name}</option>)}
            </select>
          </label>
        </div>
      )}
      <label>
        {action === "comment" ? "Comment" : `${ACTION_LABEL[action]}: note${needsText ? "" : " (optional)"}`}
        <textarea value={text} onChange={(e) => setText(e.target.value)} placeholder={action === "verify_fail" ? "What still goes wrong on this build?" : action === "need_info" ? "What do you need to know?" : ""} />
      </label>
      <input key={fileKey} type="file" name="files" multiple accept="image/*,video/*,.txt,.log,.csv,.pdf,.zip" aria-label="Attach files" />
      {error && <div className="error">{error}</div>}
      {warning && <div className="note">{warning}</div>}
      <div className="row">
        <button className={`btn ${DANGER.includes(action) ? "bad" : ""}`} disabled={busy || (needsText && !text.trim() && action !== "comment")}>
          {busy ? "Saving…" : action === "comment" ? "Post comment" : ACTION_LABEL[action]}
        </button>
        {action !== "comment" && <button type="button" className="btn ghost" onClick={() => setAction("comment")}>Cancel</button>}
      </div>
    </form>
  );
}

type EditIssue = { id: string; title: string; steps: string; expected: string | null; actual: string; foundFw: string | null; foundApp: string | null; foundHw: string | null; deviceId: string | null; module: string | null; area: Area; severity: Severity; releaseId: string | null };

export function IssueEditor({ issue, fields, modules, releases }: { issue: EditIssue; fields: readonly string[]; modules: string[]; releases: Release[] }) {
  const { submit, busy, error } = useSubmit();
  const [open, setOpen] = useState(false);
  if (!fields.length) return null;
  if (!open) return <button className="btn small ghost" onClick={() => setOpen(true)}>Edit details</button>;
  const has = (f: string) => fields.includes(f);
  return (
    <form
      className="card"
      onSubmit={async (e) => {
        e.preventDefault();
        const fd = new FormData(e.currentTarget);
        const body: Record<string, string> = {};
        for (const f of fields) if (fd.has(f)) body[f] = String(fd.get(f));
        if (fd.get("note")) body.note = String(fd.get("note"));
        if (await submit(`/api/testing/issues/${issue.id}/edit`, body)) setOpen(false);
      }}
    >
      <h3>Edit details</h3>
      {has("title") && <label>Title<input name="title" defaultValue={issue.title} required /></label>}
      <div className="fields">
        {has("area") && (
          <label>Area<select name="area" defaultValue={issue.area}>{AREAS.map((a) => <option key={a} value={a}>{AREA_LABEL[a]}</option>)}</select></label>
        )}
        {has("severity") && (
          <label>Severity<select name="severity" defaultValue={issue.severity}>{SEVERITIES.map((s) => <option key={s} value={s}>{SEVERITY_LABEL[s]}</option>)}</select></label>
        )}
        {has("module") && modules.length > 1 && (
          <label>Module<select name="module" defaultValue={issue.module ?? ""}><option value="">Whole product</option>{modules.map((m) => <option key={m}>{m}</option>)}</select></label>
        )}
        {has("releaseId") && (
          <label>Target release<select name="releaseId" defaultValue={issue.releaseId ?? ""}><option value="">Not yet decided</option>{releases.map((r) => <option key={r.id} value={r.id}>{r.name}</option>)}</select></label>
        )}
        {has("foundFw") && <label>Firmware version<input name="foundFw" defaultValue={issue.foundFw ?? ""} /></label>}
        {has("foundApp") && <label>App version<input name="foundApp" defaultValue={issue.foundApp ?? ""} /></label>}
        {has("foundHw") && <label>Board revision<input name="foundHw" defaultValue={issue.foundHw ?? ""} /></label>}
        {has("deviceId") && <label>Device ID<input name="deviceId" defaultValue={issue.deviceId ?? ""} /></label>}
      </div>
      {has("steps") && <label>Steps<textarea name="steps" defaultValue={issue.steps} required /></label>}
      {has("actual") && <label>What happened<textarea name="actual" defaultValue={issue.actual} required /></label>}
      {has("expected") && <label>What should happen<textarea name="expected" defaultValue={issue.expected ?? ""} /></label>}
      <label>Why the change <span className="hint">optional, shown in the timeline</span><input name="note" /></label>
      {error && <div className="error">{error}</div>}
      <div className="row">
        <button className="btn" disabled={busy}>{busy ? "Saving…" : "Save"}</button>
        <button type="button" className="btn ghost" onClick={() => setOpen(false)}>Cancel</button>
      </div>
    </form>
  );
}
