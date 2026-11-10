import { AbsoluteFill, interpolate, spring, useCurrentFrame, useVideoConfig } from "remotion";
import { ARTICLE } from "../ReaderWindow";
import { MONO, OMARCHY, SANS, SERIF } from "../theme";
import { findWord, scene, wordFrame } from "../timeline";

const S = scene("clip");
const L = S.lines[0];
const APPEAR = 0;
export const STRIP = wordFrame(L, findWord(L, "strips"));
export const MARKDOWN = wordFrame(L, findWord(L, "clean")) - 6;

const BROWSER = { w: 1180, h: 760 };

/** A noisy news page; everything but the article column is clutter. */
const Clutter = ({ k, at, children, style }: { k: number; at: number; children?: React.ReactNode; style: React.CSSProperties }) => {
  const frame = useCurrentFrame();
  const { fps } = useVideoConfig();
  const t = spring({ frame: frame - at - k * 3, fps, config: { damping: 18, stiffness: 120 } });
  const dir = k % 2 ? 1 : -1;
  return (
    <div
      style={{
        position: "absolute",
        borderRadius: 8,
        opacity: 1 - t,
        transform: `translate(${dir * t * 140}px, ${t * 60}px) rotate(${dir * t * 8}deg) scale(${1 - t * 0.2})`,
        ...style,
      }}
    >
      {children}
    </div>
  );
};

const AD = "repeating-linear-gradient(45deg, #ff3e7f22 0 14px, #ff3e7f11 14px 28px)";

export const Clip = () => {
  const frame = useCurrentFrame();
  const { fps } = useVideoConfig();
  const enter = spring({ frame: frame - APPEAR, fps, config: { damping: 20 } });
  const strip = spring({ frame: frame - STRIP - 10, fps, config: { damping: 22 } });
  const md = spring({ frame: frame - MARKDOWN, fps, config: { damping: 18, stiffness: 110 } });

  const browserX = interpolate(md, [0, 1], [(1920 - BROWSER.w) / 2, 120]);
  const lines = [
    "---",
    `title: "${ARTICLE.title}"`,
    "source: https://example.com/quiet-art",
    "position: 0",
    "---",
    "",
    `# ${ARTICLE.title}`,
    "",
    "Reading is a conversation with someone",
    "who is not in the room. The page waits,",
    "patient and exact, until you are ready",
    "to continue.",
    "",
    `## ${ARTICLE.heading}`,
  ];

  return (
    <AbsoluteFill style={{ background: OMARCHY.ground, fontFamily: SANS }}>
      {/* Browser */}
      <div
        style={{
          position: "absolute",
          left: browserX,
          top: 56,
          width: BROWSER.w,
          height: BROWSER.h,
          borderRadius: 14,
          background: "#f4f2ee",
          overflow: "hidden",
          opacity: enter,
          transform: `scale(${0.9 + enter * 0.1})`,
          boxShadow: "0 40px 120px rgba(0,0,0,0.6)",
        }}
      >
        <div style={{ height: 52, background: "#e4e1db", display: "flex", alignItems: "center", gap: 10, padding: "0 20px" }}>
          {["#ff5f57", "#febc2e", "#28c840"].map((c) => (
            <div key={c} style={{ width: 13, height: 13, borderRadius: 7, background: c }} />
          ))}
          <div style={{ marginLeft: 20, flex: 1, height: 30, borderRadius: 8, background: "#f8f7f4", color: "#666", fontSize: 16, display: "flex", alignItems: "center", padding: "0 14px" }}>
            https://example.com/quiet-art
          </div>
        </div>
        <div style={{ position: "relative", height: BROWSER.h - 52 }}>
          {/* The article survives and settles into reading type. */}
          <div
            style={{
              position: "absolute",
              left: interpolate(strip, [0, 1], [60, 170]),
              top: interpolate(strip, [0, 1], [100, 90]),
              width: interpolate(strip, [0, 1], [700, 840]),
              fontFamily: SERIF,
              color: "#1f1f1f",
            }}
          >
            <div style={{ fontSize: interpolate(strip, [0, 1], [40, 58]), fontWeight: 700, lineHeight: 1.1 }}>{ARTICLE.title}</div>
            <div style={{ fontFamily: SANS, fontSize: 16, color: "#888", marginTop: 14 }}>{ARTICLE.byline}</div>
            <p style={{ fontSize: 23, lineHeight: 1.6, marginTop: 26 }}>{ARTICLE.opening.join(" ")}</p>
            {/* Sponsored block sits in the flow and collapses once it is stripped. */}
            <div style={{ height: interpolate(strip, [0, 1], [118, 0]), position: "relative" }}>
              <Clutter k={3} at={STRIP} style={{ left: 0, right: 0, top: 4, height: 104, background: AD, border: "2px dashed #ff3e7f66", display: "grid", placeItems: "center", color: "#c2185b", fontWeight: 700, fontSize: 20, fontFamily: SANS }}>
                SPONSORED · Click here
              </Clutter>
            </div>
            <p style={{ fontSize: 23, lineHeight: 1.6, marginTop: 18 }}>{ARTICLE.second}</p>
            <div style={{ opacity: strip }}>
              <div style={{ fontSize: 34, fontWeight: 700, marginTop: 30 }}>{ARTICLE.heading}</div>
              <p style={{ fontSize: 23, lineHeight: 1.6, marginTop: 12 }}>{ARTICLE.third}</p>
            </div>
          </div>
          <Clutter k={0} at={STRIP} style={{ left: 0, right: 0, top: 0, height: 64, background: "#1d3557", borderRadius: 0, display: "flex", alignItems: "center", gap: 34, padding: "0 30px", color: "#fff", fontSize: 18, fontWeight: 700 }}>
            THE DAILY SCROLL <span style={{ fontWeight: 400, opacity: 0.8 }}>News · Opinion · Deals · Video · Podcasts</span>
          </Clutter>
          <Clutter k={1} at={STRIP} style={{ right: 30, top: 96, width: 300, height: 250, background: AD, border: "2px dashed #ff3e7f66", display: "grid", placeItems: "center", color: "#c2185b", fontWeight: 700, fontSize: 22 }}>
            AD · 50% OFF
          </Clutter>
          <Clutter k={2} at={STRIP} style={{ right: 30, top: 370, width: 300, height: 290, background: "#e9e6e0", padding: 20, fontSize: 17, color: "#555" }}>
            <b>Trending now</b>
            {["You won't believe #4", "10 tabs you need", "Why we doomscroll", "Sponsored story"].map((t) => (
              <div key={t} style={{ marginTop: 16 }}>• {t}</div>
            ))}
          </Clutter>
          <Clutter k={4} at={STRIP} style={{ left: 0, right: 0, bottom: 0, height: 90, background: "#222", borderRadius: 0, color: "#eee", display: "flex", alignItems: "center", padding: "0 30px", gap: 30, fontSize: 18 }}>
            We use cookies to improve your experience.
            <span style={{ background: "#ffd400", color: "#111", padding: "8px 18px", borderRadius: 6, fontWeight: 700 }}>Accept all</span>
          </Clutter>
          <div style={{ position: "absolute", inset: 0, background: "rgba(20,20,30,0.45)", opacity: 1 - spring({ frame: frame - STRIP - 15, fps, config: { damping: 18 } }) }} />
          <Clutter k={5} at={STRIP} style={{ left: 330, top: 190, width: 480, height: 230, boxSizing: "border-box", background: "#fff", boxShadow: "0 20px 60px rgba(0,0,0,0.35)", padding: 28, color: "#222" }}>
            <div style={{ fontSize: 26, fontWeight: 700 }}>Don't miss a thing!</div>
            <div style={{ marginTop: 12, fontSize: 17, color: "#555" }}>Subscribe to our newsletter for daily updates.</div>
            <div style={{ marginTop: 26, height: 46, borderRadius: 6, background: "#1d3557" }} />
          </Clutter>

        </div>
      </div>

      {/* Markdown copy */}
      <div
        style={{
          position: "absolute",
          left: 1340,
          top: 170,
          width: 520,
          borderRadius: 14,
          background: "#161616",
          boxShadow: `0 30px 90px rgba(0,0,0,0.6), 0 0 0 1px ${OMARCHY.line}`,
          opacity: md,
          transform: `translateX(${(1 - md) * 260}px) rotate(${(1 - md) * 4}deg)`,
          overflow: "hidden",
        }}
      >
        <div style={{ padding: "16px 24px", borderBottom: `1px solid ${OMARCHY.line}`, color: OMARCHY.muted, fontSize: 17, fontFamily: MONO }}>
          ~/Documents/sift/<span style={{ color: OMARCHY.accent }}>quiet-art-of-reading.md</span>
        </div>
        <pre style={{ margin: 0, padding: "20px 24px", fontFamily: MONO, fontSize: 17, lineHeight: 1.65, color: OMARCHY.text, whiteSpace: "pre-wrap" }}>
          {lines.map((l, i) => {
            const shown = interpolate(frame, [MARKDOWN + 6 + i * 2, MARKDOWN + 12 + i * 2], [0, 1], { extrapolateLeft: "clamp", extrapolateRight: "clamp" });
            const color = l.startsWith("#") ? OMARCHY.accent : l === "---" || l.includes(": ") ? OMARCHY.muted : OMARCHY.text;
            return (
              <div key={i} style={{ opacity: shown, color, fontWeight: l.startsWith("#") ? 500 : 400 }}>
                {l || " "}
              </div>
            );
          })}
        </pre>
      </div>
    </AbsoluteFill>
  );
};
