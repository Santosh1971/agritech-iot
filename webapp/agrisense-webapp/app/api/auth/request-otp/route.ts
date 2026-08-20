import { NextRequest, NextResponse } from "next/server";
import { prisma } from "@/lib/prisma";
import { generateOtpCode, sendOtpSms } from "@/lib/sms";

export async function POST(req: NextRequest) {
  const { phone } = await req.json();

  if (!phone || typeof phone !== "string") {
    return NextResponse.json({ error: "phone is required" }, { status: 400 });
  }

  const code = generateOtpCode();
  const expiresAt = new Date(Date.now() + 5 * 60 * 1000); // 5 min

  await prisma.otpCode.create({ data: { phone, code, expiresAt } });
  await sendOtpSms(phone, code);

  return NextResponse.json({ ok: true });
}
