"use client";

import { useEffect, useState, useCallback } from "react";

type Build = {
  id: string;
  product: string;
  version: string;
  variant: string;
  sizeBytes: number;
  createdAt: string;
  uploadedBy: { name: string };
};

type Grant = {
  id: string;
  phone: string | null;
  email: string | null;
  label: string;
  products: string[];
  active: boolean;
};

type FlashEventRow = {
  id: string;
  result: string;
  deviceId: string | null;
  occurredAt: string;
  grant: { label: string };
  build: { product: string; version: string; variant: string };
};

const PRODUCTS = ["FG1", "FM1", "WM1_MINI", "WM1_PRO", "WPC", "TH"];

export default function FlasherAdminClient() {
  const [builds, setBuilds] = useState<Build[]>([]);
  const [grants, setGrants] = useState<Grant[]>([]);
  const [events, setEvents] = useState<FlashEventRow[]>([]);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState("");

  const [uploadProduct, setUploadProduct] = useState("FG1");
  const [uploadVersion, setUploadVersion] = useState("");
  const [uploadVariant, setUploadVariant] = useState("esp32dev");
  const [uploadNotes, setUploadNotes] = useState("");
  const [uploadFile, setUploadFile] = useState<File | null>(null);
  const [uploading, setUploading] = useState(false);

  const [grantEmail, setGrantEmail] = useState("");
  const [grantLabel, setGrantLabel] = useState("");
  const [grantProducts, setGrantProducts] = useState<string[]>([]);
  const [creatingGrant, setCreatingGrant] = useState(false);

  // Editing an existing grant's products in place — separate from the "create
  // a new grant" form above, since adding a product to someone who already
  // has access means updating their one grant row, not creating a second one
  // (findActiveGrant only ever looks at the first match for an account).
  const [editingGrantId, setEditingGrantId] = useState<string | null>(null);
  const [editingProducts, setEditingProducts] = useState<string[]>([]);
  const [savingProducts, setSavingProducts] = useState(false);

  const load = useCallback(async () => {
    const [buildsRes, grantsRes, eventsRes] = await Promise.all([
      fetch("/api/admin/builds"),
      fetch("/api/admin/grants"),
      fetch("/api/admin/events"),
    ]);
    if (buildsRes.ok) setBuilds((await buildsRes.json()).builds);
    if (grantsRes.ok) setGrants((await grantsRes.json()).grants);
    if (eventsRes.ok) setEvents((await eventsRes.json()).events);
    setLoading(false);
  }, []);

  useEffect(() => {
    load();
  }, [load]);

  async function uploadBuild() {
    if (!uploadFile || !uploadVersion) {
      setError("Version and a file are required.");
      return;
    }
    setUploading(true);
    setError("");
    const form = new FormData();
    form.append("product", uploadProduct);
    form.append("version", uploadVersion);
    form.append("variant", uploadVariant);
    form.append("notes", uploadNotes);
    form.append("file", uploadFile);

    const res = await fetch("/api/admin/builds", { method: "POST", body: form });
    setUploading(false);
    if (res.ok) {
      setUploadVersion("");
      setUploadNotes("");
      setUploadFile(null);
      load();
    } else {
      setError((await res.json()).error || "Upload failed");
    }
  }

  async function createGrant() {
    if (!grantEmail || !grantLabel || grantProducts.length === 0) {
      setError("Label, at least one product, and an email are required.");
      return;
    }
    setCreatingGrant(true);
    setError("");
    const res = await fetch("/api/admin/grants", {
      method: "POST",
      body: JSON.stringify({
        email: grantEmail,
        label: grantLabel,
        products: grantProducts,
      }),
    });
    setCreatingGrant(false);
    if (res.ok) {
      setGrantEmail("");
      setGrantLabel("");
      setGrantProducts([]);
      load();
    } else {
      setError((await res.json()).error || "Failed to create grant");
    }
  }

  async function toggleGrant(id: string, active: boolean) {
    setError("");
    const res = await fetch(`/api/admin/grants/${id}`, {
      method: "PATCH",
      body: JSON.stringify({ active }),
    });
    if (res.ok) load();
    else setError((await res.json()).error || "Failed to update access");
  }

  async function deleteGrant(id: string, label: string) {
    if (!window.confirm(`Delete "${label}"'s access entirely? This can't be undone — use Revoke instead if they might come back.`)) return;
    setError("");
    const res = await fetch(`/api/admin/grants/${id}`, { method: "DELETE" });
    if (res.ok) load();
    else setError((await res.json()).error || "Failed to delete grant");
  }

  function toggleProduct(p: string) {
    setGrantProducts((prev) => (prev.includes(p) ? prev.filter((x) => x !== p) : [...prev, p]));
  }

  function startEditingProducts(grant: Grant) {
    setEditingGrantId(grant.id);
    setEditingProducts(grant.products);
  }

  function toggleEditingProduct(p: string) {
    setEditingProducts((prev) => (prev.includes(p) ? prev.filter((x) => x !== p) : [...prev, p]));
  }

  async function saveEditingProducts(id: string) {
    setError("");
    setSavingProducts(true);
    const res = await fetch(`/api/admin/grants/${id}`, {
      method: "PATCH",
      body: JSON.stringify({ products: editingProducts }),
    });
    setSavingProducts(false);
    if (res.ok) {
      setEditingGrantId(null);
      load();
    } else {
      setError((await res.json()).error || "Failed to update products");
    }
  }

  if (loading) return <p>Loading…</p>;

  return (
    <div>
      {error && <p style={{ color: "red" }}>{error}</p>}

      <section style={{ marginTop: 32 }}>
        <h2>Upload a build</h2>
        <div style={{ border: "1px solid #ccc", padding: 16, maxWidth: 480 }}>
          <select
            value={uploadProduct}
            onChange={(e) => setUploadProduct(e.target.value)}
            style={{ width: "100%", padding: 8, marginBottom: 8 }}
          >
            {PRODUCTS.map((p) => (
              <option key={p} value={p}>
                {p}
              </option>
            ))}
          </select>
          <input
            placeholder="Version (e.g. 1.4.2)"
            value={uploadVersion}
            onChange={(e) => setUploadVersion(e.target.value)}
            style={{ width: "100%", padding: 8, marginBottom: 8 }}
          />
          <input
            placeholder="Variant (e.g. esp32dev_ds1307)"
            value={uploadVariant}
            onChange={(e) => setUploadVariant(e.target.value)}
            style={{ width: "100%", padding: 8, marginBottom: 8 }}
          />
          <textarea
            placeholder="Notes (optional)"
            value={uploadNotes}
            onChange={(e) => setUploadNotes(e.target.value)}
            style={{ width: "100%", padding: 8, marginBottom: 8, minHeight: 60 }}
          />
          <input
            type="file"
            accept=".bin"
            onChange={(e) => setUploadFile(e.target.files?.[0] || null)}
            style={{ width: "100%", marginBottom: 8 }}
          />
          <button onClick={uploadBuild} disabled={uploading} style={{ width: "100%", padding: 8 }}>
            {uploading ? "Uploading…" : "Upload"}
          </button>
        </div>
      </section>

      <section style={{ marginTop: 32 }}>
        <h2>Builds</h2>
        {builds.length === 0 && <p>No builds uploaded yet.</p>}
        {builds.length > 0 && (
          <table style={{ width: "100%", borderCollapse: "collapse" }}>
            <thead>
              <tr style={{ textAlign: "left", borderBottom: "2px solid #ddd" }}>
                <th style={{ padding: 8 }}>Product</th>
                <th style={{ padding: 8 }}>Version</th>
                <th style={{ padding: 8 }}>Variant</th>
                <th style={{ padding: 8 }}>Size</th>
                <th style={{ padding: 8 }}>Uploaded by</th>
                <th style={{ padding: 8 }}>When</th>
              </tr>
            </thead>
            <tbody>
              {builds.map((b) => (
                <tr key={b.id} style={{ borderBottom: "1px solid #eee" }}>
                  <td style={{ padding: 8 }}>{b.product}</td>
                  <td style={{ padding: 8 }}>{b.version}</td>
                  <td style={{ padding: 8, fontFamily: "monospace" }}>{b.variant}</td>
                  <td style={{ padding: 8 }}>{(b.sizeBytes / 1024).toFixed(0)} KB</td>
                  <td style={{ padding: 8 }}>{b.uploadedBy.name}</td>
                  <td style={{ padding: 8 }}>{new Date(b.createdAt).toLocaleString()}</td>
                </tr>
              ))}
            </tbody>
          </table>
        )}
      </section>

      <section style={{ marginTop: 32 }}>
        <h2>Access</h2>
        <div style={{ border: "1px solid #ccc", padding: 16, maxWidth: 480, marginBottom: 16 }}>
          <input
            placeholder="Label (e.g. Kamta — Bihar dealer)"
            value={grantLabel}
            onChange={(e) => setGrantLabel(e.target.value)}
            style={{ width: "100%", padding: 8, marginBottom: 8 }}
          />
          <input
            placeholder="Email (verified via OTP at login)"
            value={grantEmail}
            onChange={(e) => setGrantEmail(e.target.value)}
            style={{ width: "100%", padding: 8, marginBottom: 8 }}
          />
          <div style={{ marginBottom: 8 }}>
            {PRODUCTS.map((p) => (
              <label key={p} style={{ marginRight: 12 }}>
                <input type="checkbox" checked={grantProducts.includes(p)} onChange={() => toggleProduct(p)} /> {p}
              </label>
            ))}
          </div>
          <button onClick={createGrant} disabled={creatingGrant} style={{ width: "100%", padding: 8 }}>
            {creatingGrant ? "Adding…" : "Grant access"}
          </button>
        </div>

        {grants.length === 0 && <p>No one has flasher access yet.</p>}
        {grants.length > 0 && (
          <table style={{ width: "100%", borderCollapse: "collapse" }}>
            <thead>
              <tr style={{ textAlign: "left", borderBottom: "2px solid #ddd" }}>
                <th style={{ padding: 8 }}>Label</th>
                <th style={{ padding: 8 }}>Contact</th>
                <th style={{ padding: 8 }}>Products</th>
                <th style={{ padding: 8 }}>Status</th>
                <th style={{ padding: 8 }}></th>
              </tr>
            </thead>
            <tbody>
              {grants.map((g) => (
                <tr key={g.id} style={{ borderBottom: "1px solid #eee" }}>
                  <td style={{ padding: 8 }}>{g.label}</td>
                  <td style={{ padding: 8 }}>{g.phone || g.email}</td>
                  <td style={{ padding: 8 }}>
                    {editingGrantId === g.id ? (
                      <div>
                        {PRODUCTS.map((p) => (
                          <label key={p} style={{ marginRight: 10, whiteSpace: "nowrap" }}>
                            <input
                              type="checkbox"
                              checked={editingProducts.includes(p)}
                              onChange={() => toggleEditingProduct(p)}
                            />{" "}
                            {p}
                          </label>
                        ))}
                      </div>
                    ) : (
                      g.products.join(", ")
                    )}
                  </td>
                  <td style={{ padding: 8 }}>
                    <span
                      style={{
                        display: "inline-block",
                        width: 10,
                        height: 10,
                        borderRadius: "50%",
                        background: g.active ? "#22c55e" : "#9ca3af",
                        marginRight: 6,
                      }}
                    />
                    {g.active ? "Active" : "Revoked"}
                  </td>
                  <td style={{ padding: 8, whiteSpace: "nowrap" }}>
                    {editingGrantId === g.id ? (
                      <>
                        <button onClick={() => saveEditingProducts(g.id)} disabled={savingProducts}>
                          {savingProducts ? "Saving…" : "Save"}
                        </button>{" "}
                        <button onClick={() => setEditingGrantId(null)}>Cancel</button>
                      </>
                    ) : (
                      <>
                        <button onClick={() => startEditingProducts(g)}>Edit products</button>{" "}
                        <button onClick={() => toggleGrant(g.id, !g.active)}>{g.active ? "Revoke" : "Restore"}</button>{" "}
                        <button onClick={() => deleteGrant(g.id, g.label)}>Delete</button>
                      </>
                    )}
                  </td>
                </tr>
              ))}
            </tbody>
          </table>
        )}
      </section>

      <section style={{ marginTop: 32 }}>
        <h2>Activity</h2>
        {events.length === 0 && <p>No downloads or flashes logged yet.</p>}
        {events.length > 0 && (
          <table style={{ width: "100%", borderCollapse: "collapse" }}>
            <thead>
              <tr style={{ textAlign: "left", borderBottom: "2px solid #ddd" }}>
                <th style={{ padding: 8 }}>When</th>
                <th style={{ padding: 8 }}>Who</th>
                <th style={{ padding: 8 }}>Build</th>
                <th style={{ padding: 8 }}>Result</th>
                <th style={{ padding: 8 }}>Device</th>
              </tr>
            </thead>
            <tbody>
              {events.map((ev) => (
                <tr key={ev.id} style={{ borderBottom: "1px solid #eee" }}>
                  <td style={{ padding: 8 }}>{new Date(ev.occurredAt).toLocaleString()}</td>
                  <td style={{ padding: 8 }}>{ev.grant.label}</td>
                  <td style={{ padding: 8 }}>
                    {ev.build.product} {ev.build.version} ({ev.build.variant})
                  </td>
                  <td style={{ padding: 8 }}>{ev.result}</td>
                  <td style={{ padding: 8, fontFamily: "monospace" }}>{ev.deviceId || "—"}</td>
                </tr>
              ))}
            </tbody>
          </table>
        )}
      </section>
    </div>
  );
}
