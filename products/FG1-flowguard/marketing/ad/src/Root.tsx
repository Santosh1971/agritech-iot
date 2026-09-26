import React from "react";
import { Composition } from "remotion";
import { Fg1Ad } from "./Fg1Ad";

export const RemotionRoot: React.FC = () => (
  <Composition id="Fg1AdVertical" component={Fg1Ad} durationInFrames={900} fps={30} width={1080} height={1920} />
);
