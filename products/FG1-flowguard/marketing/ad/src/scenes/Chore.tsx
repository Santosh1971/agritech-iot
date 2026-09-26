import React from "react";
import { AbsoluteFill, interpolate, useCurrentFrame } from "remotion";
import { C, Caption, Pop, clamp, useSceneFade } from "../common";
import { Drop, Seedling } from "./parts";

// 0-5 s: early morning, carrying buckets along the nursery beds.
export const Chore: React.FC<{ dur: number }> = ({ dur }) => {
  const f = useCurrentFrame();
  const fade = useSceneFade(dur, 0, 8);
  const x = interpolate(f, [0, dur], [-80, 620], clamp);
  const phase = f * 0.4;
  const sunY = interpolate(f, [0, dur], [760, 620], clamp);
  const swing = Math.sin(phase) * 6;

  return (
    <AbsoluteFill style={{ opacity: fade }}>
      <svg width="1080" height="1920" viewBox="0 0 1080 1920">
        <defs>
          <linearGradient id="morning" x1="0" y1="0" x2="0" y2="1">
            <stop offset="0" stopColor="#BFE3E8" />
            <stop offset="1" stopColor="#FCE3B0" />
          </linearGradient>
        </defs>
        <rect width="1080" height="1920" fill="url(#morning)" />
        <circle cx="860" cy={sunY} r="90" fill={C.sun} />
        <rect y="1180" width="1080" height="740" fill="#CDB88E" />
        {/* nursery beds with thirsty seedlings */}
        {[1250, 1450, 1650].map((by, r) => (
          <g key={by}>
            <rect x="40" y={by + 40} width="1000" height="40" rx="8" fill="#6B4F35" />
            {Array.from({ length: 11 }, (_, i) => (
              <Seedling key={i} x={80 + i * 90 + (r % 2) * 40} y={by + 44} perk={0.15} scale={1.2} />
            ))}
          </g>
        ))}
        {/* grower with two buckets */}
        <g transform={`translate(${x},1330) scale(1.8)`}>
          <line x1="-4" y1="60" x2={Math.sin(phase) * 18} y2="124" stroke="#5B3A28" strokeWidth="12" strokeLinecap="round" />
          <line x1="4" y1="60" x2={-Math.sin(phase) * 18} y2="124" stroke="#5B3A28" strokeWidth="12" strokeLinecap="round" />
          <rect x="-28" y="-10" width="56" height="80" rx="20" fill="#E07A4F" />
          <circle cx="0" cy="-38" r="25" fill="#8A5A3C" />
          <path d="M -27 -48 Q 0 -80 27 -48 Z" fill="#2E4A7A" />
          <line x1="-26" y1="0" x2="-44" y2="52" stroke="#8A5A3C" strokeWidth="11" strokeLinecap="round" />
          <line x1="26" y1="0" x2="44" y2="52" stroke="#8A5A3C" strokeWidth="11" strokeLinecap="round" />
          <g transform={`rotate(${swing} -44 52)`}>
            <path d="M -64 52 L -24 52 L -30 96 L -58 96 Z" fill="#6E7F8A" stroke="#3B4650" strokeWidth="3" />
          </g>
          <g transform={`rotate(${-swing} 44 52)`}>
            <path d="M 24 52 L 64 52 L 58 96 L 30 96 Z" fill="#6E7F8A" stroke="#3B4650" strokeWidth="3" />
          </g>
          {/* sweat */}
          {[0, 1].map((k) => {
            const p = ((f + k * 20) % 40) / 40;
            return <Drop key={k} x={30 + k * 8} y={-50 + p * 40} s={0.7} fill="#7FC4F0" />;
          })}
        </g>
      </svg>
      <Pop at={2} style={{ position: "absolute", top: 110, left: 70 }}>
        <div style={{ padding: "12px 28px", borderRadius: 18, background: "rgba(255,255,255,0.8)", border: `3px solid ${C.teal}`, color: C.teal, fontFamily: "monospace", fontSize: 58, fontWeight: 700 }}>
          06:00 AM
        </div>
      </Pop>
      <div style={{ position: "absolute", top: 330, width: "100%", display: "flex", flexDirection: "column", gap: 20 }}>
        <Pop at={10}>
          <Caption color={C.ink} style={{ textShadow: "none" }}>
            रोज़ सुबह-शाम…
          </Caption>
        </Pop>
        <Pop at={60}>
          <Caption size={108} color={C.teal} style={{ textShadow: "none" }}>
            बाल्टी से पानी?
          </Caption>
        </Pop>
      </div>
    </AbsoluteFill>
  );
};
