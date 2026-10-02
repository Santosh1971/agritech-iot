import { company, contacts, distributors } from "@/lib/site/company";
import { products } from "@/lib/site/products";
import ContactForm from "./ContactForm";

export const metadata = { title: "Contact" };

export default async function Contact({ searchParams }: { searchParams: Promise<{ product?: string; topic?: string }> }) {
  const { product, topic } = await searchParams;
  const known = products.find((p) => p.slug === product);
  const details = [
    company.email && { label: "Email", value: company.email, href: `mailto:${company.email}` },
    company.address && { label: "Office", value: company.address },
  ].filter(Boolean) as { label: string; value: string; href?: string }[];

  return (
    <>
      <section className="page-hero">
        <div className="wrap">
          <p className="eyebrow">Contact</p>
          <h1>Tell us what you need</h1>
          <p className="lead">Farmers, dealers, colleges and partners: send a message and we will get back to you.</p>
        </div>
      </section>
      <section>
        <div className="wrap split">
          <div className="card">
            <ContactForm
              defaultRole={topic === "college" ? "College or university" : ""}
              defaultMessage={known ? `I am interested in ${known.name}.` : ""}
            />
          </div>
          <div className="stack" style={{ gap: 24 }}>
            <div className="stack" style={{ gap: 12 }}>
              <h3>Call or WhatsApp</h3>
              {contacts.map((c) => (
                <div key={c.name} className="card" style={{ gap: 6, padding: 18 }}>
                  <b>{c.name}</b>
                  <span className="muted" style={{ fontSize: "0.9rem" }}>{c.role}</span>
                  <div className="btn-row" style={{ gap: 8 }}>
                    <a className="btn btn-primary" href={`tel:${c.phone.replace(/\s/g, "")}`}>Call {c.phone}</a>
                    <a className="btn btn-ghost" href={`https://wa.me/${c.phone.replace(/\D/g, "")}`}>WhatsApp</a>
                  </div>
                </div>
              ))}
            </div>
            {details.length > 0 && (
              <div className="stack" style={{ gap: 8 }}>
                {details.map((d) => (
                  <p key={d.label}><b>{d.label}:</b> {d.href ? <a href={d.href}>{d.value}</a> : d.value}</p>
                ))}
              </div>
            )}
            <div className="stack" style={{ gap: 8 }}>
              <h3>Distributors</h3>
              {distributors.map((d) => (
                <p key={d.name}><b>{d.name}</b>, {d.region}<br /><span className="muted">{d.role}</span></p>
              ))}
              <p className="muted">Want to sell our products in your region? Choose "Dealer" in the form.</p>
            </div>
          </div>
        </div>
      </section>
    </>
  );
}
