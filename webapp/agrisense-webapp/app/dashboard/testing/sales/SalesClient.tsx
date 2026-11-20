"use client";
import { useMemo, useState } from "react";
import { useRouter } from "next/navigation";

type Pay = "PAID" | "PARTIAL" | "PENDING";
type Unit = { id: string; product: string; deviceId: string; warrantyMonths: number; note: string | null };
type Sale = {
  id: string; customer: string; customerType: string | null; location: string | null; invoiceNo: string | null;
  dispatchDate: string | null; paymentStatus: Pay; notes: string | null; createdBy: string; units: Unit[];
};
type Devices = Record<string, { fullId: string; lastSeenAt: string | null }>;

const PRODUCTS = ["FG1", "FG1-TH", "WPC-GW", "WPC-PN", "WM1-MINI", "WM1-PRO", "WM2", "TH2", "ASC-KIT", "OTHER"];
const TYPES = ["Dealer", "Farmer", "Institution", "Other"];
const PAY: { p: Pay; label: string; pill: string }[] = [
  { p: "PAID", label: "Paid", pill: "good" },
  { p: "PARTIAL", label: "Partial", pill: "warn" },
  { p: "PENDING", label: "Pending", pill: "bad" },
];
const PAY_LABEL = Object.fromEntries(PAY.map((x) => [x.p, x.label])) as Record<Pay, string>;
const PAY_PILL = Object.fromEntries(PAY.map((x) => [x.p, x.pill])) as Record<Pay, string>;

const fmt = (iso: string | null) => (iso ? new Date(iso).toLocaleDateString("en-IN", { day: "numeric", month: "short", year: "numeric" }) : "—");
const dateInput = (iso: string | null) => (iso ? iso.slice(0, 10) : "");
function warrantyEnd(s: Sale, u: Unit) {
  if (!s.dispatchDate) return null;
  const d = new Date(s.dispatchDate);
  d.setMonth(d.getMonth() + u.warrantyMonths);
  return d;
}
function lastSeen(iso: string | null | undefined) {
  if (!iso) return "never online";
  const days = Math.floor((Date.now() - +new Date(iso)) / 86400000);
  return days <= 0 ? "online today" : days === 1 ? "1 day ago" : days < 60 ? `${days} days ago` : fmt(iso);
}

async function send(url: string, method: string, body?: object) {
  const r = await fetch(url, { method, headers: { "content-type": "application/json" }, body: body ? JSON.stringify(body) : undefined });
  const j = await r.json().catch(() => ({}));
  return r.ok ? null : (j.error as string) ?? "Could not save";
}

export default function SalesClient({ sales, devices }: { sales: Sale[]; devices: Devices }) {
  const [q, setQ] = useState("");
  const [pay, setPay] = useState<Pay | "ALL">("ALL");
  const [prod, setProd] = useState("ALL");
  const [adding, setAdding] = useState(false);
  const customers = useMemo(() => [...new Set(sales.map((s) => s.customer))].sort(), [sales]);

  const needle = q.trim().toUpperCase();
  const unitHit = (s: Sale, u: Unit) =>
    !needle || u.deviceId.includes(needle) || (devices[u.deviceId]?.fullId.toUpperCase().includes(needle) ?? false);
  const shown = sales.filter((s) => {
    if (pay !== "ALL" && s.paymentStatus !== pay) return false;
    if (prod !== "ALL" && !s.units.some((u) => u.product === prod)) return false;
    if (!needle) return true;
    const head = [s.customer, s.location, s.invoiceNo, s.customerType].join(" ").toUpperCase();
    return head.includes(needle) || s.units.some((u) => unitHit(s, u));
  });
  const totalUnits = sales.reduce((n, s) => n + s.units.length, 0);
  const unpaid = sales.filter((s) => s.paymentStatus !== "PAID").length;

  function exportCsv() {
    const rows = [["Device ID", "Full device ID", "Product", "Customer", "Type", "Location", "Invoice", "Dispatch date", "Payment", "Warranty until", "Last online", "Note"]];
    for (const s of shown) for (const u of s.units) {
      const w = warrantyEnd(s, u);
      rows.push([u.deviceId, devices[u.deviceId]?.fullId ?? "", u.product, s.customer, s.customerType ?? "", s.location ?? "", s.invoiceNo ?? "",
        s.dispatchDate ? s.dispatchDate.slice(0, 10) : "", PAY_LABEL[s.paymentStatus], w ? w.toISOString().slice(0, 10) : "",
        devices[u.deviceId]?.lastSeenAt?.slice(0, 10) ?? "", u.note ?? ""]);
    }
    const csv = rows.map((r) => r.map((c) => `"${String(c).replace(/"/g, '""')}"`).join(",")).join("\n");
    const a = document.createElement("a");
    a.href = URL.createObjectURL(new Blob([csv], { type: "text/csv" }));
    a.download = `sales-register-${new Date().toISOString().slice(0, 10)}.csv`;
    a.click();
  }

  return (
    <>
      <div className="row between">
        <div className="stat">
          <span><b>{totalUnits}</b> devices sold</span>
          <span><b>{sales.length}</b> sales</span>
          <span><b>{unpaid}</b> not fully paid</span>
        </div>
        <div className="row">
          <button type="button" className="btn ghost small" onClick={exportCsv}>Export CSV</button>
          <button type="button" className="btn small" onClick={() => setAdding((v) => !v)}>{adding ? "Close" : "+ New sale"}</button>
        </div>
      </div>
      {adding && <NewSale customers={customers} onDone={() => setAdding(false)} />}

      <section className="card">
        <label>Search device ID, customer, invoice or place
          <input type="text" value={q} onChange={(e) => setQ(e.target.value)} placeholder="e.g. 0DA8, NB Agri, A2, Raipur" />
        </label>
        <div className="filters">
          {(["ALL", ...PAY.map((x) => x.p)] as const).map((p) => (
            <a key={p} href="#" className={pay === p ? "on" : ""} onClick={(e) => { e.preventDefault(); setPay(p); }}>{p === "ALL" ? "Any payment" : PAY_LABEL[p]}</a>
          ))}
        </div>
        <div className="filters">
          {["ALL", ...PRODUCTS].map((p) => (
            <a key={p} href="#" className={prod === p ? "on" : ""} onClick={(e) => { e.preventDefault(); setProd(p); }}>{p === "ALL" ? "All products" : p}</a>
          ))}
        </div>
      </section>

      {shown.length === 0 && <p className="muted">{sales.length === 0 ? "No sales yet. Press + New sale." : "Nothing matches."}</p>}
      <div className="grid">
        {shown.map((s) => <SaleCard key={s.id} s={s} devices={devices} hit={(u) => !!needle && unitHit(s, u)} customers={customers} />)}
      </div>
    </>
  );
}

function SaleFields({ init, customers }: { init?: Partial<Sale>; customers: string[] }) {
  return (
    <div className="fields">
      <label>Customer / dealer<input name="customer" type="text" list="sale-customers" defaultValue={init?.customer ?? ""} required /></label>
      <datalist id="sale-customers">{customers.map((c) => <option key={c} value={c} />)}</datalist>
      <label>Type
        <select name="customerType" defaultValue={init?.customerType ?? "Dealer"}>{TYPES.map((t) => <option key={t}>{t}</option>)}</select>
      </label>
      <label>Location<input name="location" type="text" defaultValue={init?.location ?? ""} placeholder="Town, district" /></label>
      <label>Invoice no.<input name="invoiceNo" type="text" defaultValue={init?.invoiceNo ?? ""} placeholder="As in Zoho Books" /></label>
      <label>Dispatch date<input name="dispatchDate" type="date" defaultValue={dateInput(init?.dispatchDate ?? null)} /></label>
      <label>Payment
        <select name="paymentStatus" defaultValue={init?.paymentStatus ?? "PENDING"}>{PAY.map((x) => <option key={x.p} value={x.p}>{x.label}</option>)}</select>
      </label>
      <label style={{ gridColumn: "1 / -1" }}>Notes<input name="notes" type="text" defaultValue={init?.notes ?? ""} /></label>
    </div>
  );
}

function formObj(f: HTMLFormElement) {
  return Object.fromEntries(new FormData(f).entries()) as Record<string, string>;
}

function NewSale({ customers, onDone }: { customers: string[]; onDone: () => void }) {
  const router = useRouter();
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState("");
  return (
    <form className="card" onSubmit={async (e) => {
      e.preventDefault();
      const f = formObj(e.currentTarget);
      setBusy(true); setError("");
      const problem = await send("/api/sales", "POST", { ...f, allowDuplicate: f.allowDuplicate === "on", warrantyMonths: Number(f.warrantyMonths || 12) });
      setBusy(false);
      if (problem) { setError(problem); return; }
      onDone(); router.refresh();
    }}>
      <h2>New sale</h2>
      <SaleFields customers={customers} />
      <div className="fields">
        <label>Product<select name="product" defaultValue="FG1">{PRODUCTS.map((p) => <option key={p}>{p}</option>)}</select></label>
        <label>Warranty (months)<input name="warrantyMonths" type="text" inputMode="numeric" defaultValue="12" /></label>
      </div>
      <label>Device IDs <span className="hint">one per line or separated by commas, e.g. 0DA8, 49DC</span>
        <textarea name="deviceIds" rows={4} required />
      </label>
      <label className="small"><input type="checkbox" name="allowDuplicate" /> Allow a device that is already in another sale (resale or replacement)</label>
      <p className="muted small">Different products in one dispatch? Save with the first product, then use “Add devices” on the sale.</p>
      <div className="row">
        <button className="btn" disabled={busy}>Save sale</button>
        {error && <span className="small" style={{ color: "var(--bad)" }}>{error}</span>}
      </div>
    </form>
  );
}

function SaleCard({ s, devices, hit, customers }: { s: Sale; devices: Devices; hit: (u: Unit) => boolean; customers: string[] }) {
  const router = useRouter();
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState("");
  const [editing, setEditing] = useState(false);
  const [addingUnits, setAddingUnits] = useState(false);

  async function act(url: string, method: string, body?: object) {
    setBusy(true); setError("");
    const problem = await send(url, method, body);
    setBusy(false);
    if (problem) { setError(problem); return false; }
    router.refresh();
    return true;
  }

  return (
    <article className="card sale">
      <div className="row between">
        <div>
          <h3>{s.customer}{s.customerType ? <span className="muted small"> · {s.customerType}</span> : null}</h3>
          <p className="small muted">{[s.location, s.invoiceNo ? `Invoice ${s.invoiceNo}` : "No invoice yet", `Dispatched ${fmt(s.dispatchDate)}`].filter(Boolean).join(" · ")}</p>
        </div>
        <span className={`pill ${PAY_PILL[s.paymentStatus]}`}>{PAY_LABEL[s.paymentStatus]}</span>
      </div>
      {s.notes && <p className="small">{s.notes}</p>}

      <div className="units">
        <table>
          <thead><tr><th>#</th><th>Product</th><th>Device ID</th><th>Warranty until</th><th>Last online</th><th></th></tr></thead>
          <tbody>
            {s.units.map((u, i) => {
              const d = devices[u.deviceId];
              const w = warrantyEnd(s, u);
              return (
                <tr key={u.id} className={hit(u) ? "hit" : ""}>
                  <td>{i + 1}</td>
                  <td>{u.product}</td>
                  <td className="mono">{u.deviceId}{d && d.fullId.toUpperCase() !== u.deviceId ? <span className="muted"> ({d.fullId})</span> : null}{u.note ? <span className="muted"> · {u.note}</span> : null}</td>
                  <td>{w ? (w < new Date() ? <span style={{ color: "var(--bad)" }}>ended {fmt(w.toISOString())}</span> : fmt(w.toISOString())) : "—"}</td>
                  <td className="small">{d ? lastSeen(d.lastSeenAt) : <span className="muted">not on server</span>}</td>
                  <td><button type="button" className="x" aria-label={`Remove ${u.deviceId}`} disabled={busy}
                    onClick={() => window.confirm(`Remove ${u.deviceId} from this sale?`) && act(`/api/sales/units/${u.id}`, "DELETE")}>✕</button></td>
                </tr>
              );
            })}
          </tbody>
        </table>
      </div>

      <div className="choice" aria-label="Payment">
        {PAY.map((x) => (
          <button key={x.p} type="button" aria-pressed={s.paymentStatus === x.p} disabled={busy || s.paymentStatus === x.p}
            onClick={() => act(`/api/sales/${s.id}`, "PATCH", { paymentStatus: x.p })}>{x.label}</button>
        ))}
        <button type="button" onClick={() => setAddingUnits((v) => !v)}>+ Add devices</button>
        <button type="button" onClick={() => setEditing((v) => !v)}>{editing ? "Close edit" : "Edit details"}</button>
      </div>

      {addingUnits && (
        <form onSubmit={async (e) => {
          e.preventDefault();
          const f = formObj(e.currentTarget);
          if (await act(`/api/sales/${s.id}/units`, "POST", { ...f, allowDuplicate: f.allowDuplicate === "on", warrantyMonths: Number(f.warrantyMonths || 12) })) setAddingUnits(false);
        }}>
          <div className="fields">
            <label>Product<select name="product" defaultValue={s.units[0]?.product ?? "FG1"}>{PRODUCTS.map((p) => <option key={p}>{p}</option>)}</select></label>
            <label>Warranty (months)<input name="warrantyMonths" type="text" inputMode="numeric" defaultValue="12" /></label>
          </div>
          <label>Device IDs<textarea name="deviceIds" rows={2} required /></label>
          <label className="small"><input type="checkbox" name="allowDuplicate" /> Allow a device already in another sale</label>
          <button className="btn small" disabled={busy}>Add</button>
        </form>
      )}

      {editing && (
        <form onSubmit={async (e) => {
          e.preventDefault();
          if (await act(`/api/sales/${s.id}`, "PATCH", formObj(e.currentTarget))) setEditing(false);
        }}>
          <SaleFields init={s} customers={customers} />
          <div className="row between">
            <button className="btn small" disabled={busy}>Save details</button>
            <button type="button" className="btn bad small" disabled={busy}
              onClick={() => window.confirm(`Delete this whole sale (${s.units.length} devices)? This can't be undone.`) && act(`/api/sales/${s.id}`, "DELETE")}>Delete sale</button>
          </div>
        </form>
      )}
      {error && <p className="small" style={{ color: "var(--bad)" }}>{error}</p>}
      <p className="small muted">Entered by {s.createdBy}</p>
    </article>
  );
}
