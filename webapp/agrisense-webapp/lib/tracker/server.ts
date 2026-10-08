// Server-side helpers for the product testing tracker: who is asking, where
// files go, and who gets emailed. Rules themselves live in ./rules.ts.
import { createHash, randomUUID } from "crypto";
import { mkdir, readFile, writeFile } from "fs/promises";
import { homedir } from "os";
import { extname, join } from "path";
import { cookies } from "next/headers";
import { NextResponse } from "next/server";
import { Resend } from "resend";
import type { TrackFileKind, TrackRole } from "@prisma/client";
import { prisma } from "@/lib/prisma";
import { verifySession, type SessionPayload } from "@/lib/session";

export type TrackUser = SessionPayload & { name: string; trackRole: TrackRole | null; isAdmin: boolean };

export function err(message: string, status = 400) {
  return NextResponse.json({ error: message }, { status });
}

/** The logged-in user plus their tracker role (null = not a member). */
export async function trackUser(): Promise<TrackUser | null> {
  const token = (await cookies()).get("agrisense_session")?.value;
  const s = token ? await verifySession(token) : null;
  if (!s) return null;
  const u = await prisma.user.findUnique({ where: { id: s.userId }, select: { name: true, trackMember: true } });
  if (!u) return null;
  const m = u.trackMember && u.trackMember.active ? u.trackMember.role : null;
  return { ...s, name: u.name, trackRole: m, isAdmin: s.role === "ADMIN" };
}

/** For API routes: a member, or an error response to return. */
export async function requireMember(role?: TrackRole): Promise<{ user: TrackUser & { trackRole: TrackRole } } | { res: NextResponse }> {
  const user = await trackUser();
  if (!user) return { res: err("Not logged in", 401) };
  if (!user.trackRole) return { res: err("You are not on the testing team", 403) };
  if (role && user.trackRole !== role) return { res: err(`Only the ${role === "TESTER" ? "tester" : "developer"} can do this`, 403) };
  return { user: user as TrackUser & { trackRole: TrackRole } };
}

// ---------------------------------------------------------------------------
// Files: on the VPS disk outside the repo (the repo is public), same pattern
// as the document library. The on-disk name is always a random UUID.
// ---------------------------------------------------------------------------

const STORAGE_ROOT = () => process.env.TRACKER_STORAGE_DIR || join(homedir(), "agrisense-data", "tracker");

export const MAX_BYTES: Record<TrackFileKind, number> = {
  ATTACHMENT: 25 * 1024 * 1024,
  FIRMWARE: 16 * 1024 * 1024,
  APK: 150 * 1024 * 1024,
  GERBER: 50 * 1024 * 1024,
};

export function trackFilePath(storagePath: string) {
  return join(STORAGE_ROOT(), storagePath);
}

export async function readTrackFile(storagePath: string) {
  return readFile(trackFilePath(storagePath));
}

type Owner = { issueId?: string; buildId?: string; releaseId?: string; eventId?: string };

/** Saves uploaded files and records them. Throws a readable Error on a bad file. */
export async function saveFiles(files: File[], kind: TrackFileKind, owner: Owner, uploadedById: string) {
  const saved = [];
  for (const f of files) {
    if (!f || f.size === 0) continue;
    if (f.size > MAX_BYTES[kind]) throw new Error(`${f.name} is over the ${MAX_BYTES[kind] / 1024 / 1024} MB limit`);
    const bytes = Buffer.from(await f.arrayBuffer());
    await mkdir(STORAGE_ROOT(), { recursive: true });
    const storagePath = `${randomUUID()}${extname(f.name).toLowerCase().replace(/[^.a-z0-9]/g, "").slice(0, 10)}`;
    await writeFile(trackFilePath(storagePath), bytes);
    saved.push(
      await prisma.trackFile.create({
        data: {
          kind,
          ...owner,
          originalName: f.name.slice(0, 200),
          storagePath,
          mime: f.type || "application/octet-stream",
          sizeBytes: bytes.length,
          sha256: createHash("sha256").update(bytes).digest("hex"),
          uploadedById,
        },
      }),
    );
  }
  return saved;
}

/** Non-empty File entries of a multipart field. */
export function formFiles(form: FormData, name: string): File[] {
  return form.getAll(name).filter((v): v is File => v instanceof File && v.size > 0);
}

export function formText(form: FormData, name: string, max = 5000): string | null {
  const v = form.get(name);
  if (typeof v !== "string") return null;
  const t = v.trim().slice(0, max);
  return t.length ? t : null;
}

// ---------------------------------------------------------------------------
// Email: to every active member with the other role. Never throws: a failed
// email must not fail the action that triggered it.
// ---------------------------------------------------------------------------

export const siteUrl = () => (process.env.SITE_URL || "https://agrisenseandcontrol.in").replace(/\/$/, "");

const esc = (s: string) => s.replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" })[c]!);

export async function notify(to: TrackRole | "ALL", subject: string, lines: string[], path: string, exceptUserId?: string) {
  try {
    const members = await prisma.trackMember.findMany({
      where: { active: true, ...(to === "ALL" ? {} : { role: to }) },
      include: { user: { select: { id: true, email: true } } },
    });
    const emails = members.filter((m) => m.user.id !== exceptUserId).map((m) => m.user.email);
    if (!emails.length) return;
    const link = `${siteUrl()}${path}`;
    const html = `<div style="font-family:sans-serif;padding:16px;max-width:560px">
      ${lines.map((l) => `<p style="margin:0 0 10px">${esc(l)}</p>`).join("")}
      <p style="margin:18px 0"><a href="${link}" style="background:#1f7a5f;color:#fff;padding:10px 16px;border-radius:8px;text-decoration:none;font-weight:bold">Open in tracker</a></p>
      <p style="color:#777;font-size:12px">Agri Sensors and Controls · product testing tracker</p></div>`;
    if (!process.env.RESEND_API_KEY) {
      console.log(`[tracker email] to=${emails.join(",")} subject=${subject} link=${link}`);
      return;
    }
    const resend = new Resend(process.env.RESEND_API_KEY);
    const from = process.env.EMAIL_FROM || "Agri Sensors and Controls <onboarding@resend.dev>";
    await resend.emails.send({ from, to: emails, subject: `[Testing] ${subject}`, html });
  } catch (e) {
    console.error("[tracker email] failed", e);
  }
}

export const fmtDate = (d: Date | string) =>
  new Date(d).toLocaleString("en-IN", { timeZone: "Asia/Kolkata", day: "numeric", month: "short", hour: "2-digit", minute: "2-digit" });
export const fmtDay = (d: Date | string) =>
  new Date(d).toLocaleDateString("en-IN", { timeZone: "Asia/Kolkata", day: "numeric", month: "short", year: "numeric" });
