import React from "react";
import { C } from "../common";

/** The FG1 box. `screen` is the text on its display; `on` lights the pump LED. */
export const Fg1Device: React.FC<{
  x?: number;
  y?: number;
  width?: number;
  screen: string;
  sub?: string;
  on?: boolean;
  f: number;
}> = ({ x = 0, y = 0, width = 300, screen, sub, on = false, f }) => (
  <svg x={x} y={y} width={width} height={width * 1.2} viewBox="0 0 300 360">
    <rect x="20" y="20" width="260" height="320" rx="34" fill="#FFFFFF" stroke={C.line} strokeWidth="8" />
    <rect x="50" y="56" width="200" height="120" rx="14" fill={C.teal} />
    <text x="150" y={sub ? 118 : 132} textAnchor="middle" fontSize={sub ? 50 : 56} fontWeight="800" fill="#FFFFFF" fontFamily="monospace">
      {screen}
    </text>
    {sub && (
      <text x="150" y="156" textAnchor="middle" fontSize="26" fontWeight="700" fill={C.leaf} fontFamily="sans-serif">
        {sub}
      </text>
    )}
    <text x="150" y="226" textAnchor="middle" fontSize="40" fontWeight="800" fill={C.teal} fontFamily="sans-serif">
      FG1
    </text>
    <circle cx="95" cy="286" r="15" fill={on ? C.ok : "#9AA59F"} opacity={on ? 0.75 + 0.25 * Math.sin(f / 3) : 1} />
    <circle cx="150" cy="286" r="15" fill={C.water} />
    <circle cx="205" cy="286" r="15" fill="#F2B632" />
  </svg>
);

/** A seedling; `perk` 0 = drooping and pale, 1 = upright and green. */
export const Seedling: React.FC<{ x: number; y: number; perk: number; scale?: number }> = ({ x, y, perk, scale = 1 }) => {
  const droop = (1 - perk) * 38;
  const col = perk > 0.5 ? "#4FA33A" : "#A8B060";
  const col2 = perk > 0.5 ? "#6DC44F" : "#BDB872";
  return (
    <g transform={`translate(${x},${y}) scale(${scale})`}>
      <path d={`M 0 0 Q 0 -30 ${droop * 0.6} -${56 - droop * 0.5}`} stroke="#3E8A2E" strokeWidth="5" fill="none" />
      <g transform={`translate(${droop * 0.6},-${56 - droop * 0.5}) rotate(${droop})`}>
        <ellipse cx="-14" cy="0" rx="16" ry="8" fill={col} transform="rotate(-25 -14 0)" />
        <ellipse cx="14" cy="-2" rx="16" ry="8" fill={col2} transform="rotate(25 14 -2)" />
      </g>
    </g>
  );
};

export const Drop: React.FC<{ x: number; y: number; s?: number; fill?: string }> = ({ x, y, s = 1, fill = C.waterLight }) => (
  <path transform={`translate(${x},${y}) scale(${s})`} d="M 0 -12 C -7 -2 -9 3 -9 6 a 9 9 0 0 0 18 0 c 0 -3 -2 -8 -9 -18 Z" fill={fill} />
);
