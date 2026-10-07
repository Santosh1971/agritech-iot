import type { Metadata } from "next";
import Link from "next/link";
import { Montserrat, Nunito_Sans } from "next/font/google";
import { company, contacts, distributors } from "@/lib/site/company";
import { products } from "@/lib/site/products";
import Nav from "./Nav";
import "./site.css";

const head = Montserrat({ subsets: ["latin"], weight: ["600", "700"], variable: "--font-head" });
const body = Nunito_Sans({ subsets: ["latin"], weight: ["400", "600", "700"], variable: "--font-body" });

export const metadata: Metadata = {
  title: { default: `${company.name} | Farm automation and IoT`, template: `%s | ${company.name}` },
  description:
    "Agri Sensors and Controls designs pump controllers, irrigation automation and farm sensors, and runs hands-on IoT labs for agriculture colleges.",
  icons: { icon: "/brand/logo-mark.png" },
};

export default function SiteLayout({ children }: { children: React.ReactNode }) {
  return (
    <div className={`as-site ${head.variable} ${body.variable}`}>
      <header className="site-header">
        <div className="wrap">
          <Link href="/" className="brand" aria-label={`${company.name} home`}>
            <img src="/brand/logo-mark.png" alt="" width={40} height={40} />
            <span>
              <b>Agri Sensors</b>
              <small>AND CONTROLS</small>
            </span>
          </Link>
          <Nav />
        </div>
      </header>

      <main>{children}</main>

      <footer className="site-footer">
        <div className="wrap">
          <div className="cols">
            <div className="stack" style={{ gap: 10 }}>
              <img src="/brand/logo-full.png" alt={company.name} width={120} height={113} style={{ borderRadius: 12 }} />
              <p>{company.tagline}.</p>
              {contacts.map((c) => (
                <p key={c.name} style={{ fontSize: "0.9rem" }}>
                  {c.name}: <a href={`tel:${c.phone.replace(/\s/g, "")}`}>{c.phone}</a>
                </p>
              ))}
              {company.email && <p style={{ fontSize: "0.9rem" }}><a href={`mailto:${company.email}`}>{company.email}</a></p>}
            </div>
            <div>
              <h3>Water & pumps</h3>
              <ul>
                {products.filter((p) => p.family === "Water & pumps").map((p) => (
                  <li key={p.slug}><Link href={`/products/${p.slug}`}>{p.name}</Link></li>
                ))}
              </ul>
            </div>
            <div>
              <h3>Sasya range</h3>
              <ul>
                {products.filter((p) => p.family !== "Water & pumps").map((p) => (
                  <li key={p.slug}><Link href={`/products/${p.slug}`}>{p.short}</Link></li>
                ))}
              </ul>
            </div>
            <div>
              <h3>Company</h3>
              <ul>
                <li><Link href="/education">Education & Labs</Link></li>
                <li><Link href="/about">About us</Link></li>
                <li><Link href="/contact">Contact</Link></li>
                <li><Link href="/privacy">Privacy Policy</Link></li>
                <li><Link href="/login">Customer login</Link></li>
              </ul>
              {distributors.map((d) => (
                <p key={d.name} style={{ marginTop: 14, fontSize: "0.88rem" }}>
                  Distributor, {d.region}: {d.name}
                </p>
              ))}
            </div>
          </div>
          <p className="legal">© {new Date().getFullYear()} {company.name} · {company.domain}</p>
        </div>
      </footer>
    </div>
  );
}
