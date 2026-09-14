import { NextRequest, NextResponse } from "next/server";
import { verifySession } from "@/lib/session";

const PUBLIC_PATHS = ["/login", "/api/auth/request-otp", "/api/auth/verify-otp"];

export async function middleware(req: NextRequest) {
  const { pathname } = req.nextUrl;

  if (PUBLIC_PATHS.some((p) => pathname.startsWith(p)) || pathname.startsWith("/_next")) {
    return NextResponse.next();
  }

  // CI (GitHub Actions) has no inbox to receive an email-OTP code in, so
  // firmware and app-build uploads authenticate with a static bearer token
  // instead of a session cookie — see the matching check in each route's own
  // authorizedUploader(). Only bypasses the cookie check for these two exact
  // upload paths, and only with the exact secret; every other route
  // (including DELETE on either of these, and GET) is unaffected.
  const CI_BYPASS_PATHS = ["/api/admin/builds", "/api/admin/app-builds"];
  const ciToken = process.env.CI_UPLOAD_TOKEN;
  if (
    ciToken &&
    CI_BYPASS_PATHS.includes(pathname) &&
    req.headers.get("authorization") === `Bearer ${ciToken}`
  ) {
    return NextResponse.next();
  }

  const token = req.cookies.get("agrisense_session")?.value;
  const session = token ? await verifySession(token) : null;

  if (!session) {
    return NextResponse.redirect(new URL("/login", req.url));
  }

  return NextResponse.next();
}

export const config = {
  matcher: ["/((?!_next/static|_next/image|favicon.ico).*)"],
};
