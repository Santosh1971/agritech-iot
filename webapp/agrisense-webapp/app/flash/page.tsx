import Link from "next/link";
import { cookies } from "next/headers";
import { verifySession } from "@/lib/session";
import { findActiveGrant } from "@/lib/flasherGrant";
import { USB_PRODUCTS } from "./products";
import FlashClient from "./FlashClient";

// /flash: put released firmware on a board by USB cable from Chrome/Edge on a
// laptop, for every product the account's NB Agri Flasher access covers. The
// laptop alternative to the flasher app (which stays for field use).
export default async function FlashPage() {
  const token = (await cookies()).get("agrisense_session")?.value;
  const session = token ? await verifySession(token) : null;
  if (!session) return null;

  const grant = await findActiveGrant(session);
  const granted = new Set((grant?.products ?? []).map(String));
  const products = Object.entries(USB_PRODUCTS)
    .filter(([id]) => granted.has(id))
    .map(([id, name]) => ({ id, name }));

  return (
    <main style={{ maxWidth: 760, margin: "32px auto", padding: "0 16px", fontFamily: "sans-serif" }}>
      <p style={{ margin: 0 }}><Link href="/dashboard">← Dashboard</Link></p>
      <h1 style={{ marginBottom: 4 }}>Flash by USB</h1>
      <p style={{ color: "#666", marginTop: 0 }}>
        Put released firmware on a board with a USB cable, from Chrome or Edge on a laptop. Same
        builds and access as the NB Agri Flasher app.
      </p>
      {!grant ? (
        <p>Your account has no NB Agri Flasher access. Ask an admin to add you on the Flasher admin page.</p>
      ) : products.length === 0 ? (
        <p>None of your flasher products can be flashed from this page yet (available: {Object.values(USB_PRODUCTS).join(", ")}).</p>
      ) : (
        <FlashClient products={products} admin={session.role === "ADMIN"} />
      )}
    </main>
  );
}
