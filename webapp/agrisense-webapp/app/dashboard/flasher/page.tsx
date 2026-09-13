import { cookies } from "next/headers";
import { verifySession } from "@/lib/session";
import FlasherAdminClient from "./FlasherAdminClient";

export default async function FlasherAdminPage() {
  const token = (await cookies()).get("agrisense_session")?.value;
  const session = token ? await verifySession(token) : null;
  if (!session) return null;

  if (session.role !== "ADMIN") {
    return (
      <main style={{ maxWidth: 960, margin: "40px auto", padding: "0 16px", fontFamily: "sans-serif" }}>
        <p>This page is for admins only.</p>
      </main>
    );
  }

  return (
    <main style={{ maxWidth: 960, margin: "40px auto", padding: "0 16px", fontFamily: "sans-serif" }}>
      <h1>NB Agri Flasher — Admin</h1>
      <p style={{ color: "#666" }}>
        Upload firmware builds and manage who can flash them from the field. Revoking access
        below takes effect on the person&apos;s very next flash attempt — it can&apos;t delete a
        bin they already downloaded, but it stops that bin from being usable again.
      </p>
      <FlasherAdminClient />
    </main>
  );
}
