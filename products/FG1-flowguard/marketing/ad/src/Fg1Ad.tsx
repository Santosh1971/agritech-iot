import React from "react";
import { AbsoluteFill, Audio, Sequence, staticFile } from "remotion";
import { Chore } from "./scenes/Chore";
import { Problems } from "./scenes/Problems";
import { Reveal } from "./scenes/Reveal";
import { Watering } from "./scenes/Watering";
import { PhoneAway } from "./scenes/PhoneAway";
import { EndCard } from "./scenes/EndCard";

// Scene boundaries in frames at 30 fps; they match the cues in music/make_music.py
// (chime at 9 s, drums + drips from 13 s, final chord at 25 s).
const SCENES = [
  { from: 0, dur: 150, C: Chore },
  { from: 150, dur: 120, C: Problems },
  { from: 270, dur: 120, C: Reveal },
  { from: 390, dur: 210, C: Watering },
  { from: 600, dur: 150, C: PhoneAway },
  { from: 750, dur: 150, C: EndCard },
];

export const Fg1Ad: React.FC = () => (
  <AbsoluteFill style={{ background: "#000" }}>
    {SCENES.map(({ from, dur, C }) => (
      <Sequence key={from} from={from} durationInFrames={dur}>
        <C dur={dur} />
      </Sequence>
    ))}
    <Audio src={staticFile("fg1-music.wav")} />
  </AbsoluteFill>
);
