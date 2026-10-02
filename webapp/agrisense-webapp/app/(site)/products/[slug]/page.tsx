import Link from "next/link";
import { notFound } from "next/navigation";
import Icon from "../../Icon";
import { productBySlug, products } from "@/lib/site/products";
import { distributors } from "@/lib/site/company";

export function generateStaticParams() {
  return products.map((p) => ({ slug: p.slug }));
}

export async function generateMetadata({ params }: { params: Promise<{ slug: string }> }) {
  const p = productBySlug((await params).slug);
  return p ? { title: p.name, description: p.line } : {};
}

export default async function ProductPage({ params }: { params: Promise<{ slug: string }> }) {
  const p = productBySlug((await params).slug);
  if (!p) notFound();
  const sasya = p.family !== "Water & pumps";
  const related = products.filter((o) => o.family === p.family && o.slug !== p.slug);

  return (
    <>
      <section className="page-hero">
        <div className="wrap">
          <p className="crumbs"><Link href="/products">Products</Link> / {p.family}</p>
          <div style={{ display: "flex", gap: 18, alignItems: "center", marginBottom: 14 }}>
            <span className="icon-badge" style={{ background: "#1f4b3b", color: "var(--lime)" }}><Icon name={p.icon} size={30} /></span>
            <p className="eyebrow">{sasya ? "Sasya Systems range" : "Agri Sensors and Controls"}</p>
          </div>
          <h1>{p.name}</h1>
          <p className="lead">{p.line}</p>
          {p.status && <p className="tag dev" style={{ marginTop: 16 }}>{p.status}</p>}
        </div>
      </section>

      <section>
        <div className="wrap grid-2">
          <div className="stack">
            <p className="eyebrow">The problem</p>
            <p style={{ fontSize: "1.15rem" }}>{p.problem}</p>
          </div>
          <div className="stack">
            <p className="eyebrow">Our answer</p>
            <p style={{ fontSize: "1.15rem" }}>{p.answer}</p>
          </div>
        </div>
      </section>

      <section className="band">
        <div className="wrap">
          <div className="section-head"><p className="eyebrow">How it works</p><h2>Three steps</h2></div>
          <ol className="steps">
            {p.how.map((h) => (
              <li key={h.step}><h3>{h.step}</h3><p className="muted">{h.detail}</p></li>
            ))}
          </ol>
        </div>
      </section>

      <section>
        <div className="wrap split">
          <div className="stack">
            <h2>Features</h2>
            <ul className="ticks">{p.features.map((f) => <li key={f}>{f}</li>)}</ul>
          </div>
          <div className="card">
            <h3>Made for</h3>
            <ul className="ticks">{p.forWhom.map((f) => <li key={f}>{f}</li>)}</ul>
            <p className="muted" style={{ marginTop: 10 }}>Prices depend on the size of your farm. Ask us for a quote.</p>
            <Link href={`/contact?product=${p.slug}`} className="btn btn-primary" style={{ marginTop: 6 }}>Ask for a quote</Link>
            {!sasya && distributors.map((d) => (
              <p key={d.name} className="muted" style={{ fontSize: "0.9rem" }}>In {d.region}: {d.name}</p>
            ))}
          </div>
        </div>
      </section>

      {related.length > 0 && (
        <section className="band">
          <div className="wrap">
            <div className="section-head"><h2>Related products</h2></div>
            <div className="grid">
              {related.map((o) => (
                <Link key={o.slug} href={`/products/${o.slug}`} className="card">
                  <span className="icon-badge"><Icon name={o.icon} /></span>
                  <h3>{o.name}</h3>
                  <p className="muted">{o.line}</p>
                </Link>
              ))}
            </div>
          </div>
        </section>
      )}
    </>
  );
}
