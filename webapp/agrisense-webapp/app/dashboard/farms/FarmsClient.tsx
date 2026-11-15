"use client";

import { useCallback, useEffect, useState } from "react";

type Dev = { id: string; deviceId: string; product: string; name: string; lastSeenAt: string | null; lastStatus: Record<string, unknown> | null };
type Member = { user: { id: string; email: string; name: string; role: string } };
type Farm = { id: string; name: string; members: Member[]; devices: Dev[] };

const box: React.CSSProperties = { border: "1px solid #ddd", borderRadius: 8, padding: 16, margin: "16px 0" };
const row: React.CSSProperties = { display: "flex", gap: 8, alignItems: "center", flexWrap: "wrap", margin: "6px 0" };
const quiet: React.CSSProperties = { color: "#666", fontSize: 13 };

function nodeLabel(d: Dev) {
  const n = typeof d.lastStatus?.node === "string" ? ` · ${d.lastStatus.node}` : "";
  const online = d.lastStatus?.online === true ? "online" : "offline";
  return `${d.deviceId} (${d.product}${n}) · ${online}`;
}

export default function FarmsClient() {
  const [farms, setFarms] = useState<Farm[]>([]);
  const [unassigned, setUnassigned] = useState<Dev[]>([]);
  const [newFarm, setNewFarm] = useState("");
  const [email, setEmail] = useState<Record<string, string>>({});
  const [pick, setPick] = useState<Record<string, string>>({});
  const [msg, setMsg] = useState("");

  const load = useCallback(async () => {
    const r = await fetch("/api/admin/farms");
    const j = await r.json();
    setFarms(j.farms ?? []);
    setUnassigned(j.unassigned ?? []);
  }, []);
  useEffect(() => {
    load();
  }, [load]);

  async function call(url: string, method: string, body?: object) {
    setMsg("");
    const r = await fetch(url, { method, headers: { "Content-Type": "application/json" }, body: body ? JSON.stringify(body) : undefined });
    const j = await r.json().catch(() => ({}));
    if (!r.ok) setMsg(j.error ?? `failed (${r.status})`);
    await load();
    return r.ok;
  }

  return (
    <div>
      <div style={row}>
        <input placeholder="New farm name, e.g. Girish Farm" value={newFarm} onChange={(e) => setNewFarm(e.target.value)} style={{ minWidth: 280 }} />
        <button
          disabled={!newFarm.trim()}
          onClick={async () => {
            if (await call("/api/admin/farms", "POST", { name: newFarm })) setNewFarm("");
          }}
        >
          Create farm
        </button>
      </div>
      {msg && <p style={{ color: "#b00020" }}>{msg}</p>}

      {farms.map((f) => (
        <section key={f.id} style={box}>
          <div style={{ display: "flex", justifyContent: "space-between", alignItems: "baseline" }}>
            <h2 style={{ margin: 0 }}>{f.name}</h2>
            <button
              onClick={() => {
                if (confirm(`Delete farm "${f.name}"? Its nodes stay, in no farm; its members lose access.`)) call(`/api/admin/farms/${f.id}`, "DELETE");
              }}
            >
              Delete farm
            </button>
          </div>

          <h3>Members ({f.members.length})</h3>
          {f.members.length === 0 && <p style={quiet}>No one yet: add the emails that may use this farm.</p>}
          {f.members.map((m) => (
            <div key={m.user.id} style={row}>
              <span>{m.user.email}</span>
              <span style={quiet}>{m.user.role === "ADMIN" ? "admin" : ""}</span>
              <button
                onClick={() => {
                  if (confirm(`Remove ${m.user.email} from ${f.name}?`)) call(`/api/admin/farms/${f.id}`, "PATCH", { action: "removeMember", userId: m.user.id });
                }}
              >
                Remove
              </button>
            </div>
          ))}
          <div style={row}>
            <input
              type="email"
              placeholder="email to add"
              value={email[f.id] ?? ""}
              onChange={(e) => setEmail({ ...email, [f.id]: e.target.value })}
              style={{ minWidth: 260 }}
            />
            <button
              disabled={!(email[f.id] ?? "").trim()}
              onClick={async () => {
                if (await call(`/api/admin/farms/${f.id}`, "PATCH", { action: "addMember", email: email[f.id] })) setEmail({ ...email, [f.id]: "" });
              }}
            >
              Add member
            </button>
          </div>

          <h3>Nodes ({f.devices.length})</h3>
          {f.devices.map((d) => (
            <div key={d.id} style={row}>
              <span>{nodeLabel(d)}</span>
              <button onClick={() => call(`/api/admin/farms/${f.id}`, "PATCH", { action: "removeDevice", deviceId: d.deviceId })}>Take out</button>
            </div>
          ))}
          <div style={row}>
            <select value={pick[f.id] ?? ""} onChange={(e) => setPick({ ...pick, [f.id]: e.target.value })}>
              <option value="">Add a node in no farm…</option>
              {unassigned.map((d) => (
                <option key={d.id} value={d.deviceId}>
                  {nodeLabel(d)}
                </option>
              ))}
            </select>
            <button
              disabled={!pick[f.id]}
              onClick={async () => {
                if (await call(`/api/admin/farms/${f.id}`, "PATCH", { action: "addDevice", deviceId: pick[f.id] })) setPick({ ...pick, [f.id]: "" });
              }}
            >
              Add node
            </button>
          </div>
        </section>
      ))}
    </div>
  );
}
