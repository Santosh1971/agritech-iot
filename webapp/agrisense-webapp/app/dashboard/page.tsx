import { cookies } from "next/headers";
import { verifySession } from "@/lib/session";
import DevicesClient from "./devices/DevicesClient";

export default async function DashboardPage() {
  const token = (await cookies()).get("agrisense_session")?.value;
  const session = token ? await verifySession(token) : null;
  if (!session) return null;

  return (
    <main style={{ maxWidth: 960, margin: "40px auto", padding: "0 16px", fontFamily: "sans-serif" }}>
      <h1>Devices ({session.role})</h1>
      <DevicesClient role={session.role} />
    </main>
  );
}
