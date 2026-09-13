-- CreateTable
CREATE TABLE "FirmwareBuild" (
    "id" TEXT NOT NULL,
    "product" "Product" NOT NULL,
    "version" TEXT NOT NULL,
    "variant" TEXT NOT NULL DEFAULT 'esp32dev',
    "storagePath" TEXT NOT NULL,
    "sha256" TEXT NOT NULL,
    "sizeBytes" INTEGER NOT NULL,
    "notes" TEXT,
    "uploadedById" TEXT NOT NULL,
    "createdAt" TIMESTAMP(3) NOT NULL DEFAULT CURRENT_TIMESTAMP,

    CONSTRAINT "FirmwareBuild_pkey" PRIMARY KEY ("id")
);

-- CreateTable
CREATE TABLE "FlasherGrant" (
    "id" TEXT NOT NULL,
    "phone" TEXT,
    "email" TEXT,
    "label" TEXT NOT NULL,
    "products" "Product"[],
    "active" BOOLEAN NOT NULL DEFAULT true,
    "grantedById" TEXT NOT NULL,
    "createdAt" TIMESTAMP(3) NOT NULL DEFAULT CURRENT_TIMESTAMP,
    "revokedAt" TIMESTAMP(3),

    CONSTRAINT "FlasherGrant_pkey" PRIMARY KEY ("id")
);

-- CreateTable
CREATE TABLE "FlashEvent" (
    "id" TEXT NOT NULL,
    "grantId" TEXT NOT NULL,
    "buildId" TEXT NOT NULL,
    "deviceId" TEXT,
    "result" TEXT NOT NULL,
    "detail" TEXT,
    "occurredAt" TIMESTAMP(3) NOT NULL DEFAULT CURRENT_TIMESTAMP,

    CONSTRAINT "FlashEvent_pkey" PRIMARY KEY ("id")
);

-- CreateIndex
CREATE INDEX "FirmwareBuild_product_version_idx" ON "FirmwareBuild"("product", "version");

-- CreateIndex
CREATE INDEX "FlasherGrant_phone_idx" ON "FlasherGrant"("phone");

-- CreateIndex
CREATE INDEX "FlasherGrant_email_idx" ON "FlasherGrant"("email");

-- CreateIndex
CREATE INDEX "FlashEvent_grantId_occurredAt_idx" ON "FlashEvent"("grantId", "occurredAt");

-- AddForeignKey
ALTER TABLE "FirmwareBuild" ADD CONSTRAINT "FirmwareBuild_uploadedById_fkey" FOREIGN KEY ("uploadedById") REFERENCES "User"("id") ON DELETE RESTRICT ON UPDATE CASCADE;

-- AddForeignKey
ALTER TABLE "FlasherGrant" ADD CONSTRAINT "FlasherGrant_grantedById_fkey" FOREIGN KEY ("grantedById") REFERENCES "User"("id") ON DELETE RESTRICT ON UPDATE CASCADE;

-- AddForeignKey
ALTER TABLE "FlashEvent" ADD CONSTRAINT "FlashEvent_grantId_fkey" FOREIGN KEY ("grantId") REFERENCES "FlasherGrant"("id") ON DELETE RESTRICT ON UPDATE CASCADE;

-- AddForeignKey
ALTER TABLE "FlashEvent" ADD CONSTRAINT "FlashEvent_buildId_fkey" FOREIGN KEY ("buildId") REFERENCES "FirmwareBuild"("id") ON DELETE RESTRICT ON UPDATE CASCADE;
