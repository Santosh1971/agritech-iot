"use client";
import { useState } from "react";
import { useRouter } from "next/navigation";

/** POST FormData or JSON; on success refresh the page or go to `next(res)`. */
export function useSubmit() {
  const router = useRouter();
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [warning, setWarning] = useState<string | null>(null);

  async function submit(url: string, body: FormData | object, next?: (json: Record<string, any>) => string | void) {
    setBusy(true);
    setError(null);
    setWarning(null);
    try {
      const res = await fetch(url, {
        method: "POST",
        ...(body instanceof FormData ? { body } : { body: JSON.stringify(body), headers: { "Content-Type": "application/json" } }),
      });
      const json = await res.json().catch(() => ({ error: `Server error (${res.status})` }));
      if (!res.ok) {
        setError(json.error || `Failed (${res.status})`);
        return false;
      }
      if (json.warning) setWarning(`Saved, but: ${json.warning}`);
      const to = next?.(json);
      if (to) router.push(to);
      else router.refresh();
      return true;
    } catch {
      setError("Network error: check the connection and try again");
      return false;
    } finally {
      setBusy(false);
    }
  }
  return { submit, busy, error, warning, setError };
}
