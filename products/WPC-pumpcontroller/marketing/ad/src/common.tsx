import React from "react";
import { interpolate, spring, useCurrentFrame, useVideoConfig } from "remotion";
import { loadFont as loadBaloo } from "@remotion/google-fonts/Baloo2";
import { loadFont as loadNoto } from "@remotion/google-fonts/NotoSansDevanagari";

export const DISPLAY = loadBaloo("normal", { weights: ["600", "800"], subsets: ["devanagari", "latin"] }).fontFamily;
export const BODY = loadNoto("normal", { weights: ["400", "600", "700"], subsets: ["devanagari", "latin"] }).fontFamily;

export const C = {
  cream: "#F5F0E3",
  ink: "#17221A",
  green: "#14352A",
  deep: "#0E2A1B",
  amber: "#FFB547",
  amberDark: "#8A5200",
  water: "#2F7FC1",
  waterLight: "#8CC4F0",
  red: "#C8472F",
  ok: "#2DBE5E",
  line: "#0B120C",
};

export const clamp = { extrapolateLeft: "clamp", extrapolateRight: "clamp" } as const;

/** Scene opacity: fade in over `inF` frames and out over the last `outF`. */
export const useSceneFade = (dur: number, inF = 8, outF = 8) => {
  const f = useCurrentFrame();
  const fadeIn = inF ? interpolate(f, [0, inF], [0, 1], clamp) : 1;
  const fadeOut = outF ? interpolate(f, [dur - outF, dur], [1, 0], clamp) : 1;
  return Math.min(fadeIn, fadeOut);
};

/** Spring-in (scale + fade + rise) starting at local frame `at`. */
export const Pop: React.FC<{ at: number; children: React.ReactNode; style?: React.CSSProperties; rise?: number }> = ({
  at,
  children,
  style,
  rise = 40,
}) => {
  const f = useCurrentFrame();
  const { fps } = useVideoConfig();
  const s = spring({ frame: f - at, fps, config: { damping: 13, mass: 0.8 } });
  return (
    <div
      style={{
        ...style,
        opacity: interpolate(f - at, [0, 6], [0, 1], clamp),
        transform: `translateY(${(1 - s) * rise}px) scale(${0.85 + 0.15 * s})`,
      }}
    >
      {children}
    </div>
  );
};

export const Caption: React.FC<{ children: React.ReactNode; size?: number; color?: string; style?: React.CSSProperties }> = ({
  children,
  size = 88,
  color = "#FFFFFF",
  style,
}) => (
  <div
    style={{
      fontFamily: DISPLAY,
      fontWeight: 800,
      fontSize: size,
      lineHeight: 1.2,
      color,
      textAlign: "center",
      textShadow: "0 4px 24px rgba(0,0,0,0.35)",
      ...style,
    }}
  >
    {children}
  </div>
);
