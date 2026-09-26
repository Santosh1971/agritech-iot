import React from "react";
import { AbsoluteFill, interpolate, spring, useCurrentFrame, useVideoConfig } from "remotion";
import { BODY, C, Caption, DISPLAY, Pop, clamp, useSceneFade } from "../common";

// 20-25 s: away from the farm, the phone shows today's watering.
const HISTORY = [38, 45, 50, 42, 50, 48, 50];

export const PhoneAway: React.FC<{ dur: number }> = ({ dur }) => {
  const f = useCurrentFrame();
  const { fps } = useVideoConfig();
  const fade = useSceneFade(dur);
  const slide = spring({ frame: f - 8, fps, config: { damping: 14 } });
  const busX = interpolate(f, [0, dur], [-420, 1100], clamp);

  return (
    <AbsoluteFill style={{ opacity: fade, background: "linear-gradient(#FCE3B0, #F4F1E6)", alignItems: "center" }}>
      <Pop at={3} style={{ marginTop: 110 }}>
        <Caption size={90} color={C.teal} style={{ textShadow: "none" }}>
          कहीं से भी फोन पर
        </Caption>
      </Pop>
      <Pop at={10}>
        <div style={{ fontFamily: BODY, fontSize: 30, color: "#4A5A52" }}>(नर्सरी में Wi-Fi हो तो)</div>
      </Pop>
      <div
        style={{
          marginTop: 30,
          width: 560,
          height: 820,
          borderRadius: 60,
          background: "#111",
          padding: 18,
          boxSizing: "border-box",
          transform: `translateY(${(1 - slide) * 900}px)`,
          boxShadow: "0 30px 80px rgba(0,0,0,0.35)",
        }}
      >
        <div style={{ width: "100%", height: "100%", borderRadius: 44, background: C.cream, padding: "30px 26px", boxSizing: "border-box", display: "flex", flexDirection: "column", gap: 16, fontFamily: BODY }}>
          <div style={{ display: "flex", justifyContent: "space-between", fontSize: 24, color: "#4A5A52" }}>
            <span>FG1 फ्लोगार्ड</span>
            <span style={{ color: "#1F8A45", fontWeight: 700 }}>● ऑनलाइन</span>
          </div>
          <div style={{ padding: 22, borderRadius: 22, background: C.teal, color: "#fff" }}>
            <div style={{ fontSize: 26, opacity: 0.85 }}>आज का पानी</div>
            <div style={{ fontSize: 60, fontWeight: 700, lineHeight: 1.1 }}>50 लीटर ✓</div>
            <div style={{ marginTop: 14, height: 110, display: "flex", alignItems: "flex-end", gap: 12 }}>
              {HISTORY.map((v, i) => {
                const h = interpolate(f, [16 + i * 3, 30 + i * 3], [0, v * 2], clamp);
                return <div key={i} style={{ flexGrow: 1, height: h, borderRadius: 6, background: i === 6 ? C.leaf : "rgba(255,255,255,0.35)" }} />;
              })}
            </div>
          </div>
          <Pop at={30} rise={20}>
            <div style={{ padding: "16px 20px", borderRadius: 18, background: "#fff", border: "2px solid #DED6C0", display: "flex", justifyContent: "space-between", fontSize: 28 }}>
              <span style={{ fontWeight: 700 }}>सुबह 6:00 · 50 लीटर</span>
              <span style={{ color: "#1F8A45", fontWeight: 700 }}>पूरा ✓</span>
            </div>
          </Pop>
          <Pop at={38} rise={20}>
            <div style={{ padding: "16px 20px", borderRadius: 18, background: "#fff", border: "2px solid #DED6C0", display: "flex", justifyContent: "space-between", fontSize: 28 }}>
              <span style={{ fontWeight: 700 }}>शाम 5:30 · 20 मिनट</span>
              <span style={{ color: "#8A5200", fontWeight: 700 }}>बाकी</span>
            </div>
          </Pop>
          <Pop at={46} rise={20}>
            <div style={{ display: "flex", gap: 16, marginTop: 6 }}>
              <div style={{ flexGrow: 1, padding: "20px 0", borderRadius: 20, background: "#1F8A45", color: "#fff", textAlign: "center", fontSize: 34, fontWeight: 700 }}>चालू करें</div>
              <div style={{ flexGrow: 1, padding: "20px 0", borderRadius: 20, background: C.red, color: "#fff", textAlign: "center", fontSize: 34, fontWeight: 700 }}>बंद करें</div>
            </div>
          </Pop>
        </div>
      </div>
      <Pop at={70} style={{ marginTop: 34 }}>
        <div style={{ padding: "16px 32px", borderRadius: 20, background: C.teal, color: "#fff", fontFamily: DISPLAY, fontWeight: 800, fontSize: 46, textAlign: "center" }}>
          बिजली गई? लौटते ही पानी फिर शुरू
        </div>
      </Pop>
      {/* bus passing by */}
      <svg width="1080" height="200" viewBox="0 0 1080 200" style={{ position: "absolute", bottom: 0 }}>
        <rect y="160" width="1080" height="40" fill="#9C9686" />
        <g transform={`translate(${busX},40)`}>
          <rect width="380" height="120" rx="20" fill="#E0A21B" />
          {[0, 1, 2, 3].map((k) => (
            <rect key={k} x={24 + k * 86} y="18" width="70" height="44" rx="6" fill="#CFE8EC" />
          ))}
          <circle cx="80" cy="122" r="24" fill="#2B2F2E" />
          <circle cx="300" cy="122" r="24" fill="#2B2F2E" />
        </g>
      </svg>
    </AbsoluteFill>
  );
};
