"use client";
// Remote bench: the tester plugs a board into this PC, this page opens its
// USB serial port in the browser (Web Serial, Chrome/Edge) and relays it
// through /api/bench/page to the developer's tool, which can read the log,
// send commands, reset the board and flash new firmware (written here by
// esptool-js, so only the file crosses the internet).
import { useCallback, useEffect, useRef, useState } from "react";
import { ESPLoader, Transport, type IEspLoaderTerminal } from "esptool-js";

type Cmd =
  | { id: string; type: "write"; data: string }
  | { id: string; type: "reset" }
  | { id: string; type: "flash"; fwId: string; name: string; offset: number; size: number };
type FlashCmd = Extract<Cmd, { type: "flash" }>;
type Flash = { id: string; state: "waiting" | "flashing" | "done" | "failed"; pct: number; msg: string };

const BAUD = 115200;
const MAX_VIEW = 800;
const sleep = (ms: number) => new Promise((r) => setTimeout(r, ms));

export default function BenchClient() {
  const [supported, setSupported] = useState(true);
  const [connected, setConnected] = useState(false);
  const [view, setView] = useState<string[]>([]);
  const [server, setServer] = useState<"ok" | "error" | "idle">("idle");
  const [pending, setPending] = useState<FlashCmd | null>(null);
  const [flash, setFlash] = useState<Flash | null>(null);
  const [chip, setChip] = useState<string | undefined>();
  const [input, setInput] = useState("");

  const port = useRef<SerialPort | null>(null);
  const reader = useRef<ReadableStreamDefaultReader<Uint8Array> | null>(null);
  const reading = useRef<Promise<void> | null>(null);
  const outbox = useRef<string[]>([]);
  const busy = useRef(false); // flashing: the port belongs to esptool-js
  const flashRef = useRef<Flash | null>(null);
  const consoleEl = useRef<HTMLDivElement>(null);
  const instance = useRef<string>("");
  const [busyMsg, setBusyMsg] = useState("");

  useEffect(() => {
    setSupported(typeof navigator !== "undefined" && "serial" in navigator);
    instance.current = crypto.randomUUID();
  }, []);

  const show = useCallback((lines: string[]) => {
    setView((v) => {
      const n = [...v, ...lines];
      return n.length > MAX_VIEW ? n.slice(n.length - MAX_VIEW) : n;
    });
  }, []);
  const log = useCallback(
    (line: string, relay = true) => {
      show([line]);
      if (relay) outbox.current.push(line);
    },
    [show],
  );

  useEffect(() => {
    const el = consoleEl.current;
    if (el) el.scrollTop = el.scrollHeight;
  }, [view]);

  const setFlashState = (f: Flash | null) => {
    flashRef.current = f;
    setFlash(f);
  };

  // ---- serial monitor -------------------------------------------------------
  const startMonitor = useCallback(async () => {
    const p = port.current;
    if (!p) return;
    await p.open({ baudRate: BAUD, bufferSize: 64 * 1024 });
    // Release DTR/RTS so the programmer's auto-reset lines don't hold the
    // board in reset or in download mode.
    await p.setSignals({ dataTerminalReady: false, requestToSend: false }).catch(() => {});
    setConnected(true);
    const r = p.readable!.getReader();
    reader.current = r;
    const dec = new TextDecoder();
    let partial = "";
    reading.current = (async () => {
      try {
        for (;;) {
          const { value, done } = await r.read();
          if (done) break;
          if (!value) continue;
          partial += dec.decode(value, { stream: true });
          const parts = partial.split("\n");
          partial = parts.pop() ?? "";
          if (partial.length > 16000) {
            parts.push(partial);
            partial = "";
          }
          const lines = parts.map((l) => l.replace(/\r$/, ""));
          if (lines.length) {
            show(lines);
            outbox.current.push(...lines);
          }
        }
      } catch {
        /* port closed or unplugged */
      } finally {
        r.releaseLock();
      }
    })();
  }, [show]);

  const stopMonitor = useCallback(async () => {
    try {
      await reader.current?.cancel();
    } catch {}
    await reading.current?.catch(() => {});
    reader.current = null;
    reading.current = null;
    try {
      await port.current?.close();
    } catch {}
    setConnected(false);
  }, []);

  async function connect() {
    try {
      const p = await navigator.serial.requestPort();
      port.current = p;
      await startMonitor();
      log("[bench] board connected", true);
    } catch (e) {
      log(`[bench] could not open the port: ${(e as Error).message}`, false);
    }
  }

  async function disconnect() {
    await stopMonitor();
    log("[bench] board disconnected", true);
  }

  async function write(data: string) {
    const p = port.current;
    if (!p?.writable || busy.current) return false;
    const w = p.writable.getWriter();
    try {
      await w.write(new TextEncoder().encode(data));
      return true;
    } finally {
      w.releaseLock();
    }
  }

  async function reset() {
    const p = port.current;
    if (!p || busy.current) return;
    await p.setSignals({ dataTerminalReady: false, requestToSend: true });
    await sleep(120);
    await p.setSignals({ requestToSend: false });
  }

  // ---- flashing -------------------------------------------------------------
  // auto: the programmer's DTR/RTS put the board in download mode (no
  // buttons); manual: the tester has pressed BOOT+RESET.
  async function runFlash(cmd: FlashCmd, auto = false) {
    const p = port.current;
    if (!p || busy.current) return;
    busy.current = true;
    setPending(null);
    setFlashState({ id: cmd.id, state: "flashing", pct: 0, msg: "starting" });
    await stopMonitor();
    const terminal: IEspLoaderTerminal = { clean() {}, writeLine: (d: string) => log(`[flash] ${d}`), write: () => {} };
    const transport = new Transport(p, false);
    try {
      log(`[flash] downloading ${cmd.name}…`);
      const res = await fetch(`/api/bench/fw/${cmd.fwId}`);
      if (!res.ok) throw new Error((await res.json().catch(() => ({}))).error || `download failed (${res.status})`);
      const data = new Uint8Array(await res.arrayBuffer());
      const loader = new ESPLoader({ transport, baudrate: 460800, romBaudrate: 115200, terminal });
      if (auto) log("[flash] putting the board in download mode via the programmer (auto-reset)…");
      const c = await loader.main(auto ? "default_reset" : "no_reset");
      setChip(c);
      log(`[flash] connected to ${c}`);
      await loader.writeFlash({
        fileArray: [{ data, address: cmd.offset }],
        flashMode: "keep",
        flashFreq: "keep",
        flashSize: "keep",
        eraseAll: false,
        compress: true,
        reportProgress: (_i, written, total) =>
          setFlashState({ id: cmd.id, state: "flashing", pct: Math.round((written / total) * 100), msg: "writing" }),
      });
      // Release DTR (BOOT) before the reset pulse, or the board restarts back
      // into download mode and needs a manual RESET.
      await p.setSignals({ dataTerminalReady: false, requestToSend: true });
      await sleep(120);
      await p.setSignals({ requestToSend: false });
      setFlashState({ id: cmd.id, state: "done", pct: 100, msg: `${cmd.name} written` });
      log(`[flash] done: ${cmd.name} written at 0x${cmd.offset.toString(16)}, board restarted`);
    } catch (e) {
      const msg = (e as Error).message || String(e);
      setFlashState({ id: cmd.id, state: "failed", pct: 0, msg });
      log(`[flash] FAILED: ${msg}`);
      setPending(cmd); // let the tester try again with the buttons
    } finally {
      await transport.disconnect().catch(() => {});
      busy.current = false;
      try {
        await startMonitor();
      } catch (e) {
        log(`[bench] could not reopen the port: ${(e as Error).message}`);
      }
    }
  }

  const runFlashRef = useRef(runFlash);
  runFlashRef.current = runFlash;

  // ---- relay to the server --------------------------------------------------
  const handle = useCallback(
    async (cmds: Cmd[]) => {
      for (const c of cmds) {
        if (c.type === "write") {
          show([`> ${c.data.trimEnd()}`]);
          if (!(await write(c.data))) log("[bench] could not send: board not connected or flashing");
        } else if (c.type === "reset") {
          log("[bench] remote reset");
          await reset();
        } else if (c.type === "flash") {
          log(`[bench] new firmware to flash: ${c.name} (${c.size} bytes)`);
          await runFlashRef.current(c, true);
        }
      }
    },
    // eslint-disable-next-line react-hooks/exhaustive-deps
    [log, show],
  );

  useEffect(() => {
    let stop = false;
    let inFlight = false;
    const tick = async () => {
      if (stop || inFlight) return;
      inFlight = true;
      const lines = outbox.current.splice(0, 2000);
      try {
        const res = await fetch("/api/bench/page", {
          method: "POST",
          headers: { "content-type": "application/json" },
          body: JSON.stringify({
            instance: instance.current,
            lines,
            status: { connected: !!port.current && (connected || busy.current), chip, flash: flashRef.current ?? undefined },
          }),
        });
        if (res.status === 409) {
          setBusyMsg((await res.json()).error);
          outbox.current.unshift(...lines);
          setServer("error");
          return;
        }
        if (!res.ok) throw new Error(String(res.status));
        setBusyMsg("");
        setServer("ok");
        const { cmds } = (await res.json()) as { cmds: Cmd[] };
        if (cmds?.length) await handle(cmds);
      } catch {
        outbox.current.unshift(...lines); // try again next time
        setServer("error");
      } finally {
        inFlight = false;
      }
    };
    const t = setInterval(tick, 600);
    return () => {
      stop = true;
      clearInterval(t);
    };
  }, [connected, chip, handle]);

  async function sendInput() {
    const t = input.trim();
    if (!t) return;
    show([`> ${t}`]);
    outbox.current.push(`[bench] tester> ${t}`);
    await write(t + "\n");
    setInput("");
  }

  if (!supported) {
    return (
      <div className="card">
        <h1>Remote bench</h1>
        <p className="error">This browser can't open USB serial ports. Use Chrome or Edge on a Windows, Mac or Linux PC.</p>
      </div>
    );
  }

  return (
    <div className="card">
      <div className="between">
        <div>
          <div className="eyebrow">Remote bench</div>
          <h1>Board on this PC</h1>
        </div>
        <div className="row">
          <span className={`pill ${connected ? "good" : ""}`}>{connected ? "Board connected" : busy.current ? "Flashing" : "No board"}</span>
          <span className={`pill ${server === "ok" ? "good" : server === "error" ? "bad" : ""}`}>
            {server === "ok" ? "Linked to server" : server === "error" ? "Server not reachable" : "Linking…"}
          </span>
        </div>
      </div>

      <p className="muted">
        Plug the board's programmer into this PC, click <b>Connect board</b> and pick its USB port (CP210x / CH340). Keep this tab open:
        Santosh can then see the board's log, send it commands and flash new firmware.
      </p>
      <p className="note">
        Powered from the programmer&apos;s 5V: the battery reading is not valid and the valves can&apos;t switch (they need 12 V on J6).
      </p>

      {busyMsg && <p className="error">{busyMsg}</p>}
      {pending && (
        <div className="flashbox">
          <b>New firmware to flash: {pending.name}</b>
          <p className="small">Automatic reset did not work. Put the board in download mode by hand:</p>
          <ol>
            <li>Hold the <b>BOOT</b> button on the board.</li>
            <li>Press and release <b>RESET</b>.</li>
            <li>Release <b>BOOT</b>.</li>
            <li>Click <b>Start flash</b> within a few seconds.</li>
            <li>After the flash, press <b>RESET</b> once if the log stays quiet.</li>
          </ol>
          <button className="btn" onClick={() => runFlash(pending)} disabled={!port.current || busy.current}>
            Start flash
          </button>
          {flash?.state === "failed" && <p className="error">Last try failed: {flash.msg}. Do the button steps again and retry.</p>}
        </div>
      )}
      {flash && flash.state !== "waiting" && (
        <div>
          <span className="small">
            Flash: {flash.state === "flashing" ? `${flash.pct}%` : flash.state === "done" ? "done" : "failed"} {flash.state === "done" ? `(${flash.msg})` : ""}
          </span>
          {flash.state === "flashing" && (
            <div className="bar">
              <i style={{ width: `${flash.pct}%` }} />
            </div>
          )}
        </div>
      )}

      <div className="row" style={{ margin: "10px 0" }}>
        {!connected ? (
          <button className="btn" onClick={connect} disabled={busy.current}>
            Connect board
          </button>
        ) : (
          <button className="btn ghost" onClick={disconnect}>
            Disconnect
          </button>
        )}
        <button className="btn ghost" onClick={reset} disabled={!connected}>
          Reset board
        </button>
        <button className="btn ghost" onClick={() => setView([])}>
          Clear screen
        </button>
      </div>

      <div className="console" ref={consoleEl}>
        {view.map((l, i) => (
          <div key={i} className={l.startsWith("[bench]") || l.startsWith("[flash]") || l.startsWith(">") ? "b" : undefined}>
            {l || " "}
          </div>
        ))}
      </div>
      <div className="sendrow">
        <input
          value={input}
          onChange={(e) => setInput(e.target.value)}
          onKeyDown={(e) => e.key === "Enter" && sendInput()}
          placeholder='Command to the board, e.g. {"cmd":"get_state"}'
          disabled={!connected}
        />
        <button className="btn ghost" onClick={sendInput} disabled={!connected}>
          Send
        </button>
      </div>
    </div>
  );
}
