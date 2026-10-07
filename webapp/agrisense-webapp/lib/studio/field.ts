// Stage 9: field-log records from the board, turned into chart series.
export type FieldRecord = { t: string; design: number; values: Record<string, number | null>; outputs: Record<string, number> };

export type Point = { t: number; v: number };
export type Band = { from: number; to: number };

export function series(records: FieldRecord[], ref: string): Point[] {
  return records
    .map((r) => ({ t: Date.parse(r.t), v: r.values[ref] }))
    .filter((p): p is Point => typeof p.v === "number" && Number.isFinite(p.v));
}

// Spans where an output was ON. A gap of more than 3 logging intervals ends a span.
export function onBands(records: FieldRecord[], port: string): Band[] {
  const out: Band[] = [];
  let start: number | null = null;
  let last = 0;
  for (const r of records) {
    const t = Date.parse(r.t);
    const on = r.outputs[port] === 1;
    if (start !== null && t - last > 30 * 60 * 1000) { out.push({ from: start, to: last }); start = null; }
    if (on && start === null) start = t;
    if (!on && start !== null) { out.push({ from: start, to: t }); start = null; }
    last = t;
  }
  if (start !== null) out.push({ from: start, to: last });
  return out;
}

export function summary(records: FieldRecord[]) {
  if (!records.length) return null;
  const first = Date.parse(records[0].t), last = Date.parse(records[records.length - 1].t);
  return { count: records.length, first, last, hours: Math.round((last - first) / 36e5) };
}

// Hours an output spent ON across the trial.
export function onHours(records: FieldRecord[], port: string): number {
  return Math.round(onBands(records, port).reduce((h, b) => h + (b.to - b.from), 0) / 36e5 * 10) / 10;
}
