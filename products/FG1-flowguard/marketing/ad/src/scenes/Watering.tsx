import React from "react";
import { AbsoluteFill, interpolate, useCurrentFrame } from "remotion";
import { C, Caption, Pop, clamp, useSceneFade } from "../common";
import { Drop, Fg1Device, Seedling } from "./parts";

// 13-20 s: 6:00 -> FG1 switches the pump on, the meter counts to 50 L, pump off.
const TICK = 30; // clock turns 06:00
const ON = 36;
const FULL = 165;
const TARGET = 50;

export const Watering: React.FC<{ dur: number }> = ({ dur }) => {
  const f = useCurrentFrame();
  const fade = useSceneFade(dur);
  const running = f >= ON && f < FULL;
  const liters = Math.round(interpolate(f, [ON + 4, FULL], [0, TARGET], clamp));
  const perk = interpolate(f, [60, 150], [0.1, 1], clamp);
  const jitter = running ? Math.sin(f * 2.5) * 2 : 0;
  const screen = f < TICK ? "05:59" : f < ON ? "06:00" : `${liters} L`;
  const sub = f < ON ? undefined : f < FULL ? "ON" : "✓";
  const phase = f < 95 ? 0 : f < FULL ? 1 : 2;
  const beep = interpolate(f, [TICK, TICK + 20], [0, 1], clamp);
  const pipe = "M 210 700 H 226 V 1040 H 236 M 346 1040 H 580 V 640 H 1050";

  return (
    <AbsoluteFill style={{ opacity: fade, background: "linear-gradient(#CFE8EC, #F4F1E6 60%)", alignItems: "center" }}>
      <div style={{ marginTop: 120, height: 360, display: "flex", flexDirection: "column", alignItems: "center", gap: 6 }}>
        {phase === 0 && (
          <>
            <Pop at={4}>
              <Caption size={82} color={C.teal} style={{ textShadow: "none" }}>
                सुबह 6:00 बजे —
              </Caption>
            </Pop>
            <Pop at={TICK + 2}>
              <Caption size={100} color={C.leafDark} style={{ textShadow: "none" }}>
                अपने-आप चालू
              </Caption>
            </Pop>
          </>
        )}
        {phase === 1 && (
          <>
            <Pop at={95}>
              <Caption size={80} color={C.teal} style={{ textShadow: "none" }}>
                हर लीटर गिनता है
              </Caption>
            </Pop>
            <Pop at={100}>
              <Caption size={130} color={C.water} style={{ textShadow: "none" }}>
                {liters} लीटर
              </Caption>
            </Pop>
          </>
        )}
        {phase === 2 && (
          <>
            <Pop at={FULL}>
              <Caption size={82} color={C.teal} style={{ textShadow: "none" }}>
                50 लीटर पूरे
              </Caption>
            </Pop>
            <Pop at={FULL + 6}>
              <Caption size={100} color={C.red} style={{ textShadow: "none" }}>
                अपने-आप बंद ✓
              </Caption>
            </Pop>
          </>
        )}
      </div>
      <svg width="1080" height="1250" viewBox="0 0 1080 1250" style={{ position: "absolute", top: 670 }}>
        <rect y="1080" width="1080" height="170" fill="#CDB88E" />
        {/* shade-net house */}
        <line x1="580" y1="380" x2="580" y2="1080" stroke="#6B5B45" strokeWidth="12" />
        <line x1="1050" y1="380" x2="1050" y2="1080" stroke="#6B5B45" strokeWidth="12" />
        <rect x="560" y="360" width="520" height="50" fill="#2E6B3A" opacity="0.8" />
        {/* bench, trays, seedlings */}
        <rect x="600" y="920" width="440" height="20" rx="4" fill="#8A6A45" />
        <line x1="620" y1="940" x2="620" y2="1080" stroke="#6B5B45" strokeWidth="9" />
        <line x1="1020" y1="940" x2="1020" y2="1080" stroke="#6B5B45" strokeWidth="9" />
        {[0, 1, 2].map((t) => (
          <rect key={t} x={610 + t * 145} y="880" width="130" height="40" rx="5" fill="#3A2E24" />
        ))}
        {Array.from({ length: 9 }, (_, i) => (
          <Seedling key={i} x={635 + i * 48} y={882} perk={perk} scale={1.15} />
        ))}
        {/* tank on stand */}
        <line x1="60" y1="720" x2="60" y2="1080" stroke="#6B5B45" strokeWidth="10" />
        <line x1="190" y1="720" x2="190" y2="1080" stroke="#6B5B45" strokeWidth="10" />
        <rect x="40" y="560" width="170" height="160" rx="20" fill="#2B2F2E" />
        <ellipse cx="125" cy="562" rx="85" ry="15" fill="#3C4240" />
        {/* pipe and flowing water */}
        <path d={pipe} fill="none" stroke={running ? C.water : "#9C9686"} strokeWidth="16" strokeLinejoin="round" />
        {running && <path d={pipe} fill="none" stroke={C.waterLight} strokeWidth="6" strokeDasharray="16 22" strokeDashoffset={-f * 5} />}
        <line x1="580" y1="640" x2="1050" y2="640" stroke="#1E2B22" strokeWidth="8" />
        {/* drips */}
        {Array.from({ length: 8 }, (_, i) => {
          const dx = 620 + i * 58;
          const p = ((f * 3 + i * 17) % 60) / 60;
          return (
            <g key={i}>
              <rect x={dx - 5} y="644" width="10" height="10" fill="#1E2B22" />
              {running && f > ON + 10 && <Drop x={dx} y={668 + p * 200} s={1.1} />}
            </g>
          );
        })}
        {/* pump */}
        <g transform={`translate(${jitter},0)`}>
          <rect x="236" y="1010" width="110" height="60" rx="12" fill="#2F6FB1" stroke={C.line} strokeWidth="4" />
          <circle cx="252" cy="1040" r="20" fill="#255A91" stroke={C.line} strokeWidth="4" />
          <rect x="232" y="1068" width="120" height="12" fill="#555" />
        </g>
        {/* flow meter + counter bubble */}
        <rect x="420" y="1018" width="76" height="46" rx="8" fill="#C9A24A" stroke={C.line} strokeWidth="4" />
        <circle cx="458" cy="1041" r="16" fill="#fff" stroke={C.line} strokeWidth="3" />
        <line x1="458" y1="1041" x2={458 + 11 * Math.cos(running ? f / 2 : 0)} y2={1041 + 11 * Math.sin(running ? f / 2 : 0)} stroke={C.red} strokeWidth="4" />
        {/* FG1 on its post + cables */}
        <line x1="380" y1="760" x2="380" y2="1080" stroke="#6B5B45" strokeWidth="12" />
        <polyline points="360,770 360,980 300,980 300,1010" fill="none" stroke={C.line} strokeWidth="5" />
        <polyline points="400,770 400,990 458,990 458,1018" fill="none" stroke={C.line} strokeWidth="5" strokeDasharray="10 8" />
        {beep > 0 && beep < 1 && <circle cx="380" cy="660" r={120 + beep * 160} fill="none" stroke={C.leafDark} strokeWidth={8 * (1 - beep)} opacity={1 - beep} />}
        <Fg1Device x={270} y={540} width={220} screen={screen} sub={sub} on={running} f={f} />
        <circle cx="1000" cy="120" r="70" fill={C.sun} />
      </svg>
    </AbsoluteFill>
  );
};
