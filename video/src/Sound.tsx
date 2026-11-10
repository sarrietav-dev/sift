import { Audio, Sequence, interpolate, staticFile } from "remotion";
import { computeLineStarts, typingDuration, type TerminalLine } from "@/components/snap-cn/terminal-simulator";
import { DOT_BLOOM_CUT } from "@/components/snap-cn/dot-bloom";
import { MARKDOWN, STRIP } from "./scenes/Clip";
import { ADD, ADVANCE, DRAG, REOPEN } from "./scenes/Queue";
import { CLICK } from "./scenes/Reader";
import { STOPS } from "./scenes/Native";
import { findWord, scene, TIMELINE, TOTAL_FRAMES, wordFrame } from "./timeline";

/** The terminal script, shared with the Cli scene so keystrokes land on the typing. */
export const CLI_LINES: TerminalLine[] = [
  { text: "sift add example.com/art", type: "command", delay: 2 },
  { text: "Saved 7  Quiet Art", type: "log", delay: 3 },
  { text: "sift queue add 7", type: "command", delay: 4 },
  { text: "✓ Queued at 3", type: "success", delay: 3 },
  { text: "sift queue move 3 top", type: "command", delay: 4 },
  { text: "✓ Moved to 1", type: "success", delay: 3 },
  { text: "sift play 7", type: "command", delay: 4, pause: 10 },
];
export const CLI_SPEED = 1.15;
const CLI_CHARS_PER_FRAME = 2;
const CLI_CHUNK = 3;
/** With an intro and no command panel the terminal starts typing here. */
const CLI_TYPE_START = 64;

interface Cue {
  at: number; // absolute frame
  src: string;
  volume: number;
  name: string;
}

const sfx = (name: string) => staticFile(`sfx/${name}.wav`);
const abs = (id: string, f: number) => scene(id).from + f;

const cues = (): Cue[] => {
  const out: Cue[] = [];
  const add = (at: number, name: string, volume: number) =>
    out.push({ at: Math.max(0, Math.round(at)), src: sfx(name), volume, name });

  // Intro: a whip on every card, a low hit under "Meet sift."
  const intro = scene("intro").lines[0];
  add(abs("intro", 0), "whip", 0.35);
  add(abs("intro", wordFrame(intro, findWord(intro, "Too", 1)) - 2), "whip", 0.35);
  add(abs("intro", wordFrame(intro, findWord(intro, "Meet")) - 2), "hit", 0.9);

  // Dot Bloom cuts: an airy swell into a whoosh.
  for (const id of ["clip", "native"]) {
    add(scene(id).from - 33, "riser", 0.35);
    add(scene(id).from - DOT_BLOOM_CUT + 4, "whoosh", 0.5);
  }

  // Clip: the clutter pops off one by one, the Markdown file lands.
  add(abs("clip", 0), "page-turn", 0.4);
  for (let k = 0; k < 6; k++) add(abs("clip", STRIP + k * 3 + 2), "pop", 0.22);
  add(abs("clip", STRIP), "whoosh", 0.4);
  add(abs("clip", MARKDOWN), "whip", 0.3);

  // Reader: the window slides up, the cursor clicks Play.
  add(abs("reader", 0), "whoosh", 0.35);
  add(abs("reader", CLICK), "mouse-click", 0.8);

  // Queue: new page drops in, gets dragged, playback advances, resume toast.
  add(abs("queue", 2), "whoosh", 0.3);
  add(abs("queue", ADD), "pop", 0.4);
  add(abs("queue", DRAG - 4), "mouse-click", 0.5);
  add(abs("queue", DRAG + 14), "ui-switch", 0.4);
  add(abs("queue", ADVANCE), "ding", 0.18);
  add(abs("queue", REOPEN), "pop", 0.35);

  // CLI: a keystroke for each lurch of the typing, a tick for each output line.
  const starts = computeLineStarts(CLI_LINES, CLI_CHARS_PER_FRAME, CLI_CHUNK, CLI_TYPE_START);
  CLI_LINES.forEach((line, i) => {
    const local = (f: number) => abs("cli", f / CLI_SPEED);
    if (line.type === "command") {
      const frames = typingDuration(line.text.length, CLI_CHARS_PER_FRAME, CLI_CHUNK);
      for (let f = 0; f < frames * 1.5; f += 1.5) add(local(starts[i] + f), "key", 0.25 + ((i * 7 + f * 13) % 5) / 40);
    } else {
      add(local(starts[i]), "pop", 0.15);
    }
  });

  // Native: a switch on every theme change.
  STOPS.slice(1).forEach(([at]) => add(abs("native", at), "ui-switch", 0.35));

  // Outro: the title lands, then the tagline.
  add(abs("outro", 0), "hit", 0.6);
  add(abs("outro", 36 / 1.4), "whoosh", 0.35);
  return out;
};

/** Ducked under the voice: full between lines, low while anyone speaks. */
const MUSIC_UP = 0.22;
const MUSIC_DUCKED = 0.08;
const DUCK_RAMP = 8;

const SPEAKING = TIMELINE.flatMap((s) =>
  s.lines.map((l) => [s.from + l.from, s.from + l.from + l.frames] as const),
);

const musicVolume = (f: number) => {
  let duck = 0;
  for (const [a, b] of SPEAKING) {
    duck = Math.max(
      duck,
      interpolate(f, [a - DUCK_RAMP, a, b, b + DUCK_RAMP * 2], [0, 1, 1, 0], {
        extrapolateLeft: "clamp",
        extrapolateRight: "clamp",
      }),
    );
  }
  const fadeOut = interpolate(f, [TOTAL_FRAMES - 60, TOTAL_FRAMES], [1, 0], {
    extrapolateLeft: "clamp",
    extrapolateRight: "clamp",
  });
  return (MUSIC_UP + (MUSIC_DUCKED - MUSIC_UP) * duck) * fadeOut;
};

export const Sound = () => (
  <>
    <Audio src={staticFile("music/bed.wav")} volume={musicVolume} />
    {cues().map((c, i) => (
      <Sequence key={i} from={c.at} durationInFrames={90} name={`sfx:${c.name}`} layout="none">
        <Audio src={c.src} volume={c.volume} />
      </Sequence>
    ))}
  </>
);
