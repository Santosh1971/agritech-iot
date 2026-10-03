import { cookies } from "next/headers";
import { verifySession } from "@/lib/session";
import { findActiveGrant } from "@/lib/flasherGrant";
import { prisma } from "@/lib/prisma";
import type { Product } from "@prisma/client";

// "My apps" — one row per product this account is granted, showing only the
// latest real release (never a CI dev-<sha> build), with a direct download
// link. The companion page to /dashboard/flasher (which is admin-only and
// about *uploading* builds + managing grants); this one is for anyone with
// an active FlasherGrant, same access check as the firmware flasher itself
// since a grant really means "this person works with this product".
export default async function MyAppsPage() {
  const token = (await cookies()).get("agrisense_session")?.value;
  const session = token ? await verifySession(token) : null;
  if (!session) return null;

  const grant = await findActiveGrant(session);
  if (!grant) {
    return (
      <main style={{ maxWidth: 720, margin: "40px auto", padding: "0 16px", fontFamily: "sans-serif" }}>
        <h1>My apps</h1>
        <p style={{ color: "#666" }}>
          No active access yet for this account. Ask an admin to grant you a product under the
          flasher&apos;s access list.
        </p>
      </main>
    );
  }

  const rows = await prisma.mobileAppBuild.findMany({
    where: { product: { in: grant.products as Product[] } },
    orderBy: { createdAt: "desc" },
    select: { id: true, product: true, versionName: true, buildType: true, sizeBytes: true, notes: true, createdAt: true },
  });
  const latestByProduct = new Map<string, (typeof rows)[number]>();
  for (const row of rows) {
    if (row.versionName.startsWith("dev-")) continue;
    if (row.product && !latestByProduct.has(row.product)) latestByProduct.set(row.product, row);
  }
  const apps = Array.from(latestByProduct.values());

  return (
    <main style={{ maxWidth: 720, margin: "40px auto", padding: "0 16px", fontFamily: "sans-serif" }}>
      <h1>My apps</h1>
      <p style={{ color: "#666" }}>
        The current released app for each product you have access to. Always the latest version —
        there&apos;s nothing older to pick by mistake.
      </p>
      {apps.length === 0 ? (
        <p style={{ color: "#666" }}>
          No released app yet for {grant.products.join(", ")}. Check back once one has been
          published.
        </p>
      ) : (
        <table style={{ width: "100%", borderCollapse: "collapse", marginTop: 16 }}>
          <thead>
            <tr style={{ textAlign: "left", borderBottom: "1px solid #ddd" }}>
              <th style={{ padding: "8px 4px" }}>Product</th>
              <th style={{ padding: "8px 4px" }}>Version</th>
              <th style={{ padding: "8px 4px" }}>Size</th>
              <th style={{ padding: "8px 4px" }}>Published</th>
              <th style={{ padding: "8px 4px" }}></th>
            </tr>
          </thead>
          <tbody>
            {apps.map((a) => (
              <tr key={a.id} style={{ borderBottom: "1px solid #eee" }}>
                <td style={{ padding: "8px 4px", fontWeight: 600 }}>{a.product}</td>
                <td style={{ padding: "8px 4px" }}>{a.versionName}</td>
                <td style={{ padding: "8px 4px" }}>{(a.sizeBytes / 1_000_000).toFixed(1)} MB</td>
                <td style={{ padding: "8px 4px" }}>{a.createdAt.toLocaleDateString()}</td>
                <td style={{ padding: "8px 4px" }}>
                  <a href={`/api/flasher/apps/${a.id}`} style={{ color: "#1a7f37", fontWeight: 600 }}>
                    Download APK
                  </a>
                  {a.product === "WPC" && (
                    <>
                      {" · "}
                      <a href="/dashboard/wpc-test-plan" style={{ color: "#1a7f37", fontWeight: 600 }}>
                        Test plan
                      </a>
                    </>
                  )}
                </td>
              </tr>
            ))}
          </tbody>
        </table>
      )}
    </main>
  );
}
