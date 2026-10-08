"use client";
import { useState } from "react";
import { useSubmit } from "./useSubmit";

export function MemberForm() {
  const { submit, busy, error } = useSubmit();
  return (
    <form
      onSubmit={async (e) => {
        e.preventDefault();
        const form = e.currentTarget;
        const fd = Object.fromEntries(new FormData(form));
        if (await submit("/api/testing/members", fd)) form.reset();
      }}
    >
      <div className="fields">
        <label>Email of an existing account<input type="email" name="email" required /></label>
        <label>Role<select name="role" defaultValue="TESTER"><option value="TESTER">Tester</option><option value="DEVELOPER">Developer</option></select></label>
      </div>
      {error && <div className="error">{error}</div>}
      <div className="row"><button className="btn small" disabled={busy}>Add or update</button></div>
    </form>
  );
}

export function MemberToggle({ email, role, active }: { email: string; role: string; active: boolean }) {
  const { submit, busy } = useSubmit();
  return (
    <button className="btn small ghost" disabled={busy} onClick={() => submit("/api/testing/members", { email, role, active: !active })}>
      {active ? "Remove" : "Restore"}
    </button>
  );
}

type P = { id?: string; code: string; name: string; modules: string[]; hasFirmware: boolean; hasApp: boolean; hasHardware: boolean; active: boolean; sortOrder: number };

export function ProductForm({ product }: { product?: P }) {
  const { submit, busy, error } = useSubmit();
  const [open, setOpen] = useState(false);
  if (!open) return <button className="btn small ghost" onClick={() => setOpen(true)}>{product ? "Edit" : "Add product"}</button>;
  return (
    <form
      className="card"
      onSubmit={async (e) => {
        e.preventDefault();
        const fd = new FormData(e.currentTarget);
        const ok = await submit("/api/testing/products", {
          id: product?.id,
          code: fd.get("code"),
          name: fd.get("name"),
          modules: String(fd.get("modules") ?? "").split(","),
          hasFirmware: fd.get("hasFirmware") === "on",
          hasApp: fd.get("hasApp") === "on",
          hasHardware: fd.get("hasHardware") === "on",
          active: fd.get("active") === "on",
          sortOrder: fd.get("sortOrder"),
        });
        if (ok) setOpen(false);
      }}
    >
      <div className="fields">
        <label>Code<input name="code" defaultValue={product?.code} required placeholder="AWD1" /></label>
        <label>Name<input name="name" defaultValue={product?.name} required /></label>
        <label>Modules <span className="hint">comma separated</span><input name="modules" defaultValue={product?.modules.join(", ") ?? "Controller"} /></label>
        <label>Order<input name="sortOrder" defaultValue={product?.sortOrder ?? 100} /></label>
      </div>
      <div className="row small">
        <label style={{ display: "flex", gap: 6 }}><input type="checkbox" name="hasFirmware" defaultChecked={product?.hasFirmware ?? true} />Firmware</label>
        <label style={{ display: "flex", gap: 6 }}><input type="checkbox" name="hasApp" defaultChecked={product?.hasApp ?? true} />App</label>
        <label style={{ display: "flex", gap: 6 }}><input type="checkbox" name="hasHardware" defaultChecked={product?.hasHardware ?? true} />Hardware</label>
        <label style={{ display: "flex", gap: 6 }}><input type="checkbox" name="active" defaultChecked={product?.active ?? true} />Active</label>
      </div>
      {error && <div className="error">{error}</div>}
      <div className="row">
        <button className="btn small" disabled={busy}>Save</button>
        <button type="button" className="btn small ghost" onClick={() => setOpen(false)}>Cancel</button>
      </div>
    </form>
  );
}
