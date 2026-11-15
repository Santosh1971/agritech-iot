// POST /api/app/logout: ends the web app's session (the cookie is httpOnly,
// so the page cannot clear it itself). The Android app just drops its token.
import { NextResponse } from "next/server";

export async function POST() {
  const res = NextResponse.json({ ok: true });
  res.cookies.set("agrisense_session", "", { httpOnly: true, secure: process.env.NODE_ENV === "production", sameSite: "lax", maxAge: 0, path: "/" });
  return res;
}
