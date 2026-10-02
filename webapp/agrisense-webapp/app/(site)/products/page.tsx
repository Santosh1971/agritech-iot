import Link from "next/link";
import Icon from "../Icon";
import { families, products } from "@/lib/site/products";

export const metadata = { title: "Products" };

const intro: Record<string, string> = {
  "Water & pumps": "Controllers for pumps, drip and nursery watering, designed and built by Agri Sensors and Controls.",
  "Sasya: sensing & climate": "Sensor and automation systems from Sasya Systems, in the field since 2019.",
};

export default function Products() {
  return (
    <>
      <section className="page-hero">
        <div className="wrap">
          <p className="eyebrow">Products</p>
          <h1>Automation for every part of the farm</h1>
          <p className="lead">From a single nursery to a field of borewells, a greenhouse, a cold store or a fish farm.</p>
        </div>
      </section>
      {families.map((f, i) => (
        <section key={f} className={i % 2 ? "band" : undefined}>
          <div className="wrap">
            <div className="section-head">
              <h2>{f}</h2>
              <p className="lead">{intro[f]}</p>
            </div>
            <div className="grid">
              {products.filter((p) => p.family === f).map((p) => (
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
      ))}
    </>
  );
}
