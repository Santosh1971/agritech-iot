import React from "react";
import { AbsoluteFill, interpolate, spring, useCurrentFrame, useVideoConfig } from "remotion";
import { BODY, C, Caption, DISPLAY, Pop, clamp, useSceneFade } from "../common";
import { t } from "../i18n";

// WaterMaster explainer scenes (vertical 1080x1920). Every scene takes its length
// as `dur` and spreads its beats across it, so it fits any voice-over length.

/** The WaterMaster controller box. */
export const WmDevice: React.FC<{ x?: number; y?: number; width?: number; screen: string; f: number; zone?: number }> = ({
  x = 0,
  y = 0,
  width = 300,
  screen,
  f,
  zone = 0,
}) => (
  <svg x={x} y={y} width={width} height={width * 1.2} viewBox="0 0 300 360">
    <rect x="20" y="20" width="260" height="320" rx="30" fill="#FFFFFF" stroke={C.line} strokeWidth="8" />
    <rect x="46" y="52" width="208" height="110" rx="12" fill={C.teal} />
    <text x="150" y="122" textAnchor="middle" fontSize="44" fontWeight="800" fill="#FFFFFF" fontFamily="monospace">
      {screen}
    </text>
    <text x="150" y="212" textAnchor="middle" fontSize="30" fontWeight="800" fill={C.teal} fontFamily="sans-serif">
      WaterMaster
    </text>
    {[0, 1, 2, 3, 4, 5].map((k) => {
      const on = k === 0 ? zone > 0 : k === 1 ? zone === 1 || zone === 2 : k - 1 === zone;
      return <circle key={k} cx={60 + k * 36} cy="270" r="12" fill={on ? (k === 1 ? C.leaf : C.ok) : "#B8C2C9"} opacity={on ? 0.75 + 0.25 * Math.sin(f / 3) : 1} />;
    })}
    <text x="150" y="314" textAnchor="middle" fontSize="18" fontWeight="700" fill="#5B6B75" fontFamily="sans-serif">
      PUMP · DOSE · V1 V2 V3 V4
    </text>
  </svg>
);

/** A small farmer figure; `walk` swings the legs. */
const Farmer: React.FC<{ x: number; y: number; s?: number; walk?: number; bucket?: boolean }> = ({ x, y, s = 1, walk = 0, bucket }) => (
  <g transform={`translate(${x},${y}) scale(${s})`}>
    <line x1="-6" y1="60" x2={Math.sin(walk) * 20} y2="126" stroke="#5B3A28" strokeWidth="12" strokeLinecap="round" />
    <line x1="6" y1="60" x2={-Math.sin(walk) * 20} y2="126" stroke="#5B3A28" strokeWidth="12" strokeLinecap="round" />
    <rect x="-28" y="-10" width="56" height="80" rx="20" fill="#E8E1CF" />
    <circle cx="0" cy="-38" r="25" fill="#8A5A3C" />
    <ellipse cx="0" cy="-58" rx="28" ry="13" fill="#D98E04" />
    {bucket && (
      <g>
        <line x1="20" y1="10" x2="44" y2="50" stroke="#E8E1CF" strokeWidth="12" strokeLinecap="round" />
        <path d="M 30 50 L 66 50 L 60 92 L 36 92 Z" fill="#5C7A8A" stroke="#2F434F" strokeWidth="4" />
        <text x="48" y="80" textAnchor="middle" fontSize="20" fontWeight="800" fill="#FFF">NPK</text>
      </g>
    )}
  </g>
);

const Valve: React.FC<{ x: number; y: number; open: boolean; s?: number }> = ({ x, y, open, s = 1 }) => (
  <g transform={`translate(${x},${y}) scale(${s})`}>
    <rect x="-22" y="-14" width="44" height="28" rx="6" fill={open ? C.ok : "#8C9AA3"} stroke={C.line} strokeWidth="4" />
    <line x1="0" y1="-14" x2="0" y2="-34" stroke={C.line} strokeWidth="6" />
    <circle cx="0" cy="-38" r="9" fill={open ? C.ok : C.red} />
  </g>
);

// 1. Every zone opened by hand, fertiliser by guesswork.
export const ManualDrip: React.FC<{ dur: number }> = ({ dur }) => {
  const f = useCurrentFrame();
  const fade = useSceneFade(dur, 0, 8);
  const x = interpolate(f, [0, dur], [120, 880], clamp);
  return (
    <AbsoluteFill style={{ opacity: fade, background: "linear-gradient(#F7D9A8, #F5EBD3 45%)" }}>
      <svg width="1080" height="1920" viewBox="0 0 1080 1920">
        <circle cx="200" cy="300" r="90" fill="#F6C445" />
        <rect y="1050" width="1080" height="870" fill="#C9A36B" />
        {Array.from({ length: 8 }, (_, r) => (
          <g key={r}>
            <line x1="40" x2="1040" y1={1110 + r * 100} y2={1110 + r * 100} stroke="#3D5A6B" strokeWidth="6" />
            {Array.from({ length: 12 }, (_, c) => (
              <path key={c} d={`M ${70 + c * 82} ${1100 + r * 100} q 12 -40 0 -60 q -12 20 0 60`} fill="#4E8A3A" />
            ))}
          </g>
        ))}
        {[0, 1, 2, 3].map((k) => (
          <Valve key={k} x={150 + k * 250} y={1060} open={x > 150 + k * 250} s={1.4} />
        ))}
        <Farmer x={x} y={950} s={1.6} walk={f * 0.4} bucket />
      </svg>
      <div style={{ position: "absolute", top: 420, width: "100%", display: "flex", flexDirection: "column", gap: 26 }}>
        <Pop at={10}>
          <Caption color={C.ink} style={{ textShadow: "none" }}>
            {t("हर ज़ोन का वाल्व हाथ से?")}
          </Caption>
        </Pop>
        <Pop at={Math.round(dur * 0.45)}>
          <Caption size={96} color="#8A3A1E" style={{ textShadow: "none" }}>
            {t("खाद भी अंदाज़े से…")}
          </Caption>
        </Pop>
      </div>
    </AbsoluteFill>
  );
};

const ProblemCard: React.FC<{ icon: React.ReactNode; text: string }> = ({ icon, text }) => (
  <div
    style={{
      width: 940,
      minHeight: 240,
      borderRadius: 32,
      background: "#FFFFFF",
      border: "4px solid #E2C9A8",
      display: "flex",
      alignItems: "center",
      gap: 30,
      padding: "10px 40px",
      boxSizing: "border-box",
      position: "relative",
      boxShadow: "0 10px 30px rgba(90,53,20,0.15)",
    }}
  >
    {icon}
    <div style={{ fontFamily: DISPLAY, fontWeight: 800, fontSize: 64, color: C.ink, lineHeight: 1.2 }}>{text}</div>
    <div style={{ position: "absolute", right: -18, top: -18, width: 76, height: 76, borderRadius: 38, background: C.red, display: "flex", alignItems: "center", justifyContent: "center" }}>
      <svg width="40" height="40" viewBox="0 0 16 16">
        <path d="M3 3 L13 13 M13 3 L3 13" stroke="#fff" strokeWidth="3" strokeLinecap="round" />
      </svg>
    </div>
  </div>
);

const ClockIcon: React.FC<{ f: number }> = ({ f }) => (
  <svg width="160" height="160" viewBox="0 0 200 200">
    <circle cx="100" cy="100" r="74" fill="#F3E3B5" stroke={C.line} strokeWidth="8" />
    <line x1="100" y1="100" x2="100" y2="50" stroke={C.line} strokeWidth="8" strokeLinecap="round" transform={`rotate(${f * 6} 100 100)`} />
    <line x1="100" y1="100" x2="132" y2="100" stroke={C.line} strokeWidth="8" strokeLinecap="round" />
  </svg>
);
const UnevenIcon: React.FC = () => (
  <svg width="160" height="160" viewBox="0 0 200 200">
    {[40, 90, 140].map((x, k) => (
      <rect key={k} x={x} y={170 - [120, 40, 80][k]} width="36" height={[120, 40, 80][k]} rx="6" fill={C.leaf} stroke={C.leafDark} strokeWidth="4" />
    ))}
  </svg>
);
const PowerIcon: React.FC = () => (
  <svg width="160" height="160" viewBox="0 0 200 200">
    <path d="M 110 20 L 60 110 L 100 110 L 85 180 L 145 85 L 105 85 Z" fill="#F6C445" stroke={C.line} strokeWidth="6" />
    <path d="M 30 30 L 170 170" stroke={C.red} strokeWidth="14" strokeLinecap="round" />
  </svg>
);

// 2. Three problems.
export const DripProblems: React.FC<{ dur: number }> = ({ dur }) => {
  const f = useCurrentFrame();
  const fade = useSceneFade(dur, 8, 0);
  const hits = [8, Math.round(dur * 0.3), Math.round(dur * 0.55)];
  const shake = hits.reduce((acc, h) => acc + (f >= h ? Math.sin((f - h) * 2.2) * 12 * Math.exp(-(f - h) / 5) : 0), 0);
  return (
    <AbsoluteFill style={{ opacity: fade, background: "#F6E7D2", transform: `translateX(${shake}px)`, alignItems: "center", justifyContent: "center" }}>
      <Pop at={0}>
        <Caption size={96} color="#7A3A1E" style={{ textShadow: "none" }}>
          {t("ड्रिप की रोज़ की परेशानी")}
        </Caption>
      </Pop>
      <div style={{ marginTop: 110, display: "flex", flexDirection: "column", gap: 70, alignItems: "center" }}>
        <Pop at={hits[0]}>
          <ProblemCard icon={<ClockIcon f={f} />} text={t("ज़ोन ज़्यादा या कम चला")} />
        </Pop>
        <Pop at={hits[1]}>
          <ProblemCard icon={<UnevenIcon />} text={t("खाद हर जगह बराबर नहीं")} />
        </Pop>
        <Pop at={hits[2]}>
          <ProblemCard icon={<PowerIcon />} text={t("बिजली गई, रूटीन टूटा")} />
        </Pop>
      </div>
    </AbsoluteFill>
  );
};

// 3. Reveal.
export const WmReveal: React.FC<{ dur: number }> = ({ dur }) => {
  const f = useCurrentFrame();
  const { fps } = useVideoConfig();
  const fade = useSceneFade(dur, 0, 8);
  const flash = interpolate(f, [0, 10], [1, 0], clamp);
  const s = spring({ frame: f - 18, fps, config: { damping: 11 } });
  const ring = (f % 36) / 36;
  return (
    <AbsoluteFill style={{ opacity: fade, background: `radial-gradient(circle at 50% 55%, ${C.tealMid}, ${C.teal} 55%, ${C.deep})`, alignItems: "center", justifyContent: "center" }}>
      <Pop at={3}>
        <Caption size={124}>{t("अब अपने-आप।")}</Caption>
      </Pop>
      <div style={{ marginTop: 40, position: "relative", width: 560, height: 600, transform: `scale(${s})` }}>
        <svg width="560" height="600" viewBox="0 0 560 600" style={{ position: "absolute", inset: 0 }}>
          <circle cx="280" cy="300" r={200 + ring * 80} fill="none" stroke={C.leaf} strokeWidth={8 * (1 - ring)} opacity={1 - ring} />
          <WmDevice x={130} y={120} width={300} screen="Z1 12:00" f={f} zone={1} />
        </svg>
      </div>
      <Pop at={50} style={{ marginTop: 10 }}>
        <div style={{ padding: "18px 44px", borderRadius: 24, background: C.leaf, color: C.ink, fontFamily: DISPLAY, fontWeight: 800, fontSize: 84 }}>
          {t("वॉटरमास्टर")}
        </div>
      </Pop>
      <AbsoluteFill style={{ background: "#FFFFFF", opacity: flash }} />
    </AbsoluteFill>
  );
};

// 4. The farm runs itself: pump on, zones one by one, dosing inside the cycle.
export const ZonesRun: React.FC<{ dur: number }> = ({ dur }) => {
  const f = useCurrentFrame();
  const fade = useSceneFade(dur);
  const span = (dur - 50) / 4;
  const zone = f < 30 ? 0 : Math.min(4, 1 + Math.floor((f - 30) / span));
  const done = f > 30 + span * 4 - 10;
  const zones: [number, number][] = [
    [80, 760],
    [560, 760],
    [80, 1180],
    [560, 1180],
  ];
  const litres = Math.round(interpolate(f, [30, dur - 20], [0, 1840], clamp));
  return (
    <AbsoluteFill style={{ opacity: fade, background: C.cream, alignItems: "center" }}>
      <div style={{ marginTop: 130, height: 300, display: "flex", flexDirection: "column", alignItems: "center", gap: 10 }}>
        <Pop at={4}>
          <Caption size={78} color={C.teal} style={{ textShadow: "none" }}>
            {t("ज़ोन एक-एक करके, समय पर")}
          </Caption>
        </Pop>
        <Pop at={Math.round(dur * 0.35)}>
          <Caption size={62} color={C.ink} style={{ textShadow: "none" }}>
            {t("वाल्व खुला हो, तभी पंप चले")}
          </Caption>
        </Pop>
        <Pop at={Math.round(dur * 0.6)}>
          <Caption size={62} color={C.leafDark} style={{ textShadow: "none" }}>
            {t("खाद सिंचाई के साथ")}
          </Caption>
        </Pop>
      </div>
      <svg width="1080" height="1500" viewBox="0 380 1080 1500" style={{ position: "absolute", top: 380 }}>
        {zones.map(([x, y], k) => {
          const active = zone === k + 1 && !done;
          const wet = zone > k + 1 || done || active;
          const dosing = active && k < 2;
          return (
            <g key={k}>
              <rect x={x} y={y} width="440" height="360" rx="20" fill={wet ? "#7FB069" : "#D9C08A"} stroke={active ? C.leaf : "#B89E66"} strokeWidth={active ? 10 : 4} />
              {Array.from({ length: 4 }, (_, r) => (
                <line key={r} x1={x + 30} x2={x + 410} y1={y + 70 + r * 75} y2={y + 70 + r * 75} stroke="#3D5A6B" strokeWidth="5" />
              ))}
              {active &&
                Array.from({ length: 16 }, (_, d) => {
                  const dy = (f * 2 + d * 13) % 40;
                  return <circle key={d} cx={x + 50 + (d % 8) * 48} cy={y + 80 + Math.floor(d / 8) * 150 + dy} r="6" fill={dosing && d % 2 ? C.leaf : C.water} />;
                })}
              <text x={x + 220} y={y + 345} textAnchor="middle" fontSize="38" fontWeight="800" fill={C.ink} fontFamily="sans-serif">
                {t("ज़ोन")} {k + 1}
                {wet && !active ? " ✓" : ""}
              </text>
              <Valve x={x + 220} y={y - 4} open={active} s={1.3} />
            </g>
          );
        })}
        <WmDevice x={420} y={480} width={240} screen={done ? "DONE" : zone ? `Z${zone}` : "06:00"} f={f} zone={done ? 0 : zone} />
      </svg>
      <div style={{ position: "absolute", bottom: 90, display: "flex", gap: 30 }}>
        {[
          [t("फ्लो"), `${litres} L`],
          [t("प्रेशर"), "1.2 bar"],
        ].map(([k, v]) => (
          <div key={k} style={{ padding: "16px 34px", borderRadius: 22, background: C.teal, color: "#fff", fontFamily: BODY, fontWeight: 700, fontSize: 44 }}>
            {k}: {v}
          </div>
        ))}
      </div>
    </AbsoluteFill>
  );
};

// 5. Solar and battery; the phone shows water and fertiliser per zone.
export const SolarPhone: React.FC<{ dur: number }> = ({ dur }) => {
  const f = useCurrentFrame();
  const fade = useSceneFade(dur);
  const charge = interpolate(f, [0, dur], [0.55, 0.95], clamp);
  const rows: [string, string][] = [
    ["1", "460 L · 2 kg"],
    ["2", "455 L · 2 kg"],
    ["3", "470 L"],
    ["4", "455 L"],
  ];
  return (
    <AbsoluteFill style={{ opacity: fade, background: `linear-gradient(${C.teal}, ${C.deep})`, alignItems: "center" }}>
      <svg width="1080" height="700" viewBox="0 0 1080 700">
        <circle cx="200" cy="170" r="90" fill="#F6C445" />
        {[0, 1, 2, 3, 4, 5].map((k) => {
          const a = (k / 6) * Math.PI * 2 + f / 40;
          return <line key={k} x1={200 + Math.cos(a) * 110} y1={170 + Math.sin(a) * 110} x2={200 + Math.cos(a) * 150} y2={170 + Math.sin(a) * 150} stroke="#F6C445" strokeWidth="10" strokeLinecap="round" />;
        })}
        <polygon points="420,420 820,420 760,240 480,240" fill="#1E3F6E" stroke="#AFC6E0" strokeWidth="6" />
        {[1, 2, 3].map((k) => (
          <line key={k} x1={480 + k * 70} y1="240" x2={420 + k * 100} y2="420" stroke="#AFC6E0" strokeWidth="3" />
        ))}
        <line x1="620" y1="420" x2="620" y2="520" stroke="#AFC6E0" strokeWidth="10" />
        <rect x="520" y="520" width="200" height="110" rx="16" fill="#2B2B2B" stroke="#AFC6E0" strokeWidth="5" />
        <rect x="540" y="545" width={160 * charge} height="60" rx="8" fill={C.ok} />
      </svg>
      <Pop at={6}>
        <Caption size={72}>{t("सोलर और बैटरी से चले")}</Caption>
      </Pop>
      <Pop at={Math.round(dur * 0.3)} style={{ marginTop: 50 }}>
        <div style={{ width: 640, borderRadius: 60, background: "#111", padding: 26 }}>
          <div style={{ borderRadius: 40, background: "#F5F7F2", padding: "40px 40px 50px", display: "flex", flexDirection: "column", gap: 22 }}>
            <div style={{ fontFamily: DISPLAY, fontWeight: 800, fontSize: 48, color: C.teal }}>WaterMaster</div>
            <div style={{ fontFamily: BODY, fontWeight: 700, fontSize: 32, color: C.ok }}>{t("● ऑनलाइन")}</div>
            {rows.map(([z, v], i) => (
              <Pop key={z} at={Math.round(dur * 0.35) + i * 8} rise={16}>
                <div style={{ display: "flex", justifyContent: "space-between", fontFamily: BODY, fontWeight: 700, fontSize: 38, color: C.ink, borderTop: "2px solid #D8E0DC", paddingTop: 14 }}>
                  <span>
                    {t("ज़ोन")} {z}
                  </span>
                  <span>{v} ✓</span>
                </div>
              </Pop>
            ))}
          </div>
        </div>
      </Pop>
      <Pop at={Math.round(dur * 0.6)} style={{ marginTop: 40 }}>
        <Caption size={60}>{t("हर ज़ोन का पानी और खाद फोन पर")}</Caption>
      </Pop>
    </AbsoluteFill>
  );
};

// 7. End card.
const Chip: React.FC<{ text: string; at: number }> = ({ text, at }) => (
  <Pop at={at} rise={24}>
    <div style={{ padding: "12px 32px", borderRadius: 999, border: `4px solid ${C.leaf}`, color: "#FFE2B8", fontFamily: DISPLAY, fontWeight: 800, fontSize: 48, textAlign: "center" }}>
      {text}
    </div>
  </Pop>
);

export const WmEndCard: React.FC<{ dur: number }> = ({ dur }) => {
  const f = useCurrentFrame();
  const fade = useSceneFade(dur, 8, 0);
  return (
    <AbsoluteFill style={{ opacity: fade, background: `radial-gradient(circle at 50% 30%, ${C.tealMid}, ${C.teal} 50%, ${C.deep})`, alignItems: "center", justifyContent: "center" }}>
      <svg width="220" height="264" viewBox="0 0 220 264">
        <WmDevice width={220} screen="DONE" f={f} />
      </svg>
      <Pop at={4}>
        <div style={{ fontFamily: DISPLAY, fontWeight: 800, fontSize: 150, lineHeight: 1, color: C.leaf, marginTop: 10 }}>WaterMaster</div>
      </Pop>
      <Pop at={10}>
        <div style={{ fontFamily: DISPLAY, fontWeight: 800, fontSize: 62, color: "#fff", textAlign: "center", lineHeight: 1.25 }}>{t("ड्रिप और फर्टिगेशन कंट्रोलर")}</div>
      </Pop>
      <div style={{ marginTop: 44, display: "flex", flexDirection: "column", gap: 22, alignItems: "center" }}>
        <Chip text={t("पंप + खाद + 4 ज़ोन")} at={24} />
        <Chip text={t("सोलर और बैटरी")} at={31} />
        <Chip text={t("इंटरनेट न हो तब भी चले")} at={38} />
      </div>
      <Pop at={55} style={{ marginTop: 60 }}>
        <div style={{ padding: "20px 48px", borderRadius: 24, background: C.leaf, color: C.ink, fontFamily: BODY, fontWeight: 700, fontSize: 46 }}>{t("[डीलर का नाम] · [फोन]")}</div>
      </Pop>
      <Pop at={60} style={{ marginTop: 30 }}>
        <div style={{ fontFamily: BODY, fontWeight: 600, fontSize: 34, color: "#D5E6E2" }}>{t("NB Agri Automation")}</div>
      </Pop>
    </AbsoluteFill>
  );
};
