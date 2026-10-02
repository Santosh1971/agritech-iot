"use client";

import { useState } from "react";

const roles = ["Farmer or grower", "Dealer", "College or university", "Partner or investor", "Other"];

export default function ContactForm({ defaultRole, defaultMessage }: { defaultRole: string; defaultMessage: string }) {
  const [state, setState] = useState<"idle" | "sending" | "sent" | "error">("idle");
  const [error, setError] = useState("");

  async function submit(e: React.FormEvent<HTMLFormElement>) {
    e.preventDefault();
    setState("sending");
    const data = Object.fromEntries(new FormData(e.currentTarget));
    const res = await fetch("/api/contact", { method: "POST", body: JSON.stringify(data) }).catch(() => null);
    if (res?.ok) {
      setState("sent");
      return;
    }
    const body = await res?.json().catch(() => null);
    setError(body?.error || "Could not send. Please try again.");
    setState("error");
  }

  if (state === "sent") {
    return <p className="form-note ok">Thank you. Your message has reached us and we will get back to you.</p>;
  }

  return (
    <form onSubmit={submit}>
      <label>Your name<input name="name" required maxLength={100} autoComplete="name" /></label>
      <label>Phone or WhatsApp<input name="phone" required maxLength={30} inputMode="tel" autoComplete="tel" /></label>
      <label>Email (optional)<input name="email" type="email" maxLength={120} autoComplete="email" /></label>
      <label>
        I am a
        <select name="role" defaultValue={defaultRole} required>
          <option value="" disabled>Choose one</option>
          {roles.map((r) => <option key={r}>{r}</option>)}
        </select>
      </label>
      <label>Town and state<input name="place" maxLength={100} /></label>
      <label>Message<textarea name="message" rows={5} required maxLength={2000} defaultValue={defaultMessage} /></label>
      <button className="btn btn-primary" disabled={state === "sending"}>{state === "sending" ? "Sending…" : "Send message"}</button>
      {state === "error" && <p className="form-note err">{error}</p>}
    </form>
  );
}
