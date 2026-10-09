import Link from "next/link";
import { prisma } from "@/lib/prisma";
import { cohortFilter, projectFilter, studioUser } from "@/lib/studio/access";
import { STAGES, currentStageIndex, type StageStatus } from "@/lib/studio/stages";
import { AddMember, NewCohort, NewProject } from "./OverviewForms";

export const dynamic = "force-dynamic";

export default async function StudioHome() {
  const u = await studioUser();
  if (!u) {
    return (
      <div className="card grid" style={{ maxWidth: 620 }}>
        <h1>ASC Product Studio</h1>
        <p>The studio is for students, teachers and Agri Sensors and Controls staff. Ask your teacher to add your email to a class, then log in with that email.</p>
        <p><Link href="/login">Log in</Link></p>
      </div>
    );
  }

  const cohorts = await prisma.studioCohort.findMany({
    where: cohortFilter(u),
    orderBy: { createdAt: "asc" },
    include: {
      members: { include: { user: { select: { id: true, name: true, email: true, role: true } } }, orderBy: { addedAt: "asc" } },
      projects: {
        where: projectFilter(u),
        orderBy: { createdAt: "asc" },
        include: { stages: { select: { stage: true, status: true } }, members: { include: { user: { select: { name: true } } } } },
      },
    },
  });

  const staff = u.role !== "STUDENT";
  return (
    <div className="grid">
      <div className="row between">
        <div>
          <div className="eyebrow">{staff ? "Classes and projects" : "My projects"}</div>
          <h1>{staff ? "Studio overview" : "Your projects"}</h1>
        </div>
        {u.role === "ADMIN" && <NewCohort />}
      </div>

      {cohorts.length === 0 && (
        <div className="card">
          <p>{u.role === "ADMIN" ? "No cohorts yet. Start with Project #0: create a cohort for Agri Sensors and Controls, then a Mini project in it." : "You aren't in a class yet. Ask your teacher to add your email."}</p>
        </div>
      )}

      {cohorts.map((c) => {
        const students = c.members.filter((m) => m.user.role === "STUDENT");
        const teachers = c.members.filter((m) => m.user.role !== "STUDENT");
        return (
          <section key={c.id} className="card grid" aria-labelledby={`c-${c.id}`}>
            <div className="row between">
              <div>
                <div className="eyebrow">{c.institution}{c.session ? ` · ${c.session}` : ""}</div>
                <h2 id={`c-${c.id}`}>{c.name}</h2>
              </div>
              {staff && <span className="muted small">{teachers.length} staff · {students.length} students</span>}
            </div>

            <div>
              {c.projects.length === 0 && <p className="muted small">No projects yet.</p>}
              {c.projects.map((p) => {
                const status = Object.fromEntries(p.stages.map((s) => [s.stage, s.status as StageStatus]));
                const cur = currentStageIndex(status);
                const waiting = p.stages.some((s) => s.status === "SUBMITTED");
                return (
                  <div className="proj-row" key={p.id}>
                    <div>
                      <h3>{p.title}</h3>
                      <p className="muted small">{p.kit === "MINI" ? "Mini" : "Mega"} kit · {p.members.map((m) => m.user.name).join(", ") || "ASC team"}</p>
                    </div>
                    <div style={{ display: "grid", gap: 4 }}>
                      <div className="ladder" aria-label={`Stage ${cur + 1} of ${STAGES.length}`}>
                        {STAGES.map((s, i) => <i key={s.key} className={status[s.key] === "DONE" ? "d" : i === cur ? "n" : ""} />)}
                      </div>
                      <span className="muted small">
                        Stage {cur + 1} of {STAGES.length} · {STAGES[cur].title}
                        {waiting && <> · <span className="pill warn">Waiting for sign-off</span></>}
                      </span>
                    </div>
                    <Link className="btn ghost" href={`/studio/${p.id}`}>Open</Link>
                  </div>
                );
              })}
            </div>

            <div className="row">
              <NewProject cohortId={c.id} isStudent={u.role === "STUDENT"} />
              {staff && <AddMember cohortId={c.id} canAddTeacher={u.role === "ADMIN"} />}
            </div>

            {staff && c.members.length > 0 && (
              <details className="add">
                <summary>People in this cohort ({c.members.length})</summary>
                <div className="tbl-wrap">
                  <table>
                    <thead><tr><th>Name</th><th>Email</th><th>Role</th></tr></thead>
                    <tbody>
                      {c.members.map((m) => (
                        <tr key={m.user.id}><td>{m.user.name}</td><td>{m.user.email}</td><td>{m.user.role === "ADMIN" ? "ASC designer" : m.user.role === "TEACHER" ? "Teacher" : "Student"}</td></tr>
                      ))}
                    </tbody>
                  </table>
                </div>
              </details>
            )}
          </section>
        );
      })}
    </div>
  );
}
