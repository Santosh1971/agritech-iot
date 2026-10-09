-- AlterTable
ALTER TABLE "StudioDesign" ADD COLUMN     "app" JSONB;

-- CreateTable
CREATE TABLE "StudioFieldRecord" (
    "id" TEXT NOT NULL,
    "projectId" TEXT NOT NULL,
    "t" TIMESTAMP(3) NOT NULL,
    "design" INTEGER NOT NULL,
    "values" JSONB NOT NULL,
    "outputs" JSONB NOT NULL,

    CONSTRAINT "StudioFieldRecord_pkey" PRIMARY KEY ("id")
);

-- CreateIndex
CREATE UNIQUE INDEX "StudioFieldRecord_projectId_t_key" ON "StudioFieldRecord"("projectId", "t");

-- AddForeignKey
ALTER TABLE "StudioFieldRecord" ADD CONSTRAINT "StudioFieldRecord_projectId_fkey" FOREIGN KEY ("projectId") REFERENCES "StudioProject"("id") ON DELETE CASCADE ON UPDATE CASCADE;

