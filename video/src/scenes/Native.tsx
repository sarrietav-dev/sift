import { AbsoluteFill, Img, interpolate, spring, staticFile, useCurrentFrame, useVideoConfig } from "remotion";
import { ReaderWindow } from "../ReaderWindow";
import { GRUVBOX, LIGHT, OMARCHY, SANS, TOKYO, mixPalette, type Palette } from "../theme";
import { findWord, scene, wordFrame } from "../timeline";

const S = scene("native");
const L = S.lines[0];

/** Theme changes land on the words that name them; sift crossfades them. */
export const STOPS: [number, Palette][] = [
  [0, OMARCHY],
  [wordFrame(L, findWord(L, "theme")), TOKYO],
  [wordFrame(L, findWord(L, "fonts")), GRUVBOX],
  [wordFrame(L, findWord(L, "light")), LIGHT],
  [wordFrame(L, findWord(L, "dark")), OMARCHY],
];
const FADE = 10;
const SEGMENTS = ["Omarchy", "System", "Light", "Dark"];

export const Native = () => {
  const frame = useCurrentFrame();
  const { fps } = useVideoConfig();

  let palette = STOPS[0][1];
  for (let i = 1; i < STOPS.length; i++) {
    const t = interpolate(frame, [STOPS[i][0], STOPS[i][0] + FADE], [0, 1], { extrapolateLeft: "clamp", extrapolateRight: "clamp" });
    if (t > 0) palette = mixPalette(palette, STOPS[i][1], t);
  }
  const stopIndex = STOPS.filter(([at]) => frame >= at).length - 1;
  const segment = stopIndex === 3 ? 2 : stopIndex === 4 ? 3 : 0;

  const shot = spring({ frame: frame - 6, fps, config: { damping: 20 } });
  const shotOut = spring({ frame: frame - STOPS[1][0] + 4, fps, config: { damping: 20 } });

  return (
    <AbsoluteFill style={{ background: palette.ground }}>
      <ReaderWindow
        palette={palette}
        state={{ sentence: 0, word: -1, playing: false, progress: 0.11, position: "1 / 9" }}
        style={{ transform: "scale(0.9)" }}
      />

      {/* The real Settings dialog, straight from the GUI test. */}
      <Img
        src={staticFile("shots/settings.png")}
        style={{
          position: "absolute",
          left: 1000,
          top: 150,
          width: 820,
          borderRadius: 14,
          boxShadow: "0 40px 120px rgba(0,0,0,0.6)",
          opacity: shot * (1 - shotOut),
          transform: `translateX(${(1 - shot) * 200 + shotOut * 300}px) rotate(${(1 - shot) * 3}deg)`,
        }}
      />

      {/* Appearance picker, mirrored from Settings. */}
      <div
        style={{
          position: "absolute",
          left: "50%",
          bottom: 30,
          transform: `translateX(-50%) translateY(${(1 - shotOut) * 120}px)`,
          opacity: shotOut,
          display: "flex",
          gap: 6,
          padding: 6,
          borderRadius: 14,
          background: "rgba(20,20,20,0.85)",
          border: "1px solid rgba(255,255,255,0.1)",
          fontFamily: SANS,
          fontSize: 22,
        }}
      >
        {SEGMENTS.map((s, i) => (
          <div
            key={s}
            style={{
              padding: "12px 30px",
              borderRadius: 10,
              background: i === segment ? palette.accent : "transparent",
              color: i === segment ? "#111" : "#ddd",
              fontWeight: i === segment ? 600 : 400,
            }}
          >
            {s}
          </div>
        ))}
      </div>

      {/* Theme name */}
      <div
        style={{
          position: "absolute",
          left: 80,
          top: 30,
          fontFamily: SANS,
          fontSize: 22,
          color: palette.text,
          opacity: shotOut,
          display: "flex",
          gap: 12,
          alignItems: "center",
        }}
      >
        <span style={{ width: 12, height: 12, borderRadius: 6, background: palette.accent }} />
        {palette.name}
      </div>
    </AbsoluteFill>
  );
};
