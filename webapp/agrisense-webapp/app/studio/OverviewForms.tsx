"use client";
import { useState } from "react";
import { useRouter } from "next/navigation";

async function send(url: string, body: unknown, method = "POST") {
  const res = await fetch(url, { method, headers: { "Content-Type": "application/json" }, body: JSON.stringify(body) });
  const json = await res.json().catch(() => ({}));
  if (!res.ok) throw new Error(json.error ?? "Something went wrong. Try again.");
  return json;
}

export function NewCohort() {
  const router = useRouter();
  const [open, setOpen] = useState(false);
  const [f, setF] = useState({ name: "", institution: "", session: "" });
  const [error, setError] = useState("");
  const [busy, setBusy] = useState(false);
  if (!open) return <button className="btn" onClick={() => setOpen(true)}>New cohort</button>;
  return (
    <form className="card grid" style={{ width: "100%" }} onSubmit={async (e) => {
      e.preventDefault(); setBusy(true); setError("");
      try { await send("/api/studio/cohorts", f); setOpen(false); setF({ name: "", institution: "", session: "" }); router.refresh(); }
      catch (x) { setError((x as Error).message); } finally { setBusy(false); }
    }}>
      <h3>New cohort</h3>
      <div className="form-grid">
        <label className="field">Name<input id="cohort-name" type="text" value={f.name} onChange={(e) => setF({ ...f, name: e.target.value })} placeholder="AGR 322 Farm IoT practical" required /></label>
        <label className="field">Institution<input id="cohort-inst" type="text" value={f.institution} onChange={(e) => setF({ ...f, institution: e.target.value })} placeholder="GPSIOAM" required /></label>
        <label className="field">Session <span>optional</span><input id="cohort-session" type="text" value={f.session} onChange={(e) => setF({ ...f, session: e.target.value })} placeholder="2026-27" /></label>
      </div>
      {error && <p className="error">{error}</p>}
      <div className="row"><button className="btn" disabled={busy}>Create cohort</button><button type="button" className="btn ghost" onClick={() => setOpen(false)}>Cancel</button></div>
    </form>
  );
}

export function AddMember({ cohortId, canAddTeacher }: { cohortId: string; canAddTeacher: boolean }) {
  const router = useRouter();
  const [f, setF] = useState({ name: "", email: "", role: "STUDENT" });
  const [msg, setMsg] = useState("");
  const [error, setError] = useState("");
  const [busy, setBusy] = useState(false);
  return (
    <details className="add" style={{ width: "100%" }}>
      <summary>+ Add a person</summary>
      <form className="grid" onSubmit={async (e) => {
        e.preventDefault(); setBusy(true); setError(""); setMsg("");
        try {
          const r = await send(`/api/studio/cohorts/${cohortId}/members`, f);
          setMsg(`${r.member.name} added. They can now log in with ${r.member.email}.`);
          setF({ name: "", email: "", role: f.role }); router.refresh();
        } catch (x) { setError((x as Error).message); } finally { setBusy(false); }
      }}>
        <div className="form-grid">
          <label className="field">Name<input id={`m-name-${cohortId}`} type="text" value={f.name} onChange={(e) => setF({ ...f, name: e.target.value })} /></label>
          <label className="field">Email<input id={`m-email-${cohortId}`} type="email" value={f.email} onChange={(e) => setF({ ...f, email: e.target.value })} required /></label>
          {canAddTeacher && (
            <label className="field">Role
              <select id={`m-role-${cohortId}`} value={f.role} onChange={(e) => setF({ ...f, role: e.target.value })}>
                <option value="STUDENT">Student</option><option value="TEACHER">Teacher</option>
              </select>
            </label>
          )}
        </div>
        {error && <p className="error">{error}</p>}
        {msg && <p className="small">{msg}</p>}
        <div><button className="btn small" disabled={busy}>Add</button></div>
      </form>
    </details>
  );
}

export function NewProject({ cohortId, isStudent }: { cohortId: string; isStudent: boolean }) {
  const router = useRouter();
  const [f, setF] = useState({ title: "", kit: "MINI", team: "" });
  const [error, setError] = useState("");
  const [busy, setBusy] = useState(false);
  return (
    <details className="add" style={{ width: "100%" }}>
      <summary>+ New project</summary>
      <form className="grid" onSubmit={async (e) => {
        e.preventDefault(); setBusy(true); setError("");
        try {
          const teamEmails = f.team.split(/[\s,;]+/).filter(Boolean);
          const r = await send("/api/studio/projects", { cohortId, title: f.title, kit: f.kit, teamEmails });
          router.push(`/studio/${r.project.id}`);
        } catch (x) { setError((x as Error).message); setBusy(false); }
      }}>
        <div className="form-grid">
          <label className="field">Title<input id={`p-title-${cohortId}`} type="text" value={f.title} onChange={(e) => setF({ ...f, title: e.target.value })} placeholder="Nursery bed irrigation" required /></label>
          <label className="field">Kit <span>you can change it in the Problem stage</span>
            <select id={`p-kit-${cohortId}`} value={f.kit} onChange={(e) => setF({ ...f, kit: e.target.value })}>
              <option value="MINI">Mini (classroom, mains power)</option><option value="MEGA">Mega (field, battery and solar)</option>
            </select>
          </label>
          <label className="field">{isStudent ? "Teammates' emails" : "Team emails"} <span>optional, must already be in this cohort</span>
            <input id={`p-team-${cohortId}`} type="text" value={f.team} onChange={(e) => setF({ ...f, team: e.target.value })} placeholder="a@college.in, b@college.in" />
          </label>
        </div>
        {error && <p className="error">{error}</p>}
        <div><button className="btn small" disabled={busy}>Create project</button></div>
      </form>
    </details>
  );
}
