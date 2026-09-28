import { homedir } from "os";
import { join } from "path";

// Where student questionnaire answers are stored: one JSON object per line.
// Kept outside the repo so deploys (git stash -u / pull) never move or delete it.
export function surveyFile(): string {
  const dir = process.env.WORKSHOP_SURVEY_DIR || join(homedir(), "agrisense-data");
  return join(dir, "workshop-survey.jsonl");
}
