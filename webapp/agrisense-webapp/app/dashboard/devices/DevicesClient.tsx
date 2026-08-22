"use client";

import { useEffect, useState, useCallback } from "react";

type Device = {
  id: string;
  deviceId: string;
  product: string;
  name: string;
  lastSeenAt: string | null;
  lastStatus: unknown;
  owner: { name: string; phone: string } | null;
  dealer: { name: string; phone: string } | null;
};

const PRODUCTS = ["FG1", "FM1", "WM1_MINI", "WM1_PRO", "WPC", "TH"];

function isOnline(lastSeenAt: string | null): boolean {
  if (!lastSeenAt) return false;
  return Date.now() - new Date(lastSeenAt).getTime() < 5 * 60 * 1000; // online if seen in last 5 min
}

export default function DevicesPage({ role }: { role: string }) {
  const [devices, setDevices] = useState<Device[]>([]);
  const [loading, setLoading] = useState(true);
  const [showAdd, setShowAdd] = useState(false);
  const [newDevice, setNewDevice] = useState({ deviceId: "", product: "FG1", name: "" });
  const [error, setError] = useState("");
  const [editingId, setEditingId] = useState<string | null>(null);
  const [ownerPhone, setOwnerPhone] = useState("");

  const load = useCallback(async () => {
    const res = await fetch("/api/devices");
    const data = await res.json();
    setDevices(data.devices || []);
    setLoading(false);
  }, []);

  useEffect(() => {
    load();
    const interval = setInterval(load, 15000); // refresh every 15s for live status
    return () => clearInterval(interval);
  }, [load]);

  async function addDevice() {
    setError("");
    const res = await fetch("/api/devices", {
      method: "POST",
      body: JSON.stringify(newDevice),
    });
    if (res.ok) {
      setShowAdd(false);
      setNewDevice({ deviceId: "", product: "FG1", name: "" });
      load();
    } else {
      const data = await res.json();
      setError(data.error || "Failed to add device");
    }
  }

  async function assignOwner(deviceId: string) {
    setError("");
    const res = await fetch(`/api/devices/${deviceId}`, {
      method: "PATCH",
      body: JSON.stringify({ ownerPhone }),
    });
    if (res.ok) {
      setEditingId(null);
      setOwnerPhone("");
      load();
    } else {
      const data = await res.json();
      setError(data.error || "Failed to assign owner");
    }
  }

  if (loading) return <p>Loading devices…</p>;

  return (
    <div>
      {(role === "ADMIN" || role === "DEALER") && (
        <button onClick={() => setShowAdd(!showAdd)} style={{ marginBottom: 16, padding: "8px 16px" }}>
          {showAdd ? "Cancel" : "+ Add Device"}
        </button>
      )}

      {showAdd && (
        <div style={{ border: "1px solid #ccc", padding: 16, marginBottom: 16, maxWidth: 400 }}>
          <input
            placeholder="Device ID (e.g. FG1_A1B2)"
            value={newDevice.deviceId}
            onChange={(e) => setNewDevice({ ...newDevice, deviceId: e.target.value })}
            style={{ width: "100%", padding: 8, marginBottom: 8 }}
          />
          <select
            value={newDevice.product}
            onChange={(e) => setNewDevice({ ...newDevice, product: e.target.value })}
            style={{ width: "100%", padding: 8, marginBottom: 8 }}
          >
            {PRODUCTS.map((p) => (
              <option key={p} value={p}>
                {p}
              </option>
            ))}
          </select>
          <input
            placeholder="Friendly name"
            value={newDevice.name}
            onChange={(e) => setNewDevice({ ...newDevice, name: e.target.value })}
            style={{ width: "100%", padding: 8, marginBottom: 8 }}
          />
          <button onClick={addDevice} style={{ width: "100%", padding: 8 }}>
            Create
          </button>
        </div>
      )}

      {error && <p style={{ color: "red" }}>{error}</p>}

      {devices.length === 0 && <p>No devices yet.</p>}

      <table style={{ width: "100%", borderCollapse: "collapse" }}>
        <thead>
          <tr style={{ textAlign: "left", borderBottom: "2px solid #ddd" }}>
            <th style={{ padding: 8 }}>Status</th>
            <th style={{ padding: 8 }}>Name</th>
            <th style={{ padding: 8 }}>Product</th>
            <th style={{ padding: 8 }}>Device ID</th>
            <th style={{ padding: 8 }}>Last Seen</th>
            {role !== "CUSTOMER" && <th style={{ padding: 8 }}>Owner</th>}
            {role === "ADMIN" && <th style={{ padding: 8 }}></th>}
          </tr>
        </thead>
        <tbody>
          {devices.map((d) => (
            <tr key={d.id} style={{ borderBottom: "1px solid #eee" }}>
              <td style={{ padding: 8 }}>
                <span
                  style={{
                    display: "inline-block",
                    width: 10,
                    height: 10,
                    borderRadius: "50%",
                    background: isOnline(d.lastSeenAt) ? "#22c55e" : "#9ca3af",
                    marginRight: 6,
                  }}
                />
                {isOnline(d.lastSeenAt) ? "Online" : "Offline"}
              </td>
              <td style={{ padding: 8 }}>{d.name}</td>
              <td style={{ padding: 8 }}>{d.product}</td>
              <td style={{ padding: 8, fontFamily: "monospace" }}>{d.deviceId}</td>
              <td style={{ padding: 8 }}>
                {d.lastSeenAt ? new Date(d.lastSeenAt).toLocaleString() : "Never"}
              </td>
              {role !== "CUSTOMER" && (
                <td style={{ padding: 8 }}>
                  {d.owner ? `${d.owner.name} (${d.owner.phone})` : "Unassigned"}
                </td>
              )}
              {role === "ADMIN" && (
                <td style={{ padding: 8 }}>
                  {editingId === d.id ? (
                    <>
                      <input
                        placeholder="Owner phone"
                        value={ownerPhone}
                        onChange={(e) => setOwnerPhone(e.target.value)}
                        style={{ padding: 4, marginRight: 4 }}
                      />
                      <button onClick={() => assignOwner(d.id)}>Save</button>
                    </>
                  ) : (
                    <button onClick={() => setEditingId(d.id)}>Assign</button>
                  )}
                </td>
              )}
            </tr>
          ))}
        </tbody>
      </table>
    </div>
  );
}
