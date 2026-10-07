"use client";
import { useEffect, useState } from "react";
import Link from "next/link";
import { useRouter } from "next/navigation";
import type { ProjectAccess } from "@/lib/studio/access";
import type { ProjectState } from "@/lib/studio/server";
import { STAGES, gateLabel, type StageKey } from "@/lib/studio/stages";
import { call } from "./api";
import ProblemStage from "./ProblemStage";
import SpecStage from "./SpecStage";
import ArchStage from "./ArchStage";

export type StageProps = { state: ProjectState; access: ProjectAccess; refresh: () => void };

const STATUS_TEXT = { NOT_STARTED: "Not started", IN_PROGRESS: "In progress", SUBMITTED: "Waiting for sign-off", DONE: "Done" } as const;

export default function ProjectClient({ state, access }: { state: ProjectState; access: ProjectAccess }) {
  const router = useRouter();
  const [stage, setStage] = useState<StageKey>(STAGES[state.current].key);

  // Remember the open stage in the address bar (#spec) so a refresh returns to it.
  useEffect(() => {
    const h = window.location.hash.slice(1);
    if (STAGES.some((s) => s.key === h)) setStage(h as StageKey);
  }, []);
  const open = (k: StageKey) => { setStage(k); history.replaceState(null, "", `#${k}`); };

  const props: StageProps = { state, access, refresh: () => router.refresh() };
  const def = STAGES.find((s) => s.key === stage)!;
  const st = state.stages[stage];

  return (
    <div className="grid">
      <div className="row between">
        <div>
          <Link href="/studio" className="btn ghost small">← All projects</Link>
          <div className="eyebrow" style={{ marginTop: 8 }}>{state.cohort.institution} · {state.cohort.name} · {state.kit === "MINI" ? "Mini" : "Mega"} kit</div>
          <h1>{state.title}</h1>
          <p className="muted small">Team: {state.members.map((m) => m.name).join(", ") || "ASC team"}</p>
        </div>
        <span className="pill now">Stage {state.current + 1} of {STAGES.length}</span>
      </div>

      <div className="proj-layout">
        <nav className="rail" aria-label="Stages">
          {STAGES.map((s, i) => {
            const status = state.stages[s.key].status;
            const cls = status === "DONE" ? "d" : status === "SUBMITTED" ? "s" : i === state.current ? "c" : "";
            return (
              <button key={s.key} className={`st ${cls}`} aria-current={s.key === stage ? "step" : undefined} onClick={() => open(s.key)}>
                <span className="n">{status === "DONE" ? "✓" : i + 1}</span>
                <b>{s.title}</b>
                <small>{s.ready ? STATUS_TEXT[status] : "Coming soon"} · {gateLabel(s.gate)}</small>
              </button>
            );
          })}
        </nav>

        <section className="stage" aria-label={def.title}>
          <div className="row between">
            <div>
              <div className="eyebrow">Stage {STAGES.indexOf(def) + 1} · {gateLabel(def.gate)}</div>
              <h2>{def.title}</h2>
            </div>
            <span className={`pill ${st.status === "DONE" ? "done" : st.status === "SUBMITTED" ? "warn" : st.status === "IN_PROGRESS" ? "now" : "wait"}`}>
              {st.status === "DONE" && st.signedOffBy ? `Signed off by ${st.signedOffBy}` : def.ready ? STATUS_TEXT[st.status] : "Coming soon"}
            </span>
          </div>

          {stage === "problem" && <ProblemStage {...props} />}
          {stage === "spec" && <SpecStage {...props} />}
          {stage === "arch" && <ArchStage {...props} />}
          {!def.ready && (
            <div className="card grid">
              <p>{def.summary}</p>
              <p className="muted small">This stage is being built next. Your work in the earlier stages is saved and carries over.</p>
            </div>
          )}
        </section>
      </div>
    </div>
  );
}

// The buttons that move a stage through its gate, shared by every stage.
export function GateBar({ state, access, refresh, stage, canFinish, finishHint }: StageProps & { stage: StageKey; canFinish: boolean; finishHint?: string }) {
  const def = STAGES.find((s) => s.key === stage)!;
  const st = state.stages[stage];
  const [error, setError] = useState("");
  const [busy, setBusy] = useState(false);
  const [comment, setComment] = useState("");
  const act = async (action: string, extra?: object) => {
    setBusy(true); setError("");
    try { await call(`/api/studio/projects/${state.id}/stages/${stage}`, { action, ...extra }); refresh(); }
    catch (e) { setError((e as Error).message); } finally { setBusy(false); }
  };
  const returned = (st.data as { returnedComment?: string } | null)?.returnedComment;

  return (
    <div className="card grid">
      {returned && st.status === "IN_PROGRESS" && (
        <div className="msg warn"><span className="ic">!</span><span><b>Returned by your mentor:</b> {returned}</span></div>
      )}
      {st.status === "SUBMITTED" && !access.canSignOff && (
        <p>Submitted. Your teacher will check it and sign it off, or send it back with a comment.</p>
      )}
      {st.status === "SUBMITTED" && access.canSignOff && (
        <div className="grid">
          <p><b>This stage is waiting for your sign-off.</b> Approve it, or send it back with a comment for the team.</p>
          <label className="field">Comment for the team <span>needed only when sending it back</span>
            <textarea id={`ret-${stage}`} value={comment} onChange={(e) => setComment(e.target.value)} style={{ minHeight: 60 }} />
          </label>
          <div className="row">
            <button className="btn" disabled={busy} onClick={() => act("approve")}>Sign off</button>
            <button className="btn ghost" disabled={busy} onClick={() => act("return", { comment })}>Send back</button>
          </div>
        </div>
      )}
      {st.status === "DONE" && (
        <div className="row between">
          <p>{st.signedOffBy ? `Signed off by ${st.signedOffBy}.` : "This stage is finished."}</p>
          {(def.gate !== "mentor" || access.canSignOff) && (
            <button className="btn ghost small" disabled={busy} onClick={() => act("reopen")}>Reopen to change it</button>
          )}
        </div>
      )}
      {(st.status === "NOT_STARTED" || st.status === "IN_PROGRESS") && (
        <div className="row between">
          <p className="muted small">{finishHint ?? (def.gate === "mentor" ? "When you're happy with it, submit it to your teacher for sign-off." : "Finish this stage to move on.")}</p>
          <button className="btn" disabled={busy || !canFinish} onClick={() => act("complete")}>
            {def.gate === "mentor" ? "Submit for sign-off" : "Finish this stage"}
          </button>
        </div>
      )}
      {error && <p className="error">{error}</p>}
    </div>
  );
}
