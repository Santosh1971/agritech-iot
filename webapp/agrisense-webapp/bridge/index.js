import "dotenv/config";
import mqtt from "mqtt";
import { PrismaClient } from "@prisma/client";
import { PrismaPg } from "@prisma/adapter-pg";

const adapter = new PrismaPg({ connectionString: process.env.DATABASE_URL });
const prisma = new PrismaClient({ adapter });

const client = mqtt.connect(process.env.MQTT_BROKER_URL, {
  username: process.env.MQTT_ADMIN_USERNAME,
  password: process.env.MQTT_ADMIN_PASSWORD,
});

client.on("connect", () => {
  console.log("[bridge] connected to broker");
  client.subscribe("agrisense/#", (err) => {
    if (err) console.error("[bridge] subscribe failed", err);
    else console.log("[bridge] subscribed to agrisense/#");
  });
});

client.on("error", (err) => console.error("[bridge] mqtt error", err));

// Topic product segment -> Prisma Product enum. WM1 units publish under
// agrisense/WM1/ but are WM1_MINI in the DB (see lib/deviceIdentity.ts).
const TOPIC_PRODUCTS = { FG1: "FG1", FM1: "FM1", WM1: "WM1_MINI", WPC: "WPC", TH: "TH", WM2: "WM2" };

// WM2 keeps the sub-topic names from its spec (shared with Girish's broker);
// map them onto the bridge's kinds. Its "status" is plain text online/offline
// (MQTT last will), not JSON.
const KIND_ALIASES = {
  WM2: { state: "status", telemetry: "data", heartbeat: "data", event: "data", resp: "ack" },
};
const warnedProducts = new Set();

client.on("message", async (topic, payloadBuf) => {
  const parts = topic.split("/");
  if (parts[0] !== "agrisense" || parts.length < 4) return;

  const [, topicProduct, deviceId, rawKind, sub] = parts;
  const product = TOPIC_PRODUCTS[topicProduct];
  const kind = KIND_ALIASES[topicProduct]?.[rawKind] ?? rawKind;
  if (!product) {
    // Warn once per product instead of logging a DB error on every message.
    if (!warnedProducts.has(topicProduct)) {
      warnedProducts.add(topicProduct);
      console.warn(`[bridge] unknown product "${topicProduct}" on ${topic}, ignoring its messages`);
    }
    return;
  }
  if (topicProduct === "WM2" && rawKind === "status") {
    const online = payloadBuf.toString().trim() === "online";
    try {
      const device = await prisma.device.upsert({
        where: { deviceId },
        update: { lastSeenAt: new Date() },
        create: { deviceId, product, name: deviceId, lastSeenAt: new Date() },
      });
      const prev = device.lastStatus && typeof device.lastStatus === "object" ? device.lastStatus : {};
      await prisma.device.update({ where: { id: device.id }, data: { lastStatus: { ...prev, online } } });
    } catch (err) {
      console.error(`[bridge] failed processing ${topic}`, err);
    }
    return;
  }

  let payload;
  try {
    payload = JSON.parse(payloadBuf.toString());
  } catch {
    console.warn(`[bridge] non-JSON payload on ${topic}, skipping`);
    return;
  }

  try {
    const device = await prisma.device.upsert({
      where: { deviceId },
      update: { lastSeenAt: new Date() },
      create: { deviceId, product, name: deviceId, lastSeenAt: new Date() },
    });

    if (kind === "status" || kind === "lwt") {
      await prisma.device.update({
        where: { id: device.id },
        data: { lastStatus: topicProduct === "WM2" ? { ...payload, online: true } : payload, lastSeenAt: new Date() },
      });
    } else if (kind === "data") {
      // WM2 sends telemetry, heartbeats and events here; keep which one it was.
      const data = topicProduct === "WM2" ? { ...payload, _topic: rawKind } : payload;
      await prisma.reading.create({ data: { deviceId: device.id, payload: data } });
    } else if ((kind === "cmd" && sub === "ack") || kind === "ack") {
      const cmdId = payload.cmdId ?? payload.id;  // WM2 replies carry the command's "id"
      if (cmdId) {
        await prisma.command.updateMany({
          where: { cmdId },
          data: { status: "ACKED", ackedAt: new Date(), ackPayload: payload },
        });
      }
    }
  } catch (err) {
    console.error(`[bridge] failed processing ${topic}`, err);
  }
});

process.on("SIGTERM", async () => {
  await prisma.$disconnect();
  client.end();
  process.exit(0);
});
