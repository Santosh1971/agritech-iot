import React from "react";
import { AbsoluteFill, useCurrentFrame } from "remotion";
import { BODY, C, DISPLAY, Pop, useSceneFade } from "../common";
import { MasterDevice } from "./Reveal";

// 25-30 s: product name, three promises, dealer contact.
const Chip: React.FC<{ text: string; at: number }> = ({ text, at }) => (
  <Pop at={at} rise={24}>
    <div
      style={{
        padding: "14px 34px",
        borderRadius: 999,
        border: `4px solid ${C.amber}`,
        color: "#FFE2A8",
        fontFamily: DISPLAY,
        fontWeight: 800,
        fontSize: 50,
        textAlign: "center",
      }}
    >
      {text}
    </div>
  </Pop>
);

export const EndCard: React.FC<{ dur: number }> = ({ dur }) => {
  const f = useCurrentFrame();
  const fade = useSceneFade(dur, 8, 0);
  return (
    <AbsoluteFill style={{ opacity: fade, background: `radial-gradient(circle at 50% 30%, #245C3E, ${C.green} 50%, ${C.deep})`, alignItems: "center", justifyContent: "center" }}>
      <div style={{ transform: "scale(0.55)", height: 360, display: "flex", alignItems: "flex-start", transformOrigin: "top center" }}>
        <MasterDevice f={f} />
      </div>
      <Pop at={4}>
        <div style={{ fontFamily: DISPLAY, fontWeight: 800, fontSize: 230, lineHeight: 1, color: C.amber }}>WPC</div>
      </Pop>
      <Pop at={10}>
        <div style={{ fontFamily: DISPLAY, fontWeight: 800, fontSize: 70, color: "#fff" }}>वायरलेस पंप कंट्रोलर</div>
      </Pop>
      <div style={{ marginTop: 50, display: "flex", flexDirection: "column", gap: 26, alignItems: "center" }}>
        <Chip text="एक मास्टर से 20 पंप तक" at={22} />
        <Chip text="बिना तार · लगाना आसान" at={30} />
        <Chip text="दिन और रात, अपने-आप" at={38} />
      </div>
      <Pop at={55} style={{ marginTop: 70 }}>
        <div style={{ padding: "22px 48px", borderRadius: 24, background: C.amber, color: C.ink, fontFamily: BODY, fontWeight: 700, fontSize: 46, textAlign: "center" }}>
          [डीलर का नाम] · [फोन]
        </div>
      </Pop>
      <Pop at={60} style={{ marginTop: 34 }}>
        <div style={{ fontFamily: BODY, fontWeight: 600, fontSize: 34, color: "#D8E2D6" }}>NB Agri Automation</div>
      </Pop>
    </AbsoluteFill>
  );
};
