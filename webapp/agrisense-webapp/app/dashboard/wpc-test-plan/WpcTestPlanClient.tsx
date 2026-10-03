"use client";

import { useEffect, useMemo, useState } from "react";
import { IBM_Plex_Sans, IBM_Plex_Mono } from "next/font/google";

const sans = IBM_Plex_Sans({ subsets: ["latin"], weight: ["400", "500", "600", "700"], variable: "--font-sans" });
const mono = IBM_Plex_Mono({ subsets: ["latin"], weight: ["400", "500", "600"], variable: "--font-mono" });

// ---------------------------------------------------------------------
// Content. Ported from the interactive checklist shared with Avinash
// (Claude artifact, Oct 2026) so the same test plan now lives on our own
// site instead of a private share link. Logic rewritten as React state;
// wording and coverage unchanged. "observe" items are worded as an
// observation to record rather than an asserted pass/fail, because that
// exact behaviour hasn't been independently confirmed yet — see item ml5.
// ---------------------------------------------------------------------
type Item = { id: string; action: string; expect: string; observe?: boolean };
type Section = { id: string; title: string; context: string; items: Item[] };

const SECTIONS: Section[] = [
  {
    id: "fw",
    title: "Firmware & app versions",
    context: "Confirm you're testing what you think you're testing before anything else.",
    items: [
      { id: "fw1", action: "Download WPC Master firmware and WPC Pump Node firmware from the Flasher dashboard.",
        expect: "Both list version 1.0.0, variant master_node / pump_node." },
      { id: "fw2", action: "Download and install the WPC app from the Apps dashboard.",
        expect: "App installs and opens to the Status tab." },
      { id: "fw3", action: "On the Master, check its firmware version (app Status screen, or console command ID).",
        expect: "Shows fw=1.0.0." },
      { id: "fw4", action: "On each Pump Node, check its firmware version (Provision screen, or console ID).",
        expect: "Shows fw=1.0.0." },
    ],
  },
  {
    id: "boot",
    title: "Power-up & Master self-test",
    context: "The Master runs a one-time LED self-test on every boot; the Pump Node doesn't — its LEDs settle straight into their normal behaviour.",
    items: [
      { id: "boot1", action: "Power on the Master.",
        expect: "All onboard LEDs (3 level LEDs, No-Power LED, LoRa LED, WiFi LED) light together for about 3 seconds, then all switch off." },
      { id: "boot2", action: "Wait a few seconds more.",
        expect: "The WiFi LED settles into a slow, steady blink (about once a second) — SoftAP is up, nothing connected to it yet." },
      { id: "boot3", action: "Power on a Pump Node.",
        expect: "No self-test sweep — it goes straight to sending join requests. Its LoRa LED should single-blink roughly every 1.2 s (each join attempt) until it joins a Master." },
    ],
  },
  {
    id: "master-local",
    title: "Master local status (Local mode)",
    context: "Phone connects directly to the Master's own WiFi — no internet needed for any of this section.",
    items: [
      { id: "ml1", action: "Join your phone to WPC-Master-XXXXXXXX (no password).",
        expect: "Connects without being asked for a password." },
      { id: "ml2", action: "Open the app → Status tab.",
        expect: "Master ID shown matches the XXXXXXXX in the WiFi name; the screen finishes loading within a couple of seconds." },
      { id: "ml3", action: "Open the Master's No-Power connector (J1) so it's open-circuit.",
        expect: "A red 'No Power detected' banner appears within about 10 seconds (debounce time)." },
      { id: "ml4", action: "Short the same connector again.",
        expect: "The banner clears within about 10 seconds." },
      { id: "ml5", action: "While IN4 is open, watch the Master's own No-Power LED on the board.", observe: true,
        expect: "Record what it does (steady on, blinking, or off) — the exact LED pattern for this input hasn't been re-checked against the app-level fix, only the app's own banner has." },
    ],
  },
  {
    id: "pair",
    title: "Pair a Pump Node",
    context: "A fresh Pump Node defaults to an old bench Master's ID — it must be pointed at this Master before it can join.",
    items: [
      { id: "pr1", action: "Join your phone to this Pump Node's own WiFi, WPC-Pump-XXXX.",
        expect: "Connects without a password." },
      { id: "pr2", action: "Open the app → Provision tab. Enter the Master's 8-character ID and tap Save.",
        expect: "A confirmation appears; the Identity card updates to show the new target Master." },
      { id: "pr3", action: "Reconnect your phone to the Master's WiFi and open the Status tab.",
        expect: "Within about 15 seconds the pump appears, listed under 'Unassigned' (online, no level yet)." },
      { id: "pr4", action: "Watch both boards' LoRa LEDs during the join.",
        expect: "Pump: 1 blink when it sends; Master: 2 blinks when it receives, 1 blink when it replies." },
    ],
  },
  {
    id: "levels",
    title: "Level-based automatic control",
    context: "A float switch closes as water rises — so a pump stays ON while its level hasn't been reached (switch open), and turns OFF once the switch closes.",
    items: [
      { id: "lv1", action: "On the Assign tab, tap this pump's chip under Level 1 to assign it.",
        expect: "Chip highlights as selected." },
      { id: "lv2", action: "Open the Level 1 jumper on the Master (simulating water below that level).",
        expect: "Within about 5–10 seconds the pump shows Running on Status, relay ON, and the Pump Node's Pump-ON LED is lit." },
      { id: "lv3", action: "Close (short) the Level 1 jumper (water has reached that level).",
        expect: "Within about 5–10 seconds the pump turns OFF — relay drops, Pump-ON LED off." },
      { id: "lv4", action: "With a second, unassigned pump present, open and close every level jumper in turn.",
        expect: "The unassigned pump's relay never activates, regardless of level state." },
    ],
  },
  {
    id: "override",
    title: "Manual override",
    context: "Lets an operator force a pump on or off without touching its level assignment.",
    items: [
      { id: "ov1", action: "On Status, toggle a pump's Manual switch on, then choose ON.",
        expect: "Relay turns ON within a few seconds, regardless of the current level state. A small hand icon appears next to the pump's name." },
      { id: "ov2", action: "Choose OFF instead.",
        expect: "Relay turns OFF within a few seconds." },
      { id: "ov3", action: "Switch back to Auto.",
        expect: "Pump reverts to whatever the level logic currently says; hand icon disappears." },
      { id: "ov4", action: "Set a pump to manual ON, then power-cycle the Master.",
        expect: "After the Master reboots, that pump is back in Auto mode — override never survives a reboot." },
    ],
  },
  {
    id: "failsafe",
    title: "Fail-safe on comm loss",
    context: "The Pump Node protects itself — it doesn't need the Master's permission to turn itself off.",
    items: [
      { id: "fs1", action: "With a pump's relay ON, disconnect that Pump Node's power (or move it well out of range) and wait at least 60 seconds.",
        expect: "The relay clicks OFF by itself (fail-safe timeout) — reconnect power/range and confirm it then rejoins on its own." },
      { id: "fs2", action: "With a different pump's relay ON, re-point it at a different Master ID (Provision tab) while it's running.",
        expect: "Its relay drops to OFF immediately — becoming unjoined forces fail-safe right away, it doesn't wait for the 60 s timeout." },
    ],
  },
  {
    id: "contacts",
    title: "Power & Water Flow indicators",
    context: "Two more contacts on the Pump Node itself, independent of the Master: IN1 = Water Flow (pump confirmed running), IN4 = Power present.",
    items: [
      { id: "ct1", action: "Open the Pump Node's own Power contact (IN4).",
        expect: "Status screen and Provision screen both show Power as absent." },
      { id: "ct2", action: "Short it again.",
        expect: "Both show Power as OK/present." },
      { id: "ct3", action: "With the pump commanded ON, open its Water Flow contact (IN1).",
        expect: "'No flow' is shown as the actionable/highlighted state, since the pump should be running." },
      { id: "ct4", action: "Short the Water Flow contact again while still ON.",
        expect: "Shows 'Flow confirmed'." },
      { id: "ct5", action: "With the pump OFF and no flow contact made, check how it's displayed.",
        expect: "Shown as neutral/dimmed, not as an alert — no flow is expected while OFF." },
      { id: "ct6", action: "On the Connection screen, turn off the Power status and Water flow switches under Dashboard display.",
        expect: "Both indicators disappear from Status and Provision screens." },
      { id: "ct7", action: "Turn them back on.",
        expect: "Both reappear with their current live values." },
    ],
  },
  {
    id: "unpair",
    title: "Unpairing a pump (both sides)",
    context: "A pump has two independent 'remember' states — the Master's table and the Pump's own saved target. Both need clearing to fully disassociate it.",
    items: [
      { id: "up1", action: "From the Status (or Assign) screen, unpair/forget a pump from the Master only.",
        expect: "It disappears from the Master's pump list immediately." },
      { id: "up2", action: "Power-cycle that Pump Node without touching its own settings.",
        expect: "It silently rejoins the Master under a new slot — this is expected: the Pump still has this Master saved." },
      { id: "up3", action: "Now forget it from the Master again, and this time also open that Pump's own Provision screen and tap 'Forget this Master'.",
        expect: "Identity card shows 'Target Master: none'; relay stays off and it does not attempt to join." },
      { id: "up4", action: "Power-cycle that Pump Node again.",
        expect: "Still shows no target Master and does not rejoin — fully disassociated." },
    ],
  },
  {
    id: "multi",
    title: "Multiple pumps on one Master",
    context: "Needs the second Pump Node paired (see the Pair-a-Pump-Node section above).",
    items: [
      { id: "mp1", action: "With 2 pumps joined, assign them to different levels and operate each independently (override, or their own level jumpers).",
        expect: "Each pump's state on Status reflects only its own command — commanding one never changes the other's relay." },
      { id: "mp2", action: "Force both pumps' desired state to ON at the same moment (e.g. open both their levels together).",
        expect: "They turn on a few seconds apart, not in the same instant — staggered to avoid simultaneous inrush." },
      { id: "mp3", action: "Watch how often each pump's status/ADC values refresh on Status with 2 pumps attached vs. with only 1.",
        expect: "Refresh is noticeably slower with 2 pumps (roughly twice as long) — this is normal, not a hang; it scales with pump count." },
    ],
  },
  {
    id: "txpower",
    title: "LoRa TX power",
    context: "Each radio's power only affects what it transmits — Master and Pump are set independently. A real range test needs open ground, not a bench check.",
    items: [
      { id: "tx1", action: "On the Assign screen, move the Master TX Power slider to a new value.",
        expect: "Saves without error; still shows the new value after leaving and returning to the screen." },
      { id: "tx2", action: "On a Pump's Provision screen, move its own TX Power slider.",
        expect: "Saves the same way, independently of the Master's setting." },
    ],
  },
  {
    id: "wifi",
    title: "WiFi scan & farm WiFi setup",
    context: "Needed before the Cloud/remote test below. Still connected to the Master's own WiFi here.",
    items: [
      { id: "wf1", action: "On the Connection screen, tap 'Scan for WiFi networks'.",
        expect: "A list appears within about 10 seconds, strongest signal first; the Master's and Pumps' own WPC-… networks are not in the list." },
      { id: "wf2", action: "Pick (or type) the real farm/office network name — ideally one with a space in it, to check that specifically — enter the password, and Save.",
        expect: "Within 30–60 seconds the farm WiFi row shows 'connected' with an IP address." },
      { id: "wf3", action: "If it does not connect, read the reason shown on that row.",
        expect: "Names why: network not found, wrong password, or lost connection — not just a bare failure." },
    ],
  },
  {
    id: "cloud",
    title: "Remote (Cloud) monitoring & control",
    context: "The one path that hasn't had a full real-world run yet. Needs the Master genuinely online from the previous section.",
    items: [
      { id: "cl1", action: "With the Master confirmed connected to real internet, switch the app to Cloud mode on the Connection screen.",
        expect: "Status screen keeps showing live data; the 'Updated Xs ago' time stays low." },
      { id: "cl2", action: "Move your phone off the Master's WiFi entirely — onto mobile data — while staying in Cloud mode.",
        expect: "Status still updates from wherever you are, not just near the Master." },
      { id: "cl3", action: "From Cloud mode (on mobile data), toggle a pump's manual override.",
        expect: "The physical relay switches within several seconds — this confirms the full phone → broker → Master → LoRa → pump path." },
      { id: "cl4", action: "Power off the Master (or disconnect its WiFi) while the app is in Cloud mode.",
        expect: "App eventually shows an orange 'Master is offline — last known state' banner." },
    ],
  },
];

const LED_REF: { group: string; rows: [string, string][] }[] = [
  {
    group: "Pump Node",
    rows: [
      ["IN1 LED (GPIO33)", "Steady ON above ~250 mV on that channel, else off. Independent of join state."],
      ["IN4 LED (GPIO13)", "Same, 250 mV threshold, independent of join state."],
      ["Pump-ON LED (GPIO16)", "Mirrors the relay's actual commanded state."],
      ["LoRa LED (GPIO4)", "1 blink = this Pump transmitted. 2 blinks = it received something addressed to it. Silently-dropped packets (wrong Master/slot/CRC) don't blink."],
      ["WiFi LED (GPIO2)", "Slow blink-pause = AP up, idle. Fast continuous = a phone is connected to its SoftAP. Slow continuous = SoftAP failed to start."],
    ],
  },
  {
    group: "Master",
    rows: [
      ["WiFi LED — internet", "Double-blink then a pause (100/100/100/1000 ms) — farm WiFi has real internet."],
      ["WiFi LED — phone on AP", "Fast continuous blink (50/50 ms) — a phone is connected to the Master's own SoftAP."],
      ["WiFi LED — idle", "Slow continuous blink (1000/1000 ms) — AP up, nothing connected, no internet."],
      ["WiFi LED — AP failed", "Continuous blink at 150/150 ms — a genuine fault starting the SoftAP."],
      ["LoRa LED — normal", "1 blink sent / 2 blinks received, same convention as the Pump."],
      ["LoRa LED — link error", "Fast continuous blink (80/80 ms) only when every known pump has failed to ACK for a full round — one dead pump alone won't trigger it."],
    ],
  },
];

const KNOWN_NON_ISSUES: string[] = [
  "IN1/IN4 ADC values are 'raw' and uncalibrated. Expect arbitrary counts, not real volts or amps — calibration isn't implemented yet.",
  "A bare ESP32 not seated in its PCB halts with 'radio.begin() failed, code -2'. That's the firmware correctly detecting no LoRa chip, not a bug — reseat it in the board.",
  "Opening a board's USB serial console resets it. Normal for these dev boards, not a firmware fault.",
  "More pumps means slower status refresh (roughly N × 5 seconds for N pumps) — not a hang.",
  "Real long-range LoRa behaviour (1–2 km) is unconfirmed — only bench-tested at short range so far.",
  "The Master's SoftAP can briefly drop during a WiFi scan or when the farm WiFi is flaky — this is a known ESP32 limitation (AP and the farm-WiFi link share one radio). Reconnect and retry.",
  "No installer PIN yet. The Provision screen is open to anyone who can see a Pump's WiFi network — a known open item, not something to report.",
  "Cloud round-trip timing is unmeasured. Local override takes about 4–5.5 s; add some margin for the phone-to-broker-to-Master hop in Cloud mode.",
];

const ALL_ITEM_IDS = SECTIONS.flatMap((s) => s.items.map((i) => i.id));
const LS_KEY = "wpcTestPlan.v1";

type StoredState = {
  checked: Record<string, boolean>;
  notes: Record<string, string>;
  fields: { tester: string; date: string; master: string; pumps: string };
};

const EMPTY_STATE: StoredState = {
  checked: {},
  notes: {},
  fields: { tester: "", date: "", master: "", pumps: "" },
};

export default function WpcTestPlanClient() {
  const [state, setState] = useState<StoredState>(EMPTY_STATE);
  const [loaded, setLoaded] = useState(false);

  // Load once on mount. Guarded so a private window / blocked storage
  // doesn't crash the page — it just renders with everything unchecked.
  useEffect(() => {
    try {
      const raw = localStorage.getItem(LS_KEY);
      if (raw) setState({ ...EMPTY_STATE, ...JSON.parse(raw) });
    } catch {
      /* storage unavailable — start fresh */
    }
    setLoaded(true);
  }, []);

  // Persist on every change, once the initial load has happened (so we
  // never overwrite saved progress with the empty initial state).
  useEffect(() => {
    if (!loaded) return;
    try {
      localStorage.setItem(LS_KEY, JSON.stringify(state));
    } catch {
      /* storage unavailable — progress just won't survive a refresh */
    }
  }, [state, loaded]);

  const doneCount = useMemo(() => ALL_ITEM_IDS.filter((id) => state.checked[id]).length, [state.checked]);
  const total = ALL_ITEM_IDS.length;
  const pct = total ? Math.round((doneCount / total) * 100) : 0;

  function toggle(id: string) {
    setState((s) => ({ ...s, checked: { ...s.checked, [id]: !s.checked[id] } }));
  }
  function setNote(sectionId: string, value: string) {
    setState((s) => ({ ...s, notes: { ...s.notes, [sectionId]: value } }));
  }
  function setField(key: keyof StoredState["fields"], value: string) {
    setState((s) => ({ ...s, fields: { ...s.fields, [key]: value } }));
  }
  function resetAll() {
    setState(EMPTY_STATE);
  }

  function scrollTo(id: string) {
    document.getElementById("sec-" + id)?.scrollIntoView({ behavior: "smooth", block: "start" });
  }

  const notesWithContent = SECTIONS.filter((s) => state.notes[s.id]?.trim());

  return (
    <div className={`${sans.variable} ${mono.variable} wpc-tp`}>
      <style>{CSS}</style>

      <div className="wrap">
        <header className="bar">
          <div className="row1">
            <div>
              <h1>WPC Field Test Plan</h1>
              <div className="sub">Master + Pump Node, firmware/app v1.0.0</div>
            </div>
            <div className="progress-wrap">
              <div className="progress-track">
                <div className="progress-fill" style={{ width: pct + "%" }} />
              </div>
              <div className="progress-num">{doneCount}/{total}</div>
            </div>
          </div>
          <nav className="jump">
            {SECTIONS.map((s) => {
              const secDone = s.items.every((i) => state.checked[i.id]);
              return (
                <button key={s.id} className={secDone ? "done" : ""} onClick={() => scrollTo(s.id)}>
                  {s.title.split(" ").slice(0, 2).join(" ")}
                </button>
              );
            })}
            <button onClick={() => scrollTo("ledref")}>LED reference</button>
            <button onClick={() => scrollTo("known")}>Known behaviour</button>
          </nav>
        </header>

        <div className="intro">
          <p>
            A hands-on pass through every WPC feature, to run with real hardware and the app.
            Check items off as you go — progress is saved on this device. Items marked
            <span className="tag obs"> observe</span> ask you to record what you see rather than
            asserting a fixed pass/fail, because that behaviour hasn&apos;t been independently
            confirmed yet.
          </p>
        </div>

        <div className="signoff">
          <div className="field">
            <label htmlFor="f-tester">Tester</label>
            <input id="f-tester" type="text" placeholder="Avinash" value={state.fields.tester} onChange={(e) => setField("tester", e.target.value)} />
          </div>
          <div className="field">
            <label htmlFor="f-date">Date</label>
            <input id="f-date" type="date" value={state.fields.date} onChange={(e) => setField("date", e.target.value)} />
          </div>
          <div className="field">
            <label htmlFor="f-master">Master unit (ID / fw)</label>
            <input id="f-master" type="text" placeholder="e.g. 47D33FB0 / 1.0.0" value={state.fields.master} onChange={(e) => setField("master", e.target.value)} />
          </div>
          <div className="field">
            <label htmlFor="f-pumps">Pump Node(s) (ID / fw)</label>
            <input id="f-pumps" type="text" placeholder="e.g. 264, 7160 / 1.0.0" value={state.fields.pumps} onChange={(e) => setField("pumps", e.target.value)} />
          </div>
        </div>

        <details className="prereq">
          <summary>What you need before starting</summary>
          <ul>
            <li>1 Master and at least 2 Pump Nodes — a second pump is needed for the multi-pump section.</li>
            <li>Normal power to each board (the +8&nbsp;V input, or a bench 5–9&nbsp;V supply on the same input).</li>
            <li>A phone with the WPC app installed.</li>
            <li>A jumper wire or small switch to open/short the Master&apos;s float-switch inputs (IN1–IN3) and its No-Power input (IN4).</li>
            <li>A way to open/short each Pump Node&apos;s own IN1 (Water Flow) and IN4 (Power) contacts.</li>
            <li>The farm/office WiFi name and password, plus your phone&apos;s mobile data as a fallback for the remote-control test.</li>
            <li>
              Firmware and the app both come from the NB Agri Flasher: <a href="/dashboard/flasher">firmware</a> and{" "}
              <a href="/dashboard/apps">the app</a>.
            </li>
          </ul>
        </details>

        {SECTIONS.map((sec, idx) => (
          <section className="testsec" id={"sec-" + sec.id} key={sec.id}>
            <div className="sechead">
              <span className="secnum">{String(idx + 1).padStart(2, "0")}</span>
              <h2>{sec.title}</h2>
            </div>
            <p className="seccontext">{sec.context}</p>

            {sec.items.map((item) => {
              const checked = !!state.checked[item.id];
              return (
                <div className={"item" + (checked ? " checked" : "")} key={item.id}>
                  <input id={"cb-" + item.id} type="checkbox" checked={checked} onChange={() => toggle(item.id)} />
                  <div>
                    <label className="action" htmlFor={"cb-" + item.id}>
                      {item.action}
                      {item.observe && <span className="tag obs">observe</span>}
                    </label>
                    <div className="expect">
                      {!item.observe && <>Expected: </>}
                      {item.expect}
                    </div>
                  </div>
                </div>
              );
            })}

            <div className="notefield">
              <label htmlFor={"note-" + sec.id}>Notes for this section</label>
              <textarea
                id={"note-" + sec.id}
                placeholder="Anything unexpected, exact readings, LED behaviour you observed…"
                value={state.notes[sec.id] ?? ""}
                onChange={(e) => setNote(sec.id, e.target.value)}
              />
            </div>
          </section>
        ))}

        <section className="testsec" id="sec-ledref">
          <div className="sechead">
            <span className="secnum">REF</span>
            <h2>LED quick reference</h2>
          </div>
          <p className="seccontext">Not a checklist — use this to sanity-check what you see against what the firmware intends.</p>
          {LED_REF.map((g) => (
            <div key={g.group}>
              <div className="ledref-group">{g.group}</div>
              <table className="ledref">
                <tbody>
                  {g.rows.map((r) => (
                    <tr key={r[0]}>
                      <td>{r[0]}</td>
                      <td>{r[1]}</td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>
          ))}
        </section>

        <section className="testsec" id="sec-known">
          <div className="sechead">
            <span className="secnum">REF</span>
            <h2>Known behaviour — don&apos;t report these as bugs</h2>
          </div>
          <p className="seccontext">Already understood and either intentional or already tracked as an open item.</p>
          <ul style={{ margin: "4px 0 0", paddingLeft: 20 }}>
            {KNOWN_NON_ISSUES.map((t, i) => (
              <li key={i} style={{ marginBlock: 6, color: "var(--ink-soft)" }}>
                {t}
              </li>
            ))}
          </ul>
        </section>

        <div className="summary">
          <h2 style={{ fontSize: 16, margin: 0 }}>Result summary</h2>
          <div className="big">
            <div className="stat pass">
              <div className="n">{doneCount}</div>
              <div className="l">Checked</div>
            </div>
            <div className="stat open">
              <div className="n">{total - doneCount}</div>
              <div className="l">Remaining</div>
            </div>
            <div className="stat">
              <div className="n">{pct}%</div>
              <div className="l">Complete</div>
            </div>
          </div>
          <div className="noteslist" style={{ marginTop: 14 }}>
            {notesWithContent.length === 0 ? (
              <div style={{ color: "var(--ink-soft)", fontSize: 13 }}>No notes recorded yet.</div>
            ) : (
              notesWithContent.map((s) => (
                <div className="notecard" key={s.id}>
                  <b>{s.title}:</b> {state.notes[s.id]}
                </div>
              ))
            )}
          </div>
        </div>

        <div className="footer-actions">
          <span className="savenote">Your checkmarks, notes and sign-off fields are saved in this browser only — not shared or sent anywhere.</span>
          <button className="btn danger" onClick={resetAll}>
            Reset for a new unit
          </button>
        </div>
      </div>
    </div>
  );
}

const CSS = `
.wpc-tp{
  --paper:#F5F3EC; --ink:#1C2321; --ink-soft:#4A524D; --line:#DAD6C8;
  --surface:#FFFFFF; --surface-2:#EEEBDF;
  --teal:#156469; --teal-soft:#DCEFEE;
  --pass:#2F8F5B; --pass-bg:#E4F3EA;
  --fail:#B5482E;
  --amber:#9A6B14; --amber-bg:#FBF0DC;
  color-scheme: light;
  background:var(--paper); color:var(--ink);
  font-family: var(--font-sans), system-ui, sans-serif;
  font-size:15px; line-height:1.5;
  min-height:100vh;
  padding-inline:16px; padding-block:0 32px;
}
@media (prefers-color-scheme: dark){
  .wpc-tp{
    --paper:#14181A; --ink:#E9ECE8; --ink-soft:#AEB6AF; --line:#2D3432;
    --surface:#1C2220; --surface-2:#222A27;
    --teal:#5FC3C2; --teal-soft:#1C3534;
    --pass:#5BC98A; --pass-bg:#173827;
    --fail:#E58A73;
    --amber:#E0B35C; --amber-bg:#3A2E15;
    color-scheme: dark;
  }
}
.wpc-tp *{box-sizing:border-box;}
.wpc-tp a{color:var(--teal);}
.wpc-tp h1,.wpc-tp h2,.wpc-tp h3{text-wrap:balance;}
.wpc-tp .mono{font-family: var(--font-mono), ui-monospace, monospace;}
.wpc-tp .wrap{max-width:760px; margin:0 auto;}

.wpc-tp header.bar{
  position:sticky; top:0; z-index:30;
  background:var(--paper); border-bottom:1px solid var(--line);
  padding-block:12px 10px; margin-inline:-16px; padding-inline:16px;
}
.wpc-tp header.bar .row1{display:flex; align-items:baseline; justify-content:space-between; gap:12px; flex-wrap:wrap;}
.wpc-tp header.bar h1{font-size:19px; font-weight:700; margin:0; letter-spacing:.2px;}
.wpc-tp header.bar .sub{font-size:12px; color:var(--ink-soft); margin-top:2px;}
.wpc-tp .progress-wrap{display:flex; align-items:center; gap:8px; min-width:160px;}
.wpc-tp .progress-track{flex:1; height:8px; border-radius:5px; background:var(--surface-2); overflow:hidden;}
.wpc-tp .progress-fill{height:100%; background:var(--teal); transition:width .25s ease;}
.wpc-tp .progress-num{font-family: var(--font-mono), monospace; font-size:12px; color:var(--ink-soft); white-space:nowrap; font-variant-numeric:tabular-nums;}

.wpc-tp nav.jump{
  display:flex; gap:6px; overflow-x:auto; padding-block:8px 2px;
  margin-inline:-16px; padding-inline:16px;
}
.wpc-tp nav.jump button{
  flex:0 0 auto; border:1px solid var(--line); background:var(--surface);
  color:var(--ink-soft); border-radius:999px; padding:5px 12px;
  font-size:12px; font-family: var(--font-mono), monospace; cursor:pointer; white-space:nowrap;
}
.wpc-tp nav.jump button.done{border-color:var(--pass); color:var(--pass);}

.wpc-tp .intro{padding-block:18px 8px;}
.wpc-tp .intro p{color:var(--ink-soft); max-width:62ch;}
.wpc-tp .signoff{
  display:grid; grid-template-columns:1fr 1fr; gap:10px;
  background:var(--surface); border:1px solid var(--line); border-radius:12px; padding:14px;
  margin-block:14px;
}
.wpc-tp .signoff .field{display:flex; flex-direction:column; gap:4px;}
.wpc-tp .signoff label{font-size:11px; text-transform:uppercase; letter-spacing:.06em; color:var(--ink-soft);}
.wpc-tp .signoff input{
  font:inherit; font-size:14px; background:var(--paper); color:var(--ink);
  border:1px solid var(--line); border-radius:7px; padding:7px 9px;
}
@media (max-width:420px){.wpc-tp .signoff{grid-template-columns:1fr;}}

.wpc-tp details.prereq{
  background:var(--teal-soft); border:1px solid var(--line); border-radius:12px;
  padding:12px 14px; margin-block:14px;
}
.wpc-tp details.prereq summary{cursor:pointer; font-weight:600; font-size:14px;}
.wpc-tp details.prereq ul{margin:10px 0 2px; padding-left:20px;}
.wpc-tp details.prereq li{margin-block:4px; color:var(--ink-soft);}

.wpc-tp section.testsec{
  background:var(--surface); border:1px solid var(--line); border-radius:14px;
  padding:16px 16px 14px; margin-block:16px; scroll-margin-top:118px;
}
.wpc-tp .sechead{display:flex; align-items:baseline; gap:10px; margin-bottom:4px;}
.wpc-tp .secnum{
  font-family: var(--font-mono), monospace; font-size:12px; font-weight:600;
  color:var(--teal); background:var(--teal-soft); border-radius:6px; padding:2px 7px;
}
.wpc-tp .sechead h2{font-size:17px; margin:0; font-weight:600;}
.wpc-tp .seccontext{color:var(--ink-soft); font-size:13px; margin:4px 0 12px; max-width:60ch;}

.wpc-tp .item{
  display:grid; grid-template-columns:22px 1fr; gap:10px;
  padding-block:10px; border-top:1px solid var(--line);
}
.wpc-tp .item:first-of-type{border-top:none;}
.wpc-tp .item input[type=checkbox]{width:19px; height:19px; margin-top:2px; accent-color:var(--pass); cursor:pointer;}
.wpc-tp .item .action{font-weight:500;}
.wpc-tp .item .expect{font-size:13px; color:var(--ink-soft); margin-top:3px;}
.wpc-tp .item.checked .action{color:var(--ink-soft); text-decoration:line-through; text-decoration-color:var(--line);}
.wpc-tp .tag{display:inline-block; font-family: var(--font-mono), monospace; font-size:10.5px; padding:1px 6px; border-radius:5px; margin-left:6px; vertical-align:1px;}
.wpc-tp .tag.obs{background:var(--amber-bg); color:var(--amber);}

.wpc-tp .notefield{margin-top:10px;}
.wpc-tp .notefield label{font-size:11px; text-transform:uppercase; letter-spacing:.06em; color:var(--ink-soft); display:block; margin-bottom:4px;}
.wpc-tp .notefield textarea{
  width:100%; font:inherit; font-size:13px; background:var(--paper); color:var(--ink);
  border:1px solid var(--line); border-radius:8px; padding:8px 10px; resize:vertical; min-height:40px;
}

.wpc-tp table.ledref{width:100%; border-collapse:collapse; font-size:13px; margin-top:6px;}
.wpc-tp table.ledref td{padding:7px 6px 7px 0; border-bottom:1px solid var(--line); vertical-align:top;}
.wpc-tp table.ledref td:first-child{font-family: var(--font-mono), monospace; font-weight:600; white-space:nowrap;}
.wpc-tp .ledref-group{font-size:12px; font-weight:600; color:var(--teal); margin:14px 0 2px;}

.wpc-tp .notecard{border-left:3px solid var(--line); padding-left:10px; font-size:13.5px; color:var(--ink-soft);}
.wpc-tp .notecard b{color:var(--ink);}

.wpc-tp .footer-actions{display:flex; justify-content:space-between; align-items:center; gap:10px; margin-block:22px 10px; flex-wrap:wrap;}
.wpc-tp .btn{font:inherit; font-size:13px; font-weight:500; border-radius:8px; padding:9px 14px; border:1px solid var(--line); background:var(--surface); color:var(--ink); cursor:pointer;}
.wpc-tp .btn.danger{color:var(--fail); border-color:var(--fail);}
.wpc-tp .savenote{font-size:12px; color:var(--ink-soft);}

.wpc-tp .summary{background:var(--surface); border:1px solid var(--line); border-radius:14px; padding:16px; margin-block:16px;}
.wpc-tp .summary .big{display:flex; gap:18px; flex-wrap:wrap; margin-top:10px;}
.wpc-tp .summary .stat{min-width:90px;}
.wpc-tp .summary .stat .n{font-family: var(--font-mono), monospace; font-size:26px; font-weight:600; font-variant-numeric:tabular-nums;}
.wpc-tp .summary .stat .l{font-size:11px; text-transform:uppercase; letter-spacing:.05em; color:var(--ink-soft);}
.wpc-tp .summary .stat.pass .n{color:var(--pass);}
.wpc-tp .summary .stat.open .n{color:var(--ink-soft);}
`;
