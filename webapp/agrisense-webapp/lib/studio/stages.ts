// The nine stages of a Student Product Studio project, in order.
// docs/student-product-studio.md §4 is the source for these.

export type StageKey = "problem" | "spec" | "arch" | "sim" | "build" | "test" | "app" | "encl" | "report";
export type Gate = "auto" | "mentor" | "optional";

export type StageDef = {
  key: StageKey;
  title: string;
  gate: Gate;
  // Shown on stages that aren't built yet, so a student knows what is coming.
  summary: string;
  ready: boolean;
};

export const STAGES: StageDef[] = [
  { key: "problem", title: "Problem", gate: "auto", ready: true,
    summary: "Describe the farm problem: who has it, when, and what it costs." },
  { key: "spec", title: "Specification", gate: "mentor", ready: true,
    summary: "A one-page spec in plain language, with numbered requirements behind it." },
  { key: "arch", title: "Architecture", gate: "auto", ready: true,
    summary: "Put sensors and outputs on the kit's ports. The rules check every choice." },
  { key: "sim", title: "Simulate", gate: "optional", ready: true,
    summary: "Try the design in Wokwi before the hardware arrives." },
  { key: "build", title: "Build", gate: "auto", ready: true,
    summary: "Plug in the modules, set your rules, flash the board and send your design to it over USB." },
  { key: "test", title: "Test", gate: "auto", ready: true,
    summary: "A guided checklist. The board's self-test ticks each item off." },
  { key: "app", title: "App", gate: "auto", ready: true,
    summary: "Design your phone screen. The ASC Studio app builds it from your design." },
  { key: "encl", title: "Enclosure", gate: "mentor", ready: true,
    summary: "Place cable glands on the stock box; get a drilling template or a 3D-print file." },
  { key: "report", title: "Field trial & report", gate: "mentor", ready: true,
    summary: "Run it in the field, then get a project report built from your own data." },
];

export const STAGE_KEYS = STAGES.map((s) => s.key);

export function stageDef(key: string): StageDef | undefined {
  return STAGES.find((s) => s.key === key);
}

export type StageStatus = "NOT_STARTED" | "IN_PROGRESS" | "SUBMITTED" | "DONE";

// The stage a project is "on": the first required stage that isn't done.
export function currentStageIndex(statusByKey: Record<string, StageStatus | undefined>): number {
  const i = STAGES.findIndex((s) => s.gate !== "optional" && statusByKey[s.key] !== "DONE");
  return i === -1 ? STAGES.length - 1 : i;
}

export function gateLabel(g: Gate): string {
  return g === "mentor" ? "Mentor gate" : g === "auto" ? "Auto gate" : "Optional";
}
