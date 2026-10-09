import Link from "next/link";
import { cookies } from "next/headers";
import { redirect } from "next/navigation";
import { verifySession } from "@/lib/session";
import DevicesClient from "./devices/DevicesClient";

export default async function DashboardPage() {
  const token = (await cookies()).get("agrisense_session")?.value;
  const session = token ? await verifySession(token) : null;
  if (!session) return null;
  if (session.role === "STUDENT" || session.role === "TEACHER") redirect("/studio");

  return (
    <main style={{ maxWidth: 960, margin: "40px auto", padding: "0 16px", fontFamily: "sans-serif" }}>
      <div style={{ display: "flex", justifyContent: "space-between", alignItems: "baseline" }}>
        <h1>Devices ({session.role})</h1>
        <span style={{ display: "flex", gap: 18 }}>
          <Link href="/dashboard/apps">My apps →</Link>
          {session.role === "ADMIN" && (
            <>
              <Link href="/dashboard/testing">Product testing →</Link>
              <Link href="/studio">Product Studio →</Link>
              <Link href="/dashboard/library">Documents & videos →</Link>
              <Link href="/dashboard/workshop">Workshop library →</Link>
              <Link href="/dashboard/flasher">NB Agri Flasher admin →</Link>
            </>
          )}
        </span>
      </div>
      <DevicesClient role={session.role} />
    </main>
  );
}
