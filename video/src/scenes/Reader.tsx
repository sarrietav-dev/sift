import { AbsoluteFill, interpolate, spring, useCurrentFrame, useVideoConfig } from "remotion";
import { CursorTrack } from "@/components/snap-cn/cursor-track";
import { ARTICLE, PLAY_BUTTON, ReaderWindow, WIN } from "../ReaderWindow";
import { OMARCHY, SANS } from "../theme";
import { FPS, scene } from "../timeline";

const S = scene("reader");
const LISTEN = S.lines[0];
const ARTICLE_LINE = S.lines[1];
export const CLICK = ARTICLE_LINE.from - 8;
const N0 = ARTICLE.opening[0].split(" ").length;

export const Reader = () => {
  const frame = useCurrentFrame();
  const { fps } = useVideoConfig();
  const enter = spring({ frame, fps, config: { damping: 20 } });

  // Which word Kokoro is speaking right now.
  const ms = ((frame - ARTICLE_LINE.from) / FPS) * 1000;
  let spoken = -1;
  ARTICLE_LINE.words.forEach((w, i) => {
    if (ms >= w.startMs) spoken = i;
  });
  const reading = frame >= ARTICLE_LINE.from && frame < ARTICLE_LINE.from + ARTICLE_LINE.frames;
  const playing = frame >= CLICK + 2 && frame < ARTICLE_LINE.from + ARTICLE_LINE.frames + 6;
  const sentence = spoken < 0 ? (frame >= CLICK ? 0 : -1) : spoken < N0 ? 0 : 1;
  const word = !reading || spoken < 0 ? -1 : spoken < N0 ? spoken : spoken - N0;

  const progress = interpolate(frame, [ARTICLE_LINE.from, ARTICLE_LINE.from + ARTICLE_LINE.frames], [0, 0.22], {
    extrapolateLeft: "clamp",
    extrapolateRight: "clamp",
  });

  // Push in on the paragraph once it starts reading.
  const push = spring({ frame: frame - CLICK - 4, fps, config: { damping: 30, stiffness: 40 } });
  const scale = 1 + push * 0.32;
  const originY = WIN.y + 520;

  const badge = spring({ frame: frame - ARTICLE_LINE.from, fps, config: { damping: 18 } });

  return (
    <AbsoluteFill style={{ background: OMARCHY.ground }}>
      <AbsoluteFill style={{ transform: `scale(${scale})`, transformOrigin: `960px ${originY}px` }}>
        <ReaderWindow
          state={{ sentence, word, playing, progress, position: sentence === 1 ? "2 / 9" : "1 / 9" }}
          style={{ opacity: enter, transform: `translateY(${(1 - enter) * 60}px) scale(${0.94 + 0.06 * enter})` }}
        />
      </AbsoluteFill>
      <CursorTrack
        mode="dark"
        theme={{ primary: OMARCHY.accent }}
        size={44}
        path={[
          { at: LISTEN.from + 20, x: 0.62, y: 1.08, duration: 8 },
          { at: CLICK - 26, x: PLAY_BUTTON.x / 1920, y: PLAY_BUTTON.y / 1080, duration: 26, click: true },
          { at: CLICK + 24, x: 0.3, y: 1.12, duration: 24 },
        ]}
      />
      {/* Voice chip */}
      <div
        style={{
          position: "absolute",
          right: 70,
          top: 40,
          opacity: badge * (reading ? 1 : 0.0),
          transform: `translateY(${(1 - badge) * -20}px)`,
          background: "rgba(20,20,20,0.85)",
          border: `1px solid ${OMARCHY.line}`,
          borderRadius: 999,
          padding: "12px 24px",
          color: OMARCHY.text,
          fontFamily: SANS,
          fontSize: 22,
          display: "flex",
          gap: 12,
          alignItems: "center",
        }}
      >
        <span style={{ width: 10, height: 10, borderRadius: 5, background: OMARCHY.accent }} />
        Kokoro · <span style={{ color: OMARCHY.accent }}>af_heart</span> · on this machine
      </div>
    </AbsoluteFill>
  );
};
