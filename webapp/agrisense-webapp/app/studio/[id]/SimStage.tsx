"use client";
import { useMemo, useState } from "react";
import { defaultRules } from "@/lib/studio/automation";
import { wokwiProject } from "@/lib/studio/wokwi";
import { Eng, Why } from "../EngView";
import { GateBar, type StageProps } from "./ProjectClient";
import RulesEditor from "./RulesEditor";

function CopyBlock({ id, title, text }: { id: string; title: string; text: string }) {
  const [done, setDone] = useState(false);
  const copy = async () => {
    try { await navigator.clipboard.writeText(text); setDone(true); setTimeout(() => setDone(false), 1500); }
    catch { (document.getElementById(id) as HTMLTextAreaElement | null)?.select(); }
  };
  return (
    <div className="grid" style={{ gap: 6 }}>
      <div className="row between"><b className="mono">{title}</b><button type="button" className="btn ghost small" onClick={copy}>{done ? "Copied" : "Copy"}</button></div>
      <textarea id={id} className="code-area" readOnly value={text} />
    </div>
  );
}

export default function SimStage(props: StageProps) {
  const { state } = props;
  const archDone = state.stages.arch.status === "DONE";
  const design = state.design;
  const rules = design?.rules ?? (design ? defaultRules(design.ports) : []);
  const project = useMemo(
    () => (design ? wokwiProject(state.kit, state.title, design.ports, rules) : null),
    [design?.version, state.kit, state.title], // eslint-disable-line react-hooks/exhaustive-deps
  );

  if (!archDone || !design || !project) {
    return <div className="card"><p>Finish the Architecture stage first. The simulation is built from your design.</p></div>;
  }
  const buildLocked = state.stages.build.status === "DONE" || state.stages.build.status === "SUBMITTED";

  return (
    <>
      <div className="two">
        <div className="card grid">
          <div className="eyebrow">Try it in Wokwi</div>
          <ol className="steps">
            <li>Open <a href="https://wokwi.com/projects/new/esp32-s3" target="_blank" rel="noreferrer">a new ESP32-S3 project on wokwi.com</a> in another tab.</li>
            <li>Copy <b>sketch.ino</b> below over the code in the <span className="mono">sketch.ino</span> tab.</li>
            <li>Copy <b>diagram.json</b> over the <span className="mono">diagram.json</span> tab. The parts appear, already wired.</li>
            <li>Open the Library Manager tab and add each library listed in <b>libraries.txt</b>.</li>
            <li>Press ▶. Change the sensors and watch the outputs switch.</li>
          </ol>
          {project.notes.map((n, i) => <div key={i} className="msg ok"><span className="ic">i</span><span>{n}</span></div>)}
          <p className="small muted">The simulated clock starts at 07:00 and runs 60 times faster, so one real second is one minute. That lets you watch time windows work.</p>
          {!design.rules && <div className="msg warn"><span className="ic">!</span><span>These are the suggested rules. Change them below and the simulation follows.</span></div>}
        </div>
        <div className="grid">
          <Why title="Simulate first">
            A simulation finds a wrong threshold in seconds, before a pump runs or a plant wilts. It can&apos;t find a loose wire, which is why the Test stage still uses the real board.
          </Why>
          <CopyBlock id="sim-sketch" title="sketch.ino" text={project.sketch} />
          <CopyBlock id="sim-diagram" title="diagram.json" text={project.diagram} />
          <CopyBlock id="sim-libs" title="libraries.txt" text={project.libraries} />
          <Eng title="what this sketch is">
            <p className="small">This is your design as a plain Arduino sketch: the same ports, the same rules and the same fail-safe. The real board runs the studio firmware instead, which reads your design as data. That lets one firmware serve every project.</p>
          </Eng>
        </div>
      </div>
      <RulesEditor {...props} locked={buildLocked} />
      <GateBar {...props} stage="sim" canFinish finishHint="This stage is optional. Mark it done when you've tried the simulation, or go straight to Build." />
    </>
  );
}
