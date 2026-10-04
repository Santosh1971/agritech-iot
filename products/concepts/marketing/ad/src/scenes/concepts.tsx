import React from "react";
import { AbsoluteFill, interpolate, spring, useCurrentFrame, useVideoConfig } from "remotion";
import { BODY, C, Caption, DISPLAY, Pop, clamp, useSceneFade } from "../common";
import { t } from "../i18n";

// Concept-product explainers (vertical 1080x1920): the paddy irrigation adviser
// and WM-Pro wireless valves. Each scene takes `dur` and spreads its beats over it.

export type Theme = { bg: string; mid: string; deep: string; accent: string; accentInk: string };
export const PADDY: Theme = { bg: "#1F4D2B", mid: "#2F7A41", deep: "#0E2616", accent: "#F2C94C", accentInk: "#1D2410" };
export const WMPRO: Theme = { bg: "#0D3550", mid: "#1C5F86", deep: "#061C2B", accent: "#F2A93B", accentInk: "#15221D" };

const Farmer: React.FC<{ x: number; y: number; s?: number; walk?: number }> = ({ x, y, s = 1, walk = 0 }) => (
  <g transform={`translate(${x},${y}) scale(${s})`}>
    <line x1="-6" y1="60" x2={Math.sin(walk) * 20} y2="126" stroke="#5B3A28" strokeWidth="12" strokeLinecap="round" />
    <line x1="6" y1="60" x2={-Math.sin(walk) * 20} y2="126" stroke="#5B3A28" strokeWidth="12" strokeLinecap="round" />
    <rect x="-28" y="-10" width="56" height="80" rx="20" fill="#E8E1CF" />
    <circle cx="0" cy="-38" r="25" fill="#8A5A3C" />
    <ellipse cx="0" cy="-58" rx="28" ry="13" fill="#D98E04" />
  </g>
);

const Valve: React.FC<{ x: number; y: number; open: boolean; s?: number }> = ({ x, y, open, s = 1 }) => (
  <g transform={`translate(${x},${y}) scale(${s})`}>
    <rect x="-22" y="-14" width="44" height="28" rx="6" fill={open ? C.ok : "#8C9AA3"} stroke={C.line} strokeWidth="4" />
    <circle cx="0" cy="-26" r="9" fill={open ? C.ok : C.red} />
  </g>
);

/** eSIM motor controller box. */
export const MotorBox: React.FC<{ x?: number; y?: number; w?: number; on?: boolean; f: number }> = ({ x = 0, y = 0, w = 300, on = false, f }) => (
  <svg x={x} y={y} width={w} height={w * 1.2} viewBox="0 0 300 360">
    <rect x="20" y="20" width="260" height="320" rx="30" fill="#FFFFFF" stroke={C.line} strokeWidth="8" />
    <rect x="46" y="52" width="208" height="100" rx="12" fill={PADDY.bg} />
    <text x="150" y="116" textAnchor="middle" fontSize="44" fontWeight="800" fill="#FFFFFF" fontFamily="monospace">
      {on ? "PUMP ON" : "-15 cm"}
    </text>
    <rect x="90" y="182" width="120" height="70" rx="10" fill="#E9EEF1" stroke="#5B6B75" strokeWidth="4" />
    <text x="150" y="228" textAnchor="middle" fontSize="30" fontWeight="800" fill="#2F434F" fontFamily="sans-serif">
      eSIM
    </text>
    <circle cx="105" cy="295" r="14" fill={on ? C.ok : "#9AA59F"} opacity={on ? 0.75 + 0.25 * Math.sin(f / 3) : 1} />
    <circle cx="150" cy="295" r="14" fill="#2F7FC1" />
    <circle cx="195" cy="295" r="14" fill={PADDY.accent} />
  </svg>
);

/** WM-Pro field node with two valves. */
export const ProNode: React.FC<{ x?: number; y?: number; w?: number; a?: boolean; b?: boolean }> = ({ x = 0, y = 0, w = 300, a = false, b = false }) => (
  <svg x={x} y={y} width={w} height={w} viewBox="0 0 300 300">
    <polygon points="70,40 230,40 210,90 90,90" fill="#1E3F6E" stroke="#AFC6E0" strokeWidth="4" />
    <line x1="150" y1="90" x2="150" y2="120" stroke="#555" strokeWidth="8" />
    <rect x="80" y="120" width="140" height="100" rx="16" fill="#FFFFFF" stroke={C.line} strokeWidth="6" />
    <text x="150" y="180" textAnchor="middle" fontSize="34" fontWeight="800" fill={WMPRO.bg} fontFamily="sans-serif">
      WM-Pro
    </text>
    <line x1="110" y1="220" x2="80" y2="262" stroke="#3D5A6B" strokeWidth="8" />
    <line x1="190" y1="220" x2="220" y2="262" stroke="#3D5A6B" strokeWidth="8" />
    <Valve x={80} y={272} open={a} s={1.2} />
    <Valve x={220} y={272} open={b} s={1.2} />
  </svg>
);

// ---------- shared scenes ----------

const ProblemCard: React.FC<{ text: string; icon: string }> = ({ text, icon }) => (
  <div style={{ width: 940, minHeight: 220, borderRadius: 32, background: "#FFFFFF", border: "4px solid #E2C9A8", display: "flex", alignItems: "center", gap: 34, padding: "10px 40px", boxSizing: "border-box", position: "relative", boxShadow: "0 10px 30px rgba(90,53,20,0.15)" }}>
    <div style={{ flex: "0 0 130px", height: 130, borderRadius: 65, background: "#F6E7D2", display: "flex", alignItems: "center", justifyContent: "center", fontSize: 72 }}>{icon}</div>
    <div style={{ fontFamily: DISPLAY, fontWeight: 800, fontSize: 62, color: C.ink, lineHeight: 1.2 }}>{text}</div>
    <div style={{ position: "absolute", right: -18, top: -18, width: 76, height: 76, borderRadius: 38, background: C.red, display: "flex", alignItems: "center", justifyContent: "center" }}>
      <svg width="40" height="40" viewBox="0 0 16 16">
        <path d="M3 3 L13 13 M13 3 L3 13" stroke="#fff" strokeWidth="3" strokeLinecap="round" />
      </svg>
    </div>
  </div>
);

export const makeProblems = (title: string, items: [string, string][]) => {
  const Problems: React.FC<{ dur: number }> = ({ dur }) => {
    const f = useCurrentFrame();
    const fade = useSceneFade(dur, 8, 0);
    const hits = items.map((_, i) => 8 + Math.round(dur * 0.25 * i));
    const shake = hits.reduce((acc, h) => acc + (f >= h ? Math.sin((f - h) * 2.2) * 12 * Math.exp(-(f - h) / 5) : 0), 0);
    return (
      <AbsoluteFill style={{ opacity: fade, background: "#F6E7D2", transform: `translateX(${shake}px)`, alignItems: "center", justifyContent: "center" }}>
        <Pop at={0}>
          <Caption size={92} color="#7A3A1E" style={{ textShadow: "none" }}>
            {t(title)}
          </Caption>
        </Pop>
        <div style={{ marginTop: 100, display: "flex", flexDirection: "column", gap: 64, alignItems: "center" }}>
          {items.map(([text, icon], i) => (
            <Pop key={i} at={hits[i]}>
              <ProblemCard text={t(text)} icon={icon} />
            </Pop>
          ))}
        </div>
      </AbsoluteFill>
    );
  };
  return Problems;
};

export const makeReveal = (theme: Theme, headline: string, name: string, device: (f: number) => React.ReactNode) => {
  const Reveal: React.FC<{ dur: number }> = ({ dur }) => {
    const f = useCurrentFrame();
    const { fps } = useVideoConfig();
    const fade = useSceneFade(dur, 0, 8);
    const flash = interpolate(f, [0, 10], [1, 0], clamp);
    const s = spring({ frame: f - 18, fps, config: { damping: 11 } });
    const ring = (f % 36) / 36;
    return (
      <AbsoluteFill style={{ opacity: fade, background: `radial-gradient(circle at 50% 55%, ${theme.mid}, ${theme.bg} 55%, ${theme.deep})`, alignItems: "center", justifyContent: "center" }}>
        <Pop at={3}>
          <Caption size={112}>{t(headline)}</Caption>
        </Pop>
        <div style={{ marginTop: 40, position: "relative", width: 560, height: 600, transform: `scale(${s})` }}>
          <svg width="560" height="600" viewBox="0 0 560 600" style={{ position: "absolute", inset: 0 }}>
            <circle cx="280" cy="300" r={200 + ring * 80} fill="none" stroke={theme.accent} strokeWidth={8 * (1 - ring)} opacity={1 - ring} />
            {device(f)}
          </svg>
        </div>
        <Pop at={50} style={{ marginTop: 10 }}>
          <div style={{ padding: "18px 44px", borderRadius: 24, background: theme.accent, color: theme.accentInk, fontFamily: DISPLAY, fontWeight: 800, fontSize: 76, textAlign: "center" }}>{t(name)}</div>
        </Pop>
        <AbsoluteFill style={{ background: "#FFFFFF", opacity: flash }} />
      </AbsoluteFill>
    );
  };
  return Reveal;
};

const Chip: React.FC<{ text: string; at: number; theme: Theme }> = ({ text, at, theme }) => (
  <Pop at={at} rise={24}>
    <div style={{ padding: "12px 32px", borderRadius: 999, border: `4px solid ${theme.accent}`, color: "#FFF1CC", fontFamily: DISPLAY, fontWeight: 800, fontSize: 46, textAlign: "center" }}>{text}</div>
  </Pop>
);

export const makeEndCard = (theme: Theme, name: string, sub: string, chips: string[], device: (f: number) => React.ReactNode) => {
  const EndCard: React.FC<{ dur: number }> = ({ dur }) => {
    const f = useCurrentFrame();
    const fade = useSceneFade(dur, 8, 0);
    return (
      <AbsoluteFill style={{ opacity: fade, background: `radial-gradient(circle at 50% 30%, ${theme.mid}, ${theme.bg} 50%, ${theme.deep})`, alignItems: "center", justifyContent: "center" }}>
        <svg width="240" height="280" viewBox="0 0 300 350">
          {device(f)}
        </svg>
        <Pop at={4}>
          <div style={{ fontFamily: DISPLAY, fontWeight: 800, fontSize: 104, lineHeight: 1.05, color: theme.accent, marginTop: 10, textAlign: "center", maxWidth: 960 }}>{t(name)}</div>
        </Pop>
        <Pop at={10}>
          <div style={{ fontFamily: DISPLAY, fontWeight: 800, fontSize: 56, color: "#fff", textAlign: "center", lineHeight: 1.25, maxWidth: 960 }}>{t(sub)}</div>
        </Pop>
        <div style={{ marginTop: 40, display: "flex", flexDirection: "column", gap: 20, alignItems: "center" }}>
          {chips.map((c, i) => (
            <Chip key={i} text={t(c)} at={22 + i * 7} theme={theme} />
          ))}
        </div>
        <Pop at={55} style={{ marginTop: 52 }}>
          <div style={{ padding: "20px 48px", borderRadius: 24, background: theme.accent, color: theme.accentInk, fontFamily: BODY, fontWeight: 700, fontSize: 46 }}>{t("[डीलर का नाम] · [फोन]")}</div>
        </Pop>
        <Pop at={60} style={{ marginTop: 28 }}>
          <div style={{ fontFamily: BODY, fontWeight: 600, fontSize: 34, color: "#D5E6E2" }}>Agri Sensors and Controls</div>
        </Pop>
      </AbsoluteFill>
    );
  };
  return EndCard;
};

// ---------- paddy scenes ----------

const PaddyField: React.FC<{ y: number; water: number; f: number }> = ({ y, water, f }) => (
  <g>
    <rect x="0" y={y} width="1080" height={1920 - y} fill="#7A5A33" />
    <rect x="0" y={y} width="1080" height={60 * water} fill="#5FA8D3" opacity="0.7" />
    {Array.from({ length: 9 }, (_, r) =>
      Array.from({ length: 14 }, (_, c) => (
        <path key={`${r}-${c}`} d={`M ${30 + c * 78} ${y + 50 + r * 95} q ${-14 + Math.sin(f / 15 + c) * 3} -50 -6 -70 M ${30 + c * 78} ${y + 50 + r * 95} q 14 -50 6 -72`} stroke="#5DA23A" strokeWidth="6" fill="none" />
      )),
    )}
  </g>
);

export const PaddyWalk: React.FC<{ dur: number }> = ({ dur }) => {
  const f = useCurrentFrame();
  const fade = useSceneFade(dur, 0, 8);
  const trip = (f % 90) / 90;
  const x = trip < 0.5 ? interpolate(trip, [0, 0.5], [120, 900]) : interpolate(trip, [0.5, 1], [900, 120]);
  const sunX = interpolate(f, [0, dur], [150, 930], clamp);
  return (
    <AbsoluteFill style={{ opacity: fade, background: "linear-gradient(#BFE3F2, #F3EBD2 50%)" }}>
      <svg width="1080" height="1920" viewBox="0 0 1080 1920">
        <circle cx={sunX} cy={260 - Math.sin((sunX / 1080) * Math.PI) * 120} r="80" fill="#F6C445" />
        <PaddyField y={1080} water={0.4} f={f} />
        <Farmer x={x} y={940} s={1.6} walk={f * 0.5} />
      </svg>
      <div style={{ position: "absolute", top: 400, width: "100%", display: "flex", flexDirection: "column", gap: 24 }}>
        <Pop at={10}>
          <Caption color={C.ink} size={84} style={{ textShadow: "none" }}>
            {t("धान में पानी देखने बार-बार खेत?")}
          </Caption>
        </Pop>
        <Pop at={Math.round(dur * 0.5)}>
          <Caption size={80} color="#8A3A1E" style={{ textShadow: "none" }}>
            {t("कितना पानी दें, कब दें?")}
          </Caption>
        </Pop>
      </div>
    </AbsoluteFill>
  );
};

/** Cross-section of the AWD tube: water falls to -15 cm, pump floods back to +5 cm. */
export const AwdTube: React.FC<{ dur: number }> = ({ dur }) => {
  const f = useCurrentFrame();
  const fade = useSceneFade(dur);
  const tDrop = Math.round(dur * 0.35);
  const tPump = tDrop + 15;
  const tFull = Math.round(dur * 0.8);
  const level = f < tDrop ? interpolate(f, [0, tDrop], [5, -15]) : f < tPump ? -15 : interpolate(f, [tPump, tFull], [-15, 5], clamp);
  const pumpOn = f >= tPump && f < tFull;
  const soilY = 1060; // soil surface
  const cm = 24; // px per cm
  const waterY = soilY - level * cm;
  return (
    <AbsoluteFill style={{ opacity: fade, background: "#EAF3EC", alignItems: "center" }}>
      <div style={{ marginTop: 120, height: 330, display: "flex", flexDirection: "column", alignItems: "center", gap: 10 }}>
        {f < tPump ? (
          <Pop at={4}>
            <Caption size={70} color={PADDY.bg} style={{ textShadow: "none" }}>
              {t("पाइप में पानी 15 सेमी नीचे?")}
            </Caption>
          </Pop>
        ) : (
          <Pop at={tPump}>
            <Caption size={70} color={PADDY.bg} style={{ textShadow: "none" }}>
              {t("पंप अपने-आप चालू")}
            </Caption>
          </Pop>
        )}
        {f >= tFull && (
          <Pop at={tFull}>
            <Caption size={70} color="#8A3A1E" style={{ textShadow: "none" }}>
              {t("5 सेमी पानी भरते ही बंद")}
            </Caption>
          </Pop>
        )}
      </div>
      <svg width="1080" height="1920" viewBox="0 0 1080 1920" style={{ position: "absolute", inset: 0 }}>
        {/* soil block */}
        <rect x="120" y={soilY} width="840" height="560" fill="#7A5A33" />
        {/* standing water above soil */}
        {level > 0 && <rect x="120" y={waterY} width="840" height={soilY - waterY} fill="#5FA8D3" opacity="0.75" />}
        {/* saturated soil band below surface */}
        <rect x="120" y={Math.max(soilY, waterY)} width="840" height={soilY + 560 - Math.max(soilY, waterY)} fill="#5A4024" />
        {Array.from({ length: 6 }, (_, c) => (
          <path key={c} d={`M ${200 + c * 140} ${soilY} q -14 -60 -6 -90 M ${200 + c * 140} ${soilY} q 14 -60 6 -92`} stroke="#5DA23A" strokeWidth="8" fill="none" />
        ))}
        {/* the tube */}
        <rect x="490" y={soilY - 160} width="100" height="460" fill="rgba(255,255,255,0.35)" stroke="#E9EEF1" strokeWidth="8" />
        {Array.from({ length: 8 }, (_, k) => (
          <circle key={k} cx={k % 2 ? 510 : 570} cy={soilY + 30 + k * 30} r="7" fill="#E9EEF1" />
        ))}
        <rect x="498" y={waterY} width="84" height={soilY + 296 - waterY} fill="#5FA8D3" />
        {/* scale */}
        {[5, 0, -15].map((v) => (
          <g key={v}>
            <line x1="610" x2="660" y1={soilY - v * cm} y2={soilY - v * cm} stroke={v === -15 ? C.red : C.ink} strokeWidth="5" />
            <text x="672" y={soilY - v * cm + 14} fontSize="40" fontWeight="800" fill={v === -15 ? C.red : C.ink} fontFamily="sans-serif">
              {v > 0 ? `+${v}` : v} cm
            </text>
          </g>
        ))}
        {/* sensor + controller */}
        <rect x="515" y={soilY - 210} width="50" height="60" rx="8" fill={PADDY.accent} stroke={C.line} strokeWidth="4" />
        <MotorBox x={140} y={560} w={230} on={pumpOn} f={f} />
        {pumpOn &&
          Array.from({ length: 10 }, (_, k) => {
            const p = ((f * 3 + k * 20) % 200) / 200;
            return <circle key={k} cx={380 + p * 560} cy={soilY - 40 - Math.sin(p * Math.PI) * 30} r="9" fill="#2F7FC1" />;
          })}
      </svg>
    </AbsoluteFill>
  );
};

export const PhoneSms: React.FC<{ dur: number }> = ({ dur }) => {
  const f = useCurrentFrame();
  const fade = useSceneFade(dur);
  const msgs = ["खेत 2: पानी 15 सेमी नीचे। पंप चालू किया।", "खेत 2: 5 सेमी पानी भर गया। पंप बंद।", "कल बारिश की संभावना। सिंचाई रोकी।"];
  return (
    <AbsoluteFill style={{ opacity: fade, background: `linear-gradient(${PADDY.bg}, ${PADDY.deep})`, alignItems: "center" }}>
      <Pop at={6} style={{ marginTop: 150 }}>
        <Caption size={76}>{t("कॉल या SMS से पंप चालू-बंद")}</Caption>
      </Pop>
      <Pop at={Math.round(dur * 0.15)} style={{ marginTop: 50 }}>
        <div style={{ width: 560, borderRadius: 50, background: "#2B2B2B", padding: 30, display: "flex", flexDirection: "column", gap: 24 }}>
          <div style={{ borderRadius: 18, background: "#CFE3C9", padding: "28px 26px", display: "flex", flexDirection: "column", gap: 18, minHeight: 520 }}>
            {msgs.map((m, i) => (
              <Pop key={i} at={Math.round(dur * 0.2) + i * Math.round(dur * 0.15)} rise={14}>
                <div style={{ background: "#FFFFFF", borderRadius: 14, padding: "14px 18px", fontFamily: BODY, fontWeight: 700, fontSize: 34, color: C.ink, lineHeight: 1.3 }}>{t(m)}</div>
              </Pop>
            ))}
          </div>
          <div style={{ display: "grid", gridTemplateColumns: "repeat(3, 1fr)", gap: 14 }}>
            {Array.from({ length: 9 }, (_, k) => (
              <div key={k} style={{ height: 52, borderRadius: 12, background: "#444", color: "#DDD", fontFamily: BODY, fontSize: 30, display: "flex", alignItems: "center", justifyContent: "center" }}>
                {k + 1}
              </div>
            ))}
          </div>
        </div>
      </Pop>
      <Pop at={Math.round(dur * 0.65)} style={{ marginTop: 44 }}>
        <Caption size={60} color={PADDY.accent}>
          {t("स्मार्टफोन ज़रूरी नहीं · eSIM, अलग सिम नहीं")}
        </Caption>
      </Pop>
    </AbsoluteFill>
  );
};

// ---------- WM-Pro scenes ----------

const PLOTS: [number, number][] = [
  [60, 640],
  [560, 640],
  [60, 1000],
  [560, 1000],
  [310, 1360],
];

export const BigFarmWalk: React.FC<{ dur: number }> = ({ dur }) => {
  const f = useCurrentFrame();
  const fade = useSceneFade(dur, 0, 8);
  const k = Math.min(4, Math.floor((f / dur) * 5));
  const p = ((f / dur) * 5) % 1;
  const from = PLOTS[Math.max(0, k - 1)];
  const to = PLOTS[k];
  const x = interpolate(p, [0, 0.6], [from[0] + 230, to[0] + 230], clamp);
  const y = interpolate(p, [0, 0.6], [from[1] + 120, to[1] + 120], clamp);
  return (
    <AbsoluteFill style={{ opacity: fade, background: "#EFE6CF" }}>
      <svg width="1080" height="1920" viewBox="0 0 1080 1920">
        {PLOTS.map(([px, py], i) => (
          <g key={i}>
            <rect x={px} y={py} width="460" height="320" rx="18" fill="#9CC27A" stroke="#6E8F52" strokeWidth="5" />
            <Valve x={px + 60} y={py + 40} open={i < k} s={1.3} />
            <Valve x={px + 400} y={py + 40} open={i < k} s={1.3} />
          </g>
        ))}
        <Farmer x={x} y={y} s={1.1} walk={f * 0.5} />
      </svg>
      <div style={{ position: "absolute", top: 230, width: "100%", display: "flex", flexDirection: "column", gap: 20 }}>
        <Pop at={10}>
          <Caption color={C.ink} size={80} style={{ textShadow: "none" }}>
            {t("5 एकड़, 10 वाल्व…")}
          </Caption>
        </Pop>
        <Pop at={Math.round(dur * 0.35)}>
          <Caption size={80} color="#8A3A1E" style={{ textShadow: "none" }}>
            {t("हर बार पैदल?")}
          </Caption>
        </Pop>
      </div>
    </AbsoluteFill>
  );
};

export const ValveGrid: React.FC<{ dur: number }> = ({ dur }) => {
  const f = useCurrentFrame();
  const fade = useSceneFade(dur);
  const span = (dur - 40) / 5;
  const active = Math.min(4, Math.floor(Math.max(0, f - 20) / span));
  const master: [number, number] = [540, 560];
  return (
    <AbsoluteFill style={{ opacity: fade, background: WMPRO.deep, alignItems: "center" }}>
      <div style={{ marginTop: 110, display: "flex", flexDirection: "column", gap: 8, alignItems: "center" }}>
        <Pop at={4}>
          <Caption size={74}>{t("बिना तार, रेडियो से")}</Caption>
        </Pop>
        <Pop at={Math.round(dur * 0.45)}>
          <Caption size={58} color={WMPRO.accent}>
            {t("क्रम से, एक-एक प्लॉट")}
          </Caption>
        </Pop>
      </div>
      <svg width="1080" height="1920" viewBox="0 0 1080 1920" style={{ position: "absolute", inset: 0 }}>
        {PLOTS.map(([px, py], i) => {
          const on = i === active;
          const done = i < active;
          return (
            <g key={i}>
              <rect x={px} y={py} width="460" height="320" rx="18" fill={on ? "#6FB55A" : done ? "#8DBB6C" : "#C9B98A"} stroke={on ? WMPRO.accent : "#6E8F52"} strokeWidth={on ? 10 : 4} />
              <ProNode x={px + 150} y={py + 30} w={170} a={on} b={on} />
              {on &&
                Array.from({ length: 12 }, (_, d) => <circle key={d} cx={px + 40 + (d % 6) * 76} cy={py + 230 + Math.floor(d / 6) * 50 + ((f * 2 + d * 9) % 30)} r="7" fill="#5FA8D3" />)}
              <text x={px + 230} y={py + 305} textAnchor="middle" fontSize="34" fontWeight="800" fill={C.ink} fontFamily="sans-serif">
                {t("प्लॉट")} {i + 1}
                {done ? " ✓" : ""}
              </text>
              {on && [0, 12].map((o) => {
                const pp = ((f + o) % 24) / 24;
                return <line key={o} x1={master[0]} y1={master[1]} x2={master[0] + (px + 235 - master[0]) * pp} y2={master[1] + (py + 60 - master[1]) * pp} stroke={WMPRO.accent} strokeWidth="6" strokeDasharray="10 10" opacity={1 - pp} />;
              })}
            </g>
          );
        })}
        <rect x={master[0] - 70} y={master[1] - 50} width="140" height="80" rx="14" fill="#FFFFFF" stroke={C.line} strokeWidth="5" />
        <text x={master[0]} y={master[1] + 4} textAnchor="middle" fontSize="28" fontWeight="800" fill={WMPRO.bg} fontFamily="sans-serif">
          {t("मास्टर")}
        </text>
      </svg>
    </AbsoluteFill>
  );
};

export const ProApp: React.FC<{ dur: number }> = ({ dur }) => {
  const fade = useSceneFade(dur);
  const rows = [1, 2, 3, 4, 5];
  return (
    <AbsoluteFill style={{ opacity: fade, background: `linear-gradient(${WMPRO.bg}, ${WMPRO.deep})`, alignItems: "center" }}>
      <Pop at={6} style={{ marginTop: 170 }}>
        <Caption size={76}>{t("पूरा खेत फोन पर")}</Caption>
      </Pop>
      <Pop at={Math.round(dur * 0.15)} style={{ marginTop: 50 }}>
        <div style={{ width: 640, borderRadius: 60, background: "#111", padding: 26 }}>
          <div style={{ borderRadius: 40, background: "#F5F7F2", padding: "40px 40px 50px", display: "flex", flexDirection: "column", gap: 20 }}>
            <div style={{ fontFamily: DISPLAY, fontWeight: 800, fontSize: 48, color: WMPRO.bg }}>WM-Pro</div>
            {rows.map((r, i) => (
              <Pop key={r} at={Math.round(dur * 0.2) + i * 8} rise={16}>
                <div style={{ display: "flex", justifyContent: "space-between", fontFamily: BODY, fontWeight: 700, fontSize: 36, color: C.ink, borderTop: "2px solid #D8E0DC", paddingTop: 14 }}>
                  <span>
                    {t("प्लॉट")} {r} · V{2 * r - 1} V{2 * r}
                  </span>
                  <span>{`0${5 + r}:00 · 40 min`}</span>
                </div>
              </Pop>
            ))}
          </div>
        </div>
      </Pop>
      <Pop at={Math.round(dur * 0.6)} style={{ marginTop: 40 }}>
        <Caption size={56} color={WMPRO.accent}>
          {t("लैचिंग वाल्व · बैटरी बहुत कम खर्च")}
        </Caption>
      </Pop>
    </AbsoluteFill>
  );
};
