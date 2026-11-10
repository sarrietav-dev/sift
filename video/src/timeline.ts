import voiceover from "./voiceover.json";

export const FPS = 30;
export const WIDTH = 1920;
export const HEIGHT = 1080;

export interface Word {
  text: string;
  startMs: number;
  endMs: number;
}

export interface Line {
  id: string;
  voice: string;
  text: string;
  src: string;
  durationSec: number;
  words: Word[];
}

export const LINES = voiceover as Line[];
export const line = (id: string) => {
  const found = LINES.find((l) => l.id === id);
  if (!found) throw new Error(`No voiceover line "${id}"`);
  return found;
};

/**
 * Each scene holds one or more voiceover lines. `lead` is silence before the
 * first line, `gap` sits between lines, `tail` is the hold after the last.
 */
const SCENES = [
  { id: "intro", lines: ["intro"], lead: 0.3, tail: 0.6 },
  { id: "clip", lines: ["clip"], lead: 0.4, tail: 0.8 },
  { id: "reader", lines: ["listen", "article"], lead: 0.3, gap: 0.5, tail: 0.8 },
  { id: "queue", lines: ["queue"], lead: 0.3, tail: 0.8 },
  { id: "cli", lines: ["cli"], lead: 0.4, tail: 1.4 },
  { id: "native", lines: ["native"], lead: 0.4, tail: 1.0 },
  { id: "outro", lines: ["outro"], lead: 0.2, tail: 2.6 },
] as const;

export interface PlacedLine extends Line {
  /** Frame inside the scene where this line starts. */
  from: number;
  frames: number;
}

export interface PlacedScene {
  id: string;
  from: number;
  frames: number;
  lines: PlacedLine[];
}

const sec = (s: number) => Math.round(s * FPS);

export const TIMELINE: PlacedScene[] = (() => {
  let cursor = 0;
  return SCENES.map((scene) => {
    let t = scene.lead;
    const lines = scene.lines.map((id, i) => {
      const l = line(id);
      if (i > 0) t += "gap" in scene ? scene.gap : 0;
      const placed = { ...l, from: sec(t), frames: Math.ceil(l.durationSec * FPS) };
      t += l.durationSec;
      return placed;
    });
    const frames = sec(t + scene.tail);
    const placed = { id: scene.id, from: cursor, frames, lines };
    cursor += frames;
    return placed;
  });
})();

export const TOTAL_FRAMES = TIMELINE.reduce((sum, s) => sum + s.frames, 0);

export const scene = (id: string) => TIMELINE.find((s) => s.id === id)!;

/** Frame (inside its scene) where `word` of a placed line starts speaking. */
export const wordFrame = (l: PlacedLine, index: number) =>
  l.from + Math.round((l.words[Math.min(index, l.words.length - 1)].startMs / 1000) * FPS);

/** Index of the first word whose text matches, case-insensitive, punctuation ignored. */
export const findWord = (l: Line, needle: string, after = 0) => {
  const clean = (s: string) => s.toLowerCase().replace(/[^\w]/g, "");
  const i = l.words.findIndex((w, k) => k >= after && clean(w.text) === clean(needle));
  if (i < 0) throw new Error(`"${needle}" not in line ${l.id}`);
  return i;
};
