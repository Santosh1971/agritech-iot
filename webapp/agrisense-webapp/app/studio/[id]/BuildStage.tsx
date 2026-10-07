"use client";
import { useEffect, useState } from "react";
import { BLOCK_BY_ID } from "@/lib/studio/blocks";
import { checkRules } from "@/lib/studio/automation";
import { deviceConfig } from "@/lib/studio/deviceConfig";
import { KITS } from "@/lib/studio/kits";
import { Eng, Why } from "../EngView";
import { call } from "./api";
import { device, useSerialSupported } from "./device";
import type { FirmwareBuild } from "./flash";
import { GateBar, type StageProps } from "./ProjectClient";
import RulesEditor from "./RulesEditor";

// Re-render when the shared board connection changes.
export function useDevice() {
  const [, set] = useState(0);
  useEffect(() => device.onState(() => set((n) => n + 1)), []);
  return device;
}

type BuildData = { flashed?: { fw: string; at: string; id?: string }; sent?: { design: number; at: string; id: string; fw: string } };

export default function BuildStage(props: StageProps) {
  const { state } = props;
  const dev = useDevice();
  const serialOk = useSerialSupported();
  const design = state.design;
  const st = state.stages.build;
  const data = (st.data ?? {}) as BuildData;
  const locked = st.status === "DONE";
  const [fw, setFw] = useState<FirmwareBuild | null | undefined>(undefined);
  const [progress, setProgress] = useState(0);
  const [log, setLog] = useState<string[]>([]);
  const [busy, setBusy] = useState<"" | "flash" | "connect" | "send">("");
  const [error, setError] = useState("");

  useEffect(() => {
    fetch("/api/studio/firmware").then((r) => r.json()).then((j) => setFw(j.build ?? null)).catch(() => setFw(null));
  }, []);

  if (state.stages.arch.status !== "DONE" || !design) {
    return <div className="card"><p>Finish the Architecture stage first.</p></div>;
  }

  const used = Object.entries(design.ports).filter(([, b]) => b) as [string, string][];
  const rulesOk = !!design.rules && !checkRules(design.ports, design.rules).some((c) => c.level === "bad");
  const save = (patch: BuildData) => call(`/api/studio/projects/${state.id}/stages/build`, { data: patch }, "PUT");
  const addLog = (l: string) => setLog((x) => [...x.slice(-30), l]);
  const kitName = state.kit === "MEGA" ? "mega" : "mini";
  const boardMismatch = dev.hello && dev.hello.board !== kitName;
  const onBoard = dev.hello?.design?.design;

  const flash = async () => {
    if (!fw) return;
    setBusy("flash"); setError(""); setLog([]); setProgress(0);
    try {
      const port = (await dev.release()) ?? (await dev.pickPort());
      // esptool-js is only needed here, so it loads on first use.
      const { flashFirmware } = await import("./flash");
      await flashFirmware(port, fw, setProgress, addLog);
      await save({ flashed: { fw: fw.version, at: new Date().toISOString() } });
      addLog("Done. Now connect to send your design.");
      props.refresh();
    } catch (e) {
      setError((e as Error).message);
    } finally { setBusy(""); }
  };

  const connect = async () => {
    setBusy("connect"); setError("");
    try { await dev.connect(); }
    catch (e) { setError((e as Error).message); }
    finally { setBusy(""); }
  };

  const send = async () => {
    setBusy("send"); setError("");
    try {
      const cfg = deviceConfig(state.kit, design.version, state.title, design.ports, design.rules ?? [], design.app);
      await dev.sendConfig(cfg);
      await save({ sent: { design: design.version, at: new Date().toISOString(), id: dev.hello!.id, fw: dev.hello!.fw } });
      props.refresh();
    } catch (e) { setError((e as Error).message); }
    finally { setBusy(""); }
  };

  return (
    <>
      <div className="two">
        <div className="card grid">
          <div className="eyebrow">1 · Plug in your modules</div>
          <div className="tbl-wrap"><table><tbody>
            {used.map(([p, b]) => <tr key={p}><td className="mono">{p}</td><td>{BLOCK_BY_ID[b]?.name ?? b}</td></tr>)}
          </tbody></table></div>
          <p className="small muted">The connectors only fit one way round. Match the port name printed on the board. Unplug the USB cable while you plug modules in.</p>
          <Eng title="DevKit stand-in wiring">
            <p className="small">Until the {KITS[state.kit].name} board arrives, wire an ESP32-S3-DevKitC-1-<b>N8</b> (no PSRAM) to the same GPIOs:</p>
            <div className="tbl-wrap"><table><tbody>
              {used.map(([p]) => <tr key={p}><td className="mono">{p}</td><td className="mono">{KITS[state.kit].ports.find((x) => x.id === p)?.pins}</td></tr>)}
            </tbody></table></div>
            <p className="small">Use the DevKit&apos;s <b>USB</b> connector, not the UART one. On N8R8 and N16R8 DevKits, GPIO35–37 belong to the PSRAM, so OUT1 and OUT2 won&apos;t work there.</p>
          </Eng>
        </div>
        <Why title="One firmware, your design as data">
          Every kit runs the same firmware. Your design is a small settings file sent to the board, so changing a threshold takes seconds and needs no programming. The board checks the file itself and refuses anything that doesn&apos;t fit it.
        </Why>
      </div>

      <div className="eyebrow">2 · Set your rules</div>
      <RulesEditor {...props} locked={locked} />

      <div className="two">
        <div className="card grid">
          <div className="eyebrow">3 · Put the studio firmware on the board</div>
          {!serialOk && <div className="msg bad"><span className="ic">✗</span><span>This browser can&apos;t reach USB devices. Use Chrome or Edge on a laptop or desktop.</span></div>}
          {fw === undefined && <p className="muted small">Checking for firmware…</p>}
          {fw === null && (
            <div className="msg warn"><span className="ic">!</span><span>No firmware has been published to the studio yet. An admin can upload one on the Flasher page (product ASC_KIT). Or flash it with PlatformIO, then skip to step 4.</span></div>
          )}
          {fw && (
            <>
              <p>Firmware <b>{fw.version}</b>{data.flashed ? `, flashed ${new Date(data.flashed.at).toLocaleString()}` : ""}. You only need this once per board. After that, step 4 is enough.</p>
              <div className="prog" aria-label="Flashing progress"><i style={{ width: `${progress}%` }} /></div>
              <div className="row">
                <button className="btn" disabled={!!busy || locked || !serialOk} onClick={flash}>{busy === "flash" ? `Flashing… ${progress}%` : "Connect and flash"}</button>
                <span className="small muted">Pick the board&apos;s USB port when the browser asks.</span>
              </div>
            </>
          )}
          {log.length > 0 && <pre className="code" style={{ maxHeight: 160 }}>{log.join("\n")}</pre>}
        </div>

        <div className="card grid">
          <div className="eyebrow">4 · Send your design to the board</div>
          {!dev.connected ? (
            <div className="row">
              <button className="btn" disabled={!!busy || !serialOk} onClick={connect}>{busy === "connect" ? "Connecting…" : "Connect to the board"}</button>
            </div>
          ) : (
            <>
              <div className="tbl-wrap"><table><tbody>
                <tr><th>Board</th><td>{dev.hello?.id}{dev.hello?.standIn ? " (DevKit stand-in)" : ""}</td></tr>
                <tr><th>Firmware</th><td>{dev.hello?.fw}</td></tr>
                <tr><th>Design on the board</th><td>{onBoard ? `version ${onBoard}` : "none yet"}{onBoard === design.version ? " ✓ latest" : ""}</td></tr>
              </tbody></table></div>
              {boardMismatch && <div className="msg bad"><span className="ic">✗</span><span>This is a {dev.hello!.board} board, but the project uses the {KITS[state.kit].name} kit.</span></div>}
              <div className="row">
                <button className="btn" disabled={!!busy || locked || !rulesOk || !!boardMismatch} onClick={send}>{busy === "send" ? "Sending…" : `Send design version ${design.version}`}</button>
                <button className="btn ghost small" onClick={() => dev.disconnect()}>Disconnect</button>
              </div>
              {!rulesOk && <p className="small muted">Save rules with no red checks first.</p>}
            </>
          )}
          {data.sent && <p className="small">Design version {data.sent.design} was sent to {data.sent.id} on {new Date(data.sent.at).toLocaleString()}.</p>}
          <Eng title="the design file the board receives">
            <pre className="code">{JSON.stringify(deviceConfig(state.kit, design.version, state.title, design.ports, design.rules ?? [], design.app), null, 2)}</pre>
          </Eng>
        </div>
      </div>
      {error && <div className="msg bad"><span className="ic">✗</span><span>{error}</span></div>}
      <GateBar {...props} stage="build" canFinish={rulesOk && data.sent?.design === design.version}
        finishHint={!rulesOk ? "Save your rules first." : data.sent?.design !== design.version ? `Send design version ${design.version} to the board first.` : undefined} />
    </>
  );
}
