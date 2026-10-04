import React from "react";
import { Composition } from "remotion";
import { WmExplainer } from "./WmExplainer";
import { VO } from "./vo";

export const RemotionRoot: React.FC = () => (
  <>
    <Composition id="WmExplainerHi" component={WmExplainer} defaultProps={{ lang: "hi" as const }} durationInFrames={VO.hi.total} fps={30} width={1080} height={1920} />
    <Composition id="WmExplainerEn" component={WmExplainer} defaultProps={{ lang: "en" as const }} durationInFrames={VO.en.total} fps={30} width={1080} height={1920} />
  </>
);
