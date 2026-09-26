import React from "react";
import { AbsoluteFill, interpolate, useCurrentFrame } from "remotion";
import { C, Caption, DISPLAY, Pop, clamp, useSceneFade } from "../common";

// 5-9 s: the three pains, each card lands with a small shake.
const TankIcon: React.FC<{ f: number }> = ({ f }) => (
  <svg width="170" height="170" viewBox="0 0 200 200">
    <rect x="40" y="60" width="120" height="120" rx="10" fill="#4E5548" stroke="#0B120C" strokeWidth="6" />
    <rect x="46" y="54" width="108" height="120" rx="6" fill={C.water} />
    {[0, 1, 2, 3, 4, 5].map((k) => {
      const side = k % 2 ? 36 : 164;
      const y = 60 + ((f * 5 + k * 23) % 120);
      return <circle key={k} cx={side + (k % 2 ? -6 : 6)} cy={y} r="7" fill={C.waterLight} />;
    })}
    <path d="M40 58 Q 30 40 46 30 M160 58 Q 170 40 154 30" stroke={C.waterLight} strokeWidth="6" fill="none" />
  </svg>
);

const CoinIcon: React.FC = () => (
  <svg width="170" height="170" viewBox="0 0 200 200">
    <circle cx="100" cy="100" r="74" fill="#D9A441" stroke="#8A5200" strokeWidth="8" />
    <text x="100" y="132" textAnchor="middle" fontSize="100" fontWeight="800" fill="#6B3E00">
      ₹
    </text>
  </svg>
);

const ClockIcon: React.FC<{ f: number }> = ({ f }) => (
  <svg width="170" height="170" viewBox="0 0 200 200">
    <circle cx="100" cy="108" r="70" fill="#F3E3B5" stroke="#0B120C" strokeWidth="8" />
    <line x1="100" y1="108" x2="100" y2="62" stroke="#0B120C" strokeWidth="8" strokeLinecap="round" />
    <line x1="100" y1="108" x2="128" y2="124" stroke="#0B120C" strokeWidth="8" strokeLinecap="round" />
    <g transform={`rotate(${Math.sin(f * 1.2) * 8} 100 108)`}>
      <circle cx="52" cy="40" r="20" fill="#C8472F" />
      <circle cx="148" cy="40" r="20" fill="#C8472F" />
    </g>
  </svg>
);

const ProblemCard: React.FC<{ icon: React.ReactNode; text: string }> = ({ icon, text }) => (
  <div
    style={{
      width: 920,
      height: 250,
      borderRadius: 32,
      background: "#2C201B",
      border: "4px solid #7A3A2A",
      display: "flex",
      alignItems: "center",
      gap: 36,
      padding: "0 40px",
      boxSizing: "border-box",
      position: "relative",
    }}
  >
    {icon}
    <div style={{ fontFamily: DISPLAY, fontWeight: 800, fontSize: 76, color: C.cream, lineHeight: 1.15 }}>{text}</div>
    <div
      style={{
        position: "absolute",
        right: -18,
        top: -18,
        width: 76,
        height: 76,
        borderRadius: 38,
        background: C.red,
        display: "flex",
        alignItems: "center",
        justifyContent: "center",
      }}
    >
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
  const shake = hits.reduce((acc, h) => acc + (f >= h ? Math.sin((f - h) * 2.2) * 14 * Math.exp(-(f - h) / 5) : 0), 0);
  const redGlow = interpolate(f, [0, dur], [0.15, 0.4], clamp);

  return (
    <AbsoluteFill
      style={{
        opacity: fade,
        background: `radial-gradient(circle at 50% 60%, rgba(200,71,47,${redGlow}), #140D0B 70%)`,
        transform: `translateX(${shake}px)`,
        alignItems: "center",
        justifyContent: "center",
      }}
    >
      <Pop at={0}>
        <Caption size={92}>हर रात की परेशानी</Caption>
      </Pop>
      <div style={{ marginTop: 120, display: "flex", flexDirection: "column", gap: 70, alignItems: "center" }}>
        <Pop at={hits[0]}>
          <ProblemCard icon={<TankIcon f={f} />} text="टंकी ओवरफ्लो" />
        </Pop>
        <Pop at={hits[1]}>
          <ProblemCard icon={<CoinIcon />} text="मज़दूरी का खर्च" />
        </Pop>
        <Pop at={hits[2]}>
          <ProblemCard icon={<ClockIcon f={f} />} text="नींद खराब" />
        </Pop>
      </div>
    </AbsoluteFill>
  );
};
