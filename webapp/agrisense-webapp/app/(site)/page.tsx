import Link from "next/link";
import Icon from "./Icon";
import { products } from "@/lib/site/products";

const problems = [
  { icon: "moon", title: "Night trips to switch pumps", text: "Power comes in fixed slots. WPC switches every pump by radio from one master.", href: "/products/wpc" },
  { icon: "bolt", title: "Pumps running dry, motors burning", text: "Flow and power sensing stop the pump and tell you why.", href: "/products/wpc" },
  { icon: "valve", title: "Drip and fertigation by hand", text: "WaterMaster runs zones and dosing on schedule, on solar power.", href: "/products/watermaster" },
  { icon: "drop", title: "Nursery watering forgotten", text: "FlowGuard waters by litres or minutes, even without internet.", href: "/products/flowguard" },
  { icon: "leaf", title: "Greenhouse climate drifting", text: "Sensors and rules run fans, pads and fertigation automatically.", href: "/products/sasya-climate-control" },
  { icon: "fish", title: "Fish tank water turning bad", text: "Biofloc water quality tracked live, with alerts on the phone.", href: "/products/sasya-biofloc" },
] as const;

export default function Home() {
  return (
    <>
      <section className="hero">
        <div className="wrap">
          <div className="stack" style={{ gap: 22 }}>
            <p className="eyebrow">Farm automation · IoT · Agri education</p>
            <h1>Farm automation that saves water, power and sleepless nights</h1>
            <p className="lead">
              We design and build pump controllers, irrigation automation and farm sensors in India, and teach agriculture
              students to build their own.
            </p>
            <div className="btn-row">
              <Link href="/products" className="btn btn-primary">See our products</Link>
              <Link href="/contact" className="btn btn-ghost">Talk to us</Link>
            </div>
          </div>
          <div className="hero-art">
            <img src="/brand/logo-full.png" alt="Agri Sensors and Controls logo" width={480} height={451} />
          </div>
        </div>
      </section>

      <section>
        <div className="wrap">
          <div className="section-head">
            <p className="eyebrow">Problems we solve</p>
            <h2>Every product starts with a problem farmers told us about</h2>
          </div>
          <div className="problems">
            {problems.map((p) => (
              <Link key={p.title} href={p.href} className="problem" style={{ textDecoration: "none", color: "inherit" }}>
                <span className="icon-badge"><Icon name={p.icon} /></span>
                <span>
                  <b>{p.title}</b>
                  <span className="muted">{p.text}</span>
                </span>
              </Link>
            ))}
          </div>
        </div>
      </section>

      <section className="band">
        <div className="wrap">
          <div className="section-head">
            <p className="eyebrow">Products</p>
            <h2>Eight products, one platform</h2>
            <p className="lead">Our water and pump controllers, and the Sasya Systems range for soil, greenhouse, cold storage and aquaculture.</p>
          </div>
          <div className="grid">
            {products.map((p) => (
              <Link key={p.slug} href={`/products/${p.slug}`} className="card">
                <span className="icon-badge"><Icon name={p.icon} /></span>
                <h3>{p.name}</h3>
                <p className="muted">{p.line}</p>
                <span className="more">Learn more →</span>
              </Link>
            ))}
          </div>
        </div>
      </section>

      <section className="band-dark">
        <div className="wrap split">
          <div className="stack" style={{ gap: 18 }}>
            <p className="eyebrow">Education & Labs</p>
            <h2>Agriculture students who build their own farm devices</h2>
            <p className="lead">
              Hands-on IoT workshops and a four-year lab programme for agriculture colleges. Students program real
              sensors and pumps with the help of AI, and test them on the farm.
            </p>
            <div className="btn-row">
              <Link href="/education" className="btn btn-primary">For colleges</Link>
            </div>
          </div>
          <div className="numbers">
            <div><strong>4.7/5</strong><span className="muted">students' rating, GPSIOAM workshop, Oct 2026</span></div>
            <div><strong>11/11</strong><span className="muted">students programmed hardware on day one</span></div>
            <div><strong>4 years</strong><span className="muted">from first sensor to first customer</span></div>
          </div>
        </div>
      </section>

      <section>
        <div className="wrap">
          <div className="numbers" style={{ marginBottom: 48 }}>
            <div><strong style={{ color: "var(--teal-deep)" }}>2015</strong><span className="muted">building farm automation since our first company</span></div>
            <div><strong style={{ color: "var(--teal-deep)" }}>30+ yrs</strong><span className="muted">of embedded and IoT engineering in the team</span></div>
            <div><strong style={{ color: "var(--teal-deep)" }}>In-house</strong><span className="muted">hardware, firmware, apps, cloud and testing</span></div>
          </div>
          <div className="card cta">
            <div className="stack" style={{ gap: 6 }}>
              <h2 style={{ fontSize: "1.6rem" }}>Farmer, dealer, college or partner?</h2>
              <p className="muted">Tell us what you need. We will get back to you.</p>
            </div>
            <Link href="/contact" className="btn btn-primary">Contact us</Link>
          </div>
        </div>
      </section>
    </>
  );
}
