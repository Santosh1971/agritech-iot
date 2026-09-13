import { prisma } from "@/lib/prisma";
import type { SessionPayload } from "@/lib/session";

/**
 * Looks up the caller's active FlasherGrant by phone or email — checked
 * live on every call, never cached, so a revoke (active=false) takes effect
 * on the very next request regardless of what the app already downloaded.
 */
export async function findActiveGrant(session: SessionPayload) {
  const user = await prisma.user.findUnique({ where: { id: session.userId } });
  if (!user) return null;

  return prisma.flasherGrant.findFirst({
    where: {
      active: true,
      OR: [
        user.phone ? { phone: user.phone } : undefined,
        user.email ? { email: user.email } : undefined,
      ].filter((clause): clause is NonNullable<typeof clause> => clause !== undefined),
    },
  });
}
