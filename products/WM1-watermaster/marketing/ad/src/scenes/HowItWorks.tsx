import React from "react";
import { AbsoluteFill } from "remotion";
import { BODY, DISPLAY, Pop, useSceneFade } from "../common";
import { t } from "../i18n";

// Explainer only: three numbered steps that land one after another across the scene.
export type Step = { title: string; detail: string };

export const HowItWorks: React.FC<{ dur: number; steps: Step[]; bg: string; accent: string; ink: string }> = ({ dur, steps, bg, accent, ink }) => {
  const fade = useSceneFade(dur);
  const at = (i: number) => 18 + Math.round(((dur - 60) / steps.length) * i);
  return (
    <AbsoluteFill style={{ opacity: fade, background: bg, alignItems: "center", justifyContent: "center", gap: 60 }}>
      <Pop at={2}>
        <div style={{ fontFamily: DISPLAY, fontWeight: 800, fontSize: 96, color: "#FFFFFF", textAlign: "center" }}>{t("यह कैसे काम करता है")}</div>
      </Pop>
      <div style={{ display: "flex", flexDirection: "column", gap: 44, width: 920 }}>
        {steps.map((s, i) => (
          <Pop key={i} at={at(i)} rise={30}>
            <div style={{ display: "flex", gap: 34, alignItems: "center", background: "rgba(255,255,255,0.08)", border: `4px solid ${accent}`, borderRadius: 32, padding: "34px 40px" }}>
              <div style={{ flex: "0 0 120px", height: 120, borderRadius: 60, background: accent, color: ink, fontFamily: DISPLAY, fontWeight: 800, fontSize: 76, display: "flex", alignItems: "center", justifyContent: "center" }}>
                {i + 1}
              </div>
              <div style={{ display: "flex", flexDirection: "column", gap: 8 }}>
                <div style={{ fontFamily: DISPLAY, fontWeight: 800, fontSize: 62, color: "#FFFFFF", lineHeight: 1.15 }}>{t(s.title)}</div>
                <div style={{ fontFamily: BODY, fontWeight: 600, fontSize: 40, color: "#DDE8E2", lineHeight: 1.3 }}>{t(s.detail)}</div>
              </div>
            </div>
          </Pop>
        ))}
      </div>
    </AbsoluteFill>
  );
};
