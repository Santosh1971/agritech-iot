import { prisma } from "@/lib/prisma";
import { trackUser } from "@/lib/tracker/server";
import { deviceInfo } from "@/lib/sales/server";
import { NotMember } from "../ui";
import SalesClient from "./SalesClient";

export const dynamic = "force-dynamic";
export const metadata = { title: "Sales register" };

export default async function SalesPage() {
  const user = await trackUser();
  if (!user) return null;
  if (!user.trackRole && !user.isAdmin) return <NotMember isAdmin={false} />;

  const sales = await prisma.sale.findMany({
    include: { units: { orderBy: { createdAt: "asc" } } },
    orderBy: [{ dispatchDate: { sort: "desc", nulls: "last" } }, { createdAt: "desc" }],
  });
  const devices = await deviceInfo(sales.flatMap((s) => s.units.map((u) => u.deviceId)));
  return (
    <>
      <div>
        <span className="eyebrow">Sales</span>
        <h1>Sales register</h1>
        <p className="muted small">Which device went to whom, under which invoice, when, and whether it is paid. Money and GST stay in Zoho Books; this page tracks the devices.</p>
      </div>
      <SalesClient sales={JSON.parse(JSON.stringify(sales))} devices={devices} />
    </>
  );
}
