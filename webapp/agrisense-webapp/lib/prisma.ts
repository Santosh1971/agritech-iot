import { PrismaClient } from "@prisma/client";

// Standard Next.js dev-mode singleton pattern — avoids exhausting Postgres connections
// from hot-reload creating a new PrismaClient on every file change.
const globalForPrisma = global as unknown as { prisma: PrismaClient };

export const prisma = globalForPrisma.prisma || new PrismaClient();

if (process.env.NODE_ENV !== "production") globalForPrisma.prisma = prisma;
