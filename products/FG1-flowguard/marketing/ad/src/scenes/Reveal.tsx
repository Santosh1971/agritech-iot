import React from "react";
import { AbsoluteFill, interpolate, spring, useCurrentFrame, useVideoConfig } from "remotion";
import { C, Caption, DISPLAY, Pop, clamp, useSceneFade } from "../common";
import { Drop, Fg1Device } from "./parts";

// 9-13 s: chime, the FG1 box appears with drops and a clock ring.
export const Reveal: React.FC<{ dur: number }> = ({ dur }) => {
  const f = useCurrentFrame();
  const { fps } = useVideoConfig();
  const fade = useSceneFade(dur, 0, 8);
  const flash = interpolate(f, [0, 10], [1, 0], clamp);
  const s = spring({ frame: f - 18, fps, config: { damping: 11 } });
  const ring = ((f % 36) / 36);

  return (
    <AbsoluteFill style={{ opacity: fade, background: `radial-gradient(circle at 50% 55%, ${C.tealMid}, ${C.teal} 55%, ${C.deep})`, alignItems: "center", justifyContent: "center" }}>
      <Pop at={3}>
        <Caption size={128}>अब आसान।</Caption>
      </Pop>
      <div style={{ marginTop: 40, position: "relative", width: 560, height: 600, transform: `scale(${s})` }}>
        <svg width="560" height="600" viewBox="0 0 560 600" style={{ position: "absolute", inset: 0 }}>
          <circle cx="280" cy="300" r={200 + ring * 80} fill="none" stroke={C.leaf} strokeWidth={8 * (1 - ring)} opacity={1 - ring} />
          {[0, 1, 2, 3, 4].map((k) => {
            const a = (k / 5) * Math.PI * 2 + f / 30;
            return <Drop key={k} x={280 + Math.cos(a) * 250} y={300 + Math.sin(a) * 250} s={1.6} />;
          })}
          <Fg1Device x={130} y={120} width={300} screen="06:00" f={f} on />
        </svg>
      </div>
      <Pop at={50} style={{ marginTop: 10 }}>
        <div style={{ padding: "18px 44px", borderRadius: 24, background: C.leaf, color: C.ink, fontFamily: DISPLAY, fontWeight: 800, fontSize: 84 }}>
          FG1 फ्लोगार्ड
        </div>
      </Pop>
      <AbsoluteFill style={{ background: "#FFFFFF", opacity: flash }} />
    </AbsoluteFill>
  );
};
