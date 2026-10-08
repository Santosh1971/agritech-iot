import type { Metadata } from "next";
import Link from "next/link";
import { Montserrat, Nunito_Sans } from "next/font/google";
import { trackUser } from "@/lib/tracker/server";
import { ROLE_LABEL } from "@/lib/tracker/rules";
import "./tracker.css";

const head = Montserrat({ subsets: ["latin"], weight: ["600", "700"], variable: "--font-head" });
const body = Nunito_Sans({ subsets: ["latin"], weight: ["400", "600", "700"], variable: "--font-body" });

export const metadata: Metadata = { title: "Product Testing", icons: { icon: "/brand/logo-mark.png" } };

export default async function TestingLayout({ children }: { children: React.ReactNode }) {
  const u = await trackUser();
  return (
    <div className={`tt ${head.variable} ${body.variable}`}>
      <header className="top">
        <div className="top-in">
          <Link href="/dashboard/testing" className="brand" aria-label="Testing home">
            <img src="/brand/logo-mark.png" alt="" width={32} height={32} />
            <span>
              <b>Product Testing</b>
              <small>AGRI SENSORS AND CONTROLS</small>
            </span>
          </Link>
          {u && <span className="who">{u.name}{u.trackRole ? ` · ${ROLE_LABEL[u.trackRole]}` : ""}</span>}
          <nav className="nav">
            <Link href="/dashboard/testing">Overview</Link>
            {u?.isAdmin && <Link href="/dashboard/testing/settings">Team</Link>}
            <Link href="/dashboard">Dashboard</Link>
          </nav>
        </div>
      </header>
      <main>{children}</main>
    </div>
  );
}
