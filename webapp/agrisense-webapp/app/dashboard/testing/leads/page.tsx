import { prisma } from "@/lib/prisma";
import { trackUser } from "@/lib/tracker/server";
import { NotMember } from "../ui";
import LeadsClient from "./LeadsClient";

export const dynamic = "force-dynamic";
export const metadata = { title: "Leads" };

export default async function LeadsPage() {
  const user = await trackUser();
  if (!user) return null;
  if (!user.trackRole && !user.isAdmin) return <NotMember isAdmin={false} />;

  const leads = await prisma.lead.findMany({ include: { notes: { orderBy: { createdAt: "asc" } } }, orderBy: { createdAt: "desc" } });
  return (
    <>
      <div>
        <span className="eyebrow">Sales</span>
        <h1>Leads</h1>
      </div>
      <section className="card">
        <h2>How to use this page</h2>
        <ol className="how">
          <li>Claude finds people who asked publicly for something we make and adds them here as <b>To contact</b>, with what to do and a reply you can copy.</li>
          <li>Open the post, find their comment, and reply from the Instagram app on your phone (or send them a DM). Keep the phone number out of comments; it is on our profile.</li>
          <li>Press <b>Contacted</b>. When they answer, press <b>Talking</b> and write what they need in a note: district, pump HP, single or three phase, number of wells.</li>
          <li>Finish with <b>Order won</b> or <b>Not interested</b>. Claude reads these notes and suggests the next step.</li>
        </ol>
        <p className="muted small">Reply to about 10 to 15 people a day, each a little differently. Instagram blocks accounts that post the same text many times.</p>
      </section>
      <LeadsClient leads={JSON.parse(JSON.stringify(leads))} me={user.name} />
    </>
  );
}
