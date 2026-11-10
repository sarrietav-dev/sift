import "./index.css";
import { Composition } from "remotion";
import { SiftVideo } from "./SiftVideo";
import { FPS, HEIGHT, TOTAL_FRAMES, WIDTH } from "./timeline";

export const RemotionRoot: React.FC = () => (
  <Composition
    id="Sift"
    component={SiftVideo}
    durationInFrames={TOTAL_FRAMES}
    fps={FPS}
    width={WIDTH}
    height={HEIGHT}
  />
);
