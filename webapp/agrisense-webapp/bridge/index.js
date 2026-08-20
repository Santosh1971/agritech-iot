// Standalone service, PM2-managed separately from the Next.js app (see README).
// Subscribes to agrisense/# with an admin-level MQTT credential, writes incoming
// status/data/ack messages to Postgres via Prisma. Keeps ingestion running even
// while the web app itself is redeploying/restarting.

import mqtt from "mqtt";
import { PrismaClient } from "@prisma/client";

const prisma = new PrismaClient();

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

client.on("message", async (topic, payloadBuf) => {
  // agrisense/<product>/<deviceId>/<kind>[/ack]
  const parts = topic.split("/");
  if (parts[0] !== "agrisense" || parts.length < 4) return;

  const [, product, deviceId, kind, sub] = parts;
  let payload;
  try {
    payload = JSON.parse(payloadBuf.toString());
  } catch {
    console.warn(`[bridge] non-JSON payload on ${topic}, skipping`);
    return;
  }

  try {
    // Ensure a Device row exists — devices can connect before Admin assigns an owner.
    const device = await prisma.device.upsert({
      where: { deviceId },
      update: { lastSeenAt: new Date() },
      create: {
        deviceId,
        product,
        name: deviceId, // placeholder until Admin renames it
        lastSeenAt: new Date(),
      },
    });

    if (kind === "status" || kind === "lwt") {
      await prisma.device.update({
        where: { id: device.id },
        data: { lastStatus: payload, lastSeenAt: new Date() },
      });
    } else if (kind === "data") {
      await prisma.reading.create({
        data: { deviceId: device.id, payload },
      });
    } else if (kind === "cmd" && sub === "ack") {
      if (payload.cmdId) {
        await prisma.command.updateMany({
          where: { cmdId: payload.cmdId },
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
