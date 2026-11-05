// Leads tracker: who may read and change leads. The team (testing-tracker
// members) signs in with the site session; Claude uses the bearer token
// LEADS_TOKEN (tools/leads/leads.py). Its own token, so it can't flash boards.
import { timingSafeEqual } from "crypto";
import type { NextRequest } from "next/server";
import type { LeadHeat, LeadKind, LeadStatus } from "@prisma/client";
import { trackUser } from "@/lib/tracker/server";

export const KINDS: LeadKind[] = ["BUYER", "POLYHOUSE", "PARTNER"];
export const HEATS: LeadHeat[] = ["HOT", "WARM", "COLD"];
export const STATUSES: LeadStatus[] = ["NEW", "CONTACTED", "TALKING", "WON", "LOST", "SKIPPED"];

export const KIND_LABEL: Record<LeadKind, string> = { BUYER: "Buyer", POLYHOUSE: "Polyhouse", PARTNER: "Partner" };
export const STATUS_LABEL: Record<LeadStatus, string> = {
  NEW: "To contact",
  CONTACTED: "Contacted",
  TALKING: "Talking",
  WON: "Order won",
  LOST: "Not interested",
  SKIPPED: "Skipped",
};

function bearerOk(req: NextRequest) {
  const want = process.env.LEADS_TOKEN;
  const got = req.headers.get("authorization")?.replace(/^Bearer /, "") ?? "";
  if (!want || want.length < 24 || got.length !== want.length) return false;
  return timingSafeEqual(Buffer.from(got), Buffer.from(want));
}

/** Name of whoever is asking (team member or "Claude"), or null. */
export async function leadsActor(req?: NextRequest): Promise<string | null> {
  if (req && bearerOk(req)) return "Claude";
  const u = await trackUser();
  if (!u || (!u.trackRole && !u.isAdmin)) return null;
  return u.name;
}

const FIELDS = ["platform", "handle", "name", "said", "postUrl", "language", "location", "products", "phone", "nextStep", "suggestedReply"] as const;

/** Picks the allowed text fields from a JSON body, trimmed and length-capped. */
export function leadFields(body: Record<string, unknown>) {
  const out: Record<string, string | null> = {};
  for (const f of FIELDS) {
    if (!(f in body)) continue;
    const v = body[f];
    out[f] = typeof v === "string" && v.trim() ? v.trim().slice(0, f === "said" || f === "nextStep" || f === "suggestedReply" ? 2000 : 300) : null;
  }
  if (typeof body.kind === "string" && KINDS.includes(body.kind as LeadKind)) out.kind = body.kind;
  if (typeof body.heat === "string" && HEATS.includes(body.heat as LeadHeat)) out.heat = body.heat;
  if (typeof body.status === "string" && STATUSES.includes(body.status as LeadStatus)) out.status = body.status;
  return out;
}
