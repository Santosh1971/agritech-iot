"use client";
import { useMemo, useState } from "react";
import { BLOCKS, BLOCK_BY_ID } from "@/lib/studio/blocks";
import { KITS } from "@/lib/studio/kits";
import { canBuild, checks, cleanPorts, power, whyNot, type Ports } from "@/lib/studio/rules";
import type { SpecData } from "@/lib/studio/problem";
import { Eng, Why } from "../EngView";
import { call } from "./api";
import { GateBar, type StageProps } from "./ProjectClient";

export default function ArchStage(props: StageProps) {
  const { state } = props;
  const kit = state.kit;
  const st = state.stages.arch;
  const locked = st.status === "DONE" || st.status === "SUBMITTED";
  const specDone = state.stages.spec.status === "DONE";
  const suggested = (state.stages.spec.data as SpecData | null)?.suggested;

  // Start from the saved design, or else from the spec's suggestion.
  const initial = useMemo(() => cleanPorts(kit, state.design?.ports ?? suggested ?? {}), [kit, state.design?.version]); // eslint-disable-line react-hooks/exhaustive-deps
  const [ports, setPorts] = useState<Ports>(initial);
  const [sel, setSel] = useState(KITS[kit].ports[0].id);
  const [dirty, setDirty] = useState(!state.design && Object.values(initial).some(Boolean));
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState("");

  if (!specDone) {
    return <div className="card"><p>The Architecture stage opens once your teacher has signed off the specification.</p></div>;
  }

  const port = KITS[kit].ports.find((p) => p.id === sel)!;
  const list = checks(kit, ports);
  const pw = power(ports);
  const assign = (blockId: string | null) => {
    if (locked) return;
    setPorts({ ...ports, [sel]: blockId });
    setDirty(true);
  };
  // The same as the Reopen button at the bottom of the page, where it is easy to miss.
  const reopen = async () => {
    setBusy(true); setError("");
    try { await call(`/api/studio/projects/${state.id}/stages/arch`, { action: "reopen" }, "POST"); props.refresh(); }
    catch (e) { setError((e as Error).message); } finally { setBusy(false); }
  };
  const save = async () => {
    setBusy(true); setError("");
    try { await call(`/api/studio/projects/${state.id}/design`, { ports }, "PUT"); setDirty(false); props.refresh(); }
    catch (e) { setError((e as Error).message); } finally { setBusy(false); }
  };

  // Only show blocks of kinds this kit has, plus Mega-only ones greyed out with the reason.
  const palette = BLOCKS.map((b) => ({ b, why: whyNot(kit, b.id, port.id) }))
    .sort((x, y) => Number(!!x.why) - Number(!!y.why));

  return (
    <>
      <div className="board-wrap">
        <div className="grid">
          <div className="board">
            <div className="row between"><span className="silk">ASC {KITS[kit].name.toUpperCase()} · REV A</span><span className="silk">LOW VOLTAGE ONLY</span></div>
            <div className="mcu">ESP32-S3-MINI-1-N8 · WiFi + BLE · RTC</div>
            <div className="ports">
              {KITS[kit].ports.map((p) => {
                const b = ports[p.id] ? BLOCK_BY_ID[ports[p.id]!] : null;
                return (
                  <button key={p.id} className={`port ${sel === p.id ? "sel" : ""} ${b ? "" : "empty"}`} onClick={() => setSel(p.id)} aria-pressed={sel === p.id}>
                    <b>{p.id}</b><span>{b ? b.name : locked ? "Empty" : "Empty: tap to add"}</span>
                  </button>
                );
              })}
            </div>
            <p className="note">Tap a port, then pick a part on the right. Parts that can&apos;t go on that port are greyed out with the reason.</p>
          </div>

          <div className="card grid">
            <div className="eyebrow">Checks</div>
            {list.map((c, i) => (
              <div key={i} className={`msg ${c.level}`}><span className="ic">{c.level === "ok" ? "✓" : c.level === "warn" ? "!" : "✗"}</span><span>{c.text}</span></div>
            ))}
          </div>

          <div className="card grid">
            <div className="eyebrow">Power budget (peak)</div>
            <div className="meter">
              <div className="lbl"><span>3.3 V rail</span><span>{pw.p3} mA of 1000</span></div>
              <div className="bar"><i style={{ width: `${Math.min(100, pw.p3 / 10)}%` }} /></div>
            </div>
            <div className="meter">
              <div className="lbl"><span>5 V input (including the 3.3 V regulator)</span><span>{pw.total5} mA</span></div>
              <div className="bar"><i className={pw.total5 > 500 ? "hot" : ""} style={{ width: `${Math.min(100, pw.total5 / 10)}%` }} /></div>
            </div>
            <p className="muted small">A laptop USB port gives 500 mA. The 12 V adapter gives far more.</p>
          </div>
        </div>

        <div className="grid">
          <div className="card grid">
            <div className="row between">
              <div className="eyebrow">Parts for {port.id}</div>
              {!locked && ports[sel] && <button className="btn ghost small" onClick={() => assign(null)}>Remove from {port.id}</button>}
            </div>
            {st.status === "DONE" && (
              <div className="msg warn"><span className="ic">!</span><span>
                This stage is finished, so its parts are locked.{" "}
                <button className="btn ghost small" disabled={busy} onClick={reopen}>Reopen to change parts</button>
              </span></div>
            )}
            <div className="palette">
              {palette.map(({ b, why }) => (
                <button key={b.id} className="blk" disabled={!!why || locked} onClick={() => assign(b.id)}>
                  <b>{b.name}</b>
                  <span className={`pill ${ports[sel] === b.id ? "done" : "wait"}`}>{ports[sel] === b.id ? `On ${port.id}` : b.signal}</span>
                  <small>{why || b.why}</small>
                </button>
              ))}
            </div>
          </div>
          {!locked && (
            <div className="row">
              <button className="btn" disabled={busy || !dirty} onClick={save}>{dirty ? "Save design" : `Saved (version ${state.design?.version ?? 0})`}</button>
              {dirty && !state.design && suggested && <span className="muted small">Filled in from your spec. Change anything, then save.</span>}
            </div>
          )}
          {error && <p className="error">{error}</p>}
          <Why title="Ports, not pins">
            Every sensor port on the kit works the same way, so you only need to know the port&apos;s name. The rules know which pin is behind each port, and they stop you from putting a part where it can&apos;t work.
          </Why>
          <Eng title="pin map and design file">
            <div className="tbl-wrap"><table><thead><tr><th>Port</th><th>Pin</th><th>Part</th><th>Supply</th></tr></thead><tbody>
              {KITS[kit].ports.map((p) => {
                const b = ports[p.id] ? BLOCK_BY_ID[ports[p.id]!] : null;
                return <tr key={p.id}><td className="mono">{p.id}</td><td className="mono">{p.pins}</td><td>{b?.name ?? "—"}</td><td>{b?.supply ?? ""}</td></tr>;
              })}
            </tbody></table></div>
            <pre className="code">{JSON.stringify({ kit: kit.toLowerCase(), version: state.design?.version ?? null, ports: Object.fromEntries(Object.entries(ports).filter(([, b]) => b)) }, null, 2)}</pre>
          </Eng>
        </div>
      </div>
      <GateBar {...props} stage="arch" canFinish={!dirty && !!state.design && canBuild(kit, state.design.ports)}
        finishHint={dirty ? "Save the design first." : !state.design ? "Put at least one part on a port and save." : !canBuild(kit, state.design.ports) ? "Fix the red checks first." : undefined} />
    </>
  );
}
