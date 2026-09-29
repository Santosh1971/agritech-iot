"use client";

import Link from "next/link";
import { useEffect, useState } from "react";

// One place to reach every workshop page: website pages open inside the
// main area; planning pages hosted on claude.ai open in a new tab.

type Topic =
  | { id: string; title: string; note?: string; kind: "page"; href: string }
  | { id: string; title: string; note?: string; kind: "external"; href: string }
  | { id: string; title: string; note?: string; kind: "responses"; survey: SurveyId }
  | { id: string; title: string; note?: string; kind: "labadmin" };

type SurveyId = "gpsioam-2026" | "lab-idea";

type Group = { name: string; topics: Topic[] };

const GROUPS: Group[] = [
  {
    name: "GPSIOAM workshop · 1 Oct 2026",
    topics: [
      { id: "overview", title: "Workshop overview (AGR 322)", note: "Shared with the professor", kind: "page", href: "/workshop/agr322" },
      { id: "practical", title: "Practical session (earlier plan)", note: "English", kind: "page", href: "/workshop/practical" },
      { id: "practical-kn", title: "Practical session (earlier plan)", note: "ಕನ್ನಡ", kind: "page", href: "/workshop/practical/kn" },
      { id: "practical-hi", title: "Practical session (earlier plan)", note: "हिंदी", kind: "page", href: "/workshop/practical/hi" },
      { id: "labs", title: "Farm IoT labs", note: "Wokwi labs 1–4", kind: "page", href: "/workshop" },
    ],
  },
  {
    name: "Hands-on tools",
    topics: [
      { id: "flash", title: "ESP32 flasher", note: "Ready programs + build from Claude", kind: "page", href: "/workshop/flash" },
      { id: "mqtt", title: "MQTT live viewer", note: "Program 9 from anywhere", kind: "page", href: "/workshop/mqtt" },
      { id: "app-maker", title: "Make my farm app", note: "Students name and install their own app", kind: "page", href: "/workshop/app" },
      { id: "lab-station", title: "Lab Station (college unit)", note: "Status · remote update · history", kind: "labadmin" },
      { id: "station-history", title: "Lab Station history", note: "Graph page (college can see it)", kind: "page", href: "/workshop/station" },
      { id: "lab-students", title: "IoT lab idea: student version", note: "No costs · for the 16:00 session", kind: "page", href: "/workshop/lab" },
    ],
  },
  {
    name: "Student questionnaire",
    topics: [
      { id: "survey", title: "Questionnaire form", note: "What students fill in", kind: "page", href: "/workshop/survey" },
      { id: "insights", title: "What students told us", note: "Anonymised summary", kind: "page", href: "/workshop/insights" },
      { id: "responses", title: "Individual responses", note: "Private, with names", kind: "responses", survey: "gpsioam-2026" },
      { id: "lab-responses", title: "Lab idea feedback responses", note: "End of workshop · private", kind: "responses", survey: "lab-idea" },
    ],
  },
  {
    name: "Planning (Claude pages)",
    topics: [
      { id: "plan", title: "Workshop plan & prep board", kind: "external", href: "https://claude.ai/artifact/LxUjbPrZEWeVsqq9KVibsr" },
      { id: "deck", title: "Talk slides", kind: "external", href: "https://claude.ai/artifact/Xkw873R9jmvASsLWURGwYk" },
      { id: "call-sheet", title: "Student call sheet", kind: "external", href: "https://claude.ai/artifact/Xy15bscG74huzeVMJoM54j" },
      { id: "lab-proposal", title: "Agri IoT lab proposal", note: "Full draft with costs · for management", kind: "external", href: "https://claude.ai/artifact/TKMa2XFG7c795dvSFcipBE" },
    ],
  },
];

const ALL = GROUPS.flatMap((g) => g.topics);
const FIRST = ALL.find((t) => t.kind !== "external")!;

const C = {
  bg: "#F3F6F1", panel: "#FFFFFF", ink: "#17231C", ink2: "#4A5A50", line: "#CBD6CC",
  green: "#0E5E43", greenSoft: "#E3EFE8", copper: "#A5621F",
};

export default function WorkshopLibrary() {
  const [current, setCurrent] = useState<string>(FIRST.id);

  // Remember the open topic in the address bar (#insights) so a refresh or a
  // bookmark returns to it.
  useEffect(() => {
    const fromHash = window.location.hash.slice(1);
    if (ALL.some((t) => t.id === fromHash && t.kind !== "external")) setCurrent(fromHash);
  }, []);

  function open(t: Topic) {
    if (t.kind === "external") return;
    setCurrent(t.id);
    history.replaceState(null, "", `#${t.id}`);
  }

  const topic = ALL.find((t) => t.id === current) ?? FIRST;

  return (
    <div style={{ display: "flex", minHeight: "100vh", background: C.bg, color: C.ink, fontFamily: "system-ui, sans-serif", flexWrap: "wrap" }}>
      <nav style={{ width: 280, flex: "0 0 280px", maxWidth: "100%", borderRight: `1px solid ${C.line}`, background: C.panel, padding: "18px 12px", display: "flex", flexDirection: "column", gap: 18 }}>
        <div style={{ padding: "0 8px" }}>
          <Link href="/dashboard" style={{ fontSize: 13, color: C.ink2, textDecoration: "none" }}>← Dashboard</Link>
          <h1 style={{ fontSize: 20, margin: "6px 0 0" }}>Workshop library</h1>
        </div>
        {GROUPS.map((g) => (
          <div key={g.name} style={{ display: "flex", flexDirection: "column", gap: 2 }}>
            <div style={{ fontSize: 11, letterSpacing: ".08em", textTransform: "uppercase", color: C.copper, fontWeight: 700, padding: "0 8px 4px" }}>{g.name}</div>
            {g.topics.map((t) => {
              const active = t.id === topic.id;
              const inner = (
                <>
                  <span style={{ fontWeight: active ? 700 : 500 }}>{t.title}{t.kind === "external" ? " ↗" : ""}</span>
                  {t.note && <span style={{ fontSize: 12, color: active ? C.green : C.ink2 }}>{t.note}</span>}
                </>
              );
              const style = {
                display: "flex", flexDirection: "column" as const, gap: 1, textAlign: "left" as const, padding: "7px 10px",
                borderRadius: 8, border: 0, cursor: "pointer", fontSize: 14, textDecoration: "none", color: C.ink,
                background: active ? C.greenSoft : "transparent", borderLeft: `3px solid ${active ? C.green : "transparent"}`,
              };
              return t.kind === "external" ? (
                <a key={t.id} href={t.href} target="_blank" rel="noopener noreferrer" style={style}>{inner}</a>
              ) : (
                <button key={t.id} type="button" onClick={() => open(t)} style={style}>{inner}</button>
              );
            })}
          </div>
        ))}
      </nav>

      <main style={{ flex: "1 1 480px", minWidth: 0, display: "flex", flexDirection: "column" }}>
        <div style={{ display: "flex", justifyContent: "space-between", alignItems: "center", gap: 12, padding: "10px 16px", borderBottom: `1px solid ${C.line}`, background: C.panel }}>
          <strong>{topic.title}{topic.note ? ` · ${topic.note}` : ""}</strong>
          {topic.kind === "page" && (
            <span style={{ display: "flex", gap: 14, fontSize: 13 }}>
              <a href={topic.href} target="_blank" rel="noopener noreferrer" style={{ color: C.green }}>Open in new tab ↗</a>
              <CopyLink href={topic.href} />
            </span>
          )}
        </div>
        {topic.kind === "page" && (
          <iframe key={topic.id} src={topic.href} title={topic.title} style={{ flex: 1, width: "100%", minHeight: "80vh", border: 0, background: "#fff" }} />
        )}
        {topic.kind === "responses" && <Responses key={topic.survey} survey={topic.survey} />}
        {topic.kind === "labadmin" && <LabAdmin />}
      </main>
    </div>
  );
}

function CopyLink({ href }: { href: string }) {
  const [done, setDone] = useState(false);
  return (
    <button
      type="button"
      onClick={() => {
        navigator.clipboard?.writeText(window.location.origin + href).then(() => { setDone(true); setTimeout(() => setDone(false), 1500); });
      }}
      style={{ border: 0, background: "none", color: C.green, cursor: "pointer", fontSize: 13, padding: 0 }}
    >
      {done ? "Link copied" : "Copy link"}
    </button>
  );
}

type Entry = { at: string; survey?: string; answers: Record<string, string | string[] | number> };

const LAB_COLUMNS: [string, string][] = [
  ["name", "Name"], ["year", "Year"], ["would_use", "Would use"], ["how_often", "How often"],
  ["interests", "Interests"], ["build_first", "Would build first"], ["teach_juniors", "Teach juniors"],
  ["blockers", "What would stop them"], ["worth_it", "What makes it worth it"], ["expect", "Expectations"],
  ["rate_today", "Today 1–5"], ["liked", "Liked most"], ["difficult", "Difficult"], ["startup_interest", "Startup 1–5 now"],
];

const COLUMNS: [string, string][] = [
  ["name", "Name"], ["district", "Home"], ["family_farms", "Family farms"], ["crops", "Crops"],
  ["losses", "Biggest losses"], ["loss_story", "A loss they saw"], ["th_why", "Why temp & humidity"],
  ["spray_decision", "Spray decision"], ["pay_th", "Pay for disease alert"], ["irrigation", "Irrigation"],
  ["power", "Power & pump"], ["pump_problems", "Pump problems"], ["pay_pump", "Pay for pump control"],
  ["fyllo", "Fyllo"], ["hurdles", "Hurdles"], ["trust", "Trusts"], ["make_yes", "What makes a yes"],
  ["business_idea", "Business idea"], ["who_pays", "Who pays"], ["after_grad", "After graduation"],
  ["startup_interest", "Startup 1–5"], ["want_thursday", "Wants from Thursday"], ["problem", "One problem"],
  ["laptop", "Laptop"], ["phone", "Phone"], ["project", "Project"],
];

function Responses({ survey }: { survey: SurveyId }) {
  const [entries, setEntries] = useState<Entry[] | null>(null);
  const [error, setError] = useState("");

  useEffect(() => {
    fetch("/api/admin/workshop-survey")
      .then((r) => (r.ok ? r.json() : Promise.reject(new Error(`HTTP ${r.status}`))))
      .then((j) => setEntries((j.entries as Entry[]).filter((e) => (e.survey ?? "gpsioam-2026") === survey)))
      .catch((e) => setError(e.message));
  }, []);

  if (error) return <p style={{ padding: 16 }}>Could not load responses: {error}</p>;
  if (!entries) return <p style={{ padding: 16 }}>Loading…</p>;

  const cols = survey === "lab-idea" ? LAB_COLUMNS : COLUMNS;
  const show = (v: unknown) => (Array.isArray(v) ? v.join(", ") : v == null ? "" : String(v));
  return (
    <div style={{ padding: 16, display: "flex", flexDirection: "column", gap: 10 }}>
      <p style={{ margin: 0, color: C.ink2 }}>{entries.length} responses. Private: do not share individual answers; use the anonymised summary instead.</p>
      <div style={{ overflowX: "auto", border: `1px solid ${C.line}`, borderRadius: 10, background: C.panel }}>
        <table style={{ borderCollapse: "collapse", fontSize: 13, minWidth: 1600 }}>
          <thead>
            <tr>
              <th style={th}>Submitted</th>
              {cols.map(([, label]) => <th key={label} style={th}>{label}</th>)}
            </tr>
          </thead>
          <tbody>
            {entries.map((e, i) => (
              <tr key={i}>
                <td style={td}>{new Date(e.at).toLocaleString("en-IN", { dateStyle: "short", timeStyle: "short" })}</td>
                {cols.map(([key]) => <td key={key} style={td}>{show(e.answers[key])}</td>)}
              </tr>
            ))}
          </tbody>
        </table>
      </div>
    </div>
  );
}

const th: React.CSSProperties = { textAlign: "left", padding: "8px 10px", background: "#E8EEE5", borderBottom: "1px solid #CBD6CC", position: "sticky", top: 0, whiteSpace: "nowrap" };
const td: React.CSSProperties = { padding: "8px 10px", borderBottom: "1px solid #CBD6CC", verticalAlign: "top", maxWidth: 260 };

type LabDevice = {
  device: string; version?: string; ip?: string; ssid?: string; rssi?: number; lastSeen?: string; program?: string;
  pending?: { id: string; label: string; size: number; queuedAt: string } | null;
};

// Lab Station control: status of each gifted unit, remote firmware update, history graph.
function LabAdmin() {
  const [data, setData] = useState<{ latestFirmware: string | null; devices: LabDevice[] } | null>(null);
  const [error, setError] = useState("");
  const [busy, setBusy] = useState("");
  const [graph, setGraph] = useState<string | null>(null);

  const load = () =>
    fetch("/api/admin/lab")
      .then((r) => (r.ok ? r.json() : Promise.reject(new Error(`HTTP ${r.status}`))))
      .then((j) => { setData(j); if (!graph && j.devices[0]) setGraph(j.devices[0].device); })
      .catch((e) => setError(e.message));
  useEffect(() => { load(); const t = setInterval(load, 15000); return () => clearInterval(t); }, []); // eslint-disable-line react-hooks/exhaustive-deps

  async function act(device: string, action: "update" | "cancel") {
    setBusy(device);
    const r = await fetch("/api/admin/lab", { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify({ device, action }) });
    if (!r.ok) setError((await r.json().catch(() => ({}))).error || `HTTP ${r.status}`);
    setBusy("");
    load();
  }

  if (error) return <p style={{ padding: 16 }}>Could not load: {error}</p>;
  if (!data) return <p style={{ padding: 16 }}>Loading…</p>;
  const ago = (iso?: string) => {
    if (!iso) return "never";
    const m = Math.round((Date.now() - new Date(iso).getTime()) / 60000);
    return m < 1 ? "just now" : m < 60 ? `${m} min ago` : m < 1440 ? `${Math.round(m / 60)} h ago` : `${Math.round(m / 1440)} days ago`;
  };
  return (
    <div style={{ padding: 16, display: "flex", flexDirection: "column", gap: 12 }}>
      <p style={{ margin: 0, color: C.ink2 }}>
        Latest Lab Station firmware on the server: <b>{data.latestFirmware ?? "not built yet"}</b>. A queued update is installed the next time the unit checks in (every minute while online).
      </p>
      {data.devices.length === 0 && <p style={{ margin: 0 }}>No Lab Station has checked in yet. Flash the “Lab Station” ready program and put it on WiFi.</p>}
      {data.devices.map((d) => {
        const online = d.lastSeen && Date.now() - new Date(d.lastSeen).getTime() < 3 * 60000;
        return (
          <div key={d.device} style={{ background: C.panel, border: `1px solid ${C.line}`, borderRadius: 12, padding: 14, display: "flex", flexWrap: "wrap", gap: "8px 24px", alignItems: "center" }}>
            <div>
              <div style={{ fontWeight: 700, fontSize: 17 }}>{d.device} <span style={{ fontSize: 12, padding: "2px 8px", borderRadius: 99, background: online ? C.green : "#E3E7E1", color: online ? "#fff" : C.ink2 }}>{online ? "online" : "offline"}</span></div>
              <div style={{ fontSize: 13, color: C.ink2 }}>
                Running {d.program ?? "?"} {d.version ?? ""} · last seen {ago(d.lastSeen)} · WiFi {d.ssid ?? "?"} {d.rssi != null ? `(${d.rssi} dBm)` : ""} · IP {d.ip ?? "?"}
              </div>
              {d.pending && <div style={{ fontSize: 13, color: C.copper }}>Update waiting: {d.pending.label} ({Math.round(d.pending.size / 1024)} KB), queued {ago(d.pending.queuedAt)}</div>}
            </div>
            <div style={{ display: "flex", gap: 8, marginLeft: "auto" }}>
              <button type="button" disabled={busy === d.device || !data.latestFirmware} onClick={() => act(d.device, "update")}
                style={{ padding: "8px 12px", borderRadius: 8, border: 0, background: C.green, color: "#fff", fontWeight: 700, cursor: "pointer" }}>
                Update to Lab Station {data.latestFirmware ?? ""}
              </button>
              {d.pending && <button type="button" onClick={() => act(d.device, "cancel")} style={{ padding: "8px 12px", borderRadius: 8, border: `1px solid ${C.line}`, background: "transparent", cursor: "pointer" }}>Cancel update</button>}
              <button type="button" onClick={() => setGraph(d.device)} style={{ padding: "8px 12px", borderRadius: 8, border: `1px solid ${C.line}`, background: "transparent", cursor: "pointer" }}>History</button>
            </div>
          </div>
        );
      })}
      {graph && <iframe key={graph} src={`/workshop/station?d=${graph}`} title="Lab Station history" style={{ width: "100%", height: 1100, border: 0, borderRadius: 12, background: "#fff" }} />}
    </div>
  );
}
