import React from "react";
import { AbsoluteFill, interpolate, useCurrentFrame } from "remotion";
import { C, Caption, DISPLAY, Pop, clamp, useSceneFade } from "../common";
import { Seedling } from "./parts";

// 5-9 s: three everyday problems.
const Wilted: React.FC = () => (
  <svg width="170" height="170" viewBox="0 0 200 200">
    <path d="M 60 150 L 140 150 L 130 190 L 70 190 Z" fill="#A5462F" />
    <g transform="translate(100,150) scale(1.9)">
      <Seedling x={0} y={0} perk={0} />
    </g>
  </svg>
);

const Overflow: React.FC<{ f: number }> = ({ f }) => {
  const r = interpolate(f, [0, 120], [30, 80], clamp);
  return (
    <svg width="170" height="170" viewBox="0 0 200 200">
      <ellipse cx="110" cy="170" rx={r} ry={r * 0.22} fill={C.waterLight} />
      <rect x="30" y="40" width="80" height="24" rx="6" fill="#8C9AA3" />
      <rect x="96" y="48" width="30" height="30" rx="4" fill="#8C9AA3" />
      <rect x="60" y="28" width="20" height="14" fill="#5A6770" />
      {[0, 1, 2, 3].map((k) => (
        <circle key={k} cx="111" cy={86 + ((f * 6 + k * 22) % 80)} r="6" fill={C.water} />
      ))}
    </svg>
  );
};

const Bag: React.FC = () => (
  <svg width="170" height="170" viewBox="0 0 200 200">
    <rect x="40" y="70" width="120" height="100" rx="14" fill="#C07A3A" stroke="#5A3514" strokeWidth="6" />
    <path d="M 75 70 V 50 H 125 V 70" fill="none" stroke="#5A3514" strokeWidth="8" />
    <text x="100" y="140" textAnchor="middle" fontSize="64" fontWeight="800" fill="#FFF3D6" fontFamily="sans-serif">
      ?
    </text>
  </svg>
);

const Card: React.FC<{ icon: React.ReactNode; text: string; size?: number }> = ({ icon, text, size = 70 }) => (
  <div
    style={{
      width: 940,
      minHeight: 250,
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
    <div style={{ fontFamily: DISPLAY, fontWeight: 800, fontSize: size, color: C.ink, lineHeight: 1.2 }}>{text}</div>
    <div style={{ position: "absolute", right: -18, top: -18, width: 76, height: 76, borderRadius: 38, background: C.red, display: "flex", alignItems: "center", justifyContent: "center" }}>
      <svg width="40" height="40" viewBox="0 0 16 16">
        <path d="M3 3 L13 13 M13 3 L3 13" stroke="#fff" strokeWidth="3" strokeLinecap="round" />
      </svg>
    </div>
  </div>
);

export const Problems: React.FC<{ dur: number }> = ({ dur }) => {
  const f = useCurrentFrame();
  const fade = useSceneFade(dur, 8, 0);
  const hits = [8, 34, 60];
  const shake = hits.reduce((acc, h) => acc + (f >= h ? Math.sin((f - h) * 2.2) * 12 * Math.exp(-(f - h) / 5) : 0), 0);
  return (
    <AbsoluteFill style={{ opacity: fade, background: "#F6E7D2", transform: `translateX(${shake}px)`, alignItems: "center", justifyContent: "center" }}>
      <Pop at={0}>
        <Caption size={96} color="#7A3A1E" style={{ textShadow: "none" }}>
          रोज़ की परेशानी
        </Caption>
      </Pop>
      <div style={{ marginTop: 110, display: "flex", flexDirection: "column", gap: 70, alignItems: "center" }}>
        <Pop at={hits[0]}>
          <Card icon={<Wilted />} text="एक दिन भूले — पौध मुरझाई" size={64} />
        </Pop>
        <Pop at={hits[1]}>
          <Card icon={<Overflow f={f} />} text="ज़्यादा पानी — बर्बाद" size={64} />
        </Pop>
        <Pop at={hits[2]}>
          <Card icon={<Bag />} text="बाहर गए? पानी कौन देगा?" size={64} />
        </Pop>
      </div>
    </AbsoluteFill>
  );
};
