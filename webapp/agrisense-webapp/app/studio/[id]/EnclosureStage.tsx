"use client";
import { useMemo, useState } from "react";
import { BOXES, drillingSvg, openscad, plan, type EnclosureChoice } from "@/lib/studio/enclosure";
import { Eng, Why } from "../EngView";
import { call } from "./api";
import { GateBar, type StageProps } from "./ProjectClient";

function download(name: string, text: string, type: string) {
  const url = URL.createObjectURL(new Blob([text], { type }));
  const a = document.createElement("a");
  a.href = url;
  a.download = name;
  a.click();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}

export default function EnclosureStage(props: StageProps) {
  const { state } = props;
  const design = state.design;
  const st = state.stages.encl;
  const stored = (st.data ?? {}) as Partial<EnclosureChoice>;
  const editable = st.status === "NOT_STARTED" || st.status === "IN_PROGRESS";
  const firstFit = BOXES.find((b) => b.fits.includes(state.kit))?.id ?? "A";
  const [choice, setChoice] = useState<EnclosureChoice>({ box: stored.box ?? firstFit, window: stored.window ?? false });
  const [savedChoice, setSavedChoice] = useState(stored.box ? JSON.stringify({ box: stored.box, window: !!stored.window }) : "");
  const [error, setError] = useState("");
  const p = useMemo(() => (design ? plan(state.kit, design.ports, choice) : null), [design, state.kit, choice]);

  if (state.stages.app.status !== "DONE" || !design || !p) {
    return <div className="card"><p>Finish the App stage first.</p></div>;
  }
  const svg = drillingSvg(p, state.title);
  const slug = state.title.toLowerCase().replace(/[^a-z0-9]+/g, "-").replace(/^-|-$/g, "").slice(0, 40) || "project";
  const dirty = JSON.stringify(choice) !== savedChoice;
  const bad = p.checks.some((c) => c.level === "bad");
  const save = async () => {
    setError("");
    try { await call(`/api/studio/projects/${state.id}/stages/encl`, { data: choice }, "PUT"); setSavedChoice(JSON.stringify(choice)); props.refresh(); }
    catch (e) { setError((e as Error).message); }
  };

  return (
    <>
      <div className="two">
        <div className="card grid">
          <div className="eyebrow">Choose the box</div>
          <div className="choices" role="radiogroup">
            {BOXES.map((b) => (
              <label key={b.id}>
                <input type="radio" name="box" id={`box-${b.id}`} checked={choice.box === b.id} disabled={!editable} onChange={() => setChoice({ ...choice, box: b.id })} />
                {b.name} · {b.w}×{b.h}×{b.d} mm
              </label>
            ))}
          </div>
          {Object.values(design.ports).includes("oled") && (
            <label className="row small" style={{ gap: 6 }}>
              <input type="checkbox" id="encl-window" checked={choice.window} disabled={!editable} onChange={(e) => setChoice({ ...choice, window: e.target.checked })} />
              Cut a window in the lid for the display (30 × 16 mm)
            </label>
          )}
          <div className="eyebrow">Bottom wall · {p.holes.length} holes</div>
          <div className="drill" dangerouslySetInnerHTML={{ __html: svg.replace(/width="[\d.]+mm" height="[\d.]+mm"/, 'width="100%"') }} />
          <div className="tbl-wrap"><table><tbody>
            {p.holes.map((h) => <tr key={h.id}><td className="mono">{h.id}</td><td>{h.label}</td><td className="mono">Ø {h.dia} mm</td><td className="mono">{h.x} mm from the left, {h.y} mm up</td></tr>)}
          </tbody></table></div>
          {p.checks.map((c, i) => <div key={i} className={`msg ${c.level}`}><span className="ic">{c.level === "ok" ? "✓" : c.level === "warn" ? "!" : "✗"}</span><span>{c.text}</span></div>)}
          <div className="row">
            <button className="btn" onClick={() => download(`${slug}-drilling-template.svg`, svg, "image/svg+xml")}>Drilling template (print at 100 %)</button>
            <button className="btn ghost" onClick={() => download(`${slug}-box.scad`, openscad(p, state.title), "text/plain")}>3D-print model (OpenSCAD)</button>
            {editable && <button className="btn ghost" disabled={!dirty || bad} onClick={save}>{dirty ? "Save my choice" : "Saved"}</button>}
          </div>
          {error && <p className="error">{error}</p>}
        </div>
        <div className="grid">
          <Why title="Glands on one wall, facing down">
            Cables enter from the bottom, so rain runs off them instead of into the box. With every gland on one wall the drilling stays simple, and the board&apos;s connectors line up with the holes.
          </Why>
          <div className="card grid small">
            <div className="eyebrow">Using the template</div>
            <ol className="steps">
              <li>Open the drilling template and print it at <b>100 % / actual size</b>.</li>
              <li>Measure the 50 mm line with a ruler. If it isn&apos;t 50 mm, print it again.</li>
              <li>Tape it to the bottom wall, punch each centre, drill a 3 mm pilot hole, then widen it with a step drill.</li>
              <li>For a printed box instead: open the .scad file in OpenSCAD (free), export an STL, and upload it to JLC3DP or your college&apos;s 3D printer.</li>
            </ol>
          </div>
          <Eng title="how the holes are placed">
            <p className="small">Each gland gets its hole plus room for its nut (M12: 20 mm, M16: 25 mm, antenna: 14 mm). Each row is centred on the wall, with 15 mm kept clear at each end for the corner bosses. When one row is too long, the holes go in two rows at 30 % and 70 % of the wall height. Coordinates are in mm from the inside left edge.</p>
            <pre className="code">{JSON.stringify(p.holes, null, 2)}</pre>
          </Eng>
        </div>
      </div>
      <GateBar {...props} stage="encl" canFinish={!dirty && !bad && !!savedChoice}
        finishHint={bad ? "Fix the red check first." : dirty || !savedChoice ? "Save your choice first." : "Drill (or print) the box, fit the board, then submit it to your teacher."} />
    </>
  );
}
