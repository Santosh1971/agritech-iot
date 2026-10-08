import Link from "next/link";
import { prisma } from "@/lib/prisma";
import { ROLE_LABEL } from "@/lib/tracker/rules";
import { trackUser } from "@/lib/tracker/server";
import { MemberForm, MemberToggle, ProductForm } from "../SettingsForms";

export const dynamic = "force-dynamic";

export default async function TeamPage() {
  const user = await trackUser();
  if (!user) return null;
  if (!user.isAdmin) return <div className="card"><p>Admins only.</p></div>;
  const [members, products] = await Promise.all([
    prisma.trackMember.findMany({ include: { user: { select: { name: true, email: true } } }, orderBy: { createdAt: "asc" } }),
    prisma.trackProduct.findMany({ orderBy: [{ sortOrder: "asc" }, { code: "asc" }] }),
  ]);
  return (
    <>
      <div className="crumbs"><Link href="/dashboard/testing">Overview</Link><span>/</span><span>Team</span></div>
      <h1>Testing team and products</h1>
      <section className="card">
        <h2>Team</h2>
        <p className="small muted">Testers report, verify and sign off. Developers triage, post builds and publish. Emails go to the other role.</p>
        <div className="tbl-wrap">
          <table>
            <thead><tr><th>Name</th><th>Email</th><th>Role</th><th /></tr></thead>
            <tbody>
              {members.map((m) => (
                <tr key={m.id} style={{ opacity: m.active ? 1 : 0.5 }}>
                  <td>{m.user.name}</td><td>{m.user.email}</td><td>{ROLE_LABEL[m.role]}{m.active ? "" : " (removed)"}</td>
                  <td><MemberToggle email={m.user.email} role={m.role} active={m.active} /></td>
                </tr>
              ))}
              {members.length === 0 && <tr><td colSpan={4} className="muted">Nobody yet. Add yourself as Developer and Avinash as Tester.</td></tr>}
            </tbody>
          </table>
        </div>
        <MemberForm />
      </section>
      <section className="card">
        <div className="row between"><h2>Products</h2><ProductForm /></div>
        <div className="tbl-wrap">
          <table>
            <thead><tr><th>Code</th><th>Name</th><th>Modules</th><th>Tracks</th><th /></tr></thead>
            <tbody>
              {products.map((p) => (
                <tr key={p.id} style={{ opacity: p.active ? 1 : 0.5 }}>
                  <td className="mono">{p.code}</td><td>{p.name}</td><td>{p.modules.join(", ")}</td>
                  <td>{[p.hasFirmware && "Firmware", p.hasApp && "App", p.hasHardware && "Hardware"].filter(Boolean).join(", ")}</td>
                  <td><ProductForm product={p} /></td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      </section>
    </>
  );
}
