import { trackUser } from "@/lib/tracker/server";
import { NotMember } from "../ui";
import BenchClient from "./BenchClient";

export const dynamic = "force-dynamic";

export default async function BenchPage() {
  const user = await trackUser();
  if (!user) return null;
  if (!user.trackRole) return <NotMember isAdmin={user.isAdmin} />;
  return <BenchClient />;
}
