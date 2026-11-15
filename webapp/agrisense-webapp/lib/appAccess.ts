// Who may use which node from the product apps (/api/app/*).
// Admin: every node. Others: nodes of farms they are a member of, plus nodes
// they own or deal (the older per-device fields other products use).
import { NextRequest } from "next/server";
import { Prisma, Product } from "@prisma/client";
import { prisma } from "@/lib/prisma";
import { verifySession, SessionPayload } from "@/lib/session";

/** Session from the cookie (web app, same site) or "Authorization: Bearer" (Android app). */
export async function appSession(req: NextRequest): Promise<SessionPayload | null> {
  const auth = req.headers.get("authorization") ?? "";
  const token = auth.startsWith("Bearer ") ? auth.slice(7) : req.cookies.get("agrisense_session")?.value;
  return token ? await verifySession(token) : null;
}

export function deviceScope(s: SessionPayload): Prisma.DeviceWhereInput {
  if (s.role === "ADMIN") return {};
  return {
    OR: [{ farm: { members: { some: { userId: s.userId } } } }, { ownerId: s.userId }, { dealerId: s.userId }],
  };
}

/** The node by its own ID (e.g. WM2_0D4D4FB0), only if this user may use it. */
export async function deviceFor(s: SessionPayload, deviceId: string) {
  return prisma.device.findFirst({ where: { AND: [{ deviceId }, deviceScope(s)] }, include: { farm: true } });
}

export function isProduct(p: string | null): p is Product {
  return !!p && (Object.values(Product) as string[]).includes(p);
}

// Commands a farm member may send. Anything else (broker change, factory
// reset, bench mode, valve hardware, forgetting WiFi, clearing history, and
// any command not listed) is admin only.
const MEMBER_CMDS: Record<string, Set<string>> = {
  WM2: new Set([
    "get_state", "get_config", "get_program", "get_history", "get_days", "get_events", "device_info",
    "identify", "valve_open", "valve_close", "valve_test", "bypass", "set_mode", "set_program", "clear_program",
    "set_options", "set_flow", "set_battery", "set_report", "set_power", "set_config", "rtc_sync",
    "wifi_config", "wifi_scan", "softap_on", "reboot", "set_seq", "zone_leave",
  ]),
};

export function mayRun(s: SessionPayload, product: string, cmd: string): boolean {
  if (s.role === "ADMIN") return true;
  return MEMBER_CMDS[product]?.has(cmd) ?? false;
}

// set_config keys a farm member may change (WM2). Everything else in the
// config (WiFi and SoftAP passwords, broker and MQTT logins, zone/node names,
// valve hardware, safe-close, zone turns) is dropped unless an admin sends it.
const MEMBER_CONFIG_KEYS: Record<string, Set<string>> = {
  WM2: new Set([
    "flow", "sequential", "ps_fitted", "day_start_hour", "tz_min", "ps_settle_s", "low_pct", "high_pct",
    "imbalance_pct", "alarm_delay_s", "bypass_default_min", "active_s", "heartbeat_s", "profile",
    "chemistry", "capacity_ah", "v_warn", "v_save", "v_crit", "daily_ah",
  ]),
};

/** The command a member may actually send: unknown config keys removed, WiFi cut to name + password. */
export function memberSafe(product: string, body: Record<string, unknown>): Record<string, unknown> {
  const cmd = body.cmd;
  if (cmd === "set_config") {
    const allowed = MEMBER_CONFIG_KEYS[product] ?? new Set<string>();
    const src = body.config && typeof body.config === "object" ? (body.config as Record<string, unknown>) : {};
    const config = Object.fromEntries(Object.entries(src).filter(([k]) => allowed.has(k)));
    return { cmd, config };
  }
  if (cmd === "wifi_config") return { cmd, ssid: body.ssid, pass: body.pass };
  return body;
}

