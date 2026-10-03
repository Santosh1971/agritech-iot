import { NextRequest, NextResponse } from "next/server";
import { cookies } from "next/headers";
import { verifySession } from "@/lib/session";
import { prisma } from "@/lib/prisma";
import { findActiveGrant } from "@/lib/flasherGrant";
import { readFirmwareBuild } from "@/lib/firmwareStorage";

async function getSession() {
  const token = (await cookies()).get("agrisense_session")?.value;
  return token ? await verifySession(token) : null;
}

// Grant-gated download of a product's own companion app -- the non-admin
// counterpart to /api/admin/app-builds/[id] (which is admin-only, for the
// flasher tool itself). Re-checks the live grant on every download, same
// as /api/flasher/download for firmware, so revoking access stops this
// too. A build with product=null (the flasher tool) is never reachable
// here -- that's what /api/admin/app-builds/[id] is for.
export async function GET(_req: NextRequest, { params }: { params: Promise<{ id: string }> }) {
  const session = await getSession();
  if (!session) return NextResponse.json({ error: "Unauthorized" }, { status: 401 });

  const grant = await findActiveGrant(session);
  if (!grant) {
    return NextResponse.json({ error: "No active flasher access for this account" }, { status: 403 });
  }

  const { id } = await params;
  const appBuild = await prisma.mobileAppBuild.findUnique({ where: { id } });
  if (!appBuild) return NextResponse.json({ error: "Not found" }, { status: 404 });
  if (!appBuild.product || !grant.products.includes(appBuild.product)) {
    return NextResponse.json({ error: "Not granted access to this app" }, { status: 403 });
  }

  const bytes = await readFirmwareBuild(appBuild.storagePath);
  return new NextResponse(new Uint8Array(bytes), {
    headers: {
      "Content-Type": "application/vnd.android.package-archive",
      "Content-Disposition": `attachment; filename="${appBuild.product.toLowerCase()}-app-${appBuild.versionName}.apk"`,
      "X-App-Sha256": appBuild.sha256,
    },
  });
}
