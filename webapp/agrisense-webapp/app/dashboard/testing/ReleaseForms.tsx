"use client";
import { useState } from "react";
import { useSubmit } from "./useSubmit";

export function NewReleaseForm({ productId, productName }: { productId: string; productName: string }) {
  const { submit, busy, error } = useSubmit();
  const [open, setOpen] = useState(false);
  if (!open) return <div><button className="btn small ghost" onClick={() => setOpen(true)}>Open a release</button></div>;
  return (
    <form
      onSubmit={async (e) => {
        e.preventDefault();
        const fd = Object.fromEntries(new FormData(e.currentTarget));
        if (await submit("/api/testing/releases", { ...fd, productId })) setOpen(false);
      }}
    >
      <label>Name<input name="name" required placeholder={`${productName} fw 1.5.0 + app 2.4.0`} /></label>
      <div className="fields">
        <label>Firmware<input name="fwVersion" placeholder="1.5.0" /></label>
        <label>App<input name="appVersion" placeholder="2.4.0" /></label>
        <label>Board<input name="hwRev" placeholder="Rev C" /></label>
      </div>
      <label>Goal <span className="hint">optional</span><input name="notes" /></label>
      {error && <div className="error">{error}</div>}
      <div className="row">
        <button className="btn small" disabled={busy}>Open release</button>
        <button type="button" className="btn small ghost" onClick={() => setOpen(false)}>Cancel</button>
      </div>
    </form>
  );
}

export function SignOff({ releaseId, ready, signedOff }: { releaseId: string; ready: boolean; signedOff: boolean }) {
  const { submit, busy, error } = useSubmit();
  const [text, setText] = useState("");
  const send = (action: string) => {
    const fd = new FormData();
    fd.set("action", action);
    fd.set("text", text);
    return submit(`/api/testing/releases/${releaseId}/action`, fd);
  };
  return (
    <div className="grid" style={{ gap: 8 }}>
      <label>
        {signedOff ? "Reason to withdraw" : "Note"} <span className="hint">{signedOff ? "required" : "optional"}</span>
        <input value={text} onChange={(e) => setText(e.target.value)} />
      </label>
      <div className="row">
        {signedOff ? (
          <button className="btn bad" disabled={busy || !text.trim()} onClick={() => send("withdraw_signoff")}>Withdraw sign-off</button>
        ) : (
          <button className="btn" disabled={busy || !ready} onClick={() => send("signoff")}>Sign off this release</button>
        )}
      </div>
      {!ready && !signedOff && <p className="small muted">Sign-off opens once every gate check above is green.</p>}
      {error && <div className="error">{error}</div>}
    </div>
  );
}

export function Publish({ releaseId, hasFirmware, hasApp, hasHardware }: { releaseId: string; hasFirmware: boolean; hasApp: boolean; hasHardware: boolean }) {
  const { submit, busy, error } = useSubmit();
  return (
    <form
      onSubmit={(e) => {
        e.preventDefault();
        const fd = new FormData(e.currentTarget);
        fd.set("action", "publish");
        submit(`/api/testing/releases/${releaseId}/action`, fd);
      }}
    >
      <p className="small muted">Attach the final files. The APK must be the release-signed one (CI only builds debug).</p>
      <div className="fields">
        {hasFirmware && <label>Final firmware .bin<input type="file" name="firmware" accept=".bin" /></label>}
        {hasApp && <label>Release-signed .apk<input type="file" name="apk" accept=".apk" /></label>}
        {hasHardware && <label>Gerber .zip<input type="file" name="gerber" accept=".zip,.rar,.7z" /></label>}
      </div>
      <label>Note <span className="hint">optional</span><input name="text" /></label>
      {error && <div className="error">{error}</div>}
      <div className="row"><button className="btn" disabled={busy}>{busy ? "Publishing…" : "Publish release"}</button></div>
    </form>
  );
}

export function CopyBox({ text }: { text: string }) {
  const [done, setDone] = useState(false);
  return (
    <div className="grid" style={{ gap: 6 }}>
      <pre className="prose mono" style={{ background: "var(--panel-2)", padding: 12, borderRadius: 9, margin: 0 }}>{text}</pre>
      <div>
        <button className="btn small ghost" onClick={() => navigator.clipboard.writeText(text).then(() => setDone(true))}>{done ? "Copied" : "Copy changelog"}</button>
      </div>
    </div>
  );
}
