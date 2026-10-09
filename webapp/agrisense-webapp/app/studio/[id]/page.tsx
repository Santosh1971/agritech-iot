import Link from "next/link";
import { notFound } from "next/navigation";
import { projectAccess, studioUser } from "@/lib/studio/access";
import { loadProject } from "@/lib/studio/server";
import ProjectClient from "./ProjectClient";

export const dynamic = "force-dynamic";

export default async function ProjectPage({ params }: { params: Promise<{ id: string }> }) {
  const { id } = await params;
  const u = await studioUser();
  if (!u) return <p>Log in with a studio account. <Link href="/login">Log in</Link></p>;
  const access = await projectAccess(u, id);
  if (!access) notFound();
  const state = await loadProject(id);
  if (!state) notFound();
  return <ProjectClient state={state} access={access} />;
}
