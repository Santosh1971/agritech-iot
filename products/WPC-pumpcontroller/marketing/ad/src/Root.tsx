import React from "react";
import { Composition } from "remotion";
import { WpcAd } from "./WpcAd";

export const RemotionRoot: React.FC = () => (
  <Composition id="WpcAdVertical" component={WpcAd} durationInFrames={900} fps={30} width={1080} height={1920} />
);
