// Product testing tracker: labels, who may move an issue where, and the
// release gate. Pure functions, no Prisma or Next imports, so client
// components and the API routes share exactly the same rules.
//
// Tester (Avinash) reports, verifies and signs off. Developer (Santosh)
// triages, fixes, posts test builds and publishes. Nobody closes their own
// work: only the tester can mark a fix Verified, and Deferred / Won't fix
// need the tester's agreement.

export type Role = "TESTER" | "DEVELOPER";
export type Area = "FIRMWARE" | "APP" | "HARDWARE" | "DOCS";
export type Severity = "BLOCKER" | "MAJOR" | "MINOR" | "SUGGESTION";
export type Status =
  | "NEW"
  | "NEED_INFO"
  | "ACCEPTED"
  | "IN_PROGRESS"
  | "FIX_READY"
  | "REOPENED"
  | "VERIFIED"
  | "DEFERRED"
  | "WONT_FIX";

export const AREAS: Area[] = ["FIRMWARE", "APP", "HARDWARE", "DOCS"];
export const SEVERITIES: Severity[] = ["BLOCKER", "MAJOR", "MINOR", "SUGGESTION"];
export const STATUSES: Status[] = ["NEW", "NEED_INFO", "ACCEPTED", "IN_PROGRESS", "FIX_READY", "REOPENED", "VERIFIED", "DEFERRED", "WONT_FIX"];

export const AREA_LABEL: Record<Area, string> = { FIRMWARE: "Firmware", APP: "App", HARDWARE: "Hardware", DOCS: "Docs" };
export const SEVERITY_LABEL: Record<Severity, string> = { BLOCKER: "Blocker", MAJOR: "Major", MINOR: "Minor", SUGGESTION: "Suggestion" };
export const SEVERITY_HINT: Record<Severity, string> = {
  BLOCKER: "Unsafe or unusable in the field (pump won't stop, valve stuck, bricks on update)",
  MAJOR: "A feature does not work, no easy workaround",
  MINOR: "Works with a workaround, or a wrong value or text",
  SUGGESTION: "Improvement idea, never blocks a release",
};
export const STATUS_LABEL: Record<Status, string> = {
  NEW: "New",
  NEED_INFO: "Need info",
  ACCEPTED: "Accepted",
  IN_PROGRESS: "In progress",
  FIX_READY: "Fix ready",
  REOPENED: "Reopened",
  VERIFIED: "Verified",
  DEFERRED: "Deferred",
  WONT_FIX: "Won't fix",
};
export const ROLE_LABEL: Record<Role, string> = { TESTER: "Tester", DEVELOPER: "Developer" };

/** Not yet resolved one way or the other. */
export const OPEN_STATUSES: Status[] = ["NEW", "NEED_INFO", "ACCEPTED", "IN_PROGRESS", "FIX_READY", "REOPENED"];
export const isOpen = (s: Status) => OPEN_STATUSES.includes(s);

export const issueKey = (productCode: string, n: number) => `${productCode}-${String(n).padStart(3, "0")}`;

// ---------------------------------------------------------------------------
// Issue actions
// ---------------------------------------------------------------------------

export type IssueAction =
  | "comment"
  | "accept"
  | "need_info"
  | "answer"
  | "start"
  | "propose_defer"
  | "propose_wontfix"
  | "withdraw_proposal"
  | "agree_proposal"
  | "decline_proposal"
  | "verify_pass"
  | "verify_fail"
  | "reopen";

export const ACTION_LABEL: Record<IssueAction, string> = {
  comment: "Comment",
  accept: "Accept",
  need_info: "Ask for info",
  answer: "Answer",
  start: "Start fixing",
  propose_defer: "Propose defer",
  propose_wontfix: "Propose won't fix",
  withdraw_proposal: "Withdraw proposal",
  agree_proposal: "Agree",
  decline_proposal: "Disagree",
  verify_pass: "Pass: fixed",
  verify_fail: "Fail: still broken",
  reopen: "Reopen",
};

/** Actions whose note is required (the other side needs to know why). */
export const NEEDS_TEXT: IssueAction[] = ["comment", "need_info", "answer", "propose_defer", "propose_wontfix", "decline_proposal", "verify_fail", "reopen"];

type IssueLike = { status: Status; proposal: Status | null };

export function allowedActions(issue: IssueLike, role: Role): IssueAction[] {
  const s = issue.status;
  const out: IssueAction[] = ["comment"];
  if (role === "DEVELOPER") {
    if (issue.proposal) return [...out, "withdraw_proposal"];
    if (s === "NEW" || s === "REOPENED" || s === "DEFERRED") out.push("accept");
    if (s === "ACCEPTED" || s === "REOPENED") out.push("start");
    if (s === "NEW" || s === "ACCEPTED" || s === "REOPENED") out.push("need_info");
    if (isOpen(s) && s !== "FIX_READY") out.push("propose_defer", "propose_wontfix");
  } else {
    if (issue.proposal) out.push("agree_proposal", "decline_proposal");
    if (s === "NEED_INFO") out.push("answer");
    if (s === "FIX_READY") out.push("verify_pass", "verify_fail");
    if (s === "VERIFIED" || s === "WONT_FIX" || s === "DEFERRED") out.push("reopen");
  }
  return out;
}

/** The status an action moves the issue to (null = status unchanged). */
export function nextStatus(issue: IssueLike, action: IssueAction): Status | null {
  switch (action) {
    case "accept":
      return "ACCEPTED";
    case "need_info":
      return "NEED_INFO";
    case "answer":
      return "NEW";
    case "start":
      return "IN_PROGRESS";
    case "agree_proposal":
      return issue.proposal;
    case "verify_pass":
      return "VERIFIED";
    case "verify_fail":
    case "reopen":
      return "REOPENED";
    default:
      return null;
  }
}

/** Issues a developer may put into a test build. */
export const BUILDABLE: Status[] = ["ACCEPTED", "IN_PROGRESS", "REOPENED"];

// Which detail fields each role may edit, and when.
export const TESTER_FIELDS = ["title", "steps", "expected", "actual", "foundFw", "foundApp", "foundHw", "deviceId", "module", "area", "severity"] as const;
export const DEVELOPER_FIELDS = ["severity", "area", "module", "releaseId"] as const;
export function editableFields(issue: IssueLike, role: Role): readonly string[] {
  if (role === "DEVELOPER") return isOpen(issue.status) || issue.status === "DEFERRED" ? DEVELOPER_FIELDS : [];
  return issue.status === "NEW" || issue.status === "NEED_INFO" ? TESTER_FIELDS : [];
}

// ---------------------------------------------------------------------------
// Release gate
// ---------------------------------------------------------------------------

export type ChecklistItem = { text: string; result: "pass" | "fail" | null; note?: string };

export type GateInput = {
  /** Every issue of the product (the gate also needs untriaged ones). */
  issues: { id: string; releaseId: string | null; status: Status; severity: Severity; proposal: Status | null }[];
  releaseId: string;
  candidate: null | { checklist: ChecklistItem[]; verdicts: { verdict: string | null }[] };
  signedOff: boolean;
};

export type GateCheck = { key: string; label: string; ok: boolean; detail: string };

export function releaseGate(g: GateInput): { checks: GateCheck[]; readyForSignOff: boolean; readyToPublish: boolean } {
  const untriaged = g.issues.filter((i) => i.status === "NEW" || i.status === "NEED_INFO");
  const mine = g.issues.filter((i) => i.releaseId === g.releaseId);
  const bigOpen = mine.filter((i) => (i.severity === "BLOCKER" || i.severity === "MAJOR") && (isOpen(i.status) || i.proposal));
  const minorOpen = mine.filter((i) => i.severity === "MINOR" && (isOpen(i.status) || i.proposal));

  const c = g.candidate;
  const checklistDone = !!c && c.checklist.every((x) => x.result === "pass");
  const verdictsDone = !!c && c.verdicts.every((v) => v.verdict === "pass");
  const failed = c ? c.checklist.filter((x) => x.result === "fail").length + c.verdicts.filter((v) => v.verdict === "fail").length : 0;
  const pending = c ? c.checklist.filter((x) => x.result === null).length + c.verdicts.filter((v) => v.verdict === null).length : 0;

  const checks: GateCheck[] = [
    { key: "triaged", label: "Every reported issue is triaged", ok: untriaged.length === 0, detail: untriaged.length ? `${untriaged.length} New or Need info` : "None waiting" },
    { key: "major", label: "No open Blocker or Major in this release", ok: bigOpen.length === 0, detail: bigOpen.length ? `${bigOpen.length} open` : "All clear" },
    { key: "minor", label: "Every Minor is Verified, Deferred or Won't fix", ok: minorOpen.length === 0, detail: minorOpen.length ? `${minorOpen.length} open` : "All clear" },
    {
      key: "candidate",
      label: "Candidate build fully passed (fixes + regression checklist)",
      ok: !!c && checklistDone && verdictsDone,
      detail: !c ? "No candidate build chosen" : failed ? `${failed} failed` : pending ? `${pending} not yet tested` : "All passed",
    },
  ];
  const readyForSignOff = checks.every((x) => x.ok);
  checks.push({ key: "signoff", label: "Tester has signed off", ok: g.signedOff, detail: g.signedOff ? "Signed off" : "Waiting" });
  return { checks, readyForSignOff, readyToPublish: readyForSignOff && g.signedOff };
}

/** Default regression checklist, per area of the product. Editable per build. */
export function defaultChecklist(p: { hasFirmware: boolean; hasApp: boolean; hasHardware: boolean }): string[] {
  const out: string[] = [];
  if (p.hasFirmware) out.push("Fresh flash boots and joins WiFi / network", "Settings survive a power cut", "Main output (pump / valve) switches on and off", "Schedule or automation runs at the set time");
  if (p.hasApp) out.push("App installs over the previous version and keeps settings", "App shows live status within 10 s", "Control from app switches the output");
  if (p.hasHardware) out.push("No heating, smell or noise after 1 h running");
  return out;
}
