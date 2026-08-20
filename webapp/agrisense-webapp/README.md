# AgriSense and Control — Web App

Customer + admin dashboard for the AgriTech IoT product line (FG1, FM1, WM1, WPC, TH), plus the
Mosquitto broker they all talk to. Domain: agrisenseandcontrol.in. Hosted on a dedicated GigaNodes
VPS, separate from the AlgoMomentum Bridge.

## Stack (mirrors AlgoMomentum Bridge, proven pattern)

- Next.js 15
- Prisma + PostgreSQL (local on VPS)
- Mosquitto (auth-enabled, per-device credentials — see docs/mqtt-topics.md)
- PM2 + Nginx + Certbot

## Structure so far

- `prisma/schema.prisma` — User (Admin/Dealer/Customer roles), Device (per-product), Reading
  (generic JSON telemetry), Command (with ack tracking)
- `docs/mqtt-topics.md` — unified topic convention for all products going forward

## Access model (from WM1-Mini spec, generalized to all products)

- **Admin** (Santosh, Avinash): sees/controls every device across every customer
- **Dealer** (e.g. Kamta): sees/controls only devices assigned to them — full control, not just view,
  since dealers support their own customers directly
- **Customer** (e.g. Girish, Vinay): sees/controls only their own devices

One app, role-based — no separate admin app needed (same call WM1-Mini's spec already made).

## Decisions locked in

- **Auth: phone number + OTP**, not email/password. Fits the farmer/dealer customer base better than
  Bridge's email/password NextAuth setup — no password to remember, matches how they already use
  WhatsApp/UPI. SMS OTP via an India-focused gateway (MSG91 or Fast2SMS, TBD which). `OtpCode` model
  added to the schema; `User.phone` is now the unique login identifier, email is optional/notification-only.
- **All AgriTech products migrate** to the new broker + unified topic scheme (FG1, FM1, WM1, WPC, TH) —
  Girish may be an exception if he goes ahead with his own software instead.
- **MQTT-to-Postgres bridge is a separate standalone Node process**, PM2-managed, independent from the
  Next.js app — subscribes to `agrisense/#`, writes Reading/Device.lastStatus rows directly via Prisma.
  Keeps telemetry ingestion running uninterrupted during web app deploys/restarts.

## What's scaffolded now

- `app/` — Next.js App Router: `/login` (phone entry → OTP entry), `/dashboard` (role-scoped device
  list, server-rendered), `middleware.ts` protecting all routes except login/auth API
- `app/api/auth/request-otp`, `app/api/auth/verify-otp` — OTP flow, session issued as an httpOnly
  JWT cookie (`lib/session.ts`)
- `lib/sms.ts` — stub for the SMS gateway call; has a dev-mode console.log fallback so you can test
  the whole login flow locally before a real gateway (MSG91/Fast2SMS) is wired in
- `bridge/` — standalone MQTT-to-Postgres service (see decisions above), separate `package.json` so
  it runs as its own PM2 process independent of the Next.js app
- Note: **users are provisioned by Admin, not self-signup** — matches the WM1-Mini access model
  where Admin assigns devices to dealers/customers. A phone number that passes OTP but has no
  matching `User` row gets a clear "not registered, contact your dealer" message rather than an
  account being silently created.

## Running locally (before the VPS is reachable)

```bash
cd webapp/agrisense-webapp
npm install
cp .env.example .env   # point DATABASE_URL at a local Postgres, or use a free Neon/Supabase dev DB
npx prisma generate
npx prisma migrate dev --name init
npm run dev
```

OTP codes print to the terminal in dev mode (see `lib/sms.ts`) — no SMS gateway needed to test the
login flow end-to-end. You'll need at least one `User` row in the DB to actually log in past OTP
(Prisma Studio — `npx prisma studio` — is the quickest way to add yourself as an ADMIN for testing).

## Still open

- Which SMS OTP gateway (MSG91 vs Fast2SMS) — pick once we're setting up env vars on the VPS
- Whether TH Monitor and FG1's existing MQTT topics migrate immediately or at their next firmware update
