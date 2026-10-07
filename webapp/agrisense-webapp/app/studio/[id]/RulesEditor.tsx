"use client";
// "When should each output switch?" One rule per output, written in words
// and checked by lib/studio/automation.ts before anything is saved.
import { useState } from "react";
import { checkRules, defaultRules, describeRule, guards, outputs, readings, type Rule } from "@/lib/studio/automation";
import { Eng } from "../EngView";
import { call } from "./api";
import type { StageProps } from "./ProjectClient";

export default function RulesEditor({ state, refresh, locked }: StageProps & { locked: boolean }) {
  const design = state.design!;
  const ports = design.ports;
  const saved = design.rules;
  const [rules, setRules] = useState<Rule[]>(saved ?? defaultRules(ports));
  const [dirty, setDirty] = useState(!saved);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState("");

  const rs = readings(ports);
  const gs = guards(ports);
  const outs = outputs(ports);
  const list = checkRules(ports, rules);
  const edit = (i: number, patch: Partial<Rule>) => {
    const next = rules.map((r, k) => (k === i ? { ...r, ...patch } : r));
    setRules(next);
    setDirty(true);
  };
  const save = async () => {
    setBusy(true); setError("");
    try { await call(`/api/studio/projects/${state.id}/rules`, { rules }, "PUT"); setDirty(false); refresh(); }
    catch (e) { setError((e as Error).message); } finally { setBusy(false); }
  };
  const free = outs.filter((o) => !rules.some((r) => r.out === o.port));

  if (!outs.length) {
    return <div className="card"><p>Your design has no outputs, so there is nothing to switch. The board will report its readings to the app.</p></div>;
  }

  return (
    <div className="card grid">
      <div className="row between">
        <div className="eyebrow">Your rules</div>
        {saved && !dirty && <span className="pill done">Saved with design version {design.version}</span>}
        {!saved && <span className="pill warn">Suggested rules, not saved yet</span>}
      </div>
      {rules.map((r, i) => {
        const reading = rs.find((x) => x.ref === r.sensor);
        return (
          <fieldset key={i} className="rule" disabled={locked}>
            <legend>{outs.find((o) => o.port === r.out)?.label ?? r.out}</legend>
            <div className="rule-row">
              <label className="field">Turns ON when
                <select id={`rule-${i}-sensor`} value={r.sensor} onChange={(e) => edit(i, { sensor: e.target.value })}>
                  {rs.map((x) => <option key={x.ref} value={x.ref}>{x.label}</option>)}
                </select>
              </label>
              <label className="field">goes
                <select id={`rule-${i}-when`} value={r.when} onChange={(e) => edit(i, { when: e.target.value as Rule["when"] })}>
                  <option value="below">below</option><option value="above">above</option>
                </select>
              </label>
              <label className="field">ON at {reading && <span>{reading.unit}</span>}
                <input id={`rule-${i}-on`} type="number" step={reading?.step ?? 1} value={Number.isFinite(r.on) ? r.on : ""} onChange={(e) => edit(i, { on: e.target.valueAsNumber })} />
              </label>
              <label className="field">OFF at {reading && <span>{reading.unit}</span>}
                <input id={`rule-${i}-off`} type="number" step={reading?.step ?? 1} value={Number.isFinite(r.off) ? r.off : ""} onChange={(e) => edit(i, { off: e.target.valueAsNumber })} />
              </label>
            </div>
            <div className="rule-row">
              <label className="field wide">Only between <span>leave empty for all day</span>
                <div className="row" style={{ flexWrap: "nowrap" }}>
                  <input id={`rule-${i}-from`} type="time" value={r.from ?? ""} onChange={(e) => edit(i, { from: e.target.value || undefined, to: r.to ?? (e.target.value ? "18:00" : undefined) })} />
                  <span>and</span>
                  <input id={`rule-${i}-to`} type="time" value={r.to ?? ""} onChange={(e) => edit(i, { to: e.target.value || undefined })} />
                </div>
              </label>
              {gs.length > 0 && (
                <label className="field">And only while
                  <select id={`rule-${i}-guard`} value={r.guard ?? ""} onChange={(e) => edit(i, { guard: e.target.value || undefined })}>
                    <option value="">no condition</option>
                    {gs.map((g) => <option key={g.port} value={g.port}>{g.label.charAt(0).toLowerCase() + g.label.slice(1)}</option>)}
                  </select>
                </label>
              )}
            </div>
            <p className="small muted">{describeRule(ports, r)}</p>
            {!locked && <button type="button" className="btn ghost small" onClick={() => { setRules(rules.filter((_, k) => k !== i)); setDirty(true); }}>Remove this rule</button>}
          </fieldset>
        );
      })}
      {!locked && free.length > 0 && rs.length > 0 && (
        <div className="row">
          {free.map((o) => (
            <button key={o.port} type="button" className="btn ghost small"
              onClick={() => { setRules([...rules, { out: o.port, sensor: rs[0].ref, when: "below", on: 30, off: 45 }]); setDirty(true); }}>
              + Rule for {o.label}
            </button>
          ))}
        </div>
      )}
      {list.map((c, i) => <div key={i} className={`msg ${c.level}`}><span className="ic">{c.level === "ok" ? "✓" : c.level === "warn" ? "!" : "✗"}</span><span>{c.text}</span></div>)}
      {!locked && (
        <div className="row">
          <button className="btn" disabled={busy || !dirty || list.some((c) => c.level === "bad")} onClick={save}>{dirty ? "Save rules" : "Saved"}</button>
          {dirty && <span className="muted small">Saving makes a new design version, which you then send to the board.</span>}
        </div>
      )}
      {error && <p className="error">{error}</p>}
      <Eng title="rules as the board receives them">
        <pre className="code">{JSON.stringify(rules, null, 2)}</pre>
      </Eng>
    </div>
  );
}
