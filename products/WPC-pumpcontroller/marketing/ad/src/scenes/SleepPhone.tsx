import React from "react";
import { AbsoluteFill, interpolate, spring, useCurrentFrame, useVideoConfig } from "remotion";
import { BODY, C, Caption, Pop, clamp, useSceneFade } from "../common";

// 20-25 s: the farmer sleeps; the phone shows every pump's status.
const Row: React.FC<{ n: number; at: number }> = ({ n, at }) => (
  <Pop at={at} rise={20}>
    <div
      style={{
        display: "flex",
        alignItems: "center",
        justifyContent: "space-between",
        padding: "14px 20px",
        borderRadius: 16,
        background: "#FFFFFF",
        border: "2px solid #E3DAC3",
        fontFamily: BODY,
        fontSize: 27,
        color: C.ink,
      }}
    >
      <span style={{ fontWeight: 700 }}>पंप {n}</span>
      <span style={{ padding: "4px 12px", borderRadius: 12, background: "#1F8A45", color: "#fff", fontWeight: 700, fontSize: 22 }}>ON</span>
      <span style={{ color: "#1F6B3A", fontWeight: 600 }}>बिजली ✓</span>
      <span style={{ color: C.water, fontWeight: 600 }}>पानी ✓</span>
    </div>
  </Pop>
);

export const SleepPhone: React.FC<{ dur: number }> = ({ dur }) => {
  const f = useCurrentFrame();
  const { fps } = useVideoConfig();
  const fade = useSceneFade(dur);
  const slide = spring({ frame: f - 10, fps, config: { damping: 14 } });
  const bar = interpolate(f, [20, 120], [0.55, 0.78], clamp);

  return (
    <AbsoluteFill style={{ opacity: fade, background: "linear-gradient(#0B1622, #13283A)", alignItems: "center" }}>
      <Pop at={4} style={{ marginTop: 130 }}>
        <Caption size={92}>आप चैन से सोइए</Caption>
      </Pop>
      {/* phone */}
      <div
        style={{
          marginTop: 40,
          width: 560,
          height: 1000,
          borderRadius: 60,
          background: "#111",
          padding: 18,
          boxSizing: "border-box",
          transform: `translateY(${(1 - slide) * 900}px)`,
          boxShadow: "0 30px 80px rgba(0,0,0,0.6)",
        }}
      >
        <div style={{ width: "100%", height: "100%", borderRadius: 44, background: C.cream, padding: "34px 26px", boxSizing: "border-box", display: "flex", flexDirection: "column", gap: 16 }}>
          <div style={{ display: "flex", justifyContent: "space-between", fontFamily: BODY, fontSize: 24, color: "#4B5A4F" }}>
            <span>रात 3:15</span>
            <span style={{ color: "#1F8A45", fontWeight: 700 }}>● ऑनलाइन</span>
          </div>
          <div style={{ fontFamily: BODY, fontWeight: 700, fontSize: 36, color: C.green }}>WPC मास्टर</div>
          <div style={{ padding: 20, borderRadius: 20, background: C.green, color: "#fff", fontFamily: BODY }}>
            <div style={{ fontSize: 26, opacity: 0.85 }}>टंकी का लेवल</div>
            <div style={{ fontSize: 48, fontWeight: 700 }}>{Math.round(bar * 100)}% · भर रही है</div>
            <div style={{ marginTop: 10, height: 18, borderRadius: 9, background: "rgba(255,255,255,0.2)" }}>
              <div style={{ width: `${bar * 100}%`, height: "100%", borderRadius: 9, background: C.waterLight }} />
            </div>
          </div>
          {[1, 2, 3, 4, 5, 6].map((n, i) => (
            <Row key={n} n={n} at={24 + i * 7} />
          ))}
        </div>
      </div>
      <Pop at={60} style={{ marginTop: 40 }}>
        <Caption size={76} color={C.amber}>
          फोन पर पूरा स्टेटस
        </Caption>
      </Pop>
      {/* sleeping farmer */}
      <svg width="1080" height="330" viewBox="0 0 1080 330" style={{ position: "absolute", bottom: 0 }}>
        <rect x="120" y="170" width="840" height="120" rx="20" fill="#3B2A22" />
        <rect x="150" y="130" width="180" height="70" rx="30" fill="#E8E1CF" />
        <circle cx="240" cy="120" r="42" fill="#8A5A3C" />
        <path d="M 290 150 Q 600 80 930 150 L 930 200 L 290 200 Z" fill="#2F6B8F" />
        {[0, 1, 2].map((k) => {
          const p = ((f + k * 15) % 45) / 45;
          return (
            <text key={k} x={300 + p * 90 + k * 10} y={90 - p * 120} fontSize={40 + k * 12} fontWeight="800" fill="#F3E3B5" opacity={1 - p} fontFamily="sans-serif">
              Z
            </text>
          );
        })}
      </svg>
    </AbsoluteFill>
  );
};
