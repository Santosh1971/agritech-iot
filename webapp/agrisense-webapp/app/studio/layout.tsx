import type { Metadata } from "next";
import Link from "next/link";
import { Montserrat, Nunito_Sans } from "next/font/google";
import { studioUser } from "@/lib/studio/access";
import { EngProvider, EngToggle } from "./EngView";
import "./studio.css";

const head = Montserrat({ subsets: ["latin"], weight: ["600", "700"], variable: "--font-head" });
const body = Nunito_Sans({ subsets: ["latin"], weight: ["400", "600", "700"], variable: "--font-body" });

export const metadata: Metadata = {
  title: "ASC Product Studio",
  icons: { icon: "/brand/logo-mark.png" },
};

export default async function StudioLayout({ children }: { children: React.ReactNode }) {
  const u = await studioUser();
  return (
    <div className={`studio ${head.variable} ${body.variable}`}>
      <EngProvider>
        <header className="top">
          <div className="top-in">
            <Link href="/studio" className="brand" aria-label="Studio home">
              <img src="/brand/logo-mark.png" alt="" width={34} height={34} />
              <span>
                <b>ASC Product Studio</b>
                <small>AGRI SENSORS AND CONTROLS</small>
              </span>
            </Link>
            {u && <span className="who">{u.email} · {u.role === "ADMIN" ? "ASC designer" : u.role === "TEACHER" ? "Teacher" : "Student"}</span>}
            {u?.role === "ADMIN" && <Link href="/dashboard" className="small">Dashboard</Link>}
            <EngToggle />
          </div>
        </header>
        <main>{children}</main>
      </EngProvider>
    </div>
  );
}
