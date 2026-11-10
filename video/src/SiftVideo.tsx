import { AbsoluteFill, Audio, Sequence, interpolate, staticFile, useCurrentFrame } from "remotion";
import { PunchLines } from "@/components/snap-cn/punch-lines";
import { TerminalSimulator } from "@/components/snap-cn/terminal-simulator";
import { TitleSweep } from "@/components/snap-cn/title-sweep";
import { WordCaptions } from "@/components/snap-cn/word-captions";
import { DOT_BLOOM_CUT, DOT_BLOOM_FRAMES, DotBloom } from "@/components/snap-cn/dot-bloom";
import { Clip } from "./scenes/Clip";
import { CLI_LINES, CLI_SPEED, Sound } from "./Sound";
import { Native } from "./scenes/Native";
import { Queue } from "./scenes/Queue";
import { Reader } from "./scenes/Reader";
import { OMARCHY, SANS } from "./theme";
import { findWord, scene, TIMELINE, wordFrame, type PlacedScene } from "./timeline";

const Intro = () => {
  const L = scene("intro").lines[0];
  const second = wordFrame(L, findWord(L, "Too", 1));
  const meet = wordFrame(L, findWord(L, "Meet"));
  return (
    <PunchLines
      mode="dark"
      script="Too many tabs. | Too little time / to read them. | Meet sift."
      styles="slide,slide,punch"
      sizes="1,1,1.5"
      holds={`${second},${meet - second},${scene("intro").frames - meet + 10}`}
      accentBeat={3}
      accentColor={OMARCHY.accent}
      ground={OMARCHY.ground}
      ink={OMARCHY.text}
      fontSize={130}
    />
  );
};

const Cli = () => (
  <TerminalSimulator
    mode="dark"
    theme={{ primary: OMARCHY.accent, background: OMARCHY.ground }}
    background={OMARCHY.ground}
    intro="*scriptable* from the *shell.*"
    command={null}
    fontSize={16}
    speed={CLI_SPEED}
    lines={CLI_LINES}
  />
);

const Outro = () => {
  const frame = useCurrentFrame();
  const footer = interpolate(frame, [70, 90], [0, 1], { extrapolateLeft: "clamp", extrapolateRight: "clamp" });
  return (
    <AbsoluteFill style={{ background: "#000" }}>
      <TitleSweep titles={["sift", "Read the web, aloud."]} speed={1.4} />
      <div
        style={{
          position: "absolute",
          bottom: 90,
          width: "100%",
          textAlign: "center",
          fontFamily: SANS,
          fontSize: 26,
          letterSpacing: "0.04em",
          color: "#b9b2a8",
          opacity: footer,
        }}
      >
        Local Kokoro voices · Defuddle extraction · Native Qt 6 on Omarchy
      </div>
    </AbsoluteFill>
  );
};

const VISUALS: Record<string, React.FC> = {
  intro: Intro,
  clip: Clip,
  reader: Reader,
  queue: Queue,
  cli: Cli,
  native: Native,
  outro: Outro,
};

/** Lines whose words are already on screen another way get no captions. */
const UNCAPTIONED = new Set(["intro", "article", "outro"]);

const Voice = ({ s }: { s: PlacedScene }) => (
  <>
    {s.lines.map((l) => (
      <Sequence key={l.id} from={l.from} durationInFrames={l.frames + 6} name={`voice:${l.id}`}>
        <Audio src={staticFile(l.src)} />
        {!UNCAPTIONED.has(l.id) && (
          <WordCaptions
            captions={l.words}
            preset="clean"
            mode="dark"
            aspect="16:9"
            activeStyle="color"
            accentColor={OMARCHY.accent}
            fontSize={46}
            groupSize={3}
          />
        )}
      </Sequence>
    ))}
  </>
);

/** Dot Bloom covers these cuts; the cut itself lands at DOT_BLOOM_CUT. */
const BLOOMS = ["clip", "native"];

export const SiftVideo = () => (
  <AbsoluteFill style={{ background: OMARCHY.ground }}>
    {TIMELINE.map((s) => {
      const Visual = VISUALS[s.id];
      return (
        <Sequence key={s.id} from={s.from} durationInFrames={s.frames} name={s.id}>
          <Visual />
          <Voice s={s} />
        </Sequence>
      );
    })}
    {TIMELINE.filter((s) => BLOOMS.includes(s.id)).map((s) => (
      <Sequence key={`bloom:${s.id}`} from={s.from - DOT_BLOOM_CUT} durationInFrames={DOT_BLOOM_FRAMES} name={`bloom:${s.id}`}>
        <DotBloom />
      </Sequence>
    ))}
    <Sound />
  </AbsoluteFill>
);
