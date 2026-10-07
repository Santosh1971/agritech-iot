// Stage 1 and 2 data shapes and the checks that run in the browser too.
// Kept apart from spec.ts so the Claude SDK never reaches the client bundle.
import type { KitKey } from "./kits";
import type { Ports } from "./rules";

export type ProblemData = {
  story?: string;
  crop?: string;
  area?: string;
  water?: string;
  power?: "mains" | "solar" | "none";
  network?: "wifi" | "mobile" | "none";
  climate?: string;
};

export type Requirement = { id: string; text: string };

export type SpecData = {
  what: string;
  madeOf: string;
  use: string;
  safety: string;
  requirements: Requirement[];
  suggested: Ports;
  source: "claude" | "template";
  note?: string; // shown when the template had to stand in for Claude
  generatedAt: string;
};

export function suggestKit(p: ProblemData): KitKey {
  return p.power === "mains" && p.network === "wifi" ? "MINI" : "MEGA";
}

export function problemComplete(p: ProblemData): string[] {
  const missing: string[] = [];
  if (!p.story || p.story.trim().length < 20) missing.push("Describe the problem in a few sentences.");
  if (!p.power) missing.push("Say whether there is mains power at the site.");
  if (!p.network) missing.push("Say whether there is WiFi or mobile signal at the site.");
  return missing;
}

