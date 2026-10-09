"use client";
import { useEffect, useState } from "react";
import { BLOCK_BY_ID } from "@/lib/studio/blocks";
import type { Requirement, SpecData } from "@/lib/studio/problem";
import { Eng, Why, useEng } from "../EngView";
import { call } from "./api";
import { GateBar, type StageProps } from "./ProjectClient";

export default function SpecStage(props: StageProps) {
  const { state } = props;
  const st = state.stages.spec;
  const spec = st.data as (SpecData & { drafts?: { n: number } }) | null;
  const editable = st.status === "NOT_STARTED" || st.status === "IN_PROGRESS";
  const eng = useEng();
  const [f, setF] = useState({ what: "", madeOf: "", use: "" });
  const [reqs, setReqs] = useState<Requirement[]>([]);
  const [dirty, setDirty] = useState(false);
  const [busy, setBusy] = useState<"" | "draft" | "save">("");
  const [error, setError] = useState("");

  useEffect(() => {
    setF({ what: spec?.what ?? "", madeOf: spec?.madeOf ?? "", use: spec?.use ?? "" });
    setReqs(spec?.requirements ?? []);
    setDirty(false);
  }, [spec?.generatedAt, st.status]); // eslint-disable-line react-hooks/exhaustive-deps

  const problemDone = state.stages.problem.status === "DONE";
  const draft = async () => {
    setBusy("draft"); setError("");
    try { await call(`/api/studio/projects/${state.id}/spec`); props.refresh(); }
    catch (e) { setError((e as Error).message); } finally { setBusy(""); }
  };
  const save = async () => {
    setBusy("save"); setError("");
    try { await call(`/api/studio/projects/${state.id}/stages/spec`, { data: { ...f, requirements: reqs } }, "PUT"); setDirty(false); props.refresh(); }
    catch (e) { setError((e as Error).message); } finally { setBusy(""); }
  };

  if (!problemDone) {
    return <div className="card"><p>Finish the Problem stage first. The specification is drafted from it.</p></div>;
  }

  const para = (k: keyof typeof f, label: string) => (
    <div className="grid" style={{ gap: 4 }}>
      <b>{label}</b>
      {editable
        ? <textarea id={`spec-${k}`} value={f[k]} onChange={(e) => { setF({ ...f, [k]: e.target.value }); setDirty(true); }} style={{ minHeight: 70 }} />
        : <p>{f[k]}</p>}
    </div>
  );
  // Once Architecture has a design, show what is really on the board.
  const onBoard = state.design ? Object.entries(state.design.ports).filter(([, b]) => b) as [string, string][] : [];
  const suggested = onBoard.length ? onBoard : spec ? Object.entries(spec.suggested ?? {}).filter(([, b]) => b) as [string, string][] : [];
  const partsDiffer = onBoard.length > 0 && !!spec && JSON.stringify(Object.fromEntries(onBoard)) !== JSON.stringify(Object.fromEntries(Object.entries(spec.suggested ?? {}).filter(([, b]) => b)));

  return (
    <>
      <div className="two">
        <div className="card grid">
          <div className="row between">
            <div className="eyebrow">Your one-page specification</div>
            {editable && (
              <button className="btn ghost small" disabled={!!busy} onClick={draft}>
                {busy === "draft" ? "Drafting… this can take a minute" : spec ? "Draft again" : "Draft my specification"}
              </button>
            )}
          </div>
          {!spec && <p className="muted">Press <b>Draft my specification</b>. The studio writes a first draft from your problem. Then you edit it in your own words.</p>}
          {spec && (
            <>
              {spec.note && <div className="msg warn"><span className="ic">!</span><span>{spec.note}</span></div>}
              {partsDiffer && editable && <div className="msg warn"><span className="ic">!</span><span>This draft was written for different parts than your board has now. Press <b>Draft again</b> to describe the parts on your board, or edit the text by hand.</span></div>}
              {para("what", "What it does")}
              {para("madeOf", "What it is made of")}
              {para("use", "How you use it")}
              <div className="grid" style={{ gap: 4 }}><b>Safety</b><p>{spec.safety}</p></div>
              {editable && (
                <div className="row">
                  <button className="btn" disabled={!dirty || !!busy} onClick={save}>{dirty ? "Save changes" : "Saved"}</button>
                </div>
              )}
            </>
          )}
          {error && <p className="error">{error}</p>}
        </div>

        <div className="grid">
          <Why title="Requirements have numbers">
            Each line of the spec gets a number such as SW-01. In the Test stage every check points back to one of these numbers, so you can show the device does what you promised.
          </Why>
          {spec && suggested.length > 0 && (
            <div className="card grid">
              <div className="eyebrow">{onBoard.length ? `Parts on your board (design version ${state.design!.version})` : "Suggested parts"}</div>
              <p className="small muted">{onBoard.length
                ? "These come from the Architecture stage. Change them there."
                : "These go onto the board in the Architecture stage, where you can change them. The rules have already checked that each one fits its port."}</p>
              <div className="tbl-wrap"><table><tbody>
                {suggested.map(([port, b]) => <tr key={port}><td className="mono">{port}</td><td>{BLOCK_BY_ID[b]?.name ?? b}</td></tr>)}
              </tbody></table></div>
            </div>
          )}
          {spec && (
            <Eng title="numbered requirements">
              <div className="tbl-wrap"><table className="req"><tbody>
                {reqs.map((r, i) => (
                  <tr key={i}>
                    <td>{r.id}</td>
                    <td>
                      {editable && eng
                        ? <input type="text" id={`req-${i}`} value={r.text} onChange={(e) => { const n = [...reqs]; n[i] = { ...r, text: e.target.value }; setReqs(n); setDirty(true); }} />
                        : r.text}
                    </td>
                  </tr>
                ))}
              </tbody></table></div>
              <p className="small muted">Drafted by {spec.source === "claude" ? "Claude" : "the template"} · {new Date(spec.generatedAt).toLocaleString()}{spec.drafts ? ` · draft ${spec.drafts.n} today` : ""}</p>
            </Eng>
          )}
          {spec && !eng && <p className="muted small">Turn on <b>Engineer&apos;s view</b> to see and edit the numbered requirements behind this page.</p>}
        </div>
      </div>
      <GateBar {...props} stage="spec" canFinish={!!spec && !dirty} finishHint={dirty ? "Save your changes first." : !spec ? "Draft the specification first." : undefined} />
    </>
  );
}
