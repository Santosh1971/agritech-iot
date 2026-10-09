-- AlterEnum
ALTER TYPE "Product" ADD VALUE 'ASC_KIT';

-- AlterTable
ALTER TABLE "StudioDesign" ADD COLUMN "rules" JSONB;
