export async function sendOtpSms(phone: string, code: string): Promise<void> {
  if (process.env.SMS_GATEWAY === "msg91") {
    // TODO
  } else if (process.env.SMS_GATEWAY === "fast2sms") {
    // TODO
  }
  console.log(`[TESTING-ONLY] OTP for ${phone}: ${code}`);
}

export function generateOtpCode(): string {
  return Math.floor(100000 + Math.random() * 900000).toString();
}
