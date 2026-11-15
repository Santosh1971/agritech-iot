import Link from "next/link";
import { redirect } from "next/navigation";
import { adminSession } from "@/lib/adminSession";
import FarmsClient from "./FarmsClient";

export default async function FarmsPage() {
  if (!(await adminSession())) redirect("/dashboard");
  return (
    <main style={{ maxWidth: 960, margin: "40px auto", padding: "0 16px", fontFamily: "sans-serif" }}>
      <p><Link href="/dashboard">← Devices</Link></p>
      <h1>Farms and app access</h1>
      <p style={{ color: "#555", maxWidth: 680 }}>
        Each farm holds its nodes. Every email added to a farm can sign in to the product apps
        (web app on iPhone or laptop, Android app through the cloud) and use all of that farm&apos;s
        zones and nodes. Admins see every farm.
      </p>
      <FarmsClient />
    </main>
  );
}
