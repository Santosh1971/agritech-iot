"use client";
import { useState } from "react";
import { KITS, type KitKey } from "@/lib/studio/kits";
import { problemComplete, suggestKit, type ProblemData } from "@/lib/studio/problem";
import { Eng, Why } from "../EngView";
import { call } from "./api";
import { GateBar, type StageProps } from "./ProjectClient";

export default function ProblemStage(props: StageProps) {
  const { state } = props;
  const st = state.stages.problem;
  const locked = st.status === "DONE" || st.status === "SUBMITTED";
  const [p, setP] = useState<ProblemData>((st.data ?? {}) as ProblemData);
  const [kit, setKit] = useState<KitKey>(state.kit);
  const [saved, setSaved] = useState(true);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState("");

  const set = (k: keyof ProblemData, v: string) => { setP({ ...p, [k]: v }); setSaved(false); };
  const save = async () => {
    setBusy(true); setError("");
    try { await call(`/api/studio/projects/${state.id}/stages/problem`, { data: p, kit }, "PUT"); setSaved(true); props.refresh(); }
    catch (e) { setError((e as Error).message); } finally { setBusy(false); }
  };
  const missing = problemComplete(p);
  const suggested = p.power && p.network ? suggestKit(p) : null;

  const radio = (name: keyof ProblemData, options: [string, string][]) => (
    <div className="choices" role="radiogroup">
      {options.map(([v, label]) => (
        <label key={v}>
          <input type="radio" name={name} id={`${name}-${v}`} value={v} checked={p[name] === v} disabled={locked} onChange={() => set(name, v)} />
          {label}
        </label>
      ))}
    </div>
  );

  return (
    <>
      <div className="two">
        <div className="card grid">
          <label className="field">What is the problem? <span>Who has it, when does it happen, and what does it cost them? Hindi or English is fine.</span>
            <textarea id="story" value={p.story ?? ""} disabled={locked} onChange={(e) => set("story", e.target.value)}
              placeholder="Our nursery beds dry out by afternoon. Someone has to watch them and start the pump by hand…" style={{ minHeight: 130 }} />
          </label>
          <div className="form-grid">
            <label className="field">Crop or site<input id="crop" type="text" value={p.crop ?? ""} disabled={locked} onChange={(e) => set("crop", e.target.value)} placeholder="Nursery beds, 4 beds" /></label>
            <label className="field">Area<input id="area" type="text" value={p.area ?? ""} disabled={locked} onChange={(e) => set("area", e.target.value)} placeholder="About 40 m²" /></label>
            <label className="field">Water source<input id="water" type="text" value={p.water ?? ""} disabled={locked} onChange={(e) => set("water", e.target.value)} placeholder="1000 L overhead tank" /></label>
            <label className="field">Climate notes<input id="climate" type="text" value={p.climate ?? ""} disabled={locked} onChange={(e) => set("climate", e.target.value)} placeholder="Up to 38 °C in May" /></label>
          </div>
          <div className="grid" style={{ gap: 6 }}>
            <b className="small">Power at the site</b>
            {radio("power", [["mains", "Mains power point"], ["solar", "Solar only"], ["none", "No power"]])}
          </div>
          <div className="grid" style={{ gap: 6 }}>
            <b className="small">Network at the site</b>
            {radio("network", [["wifi", "WiFi"], ["mobile", "Mobile signal only"], ["none", "No signal"]])}
          </div>
          <div className="grid" style={{ gap: 6 }}>
            <b className="small">Kit</b>
            <div className="choices" role="radiogroup">
              {(["MINI", "MEGA"] as KitKey[]).map((k) => (
                <label key={k}>
                  <input type="radio" name="kit" id={`kit-${k}`} checked={kit === k} disabled={locked || !!state.design} onChange={() => { setKit(k); setSaved(false); }} />
                  {KITS[k].name}{suggested === k ? " (suggested)" : ""}
                </label>
              ))}
            </div>
            <p className="muted small">{KITS[kit].blurb}</p>
            {state.design && <p className="muted small">The kit is fixed now that a design has been saved for it.</p>}
          </div>
          {!locked && (
            <div className="row">
              <button className="btn" disabled={busy || saved} onClick={save}>{saved ? "Saved" : "Save"}</button>
              {!saved && <span className="muted small">Unsaved changes</span>}
            </div>
          )}
          {error && <p className="error">{error}</p>}
        </div>

        <div className="grid">
          <Why title="Start with the problem, not the sensor">
            A good product begins with who has the problem, when it happens and what it costs. Those answers decide everything else, including whether the Mini kit (classroom, mains power, WiFi) or the Mega kit (battery, long range) is right.
          </Why>
          {suggested && (
            <div className={`msg ${suggested === kit ? "ok" : "warn"}`}>
              <span className="ic">{suggested === kit ? "✓" : "!"}</span>
              <span>
                {suggested === "MINI"
                  ? "Mains power and WiFi at the site: the Mini kit fits."
                  : "No mains power or no WiFi at the site: the Mega kit has the battery, solar input, LoRa and 4G for that."}
                {suggested !== kit && ` You have chosen ${KITS[kit].name}.`}
              </span>
            </div>
          )}
          <Eng title="stored facts">
            <pre className="code">{JSON.stringify({ kit, ...p }, null, 2)}</pre>
          </Eng>
        </div>
      </div>

      <GateBar {...props} stage="problem" canFinish={saved && missing.length === 0}
        finishHint={!saved ? "Save your changes first." : missing.length ? missing.join(" ") : undefined} />
    </>
  );
}
