// Applying an issue action: permission check, status change, timeline event,
// verdict on the latest build, and the email to the other side. Used by the
// issue page and the build page alike so a Pass means the same everywhere.
import type { TrackRole, TrackSeverity, TrackStatus } from "@prisma/client";
import { prisma } from "@/lib/prisma";
import { ACTION_LABEL, NEEDS_TEXT, STATUS_LABEL, allowedActions, issueKey, nextStatus, type IssueAction } from "./rules";
import { notify, saveFiles } from "./server";

export class ActionError extends Error {}

type Actor = { userId: string; name: string; trackRole: TrackRole };

export async function applyIssueAction(
  actor: Actor,
  issueId: string,
  action: IssueAction,
  opts: { text?: string | null; severity?: TrackSeverity | null; releaseId?: string | null; files?: File[] } = {},
) {
  const issue = await prisma.trackIssue.findUnique({ where: { id: issueId }, include: { product: true } });
  if (!issue) throw new ActionError("Issue not found");
  if (!allowedActions(issue, actor.trackRole).includes(action)) {
    throw new ActionError(`"${ACTION_LABEL[action]}" is not allowed for a ${STATUS_LABEL[issue.status].toLowerCase()} issue`);
  }
  const text = opts.text?.trim() || null;
  if (NEEDS_TEXT.includes(action) && !text && !(action === "comment" && opts.files?.length)) {
    throw new ActionError("Please add a note");
  }

  const to = nextStatus(issue, action);
  const data: Record<string, unknown> = {};
  if (to) data.status = to;

  if (action === "accept") {
    if (opts.severity) data.severity = opts.severity;
    if (opts.releaseId !== undefined) {
      if (opts.releaseId) {
        const r = await prisma.trackRelease.findFirst({ where: { id: opts.releaseId, productId: issue.productId, status: "OPEN" } });
        if (!r) throw new ActionError("Pick an open release of this product");
      }
      data.releaseId = opts.releaseId || null;
    }
  }
  if (action === "propose_defer") Object.assign(data, { proposal: "DEFERRED", proposalNote: text });
  if (action === "propose_wontfix") Object.assign(data, { proposal: "WONT_FIX", proposalNote: text });
  if (action === "withdraw_proposal" || action === "decline_proposal" || action === "agree_proposal") {
    Object.assign(data, { proposal: null, proposalNote: null });
  }
  // A deferred issue leaves its release; it is re-targeted when accepted again.
  if (action === "agree_proposal" && issue.proposal === "DEFERRED") data.releaseId = null;

  const key = issueKey(issue.product.code, issue.number);
  const eventText =
    action === "propose_defer" || action === "propose_wontfix"
      ? `Proposed ${action === "propose_defer" ? "Deferred" : "Won't fix"}: ${text}`
      : action === "agree_proposal"
        ? `Agreed: ${issue.proposal === "DEFERRED" ? "Deferred" : "Won't fix"}${text ? ` · ${text}` : ""}`
        : text;

  const event = await prisma.$transaction(async (tx) => {
    if (Object.keys(data).length) await tx.trackIssue.update({ where: { id: issueId }, data });
    // A verdict on the issue is a verdict on the newest build that claims it.
    if (action === "verify_pass" || action === "verify_fail") {
      const link = await tx.trackBuildIssue.findFirst({ where: { issueId }, orderBy: { build: { number: "desc" } } });
      if (link) {
        await tx.trackBuildIssue.update({
          where: { buildId_issueId: { buildId: link.buildId, issueId } },
          data: { verdict: action === "verify_pass" ? "pass" : "fail", note: text, verdictAt: new Date() },
        });
      }
    }
    return tx.trackEvent.create({
      data: {
        issueId,
        actorId: actor.userId,
        kind: action === "comment" ? "comment" : action.startsWith("verify") ? "verdict" : "status",
        fromStatus: to ? (issue.status as TrackStatus) : null,
        toStatus: to,
        text: eventText,
      },
    });
  });

  if (opts.files?.length) await saveFiles(opts.files, "ATTACHMENT", { issueId, eventId: event.id }, actor.userId);

  const other: TrackRole = actor.trackRole === "TESTER" ? "DEVELOPER" : "TESTER";
  const what = to ? `${STATUS_LABEL[issue.status]} → ${STATUS_LABEL[to]}` : ACTION_LABEL[action];
  await notify(other, `${key} ${what}: ${issue.title}`, [`${actor.name}: ${what}`, ...(eventText ? [eventText] : [])], `/dashboard/testing/issue/${issueId}`, actor.userId);
  return event;
}
