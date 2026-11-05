-- CreateEnum
CREATE TYPE "LeadKind" AS ENUM ('BUYER', 'POLYHOUSE', 'PARTNER');

-- CreateEnum
CREATE TYPE "LeadHeat" AS ENUM ('HOT', 'WARM', 'COLD');

-- CreateEnum
CREATE TYPE "LeadStatus" AS ENUM ('NEW', 'CONTACTED', 'TALKING', 'WON', 'LOST', 'SKIPPED');

-- CreateTable
CREATE TABLE "Lead" (
    "id" TEXT NOT NULL,
    "platform" TEXT NOT NULL DEFAULT 'Instagram',
    "handle" TEXT NOT NULL,
    "name" TEXT,
    "kind" "LeadKind" NOT NULL,
    "heat" "LeadHeat" NOT NULL DEFAULT 'WARM',
    "said" TEXT NOT NULL,
    "postUrl" TEXT,
    "language" TEXT,
    "location" TEXT,
    "products" TEXT,
    "phone" TEXT,
    "nextStep" TEXT,
    "suggestedReply" TEXT,
    "status" "LeadStatus" NOT NULL DEFAULT 'NEW',
    "foundBy" TEXT NOT NULL DEFAULT 'Claude',
    "createdAt" TIMESTAMP(3) NOT NULL DEFAULT CURRENT_TIMESTAMP,
    "updatedAt" TIMESTAMP(3) NOT NULL,

    CONSTRAINT "Lead_pkey" PRIMARY KEY ("id")
);

-- CreateTable
CREATE TABLE "LeadNote" (
    "id" TEXT NOT NULL,
    "leadId" TEXT NOT NULL,
    "author" TEXT NOT NULL,
    "text" TEXT NOT NULL,
    "createdAt" TIMESTAMP(3) NOT NULL DEFAULT CURRENT_TIMESTAMP,

    CONSTRAINT "LeadNote_pkey" PRIMARY KEY ("id")
);

-- CreateIndex
CREATE UNIQUE INDEX "Lead_platform_handle_key" ON "Lead"("platform", "handle");

-- CreateIndex
CREATE INDEX "LeadNote_leadId_idx" ON "LeadNote"("leadId");

-- AddForeignKey
ALTER TABLE "LeadNote" ADD CONSTRAINT "LeadNote_leadId_fkey" FOREIGN KEY ("leadId") REFERENCES "Lead"("id") ON DELETE CASCADE ON UPDATE CASCADE;

