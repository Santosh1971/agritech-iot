import { cookies } from "next/headers";
import { verifySession } from "@/lib/session";
import { readLibraryIndex } from "@/lib/library";
import DocLibrary from "./DocLibrary";

export const metadata = { title: "Document library · Agri Sensors and Controls" };
export const dynamic = "force-dynamic";

export default async function LibraryPage() {
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
  return <DocLibrary index={await readLibraryIndex()} />;
}
