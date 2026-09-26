import React from "react";
import { AbsoluteFill, useCurrentFrame } from "remotion";
import { BODY, C, DISPLAY, Pop, useSceneFade } from "../common";
import { Fg1Device } from "./parts";

// 25-30 s: name, promises, dealer contact.
const Chip: React.FC<{ text: string; at: number }> = ({ text, at }) => (
  <Pop at={at} rise={24}>
    <div style={{ padding: "12px 32px", borderRadius: 999, border: `4px solid ${C.leaf}`, color: "#D6F2C4", fontFamily: DISPLAY, fontWeight: 800, fontSize: 48, textAlign: "center" }}>
      {text}
    </div>
  </Pop>
);

export const EndCard: React.FC<{ dur: number }> = ({ dur }) => {
  const f = useCurrentFrame();
  const fade = useSceneFade(dur, 8, 0);
  return (
    <AbsoluteFill style={{ opacity: fade, background: `radial-gradient(circle at 50% 30%, ${C.tealMid}, ${C.teal} 50%, ${C.deep})`, alignItems: "center", justifyContent: "center" }}>
      <svg width="220" height="264" viewBox="0 0 220 264">
        <Fg1Device width={220} screen="50 L" sub="✓" f={f} on />
      </svg>
      <Pop at={4}>
        <div style={{ fontFamily: DISPLAY, fontWeight: 800, fontSize: 200, lineHeight: 1, color: C.leaf, marginTop: 10 }}>FG1</div>
      </Pop>
      <Pop at={10}>
        <div style={{ fontFamily: DISPLAY, fontWeight: 800, fontSize: 66, color: "#fff", textAlign: "center", lineHeight: 1.25 }}>
          फ्लोगार्ड — स्मार्ट पानी टाइमर
        </div>
      </Pop>
      <Pop at={16}>
        <div style={{ fontFamily: BODY, fontWeight: 600, fontSize: 38, color: "#D5E6E2", marginTop: 6 }}>छोटे खेत और नर्सरी के लिए</div>
      </Pop>
      <div style={{ marginTop: 44, display: "flex", flexDirection: "column", gap: 22, alignItems: "center" }}>
        <Chip text="दिन में 4 बार तक पानी" at={24} />
        <Chip text="लीटर गिनकर पानी" at={31} />
        <Chip text="इंटरनेट न हो तब भी चले" at={38} />
      </div>
      <Pop at={55} style={{ marginTop: 60 }}>
        <div style={{ padding: "20px 48px", borderRadius: 24, background: C.leaf, color: C.ink, fontFamily: BODY, fontWeight: 700, fontSize: 46 }}>[डीलर का नाम] · [फोन]</div>
      </Pop>
      <Pop at={60} style={{ marginTop: 30 }}>
        <div style={{ fontFamily: BODY, fontWeight: 600, fontSize: 34, color: "#D5E6E2" }}>NB Agri Automation</div>
      </Pop>
    </AbsoluteFill>
  );
};
