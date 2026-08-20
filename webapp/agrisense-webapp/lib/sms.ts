// Stub — wire up the real gateway (MSG91 or Fast2SMS) once decided.
// Both offer a simple REST API: POST phone + message, get a delivery ID back.
// Keeping this as one function means swapping providers later only touches this file.

export async function sendOtpSms(phone: string, code: string): Promise<void> {
  if (process.env.SMS_GATEWAY === "msg91") {
    // TODO: call MSG91's OTP API with process.env.SMS_API_KEY
    // https://docs.msg91.com/otp
  } else if (process.env.SMS_GATEWAY === "fast2sms") {
    // TODO: call Fast2SMS's OTP API with process.env.SMS_API_KEY
  }

  // Dev-mode fallback so you can test the flow before a gateway is wired up.
  if (process.env.NODE_ENV !== "production") {
    console.log(`[DEV] OTP for ${phone}: ${code}`);
    return;
  }

  throw new Error("SMS gateway not configured — set SMS_GATEWAY and SMS_API_KEY");
}

export function generateOtpCode(): string {
  return Math.floor(100000 + Math.random() * 900000).toString(); // 6 digits
}
