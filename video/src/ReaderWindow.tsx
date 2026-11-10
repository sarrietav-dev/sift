import type { CSSProperties, ReactNode } from "react";
import { OMARCHY, SANS, SERIF, type Palette } from "./theme";

/** Window geometry on the 1920x1080 stage. Exported so cursors can aim at it. */
export const WIN = { x: 160, y: 70, w: 1600, h: 940, top: 72, bar: 88 };
/** Centre of the play button, in stage pixels. */
export const PLAY_BUTTON = { x: WIN.x + 108, y: WIN.y + WIN.h - WIN.bar / 2 };

export const ARTICLE = {
  site: "example.com",
  title: "The Quiet Art of Reading",
  byline: "By Example Author",
  /** The opening paragraph, one entry per sentence. Kokoro reads these. */
  opening: [
    "Reading is a conversation with someone who is not in the room.",
    "The page waits, patient and exact, until you are ready to continue.",
  ],
  second:
    "We tend to treat attention as a resource to spend, but a good essay asks for something quieter: a willingness to follow one thought all the way to its end.",
  heading: "A small practice",
  third:
    "Set the page in front of you and let the room go soft. Read one paragraph twice. Notice where your mind pulls away, and bring it back without scolding yourself.",
  quote: "The reader who stops to think is not falling behind. They are doing the work.",
};

export interface ReaderState {
  /** Sentence of `ARTICLE.opening` being read, or -1 for none. */
  sentence: number;
  /** Word inside that sentence currently spoken, or -1. */
  word: number;
  playing: boolean;
  /** 0-1 across the article. */
  progress: number;
  position: string;
}

const Icon = ({ d, size = 22, color, fill }: { d: string; size?: number; color: string; fill?: boolean }) => (
  <svg width={size} height={size} viewBox="0 0 24 24" fill={fill ? color : "none"} stroke={fill ? "none" : color} strokeWidth={2} strokeLinecap="round" strokeLinejoin="round">
    <path d={d} />
  </svg>
);

const ICON = {
  menu: "M4 7h16M4 12h16M4 17h16",
  prev: "M15 6l-6 6 6 6",
  next: "M9 6l6 6-6 6",
  play: "M8 5.5v13l11-6.5z",
  pause: "M8 5h3v14H8zM13 5h3v14h-3z",
  plus: "M12 5v14M5 12h14",
  help: "M12 21a9 9 0 1 0 0-18 9 9 0 0 0 0 18zM9.5 9.5a2.5 2.5 0 1 1 3.5 2.3c-.6.3-1 .8-1 1.5M12 17h.01",
  dots: "M6 12h.01M12 12h.01M18 12h.01",
};

export const ReaderWindow = ({
  palette = OMARCHY,
  state,
  children,
  style,
}: {
  palette?: Palette;
  state: ReaderState;
  children?: ReactNode;
  style?: CSSProperties;
}) => {
  const p = palette;
  const words = (s: string) => s.split(" ");
  return (
    <div
      style={{
        position: "absolute",
        left: WIN.x,
        top: WIN.y,
        width: WIN.w,
        height: WIN.h,
        background: p.window,
        borderRadius: 14,
        overflow: "hidden",
        boxShadow: "0 40px 120px rgba(0,0,0,0.55), 0 0 0 1px rgba(255,255,255,0.06)",
        fontFamily: SANS,
        color: p.text,
        ...style,
      }}
    >
      {/* Top bar */}
      <div style={{ height: WIN.top, display: "flex", alignItems: "center", padding: "0 36px", gap: 28, borderBottom: `1px solid ${p.line}`, background: p.chrome }}>
        <Icon d={ICON.menu} color={p.text} />
        <span style={{ color: p.muted, fontSize: 19 }}>{ARTICLE.site}</span>
        <div style={{ flex: 1 }} />
        <span style={{ display: "flex", alignItems: "center", gap: 10, fontSize: 19 }}>
          <Icon d={ICON.plus} color={p.text} size={20} /> Add page
        </span>
        <Icon d={ICON.help} color={p.text} size={22} />
        <Icon d={ICON.dots} color={p.text} size={26} />
      </div>

      {/* Article */}
      <div style={{ position: "absolute", top: WIN.top, bottom: WIN.bar, left: 0, right: 0, overflow: "hidden" }}>
        <div style={{ width: 1000, margin: "0 auto", paddingTop: 76, fontFamily: SERIF }}>
          <div style={{ fontFamily: SANS, fontSize: 16, letterSpacing: "0.08em", color: p.muted, textTransform: "uppercase" }}>
            {ARTICLE.site} · 1 min read
          </div>
          <h1 style={{ fontSize: 76, fontWeight: 700, margin: "26px 0 18px", letterSpacing: "-0.015em", lineHeight: 1.05 }}>
            {ARTICLE.title}
          </h1>
          <div style={{ fontFamily: SANS, fontSize: 18, color: p.muted }}>{ARTICLE.byline}</div>
          <p style={{ fontSize: 30, lineHeight: 1.62, marginTop: 64 }}>
            {ARTICLE.opening.map((sentence, si) => {
              const active = si === state.sentence;
              return (
                <span key={si}>
                  <span
                    style={{
                      background: active ? p.highlight : "transparent",
                      boxShadow: active ? `0 0 0 4px ${p.highlight}` : "none",
                      borderRadius: 6,
                    }}
                  >
                    {words(sentence).map((w, wi) => (
                      <span
                        key={wi}
                        style={{
                          textDecoration: active && wi === state.word ? `underline ${p.accent}` : "none",
                          textDecorationThickness: 3,
                          textUnderlineOffset: 8,
                        }}
                      >
                        {w}
                        {wi < words(sentence).length - 1 ? " " : ""}
                      </span>
                    ))}
                  </span>
                  {si < ARTICLE.opening.length - 1 ? " " : ""}
                </span>
              );
            })}
          </p>
          <p style={{ fontSize: 30, lineHeight: 1.62, marginTop: 30 }}>{ARTICLE.second}</p>
          <h2 style={{ fontSize: 44, fontWeight: 700, margin: "70px 0 18px" }}>{ARTICLE.heading}</h2>
          <p style={{ fontSize: 30, lineHeight: 1.62 }}>{ARTICLE.third}</p>
          <blockquote style={{ borderLeft: `4px solid ${p.accent}`, margin: "40px 0", padding: "6px 0 6px 30px", fontStyle: "italic", color: p.muted, fontSize: 28 }}>
            {ARTICLE.quote}
          </blockquote>
        </div>
      </div>

      {/* Player bar */}
      <div
        style={{
          position: "absolute",
          left: 0,
          right: 0,
          bottom: 0,
          height: WIN.bar,
          display: "flex",
          alignItems: "center",
          padding: "0 36px",
          gap: 34,
          borderTop: `1px solid ${p.line}`,
          background: p.chrome,
          fontSize: 19,
        }}
      >
        <Icon d={ICON.prev} color={p.text} size={26} />
        <Icon d={state.playing ? ICON.pause : ICON.play} color={p.text} size={26} fill />
        <Icon d={ICON.next} color={p.text} size={26} />
        <span style={{ color: p.muted, width: 110 }}>{state.playing ? "Reading" : "Ready"}</span>
        <div style={{ flex: 1, position: "relative", height: 4, borderRadius: 2, background: p.line }}>
          <div style={{ position: "absolute", left: 0, top: 0, bottom: 0, width: `${state.progress * 100}%`, background: p.accent, borderRadius: 2 }} />
          <div style={{ position: "absolute", left: `${state.progress * 100}%`, top: -7, width: 18, height: 18, marginLeft: -9, borderRadius: 9, background: p.text }} />
        </div>
        <span style={{ color: p.muted, letterSpacing: "0.1em" }}>{state.position}</span>
        <span>1.0×</span>
        <span style={{ color: p.accent }}>af_heart</span>
      </div>
      {children}
    </div>
  );
};
