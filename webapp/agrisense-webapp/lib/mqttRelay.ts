// One MQTT connection inside agrisense-web (the agrisense-backend login) for
// the product apps: the app talks HTTPS to us, we check who may use the node,
// and relay its commands and live state. No broker password in any app.
import mqtt, { MqttClient } from "mqtt";
import { EventEmitter } from "events";

// Prisma Product -> topic segment (agrisense/<seg>/<deviceId>/...).
export const TOPIC_OF: Record<string, string> = { WM2: "WM2" };

export type LiveMsg =
  | { type: "state"; data: unknown }
  | { type: "event"; data: unknown }
  | { type: "status"; online: boolean };

type Reply = Record<string, unknown>;

class Relay {
  private client: MqttClient;
  readonly bus = new EventEmitter();
  private pending = new Map<string, (r: Reply) => void>();
  readonly online = new Map<string, boolean>(); // deviceId -> last will state

  constructor() {
    this.bus.setMaxListeners(0);
    this.client = mqtt.connect(process.env.MQTT_BROKER_URL || "mqtt://localhost:1883", {
      username: process.env.MQTT_ADMIN_USERNAME,
      password: process.env.MQTT_ADMIN_PASSWORD,
      clientId: `agrisense-web-${process.pid}-${Math.random().toString(16).slice(2, 8)}`,
      reconnectPeriod: 3000,
      clean: true,
    });
    this.client.on("connect", () => {
      console.log("[relay] connected to the broker");
      for (const seg of Object.values(TOPIC_OF)) {
        for (const k of ["resp", "state", "event", "status"]) this.client.subscribe(`agrisense/${seg}/+/${k}`, { qos: 1 });
      }
    });
    this.client.on("error", (e) => console.error("[relay] mqtt error", e.message));
    this.client.on("offline", () => console.warn("[relay] broker connection lost, reconnecting"));
    this.client.on("message", (topic, buf) => this.onMessage(topic, buf));
  }

  get connected() {
    return this.client.connected;
  }

  private onMessage(topic: string, buf: Buffer) {
    const [, , dev, kind] = topic.split("/");
    if (!dev) return;
    if (kind === "status") {
      const online = buf.toString().trim() === "online";
      this.online.set(dev, online);
      this.bus.emit(dev, { type: "status", online } satisfies LiveMsg);
      return;
    }
    let j: Reply;
    try {
      j = JSON.parse(buf.toString());
    } catch {
      return;
    }
    if (kind === "resp") {
      const id = typeof j.id === "string" ? j.id : "";
      this.pending.get(id)?.(j);
    } else if (kind === "state" || kind === "event") {
      this.bus.emit(dev, { type: kind, data: j } as LiveMsg);
    }
  }

  /** Publishes `cmd` to the node and waits for its reply (or a timeout). */
  async command(seg: string, dev: string, cmd: Reply, id: string, timeoutMs: number): Promise<Reply> {
    // Just after start (or a broker restart) the connection may still be coming up.
    for (let i = 0; i < 40 && !this.client.connected; i++) await new Promise((r) => setTimeout(r, 200));
    if (!this.client.connected) return { ok: false, err: "server not connected to the broker, try again" };
    const msg = { ...cmd, id, src: "app", ts: Math.floor(Date.now() / 1000) };
    return new Promise((resolve) => {
      const timer = setTimeout(() => {
        this.pending.delete(id);
        resolve({ ok: false, err: "no reply from the node (offline or busy)" });
      }, timeoutMs);
      this.pending.set(id, (r) => {
        clearTimeout(timer);
        this.pending.delete(id);
        resolve(r);
      });
      this.client.publish(`agrisense/${seg}/${dev}/cmd`, JSON.stringify(msg), { qos: 1 }, (err) => {
        if (err) {
          clearTimeout(timer);
          this.pending.delete(id);
          resolve({ ok: false, err: "could not send to the broker" });
        }
      });
    });
  }
}

const g = globalThis as unknown as { __agrisenseRelay?: Relay };
export function relay(): Relay {
  if (!g.__agrisenseRelay) g.__agrisenseRelay = new Relay();
  return g.__agrisenseRelay;
}
