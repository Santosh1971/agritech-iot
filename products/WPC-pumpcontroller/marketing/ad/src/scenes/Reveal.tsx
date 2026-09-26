import React from "react";
import { AbsoluteFill, interpolate, spring, useCurrentFrame, useVideoConfig } from "remotion";
import { C, Caption, DISPLAY, Pop, clamp, useSceneFade } from "../common";

// 9-13 s: impact flash, the Master appears with radio pulses.
export const MasterDevice: React.FC<{ f: number; pulses?: boolean }> = ({ f, pulses = true }) => (
  <svg width="520" height="640" viewBox="0 0 520 640">
    {pulses &&
      [0, 10, 20].map((o) => {
        const p = ((f + o) % 30) / 30;
        return <circle key={o} cx="260" cy="90" r={40 + p * 220} fill="none" stroke={C.amber} strokeWidth={8 * (1 - p)} opacity={1 - p} />;
      })}
    <line x1="260" y1="200" x2="260" y2="96" stroke={C.line} strokeWidth="14" strokeLinecap="round" />
    <circle cx="260" cy="90" r="16" fill={C.amber} />
    <rect x="130" y="200" width="260" height="340" rx="30" fill="#F4F1E8" stroke={C.line} strokeWidth="10" />
    <rect x="170" y="250" width="180" height="110" rx="12" fill={C.water} />
    <text x="260" y="320" textAnchor="middle" fontSize="44" fontWeight="800" fill="#fff" fontFamily="sans-serif">
      WPC
    </text>
    <circle cx="200" cy="420" r="16" fill={Math.floor(f / 8) % 2 ? C.ok : "#1F8A45"} />
    <circle cx="260" cy="420" r="16" fill={C.amber} />
    <circle cx="320" cy="420" r="16" fill={C.ok} />
    <rect x="245" y="540" width="30" height="90" fill={C.line} />
  </svg>
);

export const Reveal: React.FC<{ dur: number }> = ({ dur }) => {
  const f = useCurrentFrame();
  const { fps } = useVideoConfig();
  const fade = useSceneFade(dur, 0, 8);
  const flash = interpolate(f, [0, 10], [1, 0], clamp);
  const s = spring({ frame: f - 22, fps, config: { damping: 10 } });

  return (
    <AbsoluteFill style={{ opacity: fade, background: `radial-gradient(circle at 50% 55%, #2E6B47, ${C.green} 55%, ${C.deep})`, alignItems: "center", justifyContent: "center" }}>
      <Pop at={3}>
        <Caption size={130}>अब नहीं।</Caption>
      </Pop>
      <div style={{ marginTop: 40, transform: `scale(${s})` }}>
        <MasterDevice f={f} />
      </div>
      <Pop at={55} style={{ marginTop: 20 }}>
        <div
          style={{
            padding: "18px 44px",
            borderRadius: 24,
            background: C.amber,
            color: C.ink,
            fontFamily: DISPLAY,
            fontWeight: 800,
            fontSize: 92,
          }}
        >
          WPC लगाइए।
        </div>
      </Pop>
      <AbsoluteFill style={{ background: "#FFFFFF", opacity: flash }} />
    </AbsoluteFill>
  );
};
