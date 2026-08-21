import { NextRequest, NextResponse } from "next/server";
import { prisma } from "@/lib/prisma";
import { signSession } from "@/lib/session";

export async function POST(req: NextRequest) {
  const { phone, code } = await req.json();

  if (!phone || !code) {
    return NextResponse.json({ error: "phone and code are required" }, { status: 400 });
  }

  const otp = await prisma.otpCode.findFirst({
    where: { phone, code, verified: false, expiresAt: { gt: new Date() } },
    orderBy: { createdAt: "desc" },
  });

  if (!otp) {
    return NextResponse.json({ error: "Invalid or expired code" }, { status: 401 });
  }

  await prisma.otpCode.update({ where: { id: otp.id }, data: { verified: true } });

  // Users are provisioned by Admin (mirrors the WM1-Mini access model — no self-signup),
  // so a phone that passes OTP but has no matching User record can't log in yet.
  const user = await prisma.user.findUnique({ where: { phone } });
  if (!user) {
    return NextResponse.json(
      { error: "This number isn't registered yet. Contact your dealer or admin." },
      { status: 403 }
    );
  }

  const token = await signSession({ userId: user.id, phone: user.phone, role: user.role });

  const res = NextResponse.json({ ok: true, role: user.role });
  res.cookies.set("agrisense_session", token, {
    httpOnly: true,
    secure: process.env.NODE_ENV === "production",
    sameSite: "lax",
    maxAge: 30 * 24 * 60 * 60,
    path: "/",
  });
  return res;
}
