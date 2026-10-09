"use client";
// One reading over the field trial: a 2 px line on a recessive grid, with the
// periods an output was ON shaded behind it. One series per chart, so the
// title names it and there's no legend; hovering snaps a crosshair to the
// nearest record. Used on the Field trial stage and in the printable report.
import { useState } from "react";
import type { Band, Point } from "@/lib/studio/field";

const W = 640, H = 180, L = 44, R = 12, T = 12, B = 26;

function niceTicks(min: number, max: number, n = 4): number[] {
  const span = max - min || 1;
  const step0 = span / n;
  const mag = 10 ** Math.floor(Math.log10(step0));
  const step = [1, 2, 2.5, 5, 10].map((m) => m * mag).find((s) => s >= step0) ?? step0;
  const out: number[] = [];
  for (let v = Math.ceil(min / step) * step; v <= max + 1e-9; v += step) out.push(Math.round(v * 100) / 100);
  return out;
}

const fmtTime = (t: number) => new Date(t).toLocaleString("en-IN", { day: "numeric", month: "short", hour: "2-digit", minute: "2-digit" });
const fmtDay = (t: number) => new Date(t).toLocaleDateString("en-IN", { day: "numeric", month: "short" });

export default function FieldChart({ title, unit, points, bands = [], bandLabel }: { title: string; unit: string; points: Point[]; bands?: Band[]; bandLabel?: string }) {
  const [hover, setHover] = useState<number | null>(null);
  if (points.length < 2) {
    return (
      <figure className="fchart">
        <figcaption><b>{title}</b></figcaption>
        <p className="small muted">Not enough records yet to draw this.</p>
      </figure>
    );
  }
  const t0 = points[0].t, t1 = points[points.length - 1].t;
  let lo = Math.min(...points.map((p) => p.v)), hi = Math.max(...points.map((p) => p.v));
  if (hi - lo < 1) { lo -= 1; hi += 1; }
  const ticks = niceTicks(lo, hi);
  lo = Math.min(lo, ticks[0]); hi = Math.max(hi, ticks[ticks.length - 1]);
  const x = (t: number) => L + ((t - t0) / (t1 - t0 || 1)) * (W - L - R);
  const y = (v: number) => T + (1 - (v - lo) / (hi - lo || 1)) * (H - T - B);
  const path = points.map((p, i) => `${i ? "L" : "M"}${x(p.t).toFixed(1)},${y(p.v).toFixed(1)}`).join("");
  const days: number[] = [];
  for (let d = new Date(t0).setHours(24, 0, 0, 0); d < t1; d += 864e5) days.push(d);
  const last = points[points.length - 1];
  const h = hover !== null ? points[hover] : null;

  const onMove = (e: React.PointerEvent<SVGSVGElement>) => {
    const box = e.currentTarget.getBoundingClientRect();
    const px = ((e.clientX - box.left) / box.width) * W;
    const t = t0 + ((px - L) / (W - L - R)) * (t1 - t0);
    let best = 0;
    for (let i = 1; i < points.length; i++) if (Math.abs(points[i].t - t) < Math.abs(points[best].t - t)) best = i;
    setHover(best);
  };

  return (
    <figure className="fchart">
      <figcaption>
        <b>{title}</b>
        <span className="small muted"> {unit && `(${unit})`} · {fmtDay(t0)} to {fmtDay(t1)}</span>
        {bands.length > 0 && bandLabel && <span className="small key"><i className="band-key" /> {bandLabel}</span>}
      </figcaption>
      <div className="fchart-wrap">
        <svg viewBox={`0 0 ${W} ${H}`} role="img" aria-label={`${title}, ${points.length} readings from ${fmtTime(t0)} to ${fmtTime(t1)}`}
          onPointerMove={onMove} onPointerLeave={() => setHover(null)}>
          {bands.map((b, i) => <rect key={i} x={x(b.from)} y={T} width={Math.max(1, x(b.to) - x(b.from))} height={H - T - B} className="band" />)}
          {ticks.map((v) => (
            <g key={v}>
              <line x1={L} x2={W - R} y1={y(v)} y2={y(v)} className="grid" />
              <text x={L - 6} y={y(v) + 4} textAnchor="end" className="axis">{v}</text>
            </g>
          ))}
          {days.map((d) => <text key={d} x={x(d)} y={H - 8} textAnchor="middle" className="axis">{fmtDay(d)}</text>)}
          <path d={path} className="line" />
          <circle cx={x(last.t)} cy={y(last.v)} r={4} className="dot" />
          <text x={Math.min(x(last.t) + 6, W - R)} y={y(last.v) - 8} textAnchor="end" className="axis strong">{last.v}{unit === "%" ? " %" : ""}</text>
          {h && (
            <g>
              <line x1={x(h.t)} x2={x(h.t)} y1={T} y2={H - B} className="cross" />
              <circle cx={x(h.t)} cy={y(h.v)} r={4} className="dot" />
            </g>
          )}
        </svg>
        {h && (
          <div className="tip" style={{ left: `${(x(h.t) / W) * 100}%` }}>
            <b>{h.v} {unit}</b><br />{fmtTime(h.t)}
          </div>
        )}
      </div>
    </figure>
  );
}
