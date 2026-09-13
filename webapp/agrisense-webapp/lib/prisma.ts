import { PrismaClient } from "@prisma/client";
import { PrismaPg } from "@prisma/adapter-pg";

// Standard Next.js dev-mode singleton pattern — avoids exhausting Postgres connections
// from hot-reload creating a new PrismaClient on every file change.
const globalForPrisma = global as unknown as { prisma: PrismaClient };

// Prisma 7 requires an explicit driver adapter instead of reading
// DATABASE_URL implicitly — @prisma/adapter-pg was already a listed
// dependency (and prisma.config.ts's datasource already uses it for the
// CLI), just never wired into the runtime client.
const adapter = new PrismaPg({ connectionString: process.env.DATABASE_URL });

export const prisma = globalForPrisma.prisma || new PrismaClient({ adapter });

if (process.env.NODE_ENV !== "production") globalForPrisma.prisma = prisma;
