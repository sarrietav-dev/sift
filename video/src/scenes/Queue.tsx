import { AbsoluteFill, interpolate, spring, useCurrentFrame, useVideoConfig } from "remotion";
import { ReaderWindow, WIN } from "../ReaderWindow";
import { MONO, OMARCHY, SANS } from "../theme";
import { findWord, scene, wordFrame } from "../timeline";

const S = scene("queue");
const L = S.lines[0];
export const ADD = wordFrame(L, findWord(L, "Line"));
export const DRAG = wordFrame(L, findWord(L, "queue")) - 4;
export const ADVANCE = wordFrame(L, findWord(L, "advances"));
export const REOPEN = wordFrame(L, findWord(L, "reopens"));

const ITEMS = [
  { id: "a", title: "The Quiet Art of Reading", site: "example.com" },
  { id: "b", title: "Second Thoughts", site: "example.com" },
  { id: "c", title: "On Slow Software", site: "notes.dev" },
  { id: "d", title: "Notes on Attention", site: "essays.org" },
  { id: "e", title: "Why We Reread", site: "longform.net" },
];

const ITEM_H = 92;
const PANEL_W = 520;

/** Queue order at each beat; the new page lands last, then is dragged up. */
const ORDERS: [number, string[]][] = [
  [0, ["a", "b", "c", "d", "e"]],
  [DRAG, ["a", "b", "e", "c", "d"]],
];

export const Queue = () => {
  const frame = useCurrentFrame();
  const { fps } = useVideoConfig();
  const open = spring({ frame: frame - 4, fps, config: { damping: 22 } });
  const added = spring({ frame: frame - ADD, fps, config: { damping: 14 } });
  const moved = spring({ frame: frame - DRAG, fps, config: { damping: 16, stiffness: 90 } });
  const advanced = spring({ frame: frame - ADVANCE, fps, config: { damping: 20 } });
  const toast = spring({ frame: frame - REOPEN, fps, config: { damping: 18 } });

  const slot = (id: string) => {
    const from = ORDERS[0][1].indexOf(id);
    const to = ORDERS[1][1].indexOf(id);
    return from + (to - from) * moved;
  };
  const dragging = frame >= DRAG - 4 && moved < 0.98;
  const playingIndex = advanced < 0.5 ? 0 : 1;

  return (
    <AbsoluteFill style={{ background: OMARCHY.ground }}>
      <ReaderWindow state={{ sentence: -1, word: -1, playing: true, progress: interpolate(advanced, [0, 1], [0.97, 0.04]), position: advanced < 0.5 ? "9 / 9" : "1 / 14" }}>
        {/* Dim the article behind the queue overlay, as the narrow layout does. */}
        <div style={{ position: "absolute", left: 0, right: 0, top: WIN.top, bottom: WIN.bar, background: `rgba(0,0,0,${0.45 * open})` }} />
        <div
          style={{
            position: "absolute",
            left: 0,
            top: WIN.top,
            bottom: WIN.bar,
            width: PANEL_W,
            background: OMARCHY.chrome,
            borderRight: `1px solid ${OMARCHY.line}`,
            transform: `translateX(${(open - 1) * PANEL_W}px)`,
            fontFamily: SANS,
          }}
        >
          <div style={{ height: 96, display: "flex", alignItems: "center", padding: "0 30px", borderBottom: `1px solid ${OMARCHY.line}`, fontSize: 28, fontWeight: 500 }}>
            Queue <span style={{ color: OMARCHY.muted, fontSize: 18, marginLeft: 10 }}>{added > 0.5 ? 5 : 4}</span>
          </div>
          <div style={{ position: "relative", margin: "14px 14px 0" }}>
            {ITEMS.map((item, i) => {
              const isNew = item.id === "e";
              const y = slot(item.id) * ITEM_H;
              const done = item.id === "a" && advanced > 0.5;
              const current = ITEMS.findIndex((x) => x.id === ["a", "b"][playingIndex]) === i;
              const lift = isNew && dragging ? 1 : 0;
              return (
                <div
                  key={item.id}
                  style={{
                    position: "absolute",
                    left: 0,
                    right: 0,
                    top: y,
                    height: ITEM_H - 8,
                    borderRadius: 8,
                    display: "flex",
                    alignItems: "center",
                    gap: 18,
                    padding: "0 18px",
                    background: current ? "#262626" : lift ? "#2a2a2a" : "transparent",
                    boxShadow: lift ? "0 18px 40px rgba(0,0,0,0.5)" : "none",
                    transform: `scale(${1 + lift * 0.03}) translateY(${isNew ? (1 - added) * 50 : 0}px)`,
                    opacity: isNew ? added : done ? 0.45 : 1,
                    zIndex: isNew ? 2 : 1,
                  }}
                >
                  <div style={{ position: "absolute", left: 0, top: 18, bottom: 18, width: 4, borderRadius: 2, background: OMARCHY.accent, opacity: current ? 1 : 0 }} />
                  <span style={{ color: OMARCHY.muted, fontSize: 18, width: 18, textAlign: "center" }}>
                    {done ? "✓" : lift ? "⠿" : Math.round(slot(item.id)) + 1}
                  </span>
                  <div>
                    <div style={{ fontSize: 23, color: OMARCHY.text }}>{item.title}</div>
                    <div style={{ fontSize: 17, color: OMARCHY.muted, marginTop: 4 }}>{item.site}</div>
                  </div>
                </div>
              );
            })}
          </div>
          <div style={{ position: "absolute", bottom: 0, left: 0, right: 0, height: 52, borderTop: `1px solid ${OMARCHY.line}`, display: "flex", alignItems: "center", justifyContent: "center", gap: 18, color: OMARCHY.muted, fontFamily: MONO, fontSize: 15 }}>
            J/K select · Shift+J/K move · Drag to reorder
          </div>
        </div>
        {/* Resume toast */}
        <div
          style={{
            position: "absolute",
            right: 50,
            bottom: WIN.bar + 40,
            opacity: toast,
            transform: `translateY(${(1 - toast) * 30}px)`,
            background: "#262626",
            border: `1px solid ${OMARCHY.line}`,
            borderRadius: 12,
            padding: "20px 28px",
            fontFamily: SANS,
            fontSize: 22,
            color: OMARCHY.text,
            boxShadow: "0 20px 50px rgba(0,0,0,0.5)",
          }}
        >
          Reopened <b>Second Thoughts</b>
          <div style={{ color: OMARCHY.muted, fontSize: 18, marginTop: 6 }}>
            at sentence <span style={{ color: OMARCHY.accent }}>14 of 32</span>, where you left off
          </div>
        </div>
      </ReaderWindow>
    </AbsoluteFill>
  );
};
