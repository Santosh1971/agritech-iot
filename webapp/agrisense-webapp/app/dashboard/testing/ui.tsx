// Display pieces shared by the tracker pages (server components, no state).
import type { TrackFile } from "@prisma/client";
import { AREA_LABEL, SEVERITY_LABEL, STATUS_LABEL, type Area, type Severity, type Status } from "@/lib/tracker/rules";
import { fmtDate } from "@/lib/tracker/server";

const STATUS_TONE: Record<Status, string> = {
  NEW: "info", NEED_INFO: "warn", ACCEPTED: "", IN_PROGRESS: "", FIX_READY: "lime", REOPENED: "bad", VERIFIED: "good", DEFERRED: "", WONT_FIX: "",
};
const SEV_TONE: Record<Severity, string> = { BLOCKER: "bad", MAJOR: "warn", MINOR: "", SUGGESTION: "" };

export const StatusPill = ({ s }: { s: Status }) => <span className={`pill ${STATUS_TONE[s]}`}>{STATUS_LABEL[s]}</span>;
export const SevPill = ({ s }: { s: Severity }) => <span className={`pill ${SEV_TONE[s]}`}>{SEVERITY_LABEL[s]}</span>;
export const AreaPill = ({ a }: { a: Area }) => <span className="pill">{AREA_LABEL[a]}</span>;

const fileUrl = (id: string) => `/api/testing/files/${id}`;
const kb = (n: number) => (n > 1024 * 1024 ? `${(n / 1024 / 1024).toFixed(1)} MB` : `${Math.max(1, Math.round(n / 1024))} KB`);

export function Files({ files }: { files: TrackFile[] }) {
  if (!files.length) return null;
  return (
    <div className="files">
      {files.map((f) =>
        f.mime.startsWith("image/") ? (
          <a key={f.id} className="thumb" href={fileUrl(f.id)} target="_blank" rel="noreferrer" title={f.originalName}>
            <img src={fileUrl(f.id)} alt={f.originalName} loading="lazy" />
          </a>
        ) : (
          <a key={f.id} className="doc" href={f.mime.startsWith("video/") ? fileUrl(f.id) : `${fileUrl(f.id)}?download`} target="_blank" rel="noreferrer">
            {f.kind === "ATTACHMENT" ? "" : `${f.kind === "APK" ? "APK" : f.kind === "GERBER" ? "Gerber" : "Firmware"}: `}
            {f.originalName} <span className="muted">({kb(f.sizeBytes)})</span>
          </a>
        ),
      )}
    </div>
  );
}

type Ev = {
  id: string;
  kind: string;
  text: string | null;
  fromStatus: Status | null;
  toStatus: Status | null;
  createdAt: Date;
  actor: { name: string };
  files: TrackFile[];
  build?: { id: string; number: number } | null;
};

const KIND_WORD: Record<string, string> = { created: "reported this", comment: "commented", edit: "edited", verdict: "tested", build: "posted a build", signoff: "sign-off", release: "release", status: "" };

export function Timeline({ events }: { events: Ev[] }) {
  if (!events.length) return <p className="muted small">Nothing yet.</p>;
  return (
    <div className="timeline">
      {events.map((e) => (
        <div key={e.id} className={`ev ${e.kind}`}>
          <div className="h">
            <b>{e.actor.name}</b>
            {KIND_WORD[e.kind] && <span>{KIND_WORD[e.kind]}</span>}
            {e.toStatus && (
              <>
                {e.fromStatus && <StatusPill s={e.fromStatus} />}
                {e.fromStatus && <span>→</span>}
                <StatusPill s={e.toStatus} />
              </>
            )}
            <span>· {fmtDate(e.createdAt)}</span>
          </div>
          {e.text && <div className="t">{e.text}</div>}
          {e.files.length > 0 && <div style={{ marginTop: 6 }}><Files files={e.files} /></div>}
        </div>
      ))}
    </div>
  );
}

export function NotMember({ isAdmin }: { isAdmin: boolean }) {
  return (
    <div className="card">
      <h1>Product testing</h1>
      <p className="muted">
        This account is not on the testing team yet.{" "}
        {isAdmin ? <a href="/dashboard/testing/settings">Add yourself and Avinash on the Team page.</a> : "Ask an admin to add you."}
      </p>
    </div>
  );
}
