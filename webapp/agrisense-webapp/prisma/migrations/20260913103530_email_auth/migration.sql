/*
  Warnings:

  - You are about to drop the column `phone` on the `OtpCode` table. All the data in the column will be lost.
  - Added the required column `email` to the `OtpCode` table without a default value. This is not possible if the table is not empty.
  - Made the column `email` on table `User` required. This step will fail if there are existing NULL values in that column.

*/
-- DropIndex
DROP INDEX "OtpCode_phone_expiresAt_idx";

-- AlterTable
ALTER TABLE "OtpCode" DROP COLUMN "phone",
ADD COLUMN     "email" TEXT NOT NULL;

-- AlterTable
ALTER TABLE "User" ALTER COLUMN "phone" DROP NOT NULL,
ALTER COLUMN "email" SET NOT NULL;

-- CreateIndex
CREATE INDEX "OtpCode_email_expiresAt_idx" ON "OtpCode"("email", "expiresAt");
