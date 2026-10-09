"use client";
import { useEffect, useState } from "react";
import { checkLayout, defaultLayout, tileSources, type Alert, type AppLayout, type Tile, type TileKind } from "@/lib/studio/appLayout";
import { deviceConfig } from "@/lib/studio/deviceConfig";
import { Eng, Why } from "../EngView";
import { call } from "./api";
import { useDevice } from "./BuildStage";
import { useSerialSupported } from "./device";
import { GateBar, type StageProps } from "./ProjectClient";

const KIND_LABEL: Record<TileKind, string> = { value: "Number", graph: "Number + graph", gauge: "Gauge", switch: "On/off switch", lamp: "Status lamp" };

type AppData = { sent?: { design: number; at: string; id: string } };

export default function AppStage(props: StageProps) {
  const { state } = props;
  const dev = useDevice();
  const serialOk = useSerialSupported();
  const design = state.design;
  const st = state.stages.app;
  const data = (st.data ?? {}) as AppData;
  const locked = st.status === "DONE";
  const [layout, setLayout] = useState<AppLayout>(design?.app ?? (design ? defaultLayout(design.ports, state.title) : { name: "", lang: "en", tiles: [], alerts: [] }));
  const [dirty, setDirty] = useState(!design?.app);
  const [busy, setBusy] = useState<"" | "save" | "send">("");
  const [error, setError] = useState("");
  const [sw, setSw] = useState<Record<string, boolean>>({});
  // The version the last save created; until the page has it, sending would send the old one.
  const [savedVersion, setSavedVersion] = useState(0);
  const [apk, setApk] = useState<{ id: string; versionName: string } | null>(null);
  useEffect(() => { fetch("/api/studio/app").then((r) => r.json()).then((j) => setApk(j.app ?? null)).catch(() => {}); }, []);

  if (state.stages.test.status !== "DONE" || !design) {
    return <div className="card"><p>Finish the Test stage first. The app shows the readings you have just tested.</p></div>;
  }

  const sources = tileSources(design.ports);
  const src = (ref: string) => sources.find((s) => s.ref === ref);
  const list = checkLayout(design.ports, layout);
  const pending = savedVersion > design.version;
  const bad = list.some((c) => c.level === "bad");
  const hi = layout.lang === "hi";
  const set = (l: AppLayout) => { setLayout(l); setDirty(true); };
  const setTile = (i: number, patch: Partial<Tile>) => set({ ...layout, tiles: layout.tiles.map((t, k) => (k === i ? { ...t, ...patch } : t)) });
  const move = (i: number, d: number) => {
    const t = [...layout.tiles];
    const j = i + d;
    if (j < 0 || j >= t.length) return;
    [t[i], t[j]] = [t[j], t[i]];
    set({ ...layout, tiles: t });
  };
  const setAlert = (i: number, patch: Partial<Alert>) => set({ ...layout, alerts: layout.alerts.map((a, k) => (k === i ? { ...a, ...patch } : a)) });
  const unused = sources.filter((s) => !layout.tiles.some((t) => t.ref === s.ref));
  const live = dev.live;
  const show = (ref: string) => {
    const v = live?.values[ref] ?? live?.outputs[ref];
    return v === undefined || v === null ? "—" : String(v);
  };

  const save = async () => {
    setBusy("save"); setError("");
    try {
      const r = await call(`/api/studio/projects/${state.id}/app`, { app: layout }, "PUT");
      setSavedVersion(r.version);
      setDirty(false);
      props.refresh();
    }
    catch (e) { setError((e as Error).message); } finally { setBusy(""); }
  };
  const send = async () => {
    setBusy("send"); setError("");
    try {
      if (!dev.connected) await dev.connect();
      await dev.sendConfig(deviceConfig(state.kit, design.version, state.title, design.ports, design.rules ?? [], design.app));
      await dev.setLive(true);
      await call(`/api/studio/projects/${state.id}/stages/app`, { data: { sent: { design: design.version, at: new Date().toISOString(), id: dev.hello!.id } } }, "PUT");
      props.refresh();
    } catch (e) { setError((e as Error).message); } finally { setBusy(""); }
  };

  return (
    <>
      <div className="two">
        <div className="card grid">
          <div className="row between">
            <div className="eyebrow">Design your phone screen</div>
            <div className="seg" role="group" aria-label="Language">
              <button type="button" aria-pressed={!hi} onClick={() => set({ ...layout, lang: "en" })}>English</button>
              <button type="button" aria-pressed={hi} onClick={() => set({ ...layout, lang: "hi" })}>हिंदी</button>
            </div>
          </div>
          <label className="field">App name<input id="app-name" type="text" disabled={locked} value={layout.name} onChange={(e) => set({ ...layout, name: e.target.value })} /></label>
          {layout.tiles.map((t, i) => (
            <div key={`${t.ref}-${i}`} className="tile-edit">
              <div className="row between">
                <b className="small"><span className="mono">{t.ref}</span> {src(t.ref)?.label}</b>
                {!locked && (
                  <div className="row" style={{ gap: 4 }}>
                    <button type="button" className="btn ghost small" aria-label="Move up" onClick={() => move(i, -1)}>↑</button>
                    <button type="button" className="btn ghost small" aria-label="Move down" onClick={() => move(i, 1)}>↓</button>
                    <button type="button" className="btn ghost small" onClick={() => set({ ...layout, tiles: layout.tiles.filter((_, k) => k !== i) })}>Remove</button>
                  </div>
                )}
              </div>
              <div className="rule-row">
                <label className="field">Shown as
                  <select id={`tile-${i}-kind`} disabled={locked} value={t.kind} onChange={(e) => setTile(i, { kind: e.target.value as TileKind })}>
                    {(src(t.ref)?.output ? ["switch", "lamp"] : ["value", "graph", "gauge", "lamp"]).map((k) => <option key={k} value={k}>{KIND_LABEL[k as TileKind]}</option>)}
                  </select>
                </label>
                <label className="field">Label<input id={`tile-${i}-label`} type="text" disabled={locked} value={t.label} onChange={(e) => setTile(i, { label: e.target.value })} /></label>
                <label className="field">Label in Hindi<input id={`tile-${i}-hi`} type="text" disabled={locked} value={t.labelHi} onChange={(e) => setTile(i, { labelHi: e.target.value })} /></label>
              </div>
            </div>
          ))}
          {!locked && unused.length > 0 && (
            <div className="row">
              {unused.map((s) => (
                <button key={s.ref} type="button" className="btn ghost small"
                  onClick={() => set({ ...layout, tiles: [...layout.tiles, { ref: s.ref, kind: s.output ? "switch" : "value", label: s.label, labelHi: s.labelHi }] })}>
                  + {s.label} ({s.ref})
                </button>
              ))}
            </div>
          )}

          <div className="eyebrow" style={{ marginTop: 8 }}>Alerts on the phone</div>
          {layout.alerts.map((a, i) => (
            <div key={i} className="rule-row">
              <label className="field">When
                <select id={`alert-${i}-ref`} disabled={locked} value={a.ref} onChange={(e) => setAlert(i, { ref: e.target.value })}>
                  {sources.filter((s) => !s.output).map((s) => <option key={s.ref} value={s.ref}>{s.label} ({s.ref})</option>)}
                </select>
              </label>
              <label className="field">goes
                <select id={`alert-${i}-when`} disabled={locked} value={a.when} onChange={(e) => setAlert(i, { when: e.target.value as Alert["when"] })}>
                  <option value="below">below</option><option value="above">above</option>
                </select>
              </label>
              <label className="field">Level<input id={`alert-${i}-value`} type="number" disabled={locked} value={a.value} onChange={(e) => setAlert(i, { value: e.target.valueAsNumber })} /></label>
              <label className="field">Message<input id={`alert-${i}-say`} type="text" disabled={locked} value={a.say} onChange={(e) => setAlert(i, { say: e.target.value })} /></label>
            </div>
          ))}
          {!locked && sources.some((s) => !s.output) && (
            <div className="row">
              <button type="button" className="btn ghost small" onClick={() => set({ ...layout, alerts: [...layout.alerts, { ref: sources.find((s) => !s.output)!.ref, when: "below", value: 30, say: "Check the field" }] })}>+ Alert</button>
              {layout.alerts.length > 0 && <button type="button" className="btn ghost small" onClick={() => set({ ...layout, alerts: layout.alerts.slice(0, -1) })}>Remove last alert</button>}
            </div>
          )}
          {list.map((c, i) => <div key={i} className={`msg ${c.level}`}><span className="ic">{c.level === "ok" ? "✓" : c.level === "warn" ? "!" : "✗"}</span><span>{c.text}</span></div>)}
          {!locked && (
            <div className="row">
              <button className="btn" disabled={!!busy || !dirty || bad} onClick={save}>{dirty ? "Save layout" : pending ? "Saving…" : `Saved with design version ${design.version}`}</button>
            </div>
          )}
          {error && <p className="error">{error}</p>}
        </div>

        <div className="grid">
          <div className="phone" aria-label="Phone preview">
            <div className="phone-top">
              <b>{layout.name || "Your app"}</b>
              <small>{live ? (hi ? "अभी अपडेट" : "Live from your board") : hi ? "नमूना" : "Preview"}</small>
            </div>
            <div className="tiles">
              {layout.tiles.map((t, i) => {
                const s = src(t.ref);
                const label = hi ? t.labelHi || t.label : t.label;
                if (t.kind === "switch") {
                  const on = live ? live.outputs[t.ref] === 1 : !!sw[t.ref];
                  return (
                    <div key={i} className="tile">
                      <small>{label}</small>
                      <button type="button" className="sw" aria-pressed={on} aria-label={label}
                        onClick={() => { if (live) dev.setOutput(t.ref, !on).catch(() => {}); else setSw({ ...sw, [t.ref]: !on }); }} />
                      <small>{on ? (hi ? "चालू" : "ON") : hi ? "बंद" : "OFF"}</small>
                    </div>
                  );
                }
                if (t.kind === "lamp") {
                  const v = live ? (live.values[t.ref] ?? live.outputs[t.ref]) : 1;
                  return <div key={i} className="tile"><small>{label}</small><span className={`lamp ${v ? "on" : ""}`} /><small>{v ? (hi ? "ठीक" : "OK") : hi ? "खाली" : "Empty"}</small></div>;
                }
                return (
                  <div key={i} className={`tile ${t.kind === "graph" ? "wide" : ""}`}>
                    <small>{label}</small>
                    <span className="v">{live ? show(t.ref) : "—"}<u>{s?.unit}</u></span>
                    {t.kind === "graph" && <svg className="spark" viewBox="0 0 100 30" preserveAspectRatio="none" aria-hidden="true"><polyline points="0,20 15,18 30,22 45,14 60,16 75,9 100,12" fill="none" stroke="var(--accent)" strokeWidth="1.6" vectorEffect="non-scaling-stroke" /></svg>}
                    {t.kind === "gauge" && <div className="bar"><i style={{ width: live ? `${Math.max(0, Math.min(100, Number(show(t.ref)) || 0))}%` : "40%" }} /></div>}
                  </div>
                );
              })}
            </div>
          </div>
          <p className="small muted" style={{ textAlign: "center" }}>{live ? "Showing your board's real readings over USB. The switches work." : "Connect the board below to see real readings here."}</p>

          <div className="card grid">
            <div className="eyebrow">Put it on the board and the phone</div>
            <ol className="steps small">
              <li>Send design version {design.version} to the board. It now carries your layout.</li>
              <li>Install the <b>ASC Studio</b> app on an Android phone: {apk ? <a href={`/api/studio/app/${apk.id}`}>download version {apk.versionName}</a> : "your teacher will share it (no app has been published yet)"}.</li>
              <li>In the app, tap your board ({dev.hello?.id ?? "ASC-MINI-…"}). The app builds this screen from your board.</li>
              <li>To switch outputs from the phone, first press the board&apos;s <b>PAIR</b> button.</li>
            </ol>
            <div className="row">
              <button className="btn" disabled={!!busy || dirty || pending || !design.app || !serialOk || locked} onClick={send}>{busy === "send" ? "Sending…" : `Send design version ${design.version} over USB`}</button>
            </div>
            {dirty && <p className="small muted">Save the layout first.</p>}
            {data.sent && <p className="small">Version {data.sent.design} sent to {data.sent.id} on {new Date(data.sent.at).toLocaleString()}.</p>}
          </div>
          <Why title="One app, your screen">
            Nobody writes app code here. Your screen is a small layout file that travels with your design. The ASC Studio app reads it from your board over Bluetooth and builds the screen from it, so the same app serves every project in the class.
          </Why>
          <Eng title="layout file and Bluetooth link">
            <pre className="code">{JSON.stringify(layout, null, 2)}</pre>
            <pre className="code">{`BLE service  6E400001-B5A3-F393-E0A9-E50E24DCCA9E
  write (RX) 6E400002-…   {"cmd":"get_design"}
  notify(TX) 6E400003-…   {"type":"design","design":{…,"app":{…}}}`}</pre>
          </Eng>
        </div>
      </div>
      <GateBar {...props} stage="app" canFinish={!dirty && !!design.app && !bad && data.sent?.design === design.version}
        finishHint={dirty || !design.app ? "Save the layout first." : data.sent?.design !== design.version ? `Send design version ${design.version} to the board first.` : undefined} />
    </>
  );
}
