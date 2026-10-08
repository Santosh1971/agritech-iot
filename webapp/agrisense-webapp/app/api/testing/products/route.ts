import { NextRequest, NextResponse } from "next/server";
import { prisma } from "@/lib/prisma";
import { err, trackUser } from "@/lib/tracker/server";

// Admins add or edit tracked products. JSON POST
// { id?, code, name, modules: string[], hasFirmware, hasApp, hasHardware, active, sortOrder }
export async function POST(req: NextRequest) {
  const user = await trackUser();
  if (!user?.isAdmin) return err("Admins only", 403);
  const b = (await req.json().catch(() => null)) as Record<string, unknown> | null;
  if (!b) return err("Bad request");
  const code = typeof b.code === "string" ? b.code.trim().toUpperCase() : "";
  const name = typeof b.name === "string" ? b.name.trim() : "";
  if (!/^[A-Z0-9-]{2,12}$/.test(code)) return err("Code: 2-12 letters, digits or dashes");
  if (!name) return err("Name is required");
  const modules = Array.isArray(b.modules) ? b.modules.map((m) => String(m).trim()).filter(Boolean).slice(0, 10) : [];
  const data = {
    code,
    name: name.slice(0, 80),
    modules: modules.length ? modules : ["Controller"],
    hasFirmware: b.hasFirmware !== false,
    hasApp: b.hasApp !== false,
    hasHardware: b.hasHardware !== false,
    active: b.active !== false,
    sortOrder: Number.isFinite(Number(b.sortOrder)) ? Number(b.sortOrder) : 100,
  };
  try {
    const product = typeof b.id === "string" && b.id
      ? await prisma.trackProduct.update({ where: { id: b.id }, data })
      : await prisma.trackProduct.create({ data });
    return NextResponse.json({ product });
  } catch {
    return err("That code is already used");
  }
}
