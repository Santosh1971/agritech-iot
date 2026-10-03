-- AlterTable
ALTER TABLE "MobileAppBuild" ADD COLUMN "product" "Product";

-- CreateIndex
CREATE INDEX "MobileAppBuild_product_createdAt_idx" ON "MobileAppBuild"("product", "createdAt");
