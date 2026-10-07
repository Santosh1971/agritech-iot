"use client";
// The studio's link to a kit over USB (Web Serial, Chrome or Edge on a
// laptop). Speaks the firmware's JSON-lines protocol (see
// products/ASC-StudentKit/firmware/README.md). One shared connection, so
// moving between the Build and Test stages keeps the board connected.
import { useEffect, useState } from "react";
import type { DeviceConfig } from "@/lib/studio/deviceConfig";

export type Hello = {
  type: "hello"; fw: string; board: "mini" | "mega"; standIn: boolean; id: string; rtc: boolean; time: number;
  design?: { design: number; name: string; ports: Record<string, string>; rules: number };
};
export type Live = { type: "live"; time: number; values: Record<string, number | null>; outputs: Record<string, number>; manual: Record<string, boolean> };
export type SelfTest = { type: "selftest"; results: { port: string; block: string; ok: boolean; detail: string }[] };
type Msg = { type: string; ok?: boolean; error?: string; [k: string]: unknown };

export function serialSupported(): boolean {
  return typeof navigator !== "undefined" && "serial" in navigator;
}

// True on the server and on first render, so server and browser HTML match;
// the real answer arrives right after the page loads.
export function useSerialSupported(): boolean {
  const [ok, setOk] = useState(true);
  useEffect(() => setOk(serialSupported()), []);
  return ok;
}

class Device {
  port: SerialPort | null = null;
  private reader: ReadableStreamDefaultReader<Uint8Array> | null = null;
  private listeners = new Set<(m: Msg) => void>();
  private stateListeners = new Set<() => void>();
  hello: Hello | null = null;
  live: Live | null = null;
  log: string[] = [];

  get connected() { return !!this.port?.readable; }

  onMessage(fn: (m: Msg) => void) { this.listeners.add(fn); return () => { this.listeners.delete(fn); }; }
  onState(fn: () => void) { this.stateListeners.add(fn); return () => { this.stateListeners.delete(fn); }; }
  private changed() { this.stateListeners.forEach((f) => f()); }

  // Reuse a port the browser already allowed, or ask the user to pick one.
  async pickPort(): Promise<SerialPort> {
    const known = await navigator.serial.getPorts();
    if (known.length === 1) return known[0];
    return navigator.serial.requestPort();
  }

  async connect(port?: SerialPort): Promise<Hello> {
    if (this.connected) await this.disconnect();
    this.port = port ?? (await this.pickPort());
    await this.port.open({ baudRate: 115200, bufferSize: 16384 });
    // Hold the chip out of reset (RTS) on the native USB port.
    await this.port.setSignals({ dataTerminalReady: true, requestToSend: false }).catch(() => {});
    this.readLoop();
    this.changed();
    const hello = (await this.request({ cmd: "hello" }, "hello", 4000)) as unknown as Hello;
    this.hello = hello;
    await this.request({ cmd: "time", unix: Math.floor(Date.now() / 1000) }, "time", 3000).catch(() => {});
    this.changed();
    return hello;
  }

  async disconnect() {
    try { await this.reader?.cancel(); } catch {}
    try { await this.port?.close(); } catch {}
    this.reader = null;
    this.port = null;
    this.live = null;
    this.changed();
  }

  // Close without forgetting the port, so the flasher can take it over.
  async release(): Promise<SerialPort | null> {
    const p = this.port;
    await this.disconnect();
    return p;
  }

  private async readLoop() {
    const port = this.port!;
    const decoder = new TextDecoder();
    let buf = "";
    while (port.readable) {
      this.reader = port.readable.getReader();
      try {
        for (;;) {
          const { value, done } = await this.reader.read();
          if (done) break;
          buf += decoder.decode(value, { stream: true });
          let i;
          while ((i = buf.indexOf("\n")) >= 0) {
            const line = buf.slice(0, i).trim();
            buf = buf.slice(i + 1);
            if (line) this.handleLine(line);
          }
        }
      } catch {
        break; // unplugged, or released for flashing
      } finally {
        this.reader.releaseLock();
      }
      if (this.reader === null) break;
    }
    if (this.port === port) { this.port = null; this.changed(); }
  }

  private handleLine(line: string) {
    if (!line.startsWith("{")) {
      this.log = [...this.log.slice(-50), line]; // boot messages from the chip
      return;
    }
    let m: Msg;
    try { m = JSON.parse(line); } catch { return; }
    // "live" is both the command's acknowledgement and the readings frame; only frames carry values.
    if (m.type === "live" && m.values) { this.live = m as unknown as Live; this.changed(); }
    this.listeners.forEach((f) => f(m));
  }

  // Writes are queued: a reply can arrive (and trigger the next command)
  // before the previous write has released the port.
  private writing: Promise<void> = Promise.resolve();
  send(obj: object): Promise<void> {
    const job = this.writing.then(async () => {
      if (!this.port?.writable) throw new Error("The board isn't connected.");
      const w = this.port.writable.getWriter();
      try { await w.write(new TextEncoder().encode(JSON.stringify(obj) + "\n")); } finally { w.releaseLock(); }
    });
    this.writing = job.catch(() => {});
    return job;
  }

  // Send a command and wait for the reply of the given type.
  request(obj: object, type: string, timeoutMs = 5000): Promise<Msg> {
    return new Promise((resolve, reject) => {
      const t = setTimeout(() => { off(); reject(new Error("The board didn't answer. Check it has the studio firmware, or press its RST button and connect again.")); }, timeoutMs);
      const off = this.onMessage((m) => {
        if (m.type !== type && m.type !== "error") return;
        clearTimeout(t); off();
        if (m.ok === false || m.type === "error") reject(new Error(m.error ?? "The board refused that."));
        else resolve(m);
      });
      this.send(obj).catch((e) => { clearTimeout(t); off(); reject(e); });
    });
  }

  async sendConfig(config: DeviceConfig) {
    await this.request({ cmd: "config", config }, "config", 6000);
    this.hello = (await this.request({ cmd: "hello" }, "hello", 4000)) as unknown as Hello;
    this.changed();
  }

  setLive(on: boolean) { return this.request({ cmd: "live", on }, "live", 3000); }
  setOutput(port: string, on: boolean) { return this.request({ cmd: "out", port, on }, "out", 3000); }
  auto() { return this.request({ cmd: "auto" }, "auto", 3000); }
  async selfTest(): Promise<SelfTest> { return (await this.request({ cmd: "selftest" }, "selftest", 8000)) as unknown as SelfTest; }
}

export const device = new Device();
