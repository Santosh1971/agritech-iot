// GET /api/app/devices/{deviceId}/live -> server-sent events for one node:
//   event: status  {"online":true}
//   event: state   <node state>   (the last one we have at once, then each new one)
//   event: event   <node event>
// A comment line every 20 s keeps proxies from closing the stream.
import { NextRequest, NextResponse } from "next/server";
import { appSession, deviceFor } from "@/lib/appAccess";
import { relay, LiveMsg, TOPIC_OF } from "@/lib/mqttRelay";

export const dynamic = "force-dynamic";

export async function GET(req: NextRequest, { params }: { params: Promise<{ deviceId: string }> }) {
  const s = await appSession(req);
  if (!s) return NextResponse.json({ ok: false, err: "sign in first" }, { status: 401 });
  const { deviceId } = await params;
  const device = await deviceFor(s, deviceId);
  if (!device) return NextResponse.json({ ok: false, err: "no such node in your farms" }, { status: 404 });
  if (!TOPIC_OF[device.product]) return NextResponse.json({ ok: false, err: "this product is not in the app yet" }, { status: 400 });

  const r = relay();
  const enc = new TextEncoder();
  let cleanup = () => {};
  const stream = new ReadableStream({
    start(ctrl) {
      const send = (event: string, data: unknown) => {
        try {
          ctrl.enqueue(enc.encode(`event: ${event}\ndata: ${JSON.stringify(data)}\n\n`));
        } catch {
          cleanup();
        }
      };
      const last = (device.lastStatus ?? {}) as Record<string, unknown>;
      const online = r.online.get(deviceId) ?? last.online === true;
      send("status", { online });
      if (device.lastStatus) {
        const { online: _o, ...state } = last;
        void _o;
        send("state", state);
      }
      const onMsg = (m: LiveMsg) => {
        if (m.type === "status") send("status", { online: m.online });
        else send(m.type, m.data);
      };
      r.bus.on(deviceId, onMsg);
      const ping = setInterval(() => {
        try {
          ctrl.enqueue(enc.encode(`: ping\n\n`));
        } catch {
          cleanup();
        }
      }, 20000);
      cleanup = () => {
        clearInterval(ping);
        r.bus.off(deviceId, onMsg);
      };
      req.signal.addEventListener("abort", () => {
        cleanup();
        try {
          ctrl.close();
        } catch {}
      });
    },
    cancel() {
      cleanup();
    },
  });
  return new Response(stream, {
    headers: {
      "Content-Type": "text/event-stream",
      "Cache-Control": "no-cache, no-transform",
      Connection: "keep-alive",
      "X-Accel-Buffering": "no", // nginx: pass each event through at once
    },
  });
}
