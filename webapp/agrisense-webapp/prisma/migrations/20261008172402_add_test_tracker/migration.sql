-- CreateEnum
CREATE TYPE "TrackRole" AS ENUM ('TESTER', 'DEVELOPER');

-- CreateEnum
CREATE TYPE "TrackArea" AS ENUM ('FIRMWARE', 'APP', 'HARDWARE', 'DOCS');

-- CreateEnum
CREATE TYPE "TrackSeverity" AS ENUM ('BLOCKER', 'MAJOR', 'MINOR', 'SUGGESTION');

-- CreateEnum
CREATE TYPE "TrackStatus" AS ENUM ('NEW', 'NEED_INFO', 'ACCEPTED', 'IN_PROGRESS', 'FIX_READY', 'REOPENED', 'VERIFIED', 'DEFERRED', 'WONT_FIX');

-- CreateEnum
CREATE TYPE "TrackReleaseStatus" AS ENUM ('OPEN', 'SIGNED_OFF', 'RELEASED');

-- CreateEnum
CREATE TYPE "TrackFileKind" AS ENUM ('ATTACHMENT', 'FIRMWARE', 'APK', 'GERBER');

-- CreateTable
CREATE TABLE "TrackProduct" (
    "id" TEXT NOT NULL,
    "code" TEXT NOT NULL,
    "name" TEXT NOT NULL,
    "modules" TEXT[],
    "hasFirmware" BOOLEAN NOT NULL DEFAULT true,
    "hasApp" BOOLEAN NOT NULL DEFAULT true,
    "hasHardware" BOOLEAN NOT NULL DEFAULT true,
    "active" BOOLEAN NOT NULL DEFAULT true,
    "sortOrder" INTEGER NOT NULL DEFAULT 0,
    "nextIssueNo" INTEGER NOT NULL DEFAULT 1,
    "nextBuildNo" INTEGER NOT NULL DEFAULT 1,

    CONSTRAINT "TrackProduct_pkey" PRIMARY KEY ("id")
);

-- CreateTable
CREATE TABLE "TrackMember" (
    "id" TEXT NOT NULL,
    "userId" TEXT NOT NULL,
    "role" "TrackRole" NOT NULL,
    "active" BOOLEAN NOT NULL DEFAULT true,
    "createdAt" TIMESTAMP(3) NOT NULL DEFAULT CURRENT_TIMESTAMP,

    CONSTRAINT "TrackMember_pkey" PRIMARY KEY ("id")
);

-- CreateTable
CREATE TABLE "TrackRelease" (
    "id" TEXT NOT NULL,
    "productId" TEXT NOT NULL,
    "name" TEXT NOT NULL,
    "fwVersion" TEXT,
    "appVersion" TEXT,
    "hwRev" TEXT,
    "notes" TEXT,
    "status" "TrackReleaseStatus" NOT NULL DEFAULT 'OPEN',
    "candidateBuildId" TEXT,
    "signedOffById" TEXT,
    "signedOffAt" TIMESTAMP(3),
    "releasedById" TEXT,
    "releasedAt" TIMESTAMP(3),
    "createdAt" TIMESTAMP(3) NOT NULL DEFAULT CURRENT_TIMESTAMP,

    CONSTRAINT "TrackRelease_pkey" PRIMARY KEY ("id")
);

-- CreateTable
CREATE TABLE "TrackIssue" (
    "id" TEXT NOT NULL,
    "productId" TEXT NOT NULL,
    "number" INTEGER NOT NULL,
    "releaseId" TEXT,
    "title" TEXT NOT NULL,
    "area" "TrackArea" NOT NULL,
    "module" TEXT,
    "severity" "TrackSeverity" NOT NULL,
    "status" "TrackStatus" NOT NULL DEFAULT 'NEW',
    "proposal" "TrackStatus",
    "proposalNote" TEXT,
    "foundFw" TEXT,
    "foundApp" TEXT,
    "foundHw" TEXT,
    "deviceId" TEXT,
    "steps" TEXT NOT NULL,
    "expected" TEXT,
    "actual" TEXT NOT NULL,
    "reporterId" TEXT NOT NULL,
    "createdAt" TIMESTAMP(3) NOT NULL DEFAULT CURRENT_TIMESTAMP,
    "updatedAt" TIMESTAMP(3) NOT NULL,

    CONSTRAINT "TrackIssue_pkey" PRIMARY KEY ("id")
);

-- CreateTable
CREATE TABLE "TrackBuild" (
    "id" TEXT NOT NULL,
    "productId" TEXT NOT NULL,
    "number" INTEGER NOT NULL,
    "releaseId" TEXT,
    "fwVersion" TEXT,
    "appVersion" TEXT,
    "hwRev" TEXT,
    "notes" TEXT,
    "checklist" JSONB NOT NULL DEFAULT '[]',
    "firmwareBuildId" TEXT,
    "appBuildId" TEXT,
    "createdById" TEXT NOT NULL,
    "createdAt" TIMESTAMP(3) NOT NULL DEFAULT CURRENT_TIMESTAMP,

    CONSTRAINT "TrackBuild_pkey" PRIMARY KEY ("id")
);

-- CreateTable
CREATE TABLE "TrackBuildIssue" (
    "buildId" TEXT NOT NULL,
    "issueId" TEXT NOT NULL,
    "verdict" TEXT,
    "note" TEXT,
    "verdictAt" TIMESTAMP(3),

    CONSTRAINT "TrackBuildIssue_pkey" PRIMARY KEY ("buildId","issueId")
);

-- CreateTable
CREATE TABLE "TrackEvent" (
    "id" TEXT NOT NULL,
    "issueId" TEXT,
    "buildId" TEXT,
    "releaseId" TEXT,
    "actorId" TEXT NOT NULL,
    "kind" TEXT NOT NULL,
    "fromStatus" "TrackStatus",
    "toStatus" "TrackStatus",
    "text" TEXT,
    "createdAt" TIMESTAMP(3) NOT NULL DEFAULT CURRENT_TIMESTAMP,

    CONSTRAINT "TrackEvent_pkey" PRIMARY KEY ("id")
);

-- CreateTable
CREATE TABLE "TrackFile" (
    "id" TEXT NOT NULL,
    "kind" "TrackFileKind" NOT NULL,
    "issueId" TEXT,
    "buildId" TEXT,
    "releaseId" TEXT,
    "eventId" TEXT,
    "originalName" TEXT NOT NULL,
    "storagePath" TEXT NOT NULL,
    "mime" TEXT NOT NULL,
    "sizeBytes" INTEGER NOT NULL,
    "sha256" TEXT NOT NULL,
    "uploadedById" TEXT NOT NULL,
    "createdAt" TIMESTAMP(3) NOT NULL DEFAULT CURRENT_TIMESTAMP,

    CONSTRAINT "TrackFile_pkey" PRIMARY KEY ("id")
);

-- CreateIndex
CREATE UNIQUE INDEX "TrackProduct_code_key" ON "TrackProduct"("code");

-- CreateIndex
CREATE UNIQUE INDEX "TrackMember_userId_key" ON "TrackMember"("userId");

-- CreateIndex
CREATE UNIQUE INDEX "TrackRelease_candidateBuildId_key" ON "TrackRelease"("candidateBuildId");

-- CreateIndex
CREATE INDEX "TrackRelease_productId_status_idx" ON "TrackRelease"("productId", "status");

-- CreateIndex
CREATE INDEX "TrackIssue_productId_status_idx" ON "TrackIssue"("productId", "status");

-- CreateIndex
CREATE UNIQUE INDEX "TrackIssue_productId_number_key" ON "TrackIssue"("productId", "number");

-- CreateIndex
CREATE UNIQUE INDEX "TrackBuild_productId_number_key" ON "TrackBuild"("productId", "number");

-- CreateIndex
CREATE INDEX "TrackEvent_issueId_createdAt_idx" ON "TrackEvent"("issueId", "createdAt");

-- CreateIndex
CREATE INDEX "TrackEvent_buildId_createdAt_idx" ON "TrackEvent"("buildId", "createdAt");

-- AddForeignKey
ALTER TABLE "TrackMember" ADD CONSTRAINT "TrackMember_userId_fkey" FOREIGN KEY ("userId") REFERENCES "User"("id") ON DELETE RESTRICT ON UPDATE CASCADE;

-- AddForeignKey
ALTER TABLE "TrackRelease" ADD CONSTRAINT "TrackRelease_productId_fkey" FOREIGN KEY ("productId") REFERENCES "TrackProduct"("id") ON DELETE RESTRICT ON UPDATE CASCADE;

-- AddForeignKey
ALTER TABLE "TrackRelease" ADD CONSTRAINT "TrackRelease_candidateBuildId_fkey" FOREIGN KEY ("candidateBuildId") REFERENCES "TrackBuild"("id") ON DELETE SET NULL ON UPDATE CASCADE;

-- AddForeignKey
ALTER TABLE "TrackRelease" ADD CONSTRAINT "TrackRelease_signedOffById_fkey" FOREIGN KEY ("signedOffById") REFERENCES "User"("id") ON DELETE SET NULL ON UPDATE CASCADE;

-- AddForeignKey
ALTER TABLE "TrackRelease" ADD CONSTRAINT "TrackRelease_releasedById_fkey" FOREIGN KEY ("releasedById") REFERENCES "User"("id") ON DELETE SET NULL ON UPDATE CASCADE;

-- AddForeignKey
ALTER TABLE "TrackIssue" ADD CONSTRAINT "TrackIssue_productId_fkey" FOREIGN KEY ("productId") REFERENCES "TrackProduct"("id") ON DELETE RESTRICT ON UPDATE CASCADE;

-- AddForeignKey
ALTER TABLE "TrackIssue" ADD CONSTRAINT "TrackIssue_releaseId_fkey" FOREIGN KEY ("releaseId") REFERENCES "TrackRelease"("id") ON DELETE SET NULL ON UPDATE CASCADE;

-- AddForeignKey
ALTER TABLE "TrackIssue" ADD CONSTRAINT "TrackIssue_reporterId_fkey" FOREIGN KEY ("reporterId") REFERENCES "User"("id") ON DELETE RESTRICT ON UPDATE CASCADE;

-- AddForeignKey
ALTER TABLE "TrackBuild" ADD CONSTRAINT "TrackBuild_productId_fkey" FOREIGN KEY ("productId") REFERENCES "TrackProduct"("id") ON DELETE RESTRICT ON UPDATE CASCADE;

-- AddForeignKey
ALTER TABLE "TrackBuild" ADD CONSTRAINT "TrackBuild_releaseId_fkey" FOREIGN KEY ("releaseId") REFERENCES "TrackRelease"("id") ON DELETE SET NULL ON UPDATE CASCADE;

-- AddForeignKey
ALTER TABLE "TrackBuild" ADD CONSTRAINT "TrackBuild_firmwareBuildId_fkey" FOREIGN KEY ("firmwareBuildId") REFERENCES "FirmwareBuild"("id") ON DELETE SET NULL ON UPDATE CASCADE;

-- AddForeignKey
ALTER TABLE "TrackBuild" ADD CONSTRAINT "TrackBuild_appBuildId_fkey" FOREIGN KEY ("appBuildId") REFERENCES "MobileAppBuild"("id") ON DELETE SET NULL ON UPDATE CASCADE;

-- AddForeignKey
ALTER TABLE "TrackBuild" ADD CONSTRAINT "TrackBuild_createdById_fkey" FOREIGN KEY ("createdById") REFERENCES "User"("id") ON DELETE RESTRICT ON UPDATE CASCADE;

-- AddForeignKey
ALTER TABLE "TrackBuildIssue" ADD CONSTRAINT "TrackBuildIssue_buildId_fkey" FOREIGN KEY ("buildId") REFERENCES "TrackBuild"("id") ON DELETE CASCADE ON UPDATE CASCADE;

-- AddForeignKey
ALTER TABLE "TrackBuildIssue" ADD CONSTRAINT "TrackBuildIssue_issueId_fkey" FOREIGN KEY ("issueId") REFERENCES "TrackIssue"("id") ON DELETE CASCADE ON UPDATE CASCADE;

-- AddForeignKey
ALTER TABLE "TrackEvent" ADD CONSTRAINT "TrackEvent_issueId_fkey" FOREIGN KEY ("issueId") REFERENCES "TrackIssue"("id") ON DELETE CASCADE ON UPDATE CASCADE;

-- AddForeignKey
ALTER TABLE "TrackEvent" ADD CONSTRAINT "TrackEvent_buildId_fkey" FOREIGN KEY ("buildId") REFERENCES "TrackBuild"("id") ON DELETE CASCADE ON UPDATE CASCADE;

-- AddForeignKey
ALTER TABLE "TrackEvent" ADD CONSTRAINT "TrackEvent_releaseId_fkey" FOREIGN KEY ("releaseId") REFERENCES "TrackRelease"("id") ON DELETE CASCADE ON UPDATE CASCADE;

-- AddForeignKey
ALTER TABLE "TrackEvent" ADD CONSTRAINT "TrackEvent_actorId_fkey" FOREIGN KEY ("actorId") REFERENCES "User"("id") ON DELETE RESTRICT ON UPDATE CASCADE;

-- AddForeignKey
ALTER TABLE "TrackFile" ADD CONSTRAINT "TrackFile_issueId_fkey" FOREIGN KEY ("issueId") REFERENCES "TrackIssue"("id") ON DELETE CASCADE ON UPDATE CASCADE;

-- AddForeignKey
ALTER TABLE "TrackFile" ADD CONSTRAINT "TrackFile_buildId_fkey" FOREIGN KEY ("buildId") REFERENCES "TrackBuild"("id") ON DELETE CASCADE ON UPDATE CASCADE;

-- AddForeignKey
ALTER TABLE "TrackFile" ADD CONSTRAINT "TrackFile_releaseId_fkey" FOREIGN KEY ("releaseId") REFERENCES "TrackRelease"("id") ON DELETE CASCADE ON UPDATE CASCADE;

-- AddForeignKey
ALTER TABLE "TrackFile" ADD CONSTRAINT "TrackFile_eventId_fkey" FOREIGN KEY ("eventId") REFERENCES "TrackEvent"("id") ON DELETE SET NULL ON UPDATE CASCADE;

-- AddForeignKey
ALTER TABLE "TrackFile" ADD CONSTRAINT "TrackFile_uploadedById_fkey" FOREIGN KEY ("uploadedById") REFERENCES "User"("id") ON DELETE RESTRICT ON UPDATE CASCADE;

-- Products tracked at launch (8 Oct 2026). The LoRa valve node is a module of
-- WM1-Pro; WM1-Mini has no wireless valve node.
INSERT INTO "TrackProduct" ("id", "code", "name", "modules", "hasFirmware", "hasApp", "hasHardware", "sortOrder") VALUES
  ('tp_fg1',     'FG1',      'FG1 FlowGuard',  ARRAY['Controller'],                    true, true,  true, 10),
  ('tp_wpc_m',   'WPC-M',    'WPC Master',     ARRAY['Master'],                        true, true,  true, 20),
  ('tp_wpc_pn',  'WPC-PN',   'WPC Pump Node',  ARRAY['Pump node'],                     true, false, true, 30),
  ('tp_wm1_mini','WM1-MINI', 'WM1-Mini',       ARRAY['Controller'],                    true, true,  true, 40),
  ('tp_wm1_pro', 'WM1-PRO',  'WM1-Pro',        ARRAY['Controller', 'LoRa valve node'], true, true,  true, 50),
  ('tp_wm2',     'WM2',      'WM2 WaterMaster',ARRAY['Controller'],                    true, true,  true, 60);
