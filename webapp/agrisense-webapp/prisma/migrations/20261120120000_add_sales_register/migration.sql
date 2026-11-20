-- CreateEnum
CREATE TYPE "PaymentStatus" AS ENUM ('PAID', 'PARTIAL', 'PENDING');

-- CreateTable
CREATE TABLE "Sale" (
    "id" TEXT NOT NULL,
    "customer" TEXT NOT NULL,
    "customerType" TEXT,
    "location" TEXT,
    "invoiceNo" TEXT,
    "dispatchDate" TIMESTAMP(3),
    "paymentStatus" "PaymentStatus" NOT NULL DEFAULT 'PENDING',
    "notes" TEXT,
    "createdBy" TEXT NOT NULL,
    "createdAt" TIMESTAMP(3) NOT NULL DEFAULT CURRENT_TIMESTAMP,
    "updatedAt" TIMESTAMP(3) NOT NULL,

    CONSTRAINT "Sale_pkey" PRIMARY KEY ("id")
);

-- CreateTable
CREATE TABLE "SaleUnit" (
    "id" TEXT NOT NULL,
    "saleId" TEXT NOT NULL,
    "product" TEXT NOT NULL,
    "deviceId" TEXT NOT NULL,
    "warrantyMonths" INTEGER NOT NULL DEFAULT 12,
    "note" TEXT,
    "createdAt" TIMESTAMP(3) NOT NULL DEFAULT CURRENT_TIMESTAMP,

    CONSTRAINT "SaleUnit_pkey" PRIMARY KEY ("id")
);

-- CreateIndex
CREATE INDEX "Sale_customer_idx" ON "Sale"("customer");

-- CreateIndex
CREATE INDEX "Sale_invoiceNo_idx" ON "Sale"("invoiceNo");

-- CreateIndex
CREATE INDEX "SaleUnit_deviceId_idx" ON "SaleUnit"("deviceId");

-- CreateIndex
CREATE INDEX "SaleUnit_saleId_idx" ON "SaleUnit"("saleId");

-- AddForeignKey
ALTER TABLE "SaleUnit" ADD CONSTRAINT "SaleUnit_saleId_fkey" FOREIGN KEY ("saleId") REFERENCES "Sale"("id") ON DELETE CASCADE ON UPDATE CASCADE;

