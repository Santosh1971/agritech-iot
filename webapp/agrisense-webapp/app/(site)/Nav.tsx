"use client";

import Link from "next/link";
import { useEffect } from "react";
import { usePathname } from "next/navigation";

const links = [
  { href: "/products", label: "Products" },
  { href: "/education", label: "Education & Labs" },
  { href: "/about", label: "About" },
  { href: "/contact", label: "Contact" },
];

export default function Nav() {
  const path = usePathname();
  // Close the phone menu after moving to another page.
  useEffect(() => {
    const box = document.getElementById("menu") as HTMLInputElement | null;
    if (box) box.checked = false;
  }, [path]);
  return (
    <>
      <input type="checkbox" id="menu" className="menu-check" />
      <label htmlFor="menu" className="menu-toggle">Menu</label>
      <nav className="nav" aria-label="Main">
        {links.map((l) => (
          <Link key={l.href} href={l.href} aria-current={path.startsWith(l.href) ? "page" : undefined}>
            {l.label}
          </Link>
        ))}
        <Link href="/login" className="btn btn-login">Login</Link>
      </nav>
    </>
  );
}
