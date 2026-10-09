"use client";
// "Engineer's view": the switch that reveals the real material behind each
// screen (requirement IDs, pins, design files). Remembered per browser.
import { createContext, useContext, useEffect, useState } from "react";

const Ctx = createContext<{ eng: boolean; setEng: (v: boolean) => void }>({ eng: false, setEng: () => {} });

export function EngProvider({ children }: { children: React.ReactNode }) {
  const [eng, setEngState] = useState(false);
  useEffect(() => {
    try { setEngState(localStorage.getItem("studio-eng") === "1"); } catch {}
  }, []);
  const setEng = (v: boolean) => {
    setEngState(v);
    try { localStorage.setItem("studio-eng", v ? "1" : "0"); } catch {}
  };
  return <Ctx.Provider value={{ eng, setEng }}>{children}</Ctx.Provider>;
}

export function useEng() {
  return useContext(Ctx).eng;
}

export function EngToggle() {
  const { eng, setEng } = useContext(Ctx);
  return (
    <button type="button" className="toggle" aria-pressed={eng} onClick={() => setEng(!eng)}>
      <span className="knob" />
      Engineer&apos;s view
    </button>
  );
}

export function Eng({ title, children }: { title: string; children: React.ReactNode }) {
  if (!useEng()) return null;
  return (
    <div className="eng">
      <div className="eng-h">⚙ Engineer&apos;s view · {title}</div>
      {children}
    </div>
  );
}

export function Why({ title, children }: { title: string; children: React.ReactNode }) {
  return (
    <div className="why">
      <b>Why? {title}</b>
      <p>{children}</p>
    </div>
  );
}
