"use client";

import Link from "next/link";
import { useEffect, useState } from "react";

// One place to reach every workshop page: website pages open inside the
// main area; planning pages hosted on claude.ai open in a new tab.

type Topic =
  | { id: string; title: string; note?: string; kind: "page"; href: string }
  | { id: string; title: string; note?: string; kind: "external"; href: string }
  | { id: string; title: string; note?: string; kind: "responses" };

type Group = { name: string; topics: Topic[] };

const GROUPS: Group[] = [
  {
    name: "GPSIOAM workshop · 1 Oct 2026",
    topics: [
      { id: "overview", title: "Workshop overview (AGR 322)", note: "Shared with the professor", kind: "page", href: "/workshop/agr322" },
      { id: "practical", title: "Practical session", note: "English", kind: "page", href: "/workshop/practical" },
      { id: "practical-kn", title: "Practical session", note: "ಕನ್ನಡ", kind: "page", href: "/workshop/practical/kn" },
      { id: "practical-hi", title: "Practical session", note: "हिंदी", kind: "page", href: "/workshop/practical/hi" },
      { id: "labs", title: "Farm IoT labs", note: "Wokwi labs 1–4", kind: "page", href: "/workshop" },
    ],
  },
  {
    name: "Hands-on tools",
    topics: [
      { id: "flash", title: "ESP32 flasher", note: "Ready programs + build from Claude", kind: "page", href: "/workshop/flash" },
      { id: "mqtt", title: "MQTT live viewer", note: "Program 7 from anywhere", kind: "page", href: "/workshop/mqtt" },
    ],
  },
  {
    name: "Student questionnaire",
    topics: [
      { id: "survey", title: "Questionnaire form", note: "What students fill in", kind: "page", href: "/workshop/survey" },
      { id: "insights", title: "What students told us", note: "Anonymised summary", kind: "page", href: "/workshop/insights" },
      { id: "responses", title: "Individual responses", note: "Private, with names", kind: "responses" },
    ],
  },
  {
    name: "Planning (Claude pages)",
    topics: [
      { id: "plan", title: "Workshop plan & prep board", kind: "external", href: "https://claude.ai/artifact/LxUjbPrZEWeVsqq9KVibsr" },
      { id: "deck", title: "Talk slides", kind: "external", href: "https://claude.ai/artifact/Xkw873R9jmvASsLWURGwYk" },
      { id: "call-sheet", title: "Student call sheet", kind: "external", href: "https://claude.ai/artifact/Xy15bscG74huzeVMJoM54j" },
      { id: "lab-proposal", title: "Agri IoT lab proposal", note: "Draft", kind: "external", href: "https://claude.ai/artifact/TKMa2XFG7c795dvSFcipBE" },
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
        {topic.kind === "responses" && <Responses />}
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

type Entry = { at: string; answers: Record<string, string | string[] | number> };

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

function Responses() {
  const [entries, setEntries] = useState<Entry[] | null>(null);
  const [error, setError] = useState("");

  useEffect(() => {
    fetch("/api/admin/workshop-survey")
      .then((r) => (r.ok ? r.json() : Promise.reject(new Error(`HTTP ${r.status}`))))
      .then((j) => setEntries(j.entries))
      .catch((e) => setError(e.message));
  }, []);

  if (error) return <p style={{ padding: 16 }}>Could not load responses: {error}</p>;
  if (!entries) return <p style={{ padding: 16 }}>Loading…</p>;

  const show = (v: unknown) => (Array.isArray(v) ? v.join(", ") : v == null ? "" : String(v));
  return (
    <div style={{ padding: 16, display: "flex", flexDirection: "column", gap: 10 }}>
      <p style={{ margin: 0, color: C.ink2 }}>{entries.length} responses. Private: do not share individual answers; use the anonymised summary instead.</p>
      <div style={{ overflowX: "auto", border: `1px solid ${C.line}`, borderRadius: 10, background: C.panel }}>
        <table style={{ borderCollapse: "collapse", fontSize: 13, minWidth: 1600 }}>
          <thead>
            <tr>
              <th style={th}>Submitted</th>
              {COLUMNS.map(([, label]) => <th key={label} style={th}>{label}</th>)}
            </tr>
          </thead>
          <tbody>
            {entries.map((e, i) => (
              <tr key={i}>
                <td style={td}>{new Date(e.at).toLocaleString("en-IN", { dateStyle: "short", timeStyle: "short" })}</td>
                {COLUMNS.map(([key]) => <td key={key} style={td}>{show(e.answers[key])}</td>)}
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
