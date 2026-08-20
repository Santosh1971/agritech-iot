"use client";

import { useState } from "react";
import { useRouter } from "next/navigation";

export default function LoginPage() {
  const [phone, setPhone] = useState("");
  const [code, setCode] = useState("");
  const [step, setStep] = useState<"phone" | "otp">("phone");
  const [error, setError] = useState("");
  const router = useRouter();

  async function requestOtp() {
    setError("");
    const res = await fetch("/api/auth/request-otp", {
      method: "POST",
      body: JSON.stringify({ phone }),
    });
    if (res.ok) setStep("otp");
    else setError("Could not send code. Check the number and try again.");
  }

  async function verifyOtp() {
    setError("");
    const res = await fetch("/api/auth/verify-otp", {
      method: "POST",
      body: JSON.stringify({ phone, code }),
    });
    if (res.ok) router.push("/dashboard");
    else {
      const data = await res.json();
      setError(data.error || "Invalid code.");
    }
  }

  return (
    <main style={{ maxWidth: 360, margin: "80px auto", fontFamily: "sans-serif" }}>
      <h1>Agri Sense and Control</h1>

      {step === "phone" && (
        <>
          <input
            placeholder="Phone number"
            value={phone}
            onChange={(e) => setPhone(e.target.value)}
            style={{ width: "100%", padding: 8, marginBottom: 8 }}
          />
          <button onClick={requestOtp} style={{ width: "100%", padding: 8 }}>
            Send code
          </button>
        </>
      )}

      {step === "otp" && (
        <>
          <input
            placeholder="6-digit code"
            value={code}
            onChange={(e) => setCode(e.target.value)}
            style={{ width: "100%", padding: 8, marginBottom: 8 }}
          />
          <button onClick={verifyOtp} style={{ width: "100%", padding: 8 }}>
            Verify & log in
          </button>
        </>
      )}

      {error && <p style={{ color: "red" }}>{error}</p>}
    </main>
  );
}
