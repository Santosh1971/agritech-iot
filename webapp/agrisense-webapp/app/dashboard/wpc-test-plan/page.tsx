import { cookies } from "next/headers";
import { verifySession } from "@/lib/session";
import { findActiveGrant } from "@/lib/flasherGrant";
import WpcTestPlanClient from "./WpcTestPlanClient";

// Same access check as /dashboard/apps (any logged-in account with an
// active WPC flasher grant) — a grant means "this person works with this
// product", which is exactly who should be running its field test plan too.
export default async function WpcTestPlanPage() {
  const token = (await cookies()).get("agrisense_session")?.value;
  const session = token ? await verifySession(token) : null;
  if (!session) return null;

  const grant = await findActiveGrant(session);
  if (!grant || !grant.products.includes("WPC")) {
    return (
      <main style={{ maxWidth: 720, margin: "40px auto", padding: "0 16px", fontFamily: "sans-serif" }}>
        <h1>WPC Field Test Plan</h1>
        <p style={{ color: "#666" }}>
          No active WPC access for this account. Ask an admin to grant you WPC under the
          flasher&apos;s access list.
        </p>
      </main>
    );
  }

  return <WpcTestPlanClient />;
}
