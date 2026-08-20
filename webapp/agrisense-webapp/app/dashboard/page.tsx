import { cookies } from "next/headers";
import { verifySession } from "@/lib/session";
import { prisma } from "@/lib/prisma";

export default async function DashboardPage() {
  const token = (await cookies()).get("agrisense_session")?.value;
  const session = token ? verifySession(token) : null;
  if (!session) return null; // middleware already redirects; this satisfies TS

  // Role-scoped device query — matches the Admin/Dealer/Customer access model.
  const where =
    session.role === "ADMIN"
      ? {}
      : session.role === "DEALER"
      ? { dealerId: session.userId }
      : { ownerId: session.userId };

  const devices = await prisma.device.findMany({ where, orderBy: { createdAt: "desc" } });

  return (
    <main style={{ maxWidth: 720, margin: "40px auto", fontFamily: "sans-serif" }}>
      <h1>Devices ({session.role})</h1>
      {devices.length === 0 && <p>No devices yet.</p>}
      <ul>
        {devices.map((d) => (
          <li key={d.id}>
            <strong>{d.name}</strong> — {d.product} — {d.deviceId} —{" "}
            {d.lastSeenAt ? `last seen ${d.lastSeenAt.toLocaleString()}` : "never seen"}
          </li>
        ))}
      </ul>
    </main>
  );
}
