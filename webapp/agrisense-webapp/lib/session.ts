import jwt from "jsonwebtoken";

const SECRET = process.env.JWT_SECRET as string;

export type SessionPayload = {
  userId: string;
  phone: string;
  role: "ADMIN" | "DEALER" | "CUSTOMER";
};

export function signSession(payload: SessionPayload): string {
  return jwt.sign(payload, SECRET, { expiresIn: "30d" });
}

export function verifySession(token: string): SessionPayload | null {
  try {
    return jwt.verify(token, SECRET) as SessionPayload;
  } catch {
    return null;
  }
}
