import React from "react";
import { AbsoluteFill, Audio, Sequence, staticFile } from "remotion";
import { NightTrip } from "./scenes/NightTrip";
import { Problems } from "./scenes/Problems";
import { Reveal } from "./scenes/Reveal";
import { FarmAuto } from "./scenes/FarmAuto";
import { SleepPhone } from "./scenes/SleepPhone";
import { EndCard } from "./scenes/EndCard";

// Scene boundaries in frames at 30 fps; they match the cues in music/make_music.py
// (impact at 9 s, drums from 13 s, final chord at 25 s).
const SCENES = [
  { from: 0, dur: 150, C: NightTrip },
  { from: 150, dur: 120, C: Problems },
  { from: 270, dur: 120, C: Reveal },
  { from: 390, dur: 210, C: FarmAuto },
  { from: 600, dur: 150, C: SleepPhone },
  { from: 750, dur: 150, C: EndCard },
];

export const WpcAd: React.FC = () => (
  <AbsoluteFill style={{ background: "#000" }}>
    {SCENES.map(({ from, dur, C }) => (
      <Sequence key={from} from={from} durationInFrames={dur}>
        <C dur={dur} />
      </Sequence>
    ))}
    <Audio src={staticFile("wpc-music.wav")} />
  </AbsoluteFill>
);
