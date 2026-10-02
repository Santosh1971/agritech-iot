import Link from "next/link";
import { team } from "@/lib/site/company";

export const metadata = { title: "About us" };

export default function About() {
  return (
    <>
      <section className="page-hero">
        <div className="wrap">
          <p className="eyebrow">About us</p>
          <h1>Engineers who have spent a decade in farmers' fields</h1>
          <p className="lead">
            Agri Sensors and Controls designs, builds and supports automation for farms, greenhouses and fisheries, and
            teaches the next generation of agri-entrepreneurs to do the same.
          </p>
        </div>
      </section>

      <section>
        <div className="wrap split">
          <div className="stack" style={{ gap: 20 }}>
            <p className="eyebrow">Our story</p>
            <ol className="timeline">
              <li><span className="when">2015</span><b>A S Agri Systems</b><span className="muted">Our first company, building the Nirantar Dhaara irrigation system.</span></li>
              <li><span className="when">2019</span><b>Sasya Systems</b><span className="muted">Smart irrigation, greenhouse climate control, cold storage and biofloc automation, from Bangalore.</span></li>
              <li><span className="when">Today</span><b>Agri Sensors and Controls</b><span className="muted">Pump controllers, drip automation and nursery watering, plus the Sasya range, sold through dealers, and Agri IoT labs for colleges.</span></li>
            </ol>
          </div>
          <div className="card">
            <h3>How we build</h3>
            <ul className="ticks">
              <li>Start from a farmer's problem, in the field</li>
              <li>Design hardware, firmware, phone apps and cloud in-house</li>
              <li>Test every unit on our own production test jig</li>
              <li>Support dealers with training and remote diagnostics</li>
              <li>Update devices over the air as they improve</li>
            </ul>
          </div>
        </div>
      </section>

      <section className="band">
        <div className="wrap">
          <div className="section-head"><p className="eyebrow">Team</p><h2>Who we are</h2></div>
          <div className="grid-2">
            {team.map((t) => (
              <div key={t.name} className="card">
                <h3>{t.name}</h3>
                <p className="tag">{t.role}</p>
                <p className="muted">{t.bio}</p>
              </div>
            ))}
          </div>
        </div>
      </section>

      <section>
        <div className="wrap">
          <div className="card cta">
            <h2 style={{ fontSize: "1.6rem" }}>Work with us as a dealer, partner or college</h2>
            <Link href="/contact" className="btn btn-primary">Contact us</Link>
          </div>
        </div>
      </section>
    </>
  );
}
