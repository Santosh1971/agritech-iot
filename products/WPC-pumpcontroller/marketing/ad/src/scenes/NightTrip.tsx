import React from "react";
import { AbsoluteFill, interpolate, useCurrentFrame } from "remotion";
import { C, Caption, Pop, clamp, useSceneFade } from "../common";

// 0-5 s: 2 AM, power arrives, the farmer walks out with a torch.
export const NightTrip: React.FC<{ dur: number }> = ({ dur }) => {
  const f = useCurrentFrame();
  const fade = useSceneFade(dur, 0, 8);
  const x = interpolate(f, [0, dur], [-120, 560], clamp);
  const phase = f * 0.45;
  const bulbOn = f > 20 || [8, 9, 12, 13, 16, 17].includes(f);
  const colonOn = Math.floor(f / 15) % 2 === 0;
  const stars = Array.from({ length: 40 }, (_, i) => ({
    x: (i * 263) % 1080,
    y: 60 + ((i * 137) % 900),
    r: 1.5 + (i % 3),
    o: 0.4 + 0.6 * Math.abs(Math.sin(f / 12 + i)),
  }));

  return (
    <AbsoluteFill style={{ opacity: fade }}>
      <svg width="1080" height="1920" viewBox="0 0 1080 1920">
        <defs>
          <linearGradient id="sky" x1="0" y1="0" x2="0" y2="1">
            <stop offset="0" stopColor="#050D16" />
            <stop offset="1" stopColor="#123043" />
          </linearGradient>
          <linearGradient id="beam" x1="0" y1="0" x2="1" y2="0">
            <stop offset="0" stopColor="#FFE9A8" stopOpacity="0.75" />
            <stop offset="1" stopColor="#FFE9A8" stopOpacity="0" />
          </linearGradient>
          <radialGradient id="glow">
            <stop offset="0" stopColor="#FFE08A" stopOpacity="0.9" />
            <stop offset="1" stopColor="#FFE08A" stopOpacity="0" />
          </radialGradient>
        </defs>
        <rect width="1080" height="1920" fill="url(#sky)" />
        {stars.map((s, i) => (
          <circle key={i} cx={s.x} cy={s.y} r={s.r} fill="#F3E3B5" opacity={s.o} />
        ))}
        <circle cx="880" cy="170" r="70" fill="#F3E3B5" />
        <circle cx="912" cy="148" r="62" fill="#07131F" />
        {/* ground with crop rows */}
        <rect y="1330" width="1080" height="590" fill="#13261A" />
        {Array.from({ length: 12 }, (_, i) => (
          <rect key={i} y={1370 + i * 46} width="1080" height="10" fill="#0C1B11" />
        ))}
        {/* power pole + bulb */}
        <line x1="960" y1="960" x2="960" y2="1340" stroke="#3A3A3A" strokeWidth="14" />
        <line x1="900" y1="990" x2="1020" y2="990" stroke="#3A3A3A" strokeWidth="10" />
        {bulbOn && <circle cx="960" cy="1030" r="120" fill="url(#glow)" />}
        <circle cx="960" cy="1030" r="18" fill={bulbOn ? "#FFE08A" : "#555"} />
        {/* far pump hut */}
        <polygon points="760,1300 820,1260 880,1300" fill="#3B2019" />
        <rect x="770" y="1300" width="100" height="60" fill="#2A2A26" />
        {/* farmer */}
        <g transform={`translate(${x},1330) scale(1.7)`}>
          <polygon points="60,20 420,-60 420,140" fill="url(#beam)" />
          <line x1="-4" y1="60" x2={Math.sin(phase) * 22} y2="128" stroke="#5B3A28" strokeWidth="12" strokeLinecap="round" />
          <line x1="4" y1="60" x2={-Math.sin(phase) * 22} y2="128" stroke="#5B3A28" strokeWidth="12" strokeLinecap="round" />
          <rect x="-28" y="-10" width="56" height="80" rx="20" fill="#E8E1CF" />
          <circle cx="0" cy="-38" r="25" fill="#8A5A3C" />
          <ellipse cx="0" cy="-58" rx="28" ry="13" fill="#D98E04" />
          <line x1="12" y1="8" x2="52" y2="22" stroke="#E8E1CF" strokeWidth="13" strokeLinecap="round" />
          <rect x="46" y="12" width="22" height="16" rx="4" fill="#444" />
        </g>
      </svg>
      {/* clock */}
      <Pop at={2} style={{ position: "absolute", top: 120, left: 70 }}>
        <div
          style={{
            padding: "14px 30px",
            borderRadius: 18,
            background: "rgba(0,0,0,0.45)",
            border: `3px solid ${C.amber}`,
            color: C.amber,
            fontFamily: "monospace",
            fontSize: 64,
            fontWeight: 700,
          }}
        >
          02{colonOn ? ":" : " "}00 AM
        </div>
      </Pop>
      <div style={{ position: "absolute", top: 420, width: "100%", display: "flex", flexDirection: "column", gap: 24 }}>
        <Pop at={12}>
          <Caption>रात 2 बजे बिजली आई…</Caption>
        </Pop>
        <Pop at={70}>
          <Caption size={104} color={C.amber}>
            फिर खेत जाना?
          </Caption>
        </Pop>
      </div>
    </AbsoluteFill>
  );
};
