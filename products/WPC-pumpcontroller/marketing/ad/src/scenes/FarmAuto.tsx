import React from "react";
import { AbsoluteFill, interpolate, useCurrentFrame } from "remotion";
import { BODY, C, Caption, Pop, clamp, useSceneFade } from "../common";

// 13-20 s: level low -> pumps switch ON one by one -> tank fills -> pumps OFF.
const PUMPS: [number, number][] = [
  [180, 150],
  [800, 150],
  [150, 520],
  [840, 580],
  [200, 900],
  [780, 920],
];
const PIPES: [number, number][][] = [
  [[180, 170], [180, 365], [470, 365], [470, 480]],
  [[800, 170], [800, 365], [530, 365], [530, 480]],
  [[172, 540], [420, 540]],
  [[818, 590], [580, 590]],
  [[200, 880], [200, 735], [470, 735], [470, 620]],
  [[780, 900], [780, 735], [530, 735], [530, 620]],
];
const TIP: [number, number] = [617, 440];
const ON_AT = (i: number) => 35 + i * 12;
const OFF_AT = (i: number) => 172 + i * 4;
const FULL_AT = 165;

export const FarmAuto: React.FC<{ dur: number }> = ({ dur }) => {
  const f = useCurrentFrame();
  const fade = useSceneFade(dur);
  const level = interpolate(f, [50, FULL_AT], [0.22, 0.95], clamp);
  const full = f >= FULL_AT;
  const pulse = (start: number) => {
    const p = interpolate(f, [start, start + 24], [0, 1], clamp);
    return p > 0 && p < 1 ? <circle cx={TIP[0]} cy={TIP[1]} r={20 + p * 520} fill="none" stroke={C.amberDark} strokeWidth={6 * (1 - p)} opacity={1 - p} /> : null;
  };

  return (
    <AbsoluteFill style={{ opacity: fade, background: C.cream, alignItems: "center" }}>
      <div style={{ marginTop: 150, height: 330, display: "flex", flexDirection: "column", alignItems: "center", gap: 10 }}>
        {!full ? (
          <>
            <Pop at={4}>
              <Caption size={80} color={C.green} style={{ textShadow: "none" }}>
                टंकी का लेवल कम?
              </Caption>
            </Pop>
            <Pop at={40}>
              <Caption size={92} color={C.ink} style={{ textShadow: "none" }}>
                पंप <span style={{ color: "#1F8A45" }}>अपने-आप ON</span>
              </Caption>
            </Pop>
          </>
        ) : (
          <>
            <Pop at={FULL_AT}>
              <Caption size={80} color={C.green} style={{ textShadow: "none" }}>
                टंकी भर गई
              </Caption>
            </Pop>
            <Pop at={FULL_AT + 6}>
              <Caption size={92} color={C.ink} style={{ textShadow: "none" }}>
                पंप <span style={{ color: C.red }}>अपने-आप OFF</span>
              </Caption>
            </Pop>
          </>
        )}
      </div>
      <svg width="1000" height="1100" viewBox="0 0 1000 1100" style={{ fontFamily: BODY }}>
        <defs>
          <pattern id="rows" width="18" height="18" patternUnits="userSpaceOnUse">
            <rect width="18" height="5" fill="rgba(40,60,20,0.18)" />
          </pattern>
        </defs>
        <rect width="1000" height="1100" rx="28" fill="#E9DFC4" />
        {[
          [30, 30, 440, 300, "#A7C47A"],
          [530, 30, 440, 300, "#86B062"],
          [30, 400, 300, 300, "#D8C477"],
          [670, 400, 300, 300, "#A7C47A"],
          [30, 770, 440, 300, "#86B062"],
          [530, 770, 440, 300, "#A7C47A"],
        ].map(([x, y, w, h, col], i) => (
          <g key={i}>
            <rect x={x} y={y} width={w} height={h} rx="12" fill={col as string} />
            <rect x={x} y={y} width={w} height={h} rx="12" fill="url(#rows)" />
          </g>
        ))}
        {/* pipes: grey when idle, flowing water while that pump runs */}
        {PIPES.map((pts, i) => {
          const on = f >= ON_AT(i) && f < OFF_AT(i);
          const d = pts.map((p) => p.join(",")).join(" ");
          return (
            <g key={i}>
              <polyline points={d} fill="none" stroke={on ? C.water : "#A89F88"} strokeWidth="12" strokeLinejoin="round" strokeLinecap="round" />
              {on && (
                <polyline points={d} fill="none" stroke={C.waterLight} strokeWidth="5" strokeDasharray="14 22" strokeDashoffset={-f * 4} strokeLinecap="round" />
              )}
            </g>
          );
        })}
        {/* radio links draw out one by one */}
        {PUMPS.map(([x, y], i) => {
          const tx = x + 40;
          const ty = y - 42;
          const p = interpolate(f, [ON_AT(i) - 10, ON_AT(i)], [0, 1], clamp);
          return (
            <line
              key={i}
              x1={TIP[0]}
              y1={TIP[1]}
              x2={TIP[0] + (tx - TIP[0]) * p}
              y2={TIP[1] + (ty - TIP[1]) * p}
              stroke={C.amberDark}
              strokeWidth="4"
              strokeDasharray="12 10"
              opacity={p > 0 ? 0.85 : 0}
            />
          );
        })}
        {pulse(20)}
        {pulse(FULL_AT)}
        {/* tank */}
        <rect x="420" y="480" width="160" height="140" rx="14" fill="#B9AE95" stroke={C.line} strokeWidth="4" />
        <clipPath id="tankIn">
          <rect x="432" y="492" width="136" height="116" rx="8" />
        </clipPath>
        <rect x="432" y={492 + 116 * (1 - level)} width="136" height={116 * level} fill={C.water} clipPath="url(#tankIn)" />
        <path
          d={`M 440 ${496 + 116 * (1 - level)} q 15 -8 30 0 t 30 0 t 30 0 t 30 0`}
          fill="none"
          stroke={C.waterLight}
          strokeWidth="4"
          opacity={level > 0.3 ? 1 : 0}
        />
        <rect x="378" y="636" width="244" height="46" rx="23" fill={full ? "#1F8A45" : C.red} />
        <text x="500" y="668" textAnchor="middle" fontSize="28" fontWeight="700" fill="#fff">
          {full ? "टंकी FULL ✓" : `टंकी ${Math.round(level * 100)}%`}
        </text>
        {/* master */}
        <line x1="617" y1="516" x2="617" y2="560" stroke={C.line} strokeWidth="7" />
        <rect x="598" y="470" width="38" height="50" rx="6" fill="#fff" stroke={C.line} strokeWidth="4" />
        <circle cx="617" cy="486" r="6" fill={C.ok} />
        <line x1="617" y1="470" x2="617" y2="444" stroke={C.line} strokeWidth="4" />
        <circle cx={TIP[0]} cy={TIP[1]} r="7" fill={C.amberDark} />
        <rect x="646" y="440" width="120" height="40" rx="20" fill={C.green} />
        <text x="706" y="468" textAnchor="middle" fontSize="24" fontWeight="700" fill="#fff">
          मास्टर
        </text>
        {/* pumps */}
        {PUMPS.map(([x, y], i) => {
          const on = f >= ON_AT(i) && f < OFF_AT(i);
          const glow = on ? 0.5 + 0.5 * Math.sin(f / 3) : 0;
          return (
            <g key={i}>
              {on && <circle cx={x} cy={y} r={58} fill={C.ok} opacity={0.18 + 0.12 * glow} />}
              <polygon points={`${x - 32},${y - 16} ${x},${y - 40} ${x + 32},${y - 16}`} fill="#A5462F" />
              <rect x={x - 27} y={y - 16} width="54" height="40" fill="#F4EEDF" stroke={C.line} strokeWidth="3" />
              <circle cx={x} cy={y + 4} r="10" fill={on ? C.water : "#8C8472"} stroke={C.line} strokeWidth="3" />
              <rect x={x + 32} y={y - 20} width="22" height="32" rx="4" fill="#fff" stroke={C.line} strokeWidth="3" />
              <line x1={x + 43} y1={y - 20} x2={x + 43} y2={y - 38} stroke={C.line} strokeWidth="3" />
              <circle cx={x + 40} cy={y - 42} r="5" fill={C.amberDark} />
              <circle cx={x + 43} cy={y - 6} r="5" fill={on ? C.ok : "#666"} />
              <rect x={x - 58} y={y + 34} width="84" height="36" rx="18" fill="#fff" />
              <text x={x - 16} y={y + 60} textAnchor="middle" fontSize="22" fontWeight="700" fill={C.ink}>
                पंप {i + 1}
              </text>
              <rect x={x + 30} y={y + 34} width="62" height="36" rx="18" fill={on ? "#1F8A45" : "#6B6B6B"} />
              <text x={x + 61} y={y + 59} textAnchor="middle" fontSize="20" fontWeight="800" fill="#fff" fontFamily="sans-serif">
                {on ? "ON" : "OFF"}
              </text>
            </g>
          );
        })}
      </svg>
    </AbsoluteFill>
  );
};
