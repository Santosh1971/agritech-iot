import { readFile } from "fs/promises";
import { homedir } from "os";
import { basename, join } from "path";

// The document library (presentations, proposals, videos) lives on the server
// under ~/agrisense-data/library, outside the repo: the repo is public and these
// files are internal. index.json lists every item; files/ holds the PDFs and MP4s.
// tools/library/publish.sh uploads both from Santosh's Mac.

export type LibraryCategory = { id: string; name: string; note?: string; archive?: boolean };
export type LibraryItem = {
  id: string;
  cat: string;
  kind: "pdf" | "video" | "link";
  title: string;
  date: string;
  note?: string;
  lang?: string;
  file?: string;
  href?: string;
  live?: string;
};
export type LibraryIndex = { updated: string; categories: LibraryCategory[]; items: LibraryItem[] };

export const libraryRoot = () => join(process.env.WORKSHOP_SURVEY_DIR || join(homedir(), "agrisense-data"), "library");

export async function readLibraryIndex(): Promise<LibraryIndex> {
  try {
    return JSON.parse(await readFile(join(libraryRoot(), "index.json"), "utf8")) as LibraryIndex;
  } catch {
    return { updated: "", categories: [], items: [] };
  }
}

/** Path of a library file, only if index.json lists it (no path tricks). */
export async function libraryFilePath(name: string): Promise<string | null> {
  const safe = basename(name);
  if (safe !== name) return null;
  const index = await readLibraryIndex();
  if (!index.items.some((i) => i.file === safe)) return null;
  return join(libraryRoot(), "files", safe);
}
