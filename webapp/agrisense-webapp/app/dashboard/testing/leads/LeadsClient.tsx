"use client";
import { useMemo, useState } from "react";
import { useRouter } from "next/navigation";

type Note = { id: string; author: string; text: string; createdAt: string };
type Lead = {
  id: string; platform: string; handle: string; name: string | null; kind: "BUYER" | "POLYHOUSE" | "PARTNER";
  heat: "HOT" | "WARM" | "COLD"; said: string; postUrl: string | null; language: string | null; location: string | null;
  products: string | null; phone: string | null; nextStep: string | null; suggestedReply: string | null;
  status: Status; foundBy: string; createdAt: string; notes: Note[];
};
type Status = "NEW" | "CONTACTED" | "TALKING" | "WON" | "LOST" | "SKIPPED";

const STATUS: { s: Status; label: string }[] = [
  { s: "NEW", label: "To contact" },
  { s: "CONTACTED", label: "Contacted" },
  { s: "TALKING", label: "Talking" },
  { s: "WON", label: "Order won" },
  { s: "LOST", label: "Not interested" },
  { s: "SKIPPED", label: "Skipped" },
];
const LABEL = Object.fromEntries(STATUS.map((x) => [x.s, x.label])) as Record<Status, string>;
const KIND = { BUYER: "Buyer", POLYHOUSE: "Polyhouse", PARTNER: "Partner" } as const;
const HEAT_PILL = { HOT: "bad", WARM: "warn", COLD: "" } as const;

function profileUrl(l: Lead) {
  return l.platform === "Instagram" ? `https://www.instagram.com/${l.handle.replace(/^@/, "")}/` : null;
}
function when(iso: string) {
  return new Date(iso).toLocaleString("en-IN", { day: "numeric", month: "short", hour: "2-digit", minute: "2-digit" });
}

export default function LeadsClient({ leads, me }: { leads: Lead[]; me: string }) {
  const [tab, setTab] = useState<Status | "ALL">("NEW");
  const [kind, setKind] = useState<Lead["kind"] | "ALL">("ALL");
  const counts = useMemo(() => Object.fromEntries(STATUS.map((x) => [x.s, leads.filter((l) => l.status === x.s).length])), [leads]);
  const heatOrder = { HOT: 0, WARM: 1, COLD: 2 };
  const shown = leads
    .filter((l) => (tab === "ALL" || l.status === tab) && (kind === "ALL" || l.kind === kind))
    .sort((a, b) => heatOrder[a.heat] - heatOrder[b.heat]);

  return (
    <>
      <div className="filters" role="tablist" aria-label="Status">
        {STATUS.map((x) => (
          <a key={x.s} href="#" className={tab === x.s ? "on" : ""} onClick={(e) => { e.preventDefault(); setTab(x.s); }}>
            {x.label} ({counts[x.s] ?? 0})
          </a>
        ))}
        <a href="#" className={tab === "ALL" ? "on" : ""} onClick={(e) => { e.preventDefault(); setTab("ALL"); }}>All ({leads.length})</a>
      </div>
      <div className="filters" aria-label="Kind">
        {(["ALL", "BUYER", "POLYHOUSE", "PARTNER"] as const).map((k) => (
          <a key={k} href="#" className={kind === k ? "on" : ""} onClick={(e) => { e.preventDefault(); setKind(k); }}>
            {k === "ALL" ? "All kinds" : KIND[k]}
          </a>
        ))}
      </div>
      {shown.length === 0 && <p className="muted">Nothing here.</p>}
      <div className="grid">
        {shown.map((l) => <LeadCard key={l.id} l={l} me={me} />)}
      </div>
    </>
  );
}

function LeadCard({ l, me }: { l: Lead; me: string }) {
  const router = useRouter();
  const [busy, setBusy] = useState(false);
  const [note, setNote] = useState("");
  const [copied, setCopied] = useState(false);
  const [error, setError] = useState("");

  async function call(url: string, method: string, body: object) {
    setBusy(true);
    setError("");
    const r = await fetch(url, { method, headers: { "content-type": "application/json" }, body: JSON.stringify(body) });
    setBusy(false);
    if (!r.ok) { setError((await r.json().catch(() => ({}))).error ?? "Could not save"); return false; }
    router.refresh();
    return true;
  }
  const prof = profileUrl(l);

  return (
    <article className="card lead">
      <div className="row between">
        <div className="row">
          <span className={`pill ${HEAT_PILL[l.heat]}`}>{l.heat === "HOT" ? "Hot" : l.heat === "WARM" ? "Warm" : "Cold"}</span>
          <span className="pill info">{KIND[l.kind]}</span>
          <h3>{prof ? <a href={prof} target="_blank" rel="noreferrer">@{l.handle.replace(/^@/, "")}</a> : l.handle}{l.name ? ` · ${l.name}` : ""}</h3>
        </div>
        <span className="pill">{LABEL[l.status]}</span>
      </div>

      <blockquote className="said">{l.said}</blockquote>
      <div className="row small muted">
        {l.postUrl && <a href={l.postUrl} target="_blank" rel="noreferrer">Open post ↗</a>}
        {l.location && <span>📍 {l.location}</span>}
        {l.language && <span>🗣 {l.language}</span>}
        {l.products && <span>📦 {l.products}</span>}
        {l.phone && <a href={`tel:${l.phone.replace(/\s/g, "")}`}>📞 {l.phone}</a>}
      </div>

      {l.nextStep && (
        <div className="next">
          <b>What to do</b>
          <p>{l.nextStep}</p>
        </div>
      )}
      {l.suggestedReply && (
        <div className="reply">
          <div className="row between"><b>Reply to copy</b>
            <button type="button" className="btn ghost small" onClick={async () => { await navigator.clipboard.writeText(l.suggestedReply!); setCopied(true); setTimeout(() => setCopied(false), 1500); }}>
              {copied ? "Copied ✓" : "Copy"}
            </button>
          </div>
          <p>{l.suggestedReply}</p>
        </div>
      )}

      <div className="choice" aria-label="Status">
        {STATUS.map((x) => (
          <button key={x.s} type="button" aria-pressed={l.status === x.s} disabled={busy || l.status === x.s}
            onClick={() => call(`/api/leads/${l.id}`, "PATCH", { status: x.s })}>
            {x.label}
          </button>
        ))}
      </div>

      {l.notes.length > 0 && (
        <ul className="notes">
          {l.notes.map((n) => (
            <li key={n.id} className={n.author === "Claude" ? "claude" : ""}>
              <span className="small muted"><b>{n.author}</b> · {when(n.createdAt)}</span>
              <p>{n.text}</p>
            </li>
          ))}
        </ul>
      )}
      <form onSubmit={async (e) => { e.preventDefault(); if (await call(`/api/leads/${l.id}/notes`, "POST", { text: note })) setNote(""); }}>
        <textarea value={note} onChange={(e) => setNote(e.target.value)} placeholder={`Note as ${me}: what they said, what they need, what you did`} rows={2} />
        <div className="row">
          <button className="btn small" disabled={busy || !note.trim()}>Add note</button>
          {error && <span className="small" style={{ color: "var(--bad)" }}>{error}</span>}
        </div>
      </form>
      <p className="small muted">Found by {l.foundBy} · {when(l.createdAt)}</p>
    </article>
  );
}
