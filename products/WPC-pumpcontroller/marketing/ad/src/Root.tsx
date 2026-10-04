import React from "react";
import { Composition } from "remotion";
import { WpcAd } from "./WpcAd";
import { WpcExplainer } from "./WpcExplainer";
import { VO } from "./vo";

export const RemotionRoot: React.FC = () => (
  <>
    <Composition id="WpcAdVertical" component={WpcAd} durationInFrames={900} fps={30} width={1080} height={1920} />
    <Composition id="WpcExplainerHi" component={WpcExplainer} defaultProps={{ lang: "hi" as const }} durationInFrames={VO.hi.total} fps={30} width={1080} height={1920} />
    <Composition id="WpcExplainerEn" component={WpcExplainer} defaultProps={{ lang: "en" as const }} durationInFrames={VO.en.total} fps={30} width={1080} height={1920} />
  </>
);
