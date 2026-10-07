-- CreateEnum
CREATE TYPE "StudioKit" AS ENUM ('MINI', 'MEGA');

-- CreateEnum
CREATE TYPE "StudioStageStatus" AS ENUM ('NOT_STARTED', 'IN_PROGRESS', 'SUBMITTED', 'DONE');

-- AlterEnum (Postgres 12+ allows several ADD VALUEs in one migration)
ALTER TYPE "Role" ADD VALUE 'TEACHER';
ALTER TYPE "Role" ADD VALUE 'STUDENT';

-- CreateTable
CREATE TABLE "StudioCohort" (
    "id" TEXT NOT NULL,
    "name" TEXT NOT NULL,
    "institution" TEXT NOT NULL,
    "session" TEXT,
    "createdById" TEXT NOT NULL,
    "createdAt" TIMESTAMP(3) NOT NULL DEFAULT CURRENT_TIMESTAMP,

    CONSTRAINT "StudioCohort_pkey" PRIMARY KEY ("id")
);

-- CreateTable
CREATE TABLE "StudioCohortMember" (
    "cohortId" TEXT NOT NULL,
    "userId" TEXT NOT NULL,
    "addedAt" TIMESTAMP(3) NOT NULL DEFAULT CURRENT_TIMESTAMP,

    CONSTRAINT "StudioCohortMember_pkey" PRIMARY KEY ("cohortId","userId")
);

-- CreateTable
CREATE TABLE "StudioProject" (
    "id" TEXT NOT NULL,
    "title" TEXT NOT NULL,
    "kit" "StudioKit" NOT NULL DEFAULT 'MINI',
    "cohortId" TEXT NOT NULL,
    "createdById" TEXT NOT NULL,
    "createdAt" TIMESTAMP(3) NOT NULL DEFAULT CURRENT_TIMESTAMP,
    "updatedAt" TIMESTAMP(3) NOT NULL,

    CONSTRAINT "StudioProject_pkey" PRIMARY KEY ("id")
);

-- CreateTable
CREATE TABLE "StudioProjectMember" (
    "projectId" TEXT NOT NULL,
    "userId" TEXT NOT NULL,

    CONSTRAINT "StudioProjectMember_pkey" PRIMARY KEY ("projectId","userId")
);

-- CreateTable
CREATE TABLE "StudioStage" (
    "id" TEXT NOT NULL,
    "projectId" TEXT NOT NULL,
    "stage" TEXT NOT NULL,
    "status" "StudioStageStatus" NOT NULL DEFAULT 'NOT_STARTED',
    "data" JSONB,
    "signedOffById" TEXT,
    "signedOffAt" TIMESTAMP(3),
    "updatedAt" TIMESTAMP(3) NOT NULL,

    CONSTRAINT "StudioStage_pkey" PRIMARY KEY ("id")
);

-- CreateTable
CREATE TABLE "StudioDesign" (
    "id" TEXT NOT NULL,
    "projectId" TEXT NOT NULL,
    "version" INTEGER NOT NULL,
    "ports" JSONB NOT NULL,
    "createdById" TEXT NOT NULL,
    "createdAt" TIMESTAMP(3) NOT NULL DEFAULT CURRENT_TIMESTAMP,

    CONSTRAINT "StudioDesign_pkey" PRIMARY KEY ("id")
);

-- CreateIndex
CREATE INDEX "StudioCohortMember_userId_idx" ON "StudioCohortMember"("userId");

-- CreateIndex
CREATE INDEX "StudioProject_cohortId_idx" ON "StudioProject"("cohortId");

-- CreateIndex
CREATE INDEX "StudioProjectMember_userId_idx" ON "StudioProjectMember"("userId");

-- CreateIndex
CREATE UNIQUE INDEX "StudioStage_projectId_stage_key" ON "StudioStage"("projectId", "stage");

-- CreateIndex
CREATE UNIQUE INDEX "StudioDesign_projectId_version_key" ON "StudioDesign"("projectId", "version");

-- AddForeignKey
ALTER TABLE "StudioCohort" ADD CONSTRAINT "StudioCohort_createdById_fkey" FOREIGN KEY ("createdById") REFERENCES "User"("id") ON DELETE RESTRICT ON UPDATE CASCADE;

-- AddForeignKey
ALTER TABLE "StudioCohortMember" ADD CONSTRAINT "StudioCohortMember_cohortId_fkey" FOREIGN KEY ("cohortId") REFERENCES "StudioCohort"("id") ON DELETE CASCADE ON UPDATE CASCADE;

-- AddForeignKey
ALTER TABLE "StudioCohortMember" ADD CONSTRAINT "StudioCohortMember_userId_fkey" FOREIGN KEY ("userId") REFERENCES "User"("id") ON DELETE CASCADE ON UPDATE CASCADE;

-- AddForeignKey
ALTER TABLE "StudioProject" ADD CONSTRAINT "StudioProject_cohortId_fkey" FOREIGN KEY ("cohortId") REFERENCES "StudioCohort"("id") ON DELETE CASCADE ON UPDATE CASCADE;

-- AddForeignKey
ALTER TABLE "StudioProject" ADD CONSTRAINT "StudioProject_createdById_fkey" FOREIGN KEY ("createdById") REFERENCES "User"("id") ON DELETE RESTRICT ON UPDATE CASCADE;

-- AddForeignKey
ALTER TABLE "StudioProjectMember" ADD CONSTRAINT "StudioProjectMember_projectId_fkey" FOREIGN KEY ("projectId") REFERENCES "StudioProject"("id") ON DELETE CASCADE ON UPDATE CASCADE;

-- AddForeignKey
ALTER TABLE "StudioProjectMember" ADD CONSTRAINT "StudioProjectMember_userId_fkey" FOREIGN KEY ("userId") REFERENCES "User"("id") ON DELETE CASCADE ON UPDATE CASCADE;

-- AddForeignKey
ALTER TABLE "StudioStage" ADD CONSTRAINT "StudioStage_projectId_fkey" FOREIGN KEY ("projectId") REFERENCES "StudioProject"("id") ON DELETE CASCADE ON UPDATE CASCADE;

-- AddForeignKey
ALTER TABLE "StudioStage" ADD CONSTRAINT "StudioStage_signedOffById_fkey" FOREIGN KEY ("signedOffById") REFERENCES "User"("id") ON DELETE SET NULL ON UPDATE CASCADE;

-- AddForeignKey
ALTER TABLE "StudioDesign" ADD CONSTRAINT "StudioDesign_projectId_fkey" FOREIGN KEY ("projectId") REFERENCES "StudioProject"("id") ON DELETE CASCADE ON UPDATE CASCADE;

-- AddForeignKey
ALTER TABLE "StudioDesign" ADD CONSTRAINT "StudioDesign_createdById_fkey" FOREIGN KEY ("createdById") REFERENCES "User"("id") ON DELETE RESTRICT ON UPDATE CASCADE;

