import { cookies } from "next/headers";
import { verifySession } from "@/lib/session";

/** The signed-in session when it is an admin, else null. */
export async function adminSession() {
  const token = (await cookies()).get("agrisense_session")?.value;
  const s = token ? await verifySession(token) : null;
  return s?.role === "ADMIN" ? s : null;
}
