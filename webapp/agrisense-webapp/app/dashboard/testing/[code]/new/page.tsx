import Link from "next/link";
import { notFound } from "next/navigation";
import { prisma } from "@/lib/prisma";
import { trackUser } from "@/lib/tracker/server";
import { NotMember } from "../../ui";
import { NewIssueForm } from "../../IssueForms";

export const dynamic = "force-dynamic";

// ?title=&steps=&build= prefill (used by "Report as issue" on a failed checklist item).
export default async function NewIssuePage({ params, searchParams }: { params: Promise<{ code: string }>; searchParams: Promise<{ title?: string; steps?: string; build?: string }> }) {
  const user = await trackUser();
  if (!user) return null;
  if (!user.trackRole) return <NotMember isAdmin={user.isAdmin} />;
  const { code } = await params;
  const sp = await searchParams;
  const product = await prisma.trackProduct.findUnique({ where: { code: decodeURIComponent(code) } });
  if (!product || !product.active) notFound();

  // Versions default to the build being tested (or the latest one).
  const build = sp.build
    ? await prisma.trackBuild.findFirst({ where: { id: sp.build, productId: product.id } })
    : await prisma.trackBuild.findFirst({ where: { productId: product.id }, orderBy: { number: "desc" } });

  return (
    <>
      <div className="crumbs">
        <Link href="/dashboard/testing">Overview</Link><span>/</span>
        <Link href={`/dashboard/testing/${encodeURIComponent(product.code)}`}>{product.code}</Link><span>/</span><span>New issue</span>
      </div>
      <section className="card" style={{ maxWidth: 720 }}>
        <h1>Report an issue on {product.name}</h1>
        <NewIssueForm
          product={{ id: product.id, code: product.code, name: product.name, modules: product.modules, hasFirmware: product.hasFirmware, hasApp: product.hasApp, hasHardware: product.hasHardware }}
          defaults={{ foundFw: build?.fwVersion ?? undefined, foundApp: build?.appVersion ?? undefined, foundHw: build?.hwRev ?? undefined, title: sp.title?.slice(0, 200), steps: sp.steps?.slice(0, 2000) }}
        />
      </section>
    </>
  );
}
