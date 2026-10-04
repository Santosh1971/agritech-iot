import React from "react";
import { Composition } from "remotion";
import { PaddyExplainer, WmProExplainer } from "./Explainers";
import { VO as VO_PADDY } from "./vo_paddy";
import { VO as VO_WMPRO } from "./vo_wmpro";

const V = { fps: 30, width: 1080, height: 1920 };
export const RemotionRoot: React.FC = () => (
  <>
    <Composition id="PaddyHi" component={PaddyExplainer} defaultProps={{ lang: "hi" as const }} durationInFrames={VO_PADDY.hi.total} {...V} />
    <Composition id="PaddyEn" component={PaddyExplainer} defaultProps={{ lang: "en" as const }} durationInFrames={VO_PADDY.en.total} {...V} />
    <Composition id="WmProHi" component={WmProExplainer} defaultProps={{ lang: "hi" as const }} durationInFrames={VO_WMPRO.hi.total} {...V} />
    <Composition id="WmProEn" component={WmProExplainer} defaultProps={{ lang: "en" as const }} durationInFrames={VO_WMPRO.en.total} {...V} />
  </>
);
