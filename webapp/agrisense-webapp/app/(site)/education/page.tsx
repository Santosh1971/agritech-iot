import Link from "next/link";
import Icon from "../Icon";

export const metadata = {
  title: "Education & Labs",
  description: "Hands-on IoT workshops and a four-year Agri IoT lab programme for agriculture colleges.",
};

const years = [
  { year: "Year 1", name: "Discover", text: "Basic electronics, sensors, first programs and their own phone app." },
  { year: "Year 2", name: "Build", text: "Bluetooth, WiFi, LoRa and cloud; automatic control of pumps and valves." },
  { year: "Year 3", name: "Solve", text: "A real farmer's problem, a field-proof design and a pilot on a farm." },
  { year: "Year 4", name: "Launch", text: "A small product batch, pricing, service and a pitch, inside RAWE and ELP." },
];

const offers = [
  { icon: "school", title: "One-day workshops", text: "Students flash real hardware, then write their own programs by describing them to an AI assistant. No coding background needed." },
  { icon: "chip", title: "Agri IoT lab", text: "Lab design, student kits and a living lab on the college farm: weather, soil moisture, smart drip and pump control." },
  { icon: "users", title: "Four-year programme", text: "Weekly teaching visits, curriculum, and a seniors-teach-juniors culture that keeps the lab running by itself." },
  { icon: "phone", title: "Online lab tools", text: "A browser flasher, ready programs, a personal farm app and prompt cards that stay open after class." },
] as const;

export default function Education() {
  return (
    <>
      <section className="page-hero">
        <div className="wrap">
          <p className="eyebrow">Education & Labs</p>
          <h1>Agriculture students who build, not only use, farm technology</h1>
          <p className="lead">
            Students learn about sensors, IoT and drones, but rarely build one. We bring hands-on labs, real farm
            hardware and AI-assisted programming to agriculture colleges.
          </p>
        </div>
      </section>

      <section>
        <div className="wrap">
          <div className="section-head"><p className="eyebrow">What we offer colleges</p><h2>From a single workshop to a full lab</h2></div>
          <div className="grid">
            {offers.map((o) => (
              <div key={o.title} className="card">
                <span className="icon-badge"><Icon name={o.icon} /></span>
                <h3>{o.title}</h3>
                <p className="muted">{o.text}</p>
              </div>
            ))}
          </div>
        </div>
      </section>

      <section className="band-dark">
        <div className="wrap split">
          <div className="stack" style={{ gap: 16 }}>
            <p className="eyebrow">Pilot · GPS Institute of Agricultural Management · 1 Oct 2026</p>
            <h2>In one day, 11 students with no coding background programmed real farm hardware</h2>
            <p className="lead">
              They read temperature and humidity, measured water flow, switched a pump, and built their own phone app
              over Bluetooth, WiFi and the cloud. In the afternoon each student wrote a program by describing it to an AI
              assistant and ran it on the board.
            </p>
          </div>
          <div className="numbers">
            <div><strong>4.7/5</strong><span className="muted">students' rating of the day</span></div>
            <div><strong>8 of 11</strong><span className="muted">want to use an IoT lab regularly</span></div>
            <div><strong>10 of 11</strong><span className="muted">would happily teach juniors</span></div>
          </div>
        </div>
      </section>

      <section>
        <div className="wrap">
          <div className="section-head">
            <p className="eyebrow">The four-year programme</p>
            <h2>From first blink to first customer</h2>
            <p className="lead">About 70% hands-on, a kit for every student, and every practical tied to a farm problem.</p>
          </div>
          <ol className="steps">
            {years.map((y) => (
              <li key={y.year}>
                <p className="eyebrow">{y.year}</p>
                <h3>{y.name}</h3>
                <p className="muted">{y.text}</p>
              </li>
            ))}
          </ol>
        </div>
      </section>

      <section className="band">
        <div className="wrap">
          <div className="section-head">
            <p className="eyebrow">What students want to build</p>
            <h2>Agriculture problems first, electronics second</h2>
          </div>
          <ul className="ticks" style={{ gridTemplateColumns: "repeat(auto-fit, minmax(min(320px, 100%), 1fr))" }}>
            <li>Low-cost irrigation for small and marginal farmers</li>
            <li>Weight-based feeding for goats and small ruminants</li>
            <li>Pest and disease risk sensing in storage and export containers</li>
            <li>Detecting adulteration in fertiliser and inputs</li>
            <li>Crop health and early disease detection</li>
            <li>Water-saving and nutrient requirement sensors</li>
          </ul>
        </div>
      </section>

      <section>
        <div className="wrap">
          <div className="card cta">
            <div className="stack" style={{ gap: 6 }}>
              <h2 style={{ fontSize: "1.6rem" }}>Bring a workshop or lab to your college</h2>
              <p className="muted">Students from our workshops can keep building with the <Link href="/workshop">online lab pages</Link>.</p>
            </div>
            <Link href="/contact?topic=college" className="btn btn-primary">Invite us</Link>
          </div>
        </div>
      </section>
    </>
  );
}
