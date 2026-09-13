import { Resend } from "resend";

export async function sendOtpEmail(email: string, code: string): Promise<void> {
  if (!process.env.RESEND_API_KEY) {
    console.log(`[TESTING-ONLY] OTP for ${email}: ${code}`);
    return;
  }

  const resend = new Resend(process.env.RESEND_API_KEY);
  const from = process.env.EMAIL_FROM || "NB Agri Flasher <onboarding@resend.dev>";

  await resend.emails.send({
    from,
    to: email,
    subject: "Your Agri Sense and Control login code",
    html: `
      <div style="font-family: sans-serif; padding: 20px;">
        <h2>Your login code</h2>
        <p style="font-size: 28px; letter-spacing: 4px; font-weight: bold;">${code}</p>
        <p>This code expires in 5 minutes.</p>
      </div>
    `,
  });
}

export function generateOtpCode(): string {
  return Math.floor(100000 + Math.random() * 900000).toString();
}
