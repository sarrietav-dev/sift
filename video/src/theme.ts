import { loadFont as loadSerif } from "@remotion/google-fonts/IBMPlexSerif";
import { loadFont as loadSans } from "@remotion/google-fonts/NotoSans";
import { loadFont as loadMono } from "@remotion/google-fonts/JetBrainsMono";

export const SERIF = loadSerif("normal", {
  weights: ["400", "600", "700"],
  subsets: ["latin"],
}).fontFamily;
export const SANS = loadSans("normal", {
  weights: ["400", "500", "600", "700"],
  subsets: ["latin"],
}).fontFamily;
export const MONO = loadMono("normal", {
  weights: ["400", "500"],
  subsets: ["latin"],
}).fontFamily;

/** The surfaces sift paints, taken from the app's dark Omarchy screenshot. */
export interface Palette {
  name: string;
  ground: string; // behind the window
  window: string;
  chrome: string; // top bar, player bar, sidebar
  line: string;
  text: string;
  muted: string;
  accent: string;
  highlight: string; // sentence highlight fill
}

export const OMARCHY: Palette = {
  name: "Omarchy",
  ground: "#0d0d0d",
  window: "#1b1b1b",
  chrome: "#1b1b1b",
  line: "#2c2c2c",
  text: "#ececec",
  muted: "#8a8a8a",
  accent: "#ffd400",
  highlight: "rgba(255, 212, 0, 0.22)",
};

export const TOKYO: Palette = {
  name: "Tokyo Night",
  ground: "#0f1017",
  window: "#1a1b26",
  chrome: "#16161e",
  line: "#2a2e42",
  text: "#c0caf5",
  muted: "#7a82a8",
  accent: "#7aa2f7",
  highlight: "rgba(122, 162, 247, 0.24)",
};

export const LIGHT: Palette = {
  name: "Light",
  ground: "#d9d6cf",
  window: "#faf9f6",
  chrome: "#f1efea",
  line: "#e2dfd8",
  text: "#1f1f1f",
  muted: "#77736b",
  accent: "#c07a00",
  highlight: "rgba(255, 196, 0, 0.30)",
};

export const GRUVBOX: Palette = {
  name: "Gruvbox",
  ground: "#141414",
  window: "#282828",
  chrome: "#232323",
  line: "#3c3836",
  text: "#ebdbb2",
  muted: "#a89984",
  accent: "#fe8019",
  highlight: "rgba(254, 128, 25, 0.22)",
};

const mixChannel = (a: number, b: number, t: number) => Math.round(a + (b - a) * t);

const parse = (c: string): [number, number, number, number] => {
  if (c.startsWith("#")) {
    const n = parseInt(c.slice(1), 16);
    return [(n >> 16) & 255, (n >> 8) & 255, n & 255, 1];
  }
  const m = c.match(/[\d.]+/g)!.map(Number);
  return [m[0], m[1], m[2], m[3] ?? 1];
};

export const mix = (a: string, b: string, t: number) => {
  const x = parse(a);
  const y = parse(b);
  const alpha = x[3] + (y[3] - x[3]) * t;
  return `rgba(${mixChannel(x[0], y[0], t)}, ${mixChannel(x[1], y[1], t)}, ${mixChannel(x[2], y[2], t)}, ${alpha.toFixed(3)})`;
};

export const mixPalette = (a: Palette, b: Palette, t: number): Palette => ({
  name: t < 0.5 ? a.name : b.name,
  ground: mix(a.ground, b.ground, t),
  window: mix(a.window, b.window, t),
  chrome: mix(a.chrome, b.chrome, t),
  line: mix(a.line, b.line, t),
  text: mix(a.text, b.text, t),
  muted: mix(a.muted, b.muted, t),
  accent: mix(a.accent, b.accent, t),
  highlight: mix(a.highlight, b.highlight, t),
});
