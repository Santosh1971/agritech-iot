"use client";
import { useEffect, useRef, useState } from "react";
import { checksFor, type TestCheck as Check } from "@/lib/studio/testplan";
import { Eng, Why } from "../EngView";
import { call } from "./api";
import { useSerialSupported, type SelfTest } from "./device";
import { useDevice } from "./BuildStage";
import { GateBar, type StageProps } from "./ProjectClient";

type Item = { key: string; label: string; ok: boolean; at: string; value?: number };
type TestData = { design?: number; device?: string; items?: Item[]; selftest?: SelfTest["results"] };

export default function TestStage(props: StageProps) {
  const { state } = props;
  const dev = useDevice();
  const serialOk = useSerialSupported();
  const design = state.design;
  const st = state.stages.test;
  const stored = (st.data ?? {}) as TestData;
  const locked = st.status === "DONE";
  const checks = design ? checksFor(design.ports) : [];
  const fresh = stored.design === design?.version;
  const [items, setItems] = useState<Record<string, Item>>(fresh ? Object.fromEntries((stored.items ?? []).map((i) => [i.key, i])) : {});
  const [self, setSelf] = useState<SelfTest["results"] | null>(fresh ? stored.selftest ?? null : null);
  const [watching, setWatching] = useState(false);
  const [confirmed, setConfirmed] = useState<Record<string, boolean>>({});
  const [switched, setSwitched] = useState<Record<string, boolean>>({});
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState("");
  const base = useRef<Record<string, number>>({});
  const seen = useRef<Record<string, Set<number>>>({});

  const persist = (next: Record<string, Item>, selftest = self) =>
    call(`/api/studio/projects/${state.id}/stages/test`, {
      data: { design: design?.version, device: dev.hello?.id, items: checks.map((c) => next[c.key]).filter(Boolean), selftest },
    }, "PUT").catch(() => {});

  const pass = (c: Check, value?: number) => {
    setItems((cur) => {
      if (cur[c.key]?.ok) return cur;
      const next = { ...cur, [c.key]: { key: c.key, label: c.title, ok: true, at: new Date().toISOString(), value } };
      persist(next);
      return next;
    });
  };

  // Watch the live readings and tick items off as their condition is met.
  const live = dev.live;
  useEffect(() => {
    if (!watching || !live) return;
    for (const c of checks) {
      if (!c.ref || !c.pass || items[c.key]?.ok) continue;
      const v = live.values[c.ref];
      if (v === null || v === undefined) continue;
      if (base.current[c.ref] === undefined) base.current[c.ref] = v;
      (seen.current[c.ref] ??= new Set()).add(v);
      if (c.pass(v, base.current[c.ref], seen.current[c.ref])) pass(c, v);
    }
  }, [live, watching]); // eslint-disable-line react-hooks/exhaustive-deps

  if (state.stages.build.status !== "DONE" || !design) {
    return <div className="card"><p>Finish the Build stage first: the Test stage needs your design on the board.</p></div>;
  }

  const onBoard = dev.hello?.design?.design;
  const start = async () => {
    setBusy(true); setError("");
    try {
      if (!dev.connected) await dev.connect();
      if (dev.hello?.design?.design !== design.version) throw new Error(`The board has design version ${dev.hello?.design?.design ?? "none"}, not ${design.version}. Send it from the Build stage.`);
      const r = await dev.selfTest();
      setSelf(r.results);
      base.current = {};
      seen.current = {};
      await dev.setLive(true);
      setWatching(true);
      persist(items, r.results);
    } catch (e) { setError((e as Error).message); }
    finally { setBusy(false); }
  };
  const toggle = async (port: string) => {
    const on = !switched[port];
    try { await dev.setOutput(port, on); setSwitched({ ...switched, [port]: on }); }
    catch (e) { setError((e as Error).message); }
  };
  const selfFor = (port: string) => self?.find((r) => r.port === port);
  const done = checks.filter((c) => items[c.key]?.ok).length;

  return (
    <>
      <div className="two">
        <div className="card grid">
          <div className="row between">
            <div className="eyebrow">Test checklist · {done} of {checks.length} passed</div>
            {!locked && (
              <button className="btn" disabled={busy || !serialOk} onClick={start}>
                {busy ? "Starting…" : watching ? "Run the self-test again" : "Connect and start"}
              </button>
            )}
          </div>
          {!serialOk && <div className="msg bad"><span className="ic">✗</span><span>Use Chrome or Edge on a laptop to reach the board over USB.</span></div>}
          {dev.connected && onBoard !== design.version && <div className="msg warn"><span className="ic">!</span><span>The board has design version {onBoard ?? "none"}. Send version {design.version} from the Build stage first.</span></div>}
          {checks.map((c) => {
            const it = items[c.key];
            const s = selfFor(c.port);
            const v = c.ref && live ? live.values[c.ref] : undefined;
            return (
              <div key={c.key} className="check">
                <span className={`box ${it?.ok ? "ok" : watching ? "run" : ""}`}>{it?.ok ? "✓" : ""}</span>
                <div className="grid" style={{ gap: 4 }}>
                  <b>{c.title}</b>
                  <p className="small">{c.todo}</p>
                  {s && !s.ok && <p className="small error">{s.detail}</p>}
                  {watching && v !== undefined && <p className="small muted">Now reading: {v === null ? "no reading" : v}</p>}
                  {c.output && watching && !it?.ok && (
                    <div className="row">
                      <button className="btn ghost small" onClick={() => toggle(c.port)}>{switched[c.port] ? `Switch ${c.port} OFF` : `Switch ${c.port} ON`}</button>
                    </div>
                  )}
                  {c.confirm && watching && !it?.ok && (
                    <label className="row small" style={{ gap: 6 }}>
                      <input type="checkbox" id={`confirm-${c.key}`} checked={!!confirmed[c.key]}
                        disabled={c.output && switched[c.port] === undefined}
                        onChange={(e) => {
                          setConfirmed({ ...confirmed, [c.key]: e.target.checked });
                          if (!e.target.checked) return;
                          pass(c);
                          // Tested: switch it off and hand it back to the rules.
                          if (c.output) dev.setOutput(c.port, false).then(() => dev.auto()).catch(() => {});
                        }} />
                      {c.confirm}
                    </label>
                  )}
                </div>
                <span className={`pill ${it?.ok ? "done" : "wait"}`}>{it?.ok ? "Passed" : watching ? "Watching" : "To do"}</span>
              </div>
            );
          })}
          {error && <p className="error">{error}</p>}
        </div>
        <div className="grid">
          <Why title="The board checks itself">
            Each part has a built-in self-test, and the board reports what it measures every second. You do the physical part (water, warmth, a lifted float) and the studio ticks the item off when the reading proves it. Each tick is a real measurement, not a guess.
          </Why>
          {watching && (
            <div className="card grid">
              <div className="eyebrow">Outputs</div>
              <p className="small">When you switch an output here, it stays under your control for 10 minutes, then goes back to your rules. Press the button below to hand control back now.</p>
              <button className="btn ghost small" onClick={() => { dev.auto().catch(() => {}); setSwitched({}); }}>Back to my rules now</button>
            </div>
          )}
          <Eng title="raw readings and self-test">
            <pre className="code">{JSON.stringify({ live: live?.values ?? null, outputs: live?.outputs ?? null, selftest: self }, null, 2)}</pre>
          </Eng>
        </div>
      </div>
      <GateBar {...props} stage="test" canFinish={!!checks.length && checks.every((c) => items[c.key]?.ok)}
        finishHint={checks.every((c) => items[c.key]?.ok) ? undefined : "Pass every check first."} />
    </>
  );
}
