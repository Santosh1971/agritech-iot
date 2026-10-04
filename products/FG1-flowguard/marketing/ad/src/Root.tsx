import React from "react";
import { Composition } from "remotion";
import { Fg1Ad } from "./Fg1Ad";
import { Fg1Explainer } from "./Fg1Explainer";
import { VO } from "./vo";

export const RemotionRoot: React.FC = () => (
  <>
    <Composition id="Fg1AdVertical" component={Fg1Ad} durationInFrames={900} fps={30} width={1080} height={1920} />
    <Composition id="Fg1ExplainerHi" component={Fg1Explainer} defaultProps={{ lang: "hi" as const }} durationInFrames={VO.hi.total} fps={30} width={1080} height={1920} />
    <Composition id="Fg1ExplainerEn" component={Fg1Explainer} defaultProps={{ lang: "en" as const }} durationInFrames={VO.en.total} fps={30} width={1080} height={1920} />
  </>
);
