"use client";
import { useEffect, useRef, useState } from "react";
import type { Build } from "./usbFlash";
import { chipForVariant } from "./products";

type Product = { id: string; name: string };
type Info = { fw?: string; device?: string; ssid?: string; pass?: string };

// What the boot log says about the node (WM2 firmware lines:
// "=== WM2 WaterMaster fw X ===", "[cfg] device ID, ... SoftAP SSID / PASS",
// "[net] SoftAP SSID up, password PASS, ...").
function parseBoot(text: string): Info {
  const i: Info = {};
  const fw = text.match(/=== .* fw (\S+) ===/);
  if (fw) i.fw = fw[1];
  const cfg = text.match(/\[cfg\] device (\S+?),.*SoftAP (\S+) \/ (\S+)/);
  if (cfg) [i.device, i.ssid, i.pass] = [cfg[1], cfg[2], cfg[3]];
  const ap = text.match(/SoftAP (\S+) up, password ([^,\s]+)/);
  if (ap) [i.ssid, i.pass] = [ap[1], ap[2]];
  return i;
}

async function pickPort(): Promise<SerialPort> {
  const known = await navigator.serial.getPorts();
  if (known.length === 1) return known[0];
  return navigator.serial.requestPort();
}

const box: React.CSSProperties = { border: "1px solid #ddd", borderRadius: 8, padding: 16, margin: "16px 0" };
const pre: React.CSSProperties = {
  background: "#111", color: "#ddd", padding: 10, borderRadius: 6, fontSize: 12, maxHeight: 260, overflow: "auto", whiteSpace: "pre-wrap",
};

export default function FlashClient({ products, admin }: { products: Product[]; admin: boolean }) {
  const [serialOk, setSerialOk] = useState(true);
  const [product, setProductState] = useState(products[0].id);
  const setProduct = (p: string) => {
    setProductState(p);
    try { localStorage.setItem("flash.product", p); } catch {}
  };
  const [builds, setBuilds] = useState<Build[] | null>(null);
  const [buildId, setBuildId] = useState("");
  const [full, setFull] = useState(admin);
  const [eraseAll, setEraseAll] = useState(false);
  const [busy, setBusy] = useState<"" | "flash" | "log">("");
  const [progress, setProgress] = useState(0);
  const [log, setLog] = useState<string[]>([]);
  const [serial, setSerial] = useState("");
  const [error, setError] = useState("");
  const [done, setDone] = useState<{ version: string; deviceId: string | null } | null>(null);
  const stop = useRef({ stopped: false });

  useEffect(() => {
    setSerialOk(typeof navigator !== "undefined" && "serial" in navigator);
    // Back to the product picked last time on this laptop.
    try {
      const last = localStorage.getItem("flash.product");
      if (last && products.some((p) => p.id === last)) setProductState(last);
    } catch {}
  }, [products]);

  useEffect(() => {
    let cancelled = false;
    setBuilds(null);
    setError("");
    setDone(null);
    fetch(`/api/flasher/builds?product=${product}`, { cache: "no-store" })
      .then(async (r) => {
        const j = await r.json();
        if (!r.ok) throw new Error(j.error ?? "Couldn't load the builds.");
        if (cancelled) return;
        const list: Build[] = j.builds;
        setBuilds(list);
        // Default: the newest real release, not a CI dev build.
        setBuildId((list.find((b) => !b.version.startsWith("dev-")) ?? list[0])?.id ?? "");
      })
      .catch((e) => {
        if (cancelled) return;
        setBuilds([]);
        setError((e as Error).message);
      });
    return () => { cancelled = true; };
  }, [product]);

  const build = builds?.find((b) => b.id === buildId);
  const canFull = admin && !!build?.hasFullFlash;
  const doFull = full && canFull;
  const info = parseBoot(serial);
  const addLog = (l: string) => setLog((x) => [...x.slice(-40), l]);

  const showLog = async (port: SerialPort, seconds: number) => {
    const { bootLog } = await import("./usbFlash");
    setBusy("log");
    setSerial("");
    stop.current = { stopped: false };
    try {
      await bootLog(port, (t) => setSerial((s) => (s + t).slice(-20000)), seconds, stop.current);
    } finally { setBusy(""); }
  };

  const flash = async () => {
    if (!build) return;
    if (eraseAll && !confirm("Erase everything wipes this board's saved WiFi, settings and data. Continue?")) return;
    setBusy("flash"); setError(""); setLog([]); setSerial(""); setProgress(0); setDone(null);
    let port: SerialPort;
    try {
      port = await pickPort();
    } catch {
      setBusy("");
      return; // picker closed
    }
    const { flashOverUsb, report } = await import("./usbFlash");
    try {
      const r = await flashOverUsb(port, {
        product, build, full: doFull, eraseAll: doFull && eraseAll, onProgress: setProgress, onLog: addLog,
      });
      await report(build.id, "flash_ok", r.mac);
      setDone({ version: build.version, deviceId: r.deviceId });
      addLog("Done. Reading the boot log…");
      await showLog(port, 25);
    } catch (e) {
      const err = e as Error & { mac?: string | null };
      setError(err.message);
      if (err.mac) await report(build.id, "flash_failed", err.mac, err.message);
      setBusy("");
    }
  };

  const restart = async () => {
    setError("");
    try {
      await showLog(await pickPort(), 30);
    } catch (e) {
      if ((e as Error).name !== "NotFoundError") setError((e as Error).message);
      setBusy("");
    }
  };

  return (
    <>
      {!serialOk && (
        <p style={{ color: "#b00020" }}>
          This browser can&apos;t reach USB serial ports. Use Chrome or Edge on a Windows, Mac or Linux laptop.
        </p>
      )}

      <section style={box}>
        <h3 style={{ marginTop: 0 }}>1 · Firmware</h3>
        <p>
          Product{" "}
          <select value={product} onChange={(e) => setProduct(e.target.value)} disabled={!!busy} style={{ fontSize: 15 }}>
            {products.map((p) => <option key={p.id} value={p.id}>{p.name}</option>)}
          </select>
        </p>
        {builds === null ? (
          <p>Loading builds…</p>
        ) : builds.length === 0 ? (
          <p>No {products.find((p) => p.id === product)?.name} firmware has been published yet.</p>
        ) : (
          <>
            <select value={buildId} onChange={(e) => setBuildId(e.target.value)} disabled={!!busy} style={{ maxWidth: "100%" }}>
              {builds.map((b) => (
                <option key={b.id} value={b.id}>
                  {b.version} · {b.variant} ({chipForVariant(b.variant)}, {Math.round(b.sizeBytes / 1024)} KB, {new Date(b.createdAt).toLocaleDateString()})
                </option>
              ))}
            </select>
            {build?.notes && <p style={{ color: "#666", fontSize: 13 }}>{build.notes}</p>}
            {admin && (
              <div style={{ fontSize: 14, marginTop: 8 }}>
                <label style={{ display: "block", opacity: canFull ? 1 : 0.5 }}>
                  <input type="checkbox" checked={doFull} disabled={!canFull || !!busy} onChange={(e) => setFull(e.target.checked)} />{" "}
                  Also write the bootloader and partition table (needed on a new, blank module; harmless on a working board)
                  {!build?.hasFullFlash && " (this build has none)"}
                </label>
                <label style={{ display: "block", opacity: doFull ? 1 : 0.5 }}>
                  <input type="checkbox" checked={doFull && eraseAll} disabled={!doFull || !!busy} onChange={(e) => setEraseAll(e.target.checked)} />{" "}
                  Erase everything first (wipes the board&apos;s saved WiFi, settings and data)
                </label>
              </div>
            )}
            {!admin && <p style={{ fontSize: 13, color: "#666" }}>Updates the firmware only; the board keeps its WiFi and settings.</p>}
          </>
        )}
      </section>

      <section style={box}>
        <h3 style={{ marginTop: 0 }}>2 · Connect and flash</h3>
        <ol style={{ fontSize: 14, paddingLeft: 20 }}>
          <li>Plug the board into the laptop with a data USB cable (power from USB is enough).</li>
          <li>Close any serial monitor (Arduino, PlatformIO) using the port.</li>
          <li>Click the button and pick the board&apos;s port (CP2102, CH340, &quot;USB to UART&quot; or &quot;USB JTAG/serial&quot;).</li>
          <li>If it can&apos;t connect: hold <b>BOOT</b>, tap <b>RESET</b> (EN), release BOOT, and try again.</li>
        </ol>
        <button onClick={flash} disabled={!serialOk || !build || !!busy} style={{ padding: "8px 16px", fontSize: 15 }}>
          {busy === "flash" ? `Flashing… ${progress}%` : "Connect and flash"}
        </button>{" "}
        <button onClick={restart} disabled={!serialOk || !!busy} style={{ padding: "8px 12px" }}>
          Restart and show log
        </button>
        {busy === "log" && <> <button onClick={() => (stop.current.stopped = true)}>Stop log</button></>}
        {busy === "flash" && (
          <div style={{ height: 8, background: "#eee", borderRadius: 4, marginTop: 10 }}>
            <div style={{ width: `${progress}%`, height: 8, background: "#2E9C7E", borderRadius: 4 }} />
          </div>
        )}
        {error && <p style={{ color: "#b00020" }}>✗ {error}</p>}
        {done && <p style={{ color: "#1b7f3b" }}>✓ Flashed {done.version}{done.deviceId ? ` on ${done.deviceId}` : ""}.</p>}
        {log.length > 0 && <pre style={pre}>{log.join("\n")}</pre>}
      </section>

      {(serial || busy === "log") && (
        <section style={box}>
          <h3 style={{ marginTop: 0 }}>3 · Board log</h3>
          {(info.fw || info.device || info.ssid) && (
            <table style={{ fontSize: 14, marginBottom: 8 }}>
              <tbody>
                {info.fw && <tr><td>Firmware</td><td><b>{info.fw}</b></td></tr>}
                {info.device && <tr><td>Device ID</td><td><b>{info.device}</b></td></tr>}
                {info.ssid && <tr><td>SoftAP WiFi&nbsp;</td><td><b>{info.ssid}</b></td></tr>}
                {info.pass && <tr><td>Password</td><td><b>{info.pass}</b></td></tr>}
              </tbody>
            </table>
          )}
          {info.ssid && (
            <p style={{ fontSize: 13, color: "#666" }}>
              Join that WiFi from a phone and open http://192.168.4.1 to set it up.
            </p>
          )}
          <pre style={pre}>{serial || "Waiting for the board…"}</pre>
        </section>
      )}
    </>
  );
}
