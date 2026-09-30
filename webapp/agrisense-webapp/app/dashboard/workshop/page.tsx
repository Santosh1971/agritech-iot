import { cookies } from "next/headers";
import { verifySession } from "@/lib/session";
import WorkshopLibrary from "./WorkshopLibrary";

export const metadata = { title: "Workshop library · Agri Sensors and Controls" };

export default async function WorkshopLibraryPage() {
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

  return <WorkshopLibrary />;
}
