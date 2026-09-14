-- CreateTable
CREATE TABLE "MobileAppBuild" (
    "id" TEXT NOT NULL,
    "versionName" TEXT NOT NULL,
    "buildType" TEXT NOT NULL DEFAULT 'debug',
    "storagePath" TEXT NOT NULL,
    "sha256" TEXT NOT NULL,
    "sizeBytes" INTEGER NOT NULL,
    "notes" TEXT,
    "uploadedById" TEXT NOT NULL,
    "createdAt" TIMESTAMP(3) NOT NULL DEFAULT CURRENT_TIMESTAMP,

    CONSTRAINT "MobileAppBuild_pkey" PRIMARY KEY ("id")
);

-- CreateIndex
CREATE INDEX "MobileAppBuild_createdAt_idx" ON "MobileAppBuild"("createdAt");

-- AddForeignKey
ALTER TABLE "MobileAppBuild" ADD CONSTRAINT "MobileAppBuild_uploadedById_fkey" FOREIGN KEY ("uploadedById") REFERENCES "User"("id") ON DELETE RESTRICT ON UPDATE CASCADE;
