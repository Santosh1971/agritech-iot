"use client";

import Link from "next/link";
import { useMemo, useState } from "react";
import type { LibraryIndex, LibraryItem } from "@/lib/library";

// Admins' document library: presentations, proposals and videos grouped by
// category, with finished work under "Archive". Files are served by
// /api/admin/library/file/<name>; "Live on claude.ai" opens the editable source.

const C = {
  bg: "#F5F7F2", panel: "#FFFFFF", ink: "#12372A", ink2: "#4B6358", line: "#D3E0D8",
  teal: "#1F7A5F", sage: "#E6F0EA", lime: "#B5D63C", ochre: "#C9822B",
};

const fileUrl = (name: string, download = false) =>
  `/api/admin/library/file/${encodeURIComponent(name)}${download ? "?download=1" : ""}`;

const fmtDate = (d: string) => {
  const t = Date.parse(d);
  return Number.isNaN(t) ? d : new Date(t).toLocaleDateString("en-IN", { day: "numeric", month: "short", year: "numeric" });
};

const KIND_LABEL: Record<LibraryItem["kind"], string> = { pdf: "PDF", video: "Video", link: "Link" };

function Card({ item }: { item: LibraryItem }) {
  const btn = { padding: "7px 12px", borderRadius: 8, fontSize: 14, fontWeight: 700, textDecoration: "none", border: `1.5px solid ${C.teal}` } as const;
  return (
    <div style={{ background: C.panel, border: `1px solid ${C.line}`, borderRadius: 12, padding: 14, display: "grid", gap: 8, alignContent: "start" }}>
      <div style={{ display: "flex", gap: 8, alignItems: "center", flexWrap: "wrap" }}>
        <span style={{ fontSize: 12, fontWeight: 700, letterSpacing: ".06em", textTransform: "uppercase", color: item.kind === "video" ? C.ochre : C.teal }}>{KIND_LABEL[item.kind]}</span>
        {item.lang && <span style={{ fontSize: 12, fontWeight: 700, padding: "1px 8px", borderRadius: 99, background: C.sage, color: C.ink }}>{item.lang}</span>}
        <span style={{ fontSize: 12, color: C.ink2, marginLeft: "auto" }}>{fmtDate(item.date)}</span>
      </div>
      <div style={{ fontWeight: 700, fontSize: 16, color: C.ink, lineHeight: 1.3 }}>{item.title}</div>
      {item.note && <div style={{ fontSize: 14, color: C.ink2, lineHeight: 1.4 }}>{item.note}</div>}
      {item.kind === "video" && item.file && (
        <video controls playsInline preload="none" src={fileUrl(item.file)} style={{ width: "100%", maxHeight: 360, borderRadius: 8, background: "#000" }} />
      )}
      <div style={{ display: "flex", gap: 8, flexWrap: "wrap", marginTop: 2 }}>
        {item.kind === "pdf" && item.file && (
          <a href={fileUrl(item.file)} target="_blank" rel="noopener" style={{ ...btn, background: C.teal, color: "#fff" }}>Open</a>
        )}
        {item.file && (
          <a href={fileUrl(item.file, true)} style={{ ...btn, color: C.teal }}>Download</a>
        )}
        {item.kind === "link" && item.href && (
          item.href.startsWith("/") ? (
            <Link href={item.href} style={{ ...btn, background: C.teal, color: "#fff" }}>Open</Link>
          ) : (
            <a href={item.href} target="_blank" rel="noopener" style={{ ...btn, background: C.teal, color: "#fff" }}>Open</a>
          )
        )}
        {item.live && (
          <a href={item.live} target="_blank" rel="noopener" style={{ ...btn, border: "none", color: C.ink2, fontWeight: 600, padding: "7px 4px" }}>Live on claude.ai ↗</a>
        )}
      </div>
    </div>
  );
}

export default function DocLibrary({ index }: { index: LibraryIndex }) {
  const [view, setView] = useState<"current" | "archive">("current");
  const [q, setQ] = useState("");

  const sections = useMemo(() => {
    const needle = q.trim().toLowerCase();
    return index.categories
      .filter((c) => (view === "archive" ? c.archive : !c.archive))
      .map((c) => ({
        cat: c,
        items: index.items
          .filter((i) => i.cat === c.id)
          .filter((i) => !needle || `${i.title} ${i.note ?? ""} ${i.lang ?? ""}`.toLowerCase().includes(needle))
          .sort((a, b) => b.date.localeCompare(a.date)),
      }))
      .filter((s) => s.items.length);
  }, [index, view, q]);

  const tab = (v: "current" | "archive", label: string) => (
    <button
      type="button"
      onClick={() => setView(v)}
      style={{ font: "inherit", fontWeight: 700, fontSize: 15, padding: "8px 16px", borderRadius: 99, cursor: "pointer", border: `1.5px solid ${C.teal}`, background: view === v ? C.teal : "transparent", color: view === v ? "#fff" : C.teal }}
    >
      {label}
    </button>
  );

  return (
    <main style={{ background: C.bg, minHeight: "100vh", fontFamily: "'Nunito Sans', system-ui, sans-serif", color: C.ink }}>
      <div style={{ maxWidth: 1100, margin: "0 auto", padding: "24px 16px 60px", display: "grid", gap: 22 }}>
        <header style={{ display: "grid", gap: 8, borderBottom: `3px solid ${C.lime}`, paddingBottom: 14 }}>
          <Link href="/dashboard" style={{ fontSize: 14, color: C.teal }}>← Dashboard</Link>
          <h1 style={{ margin: 0, fontSize: 30 }}>Documents &amp; videos</h1>
          <p style={{ margin: 0, color: C.ink2 }}>
            Proposals, presentations and product videos for Santosh and Avinash. {index.updated && <>Updated {fmtDate(index.updated)}.</>}
          </p>
        </header>

        <div style={{ display: "flex", gap: 10, flexWrap: "wrap", alignItems: "center" }}>
          {tab("current", "Current")}
          {tab("archive", "Archive")}
          <input
            id="library-search"
            value={q}
            onChange={(e) => setQ(e.target.value)}
            placeholder="Search titles"
            style={{ font: "inherit", flex: "1 1 200px", maxWidth: 320, marginLeft: "auto", padding: "8px 12px", borderRadius: 8, border: `1.5px solid ${C.line}`, background: C.panel, color: C.ink }}
          />
        </div>

        {sections.length === 0 && <p style={{ color: C.ink2 }}>Nothing here yet.</p>}

        {sections.map(({ cat, items }) => (
          <section key={cat.id} style={{ display: "grid", gap: 12 }}>
            <div style={{ display: "flex", gap: 12, alignItems: "baseline", flexWrap: "wrap" }}>
              <h2 style={{ margin: 0, fontSize: 20 }}>{cat.name}</h2>
              {cat.note && <span style={{ fontSize: 14, color: C.ink2 }}>{cat.note}</span>}
              <span style={{ fontSize: 13, color: C.ink2, marginLeft: "auto" }}>{items.length} item{items.length === 1 ? "" : "s"}</span>
            </div>
            <div style={{ display: "grid", gridTemplateColumns: "repeat(auto-fill, minmax(min(300px, 100%), 1fr))", gap: 12 }}>
              {items.map((it) => (
                <Card key={it.id} item={it} />
              ))}
            </div>
          </section>
        ))}

        <p style={{ fontSize: 13, color: C.ink2 }}>
          “Live on claude.ai” opens the editable version; it works only for people the document is shared with. The PDFs and videos here work for every admin.
        </p>
      </div>
    </main>
  );
}
