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

- **Auth: email + OTP**, not phone/SMS or email/password. Started as phone+SMS, but switched since
  SMS gateways (MSG91/Fast2SMS) cost per message while email (Resend, free tier) doesn't — no
  password to remember either way. `OtpCode` model keyed by email; `User.email` is the unique login
  identifier, phone is now optional/contact-only (still used for `FlasherGrant` lookups).
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
- `lib/email.ts` — sends the OTP email via Resend; has a dev-mode console.log fallback (when
  `RESEND_API_KEY` is unset) so you can test the whole login flow locally with no real account
- `bridge/` — standalone MQTT-to-Postgres service (see decisions above), separate `package.json` so
  it runs as its own PM2 process independent of the Next.js app
- Note: **users are provisioned by Admin, not self-signup** — matches the WM1-Mini access model
  where Admin assigns devices to dealers/customers. A phone number that passes OTP but has no
  matching `User` row gets a clear "not registered, contact your dealer" message rather than an
  account being silently created.

## Student Product Studio (`/studio`)

The studio is where BSc Agriculture students take a farm IoT product from problem to field trial. The plan is in `docs/student-product-studio.md` and the student kits are in `products/ASC-StudentKit/`.

- **Roles.** `TEACHER` and `STUDENT` were added to `Role`.
  - Admins create cohorts (one class at one college, or our internal Project #0) and act as the ASC designers.
  - Teachers add students and sign off the mentor gates.
  - Students see only their own team's projects.
  - There is still no self-signup. Adding someone to a cohort creates their account, and they log in with the usual email OTP. After login, students and teachers land on `/studio`.
- **Stages working now:** Problem → Specification (mentor sign-off) → Architecture → Simulate (Wokwi project files, optional) → Build (rules, flashing in the browser, sending the design over USB) → Test (a live checklist driven by the board's readings). App, Enclosure and Report show what is coming.
- **Talking to boards.** Build and Test use Web Serial, so they need Chrome or Edge on a laptop or desktop.
  - `app/studio/[id]/device.ts` speaks the firmware's JSON-lines protocol.
  - `app/studio/[id]/flash.ts` flashes with esptool-js. It loads only when someone presses Flash.
  - The firmware is `products/ASC-StudentKit/firmware/asc-studio-fw`. CI uploads it as product `ASC_KIT`: `main` uploads dev builds (admins only), and an `asc-v*` tag uploads a release (everyone).
- **Code layout:**
  - `lib/studio/`: stages, kits, block library and rule checks. These run in the browser and on the server.
  - `app/studio/`: the pages.
  - `app/api/studio/`: the API routes.
- **Pin map copy.** `lib/studio/pinmap.json` is a copy of `products/ASC-StudentKit/hardware/pinmap.json`. `python3 products/ASC-StudentKit/hardware/tools/check_pinmap.py` fails if the two differ.
- **Claude drafts the spec** when `ANTHROPIC_API_KEY` is set in the server's environment. It uses Claude Opus 5.5 with server-side refusal fallback. Without a key, or if a call fails, a fixed template drafts it instead, so the stage always works. Each project gets at most 10 drafts a day.

**Deploying this change:** run `npm ci`, `npx prisma migrate deploy` (this applies `20261007120000_add_student_product_studio` and `20261007150000_studio_firmware_and_rules`), and `npm run build`. Then restart with PM2. Optionally, add `ANTHROPIC_API_KEY=...` to the app's `.env` first.

### Trying stages 4–6 with a DevKit on a Mac

1. **Firmware.** Flash a ESP32-S3-DevKitC-1-**N8** over its **USB** connector with `cd products/ASC-StudentKit/firmware/asc-studio-fw && pio run -t upload`. Or upload `firmware.bin`, `bootloader.bin` and `partitions.bin` from `.pio/build/asc_s3/` on the Flasher admin page as product **ASC_KIT**, and let the studio flash it.
2. **Wiring.** Wire the parts your design uses to the GPIOs in the Build stage's Engineer's view. For Mini: S1–S4 on GPIO1–4, I²C on SDA 14 / SCL 15, OUT1 on GPIO35, OUT2 on GPIO36.
3. **Studio.** Open the project in Chrome, finish Architecture, save rules, and in Build press **Connect to the board** then **Send design**. Then run the Test checklist.

## Running locally (before the VPS is reachable)

```bash
cd webapp/agrisense-webapp
npm install
cp .env.example .env   # point DATABASE_URL at a local Postgres, or use a free Neon/Supabase dev DB
npx prisma generate
npx prisma migrate dev --name init
npm run dev
```

OTP codes print to the terminal in dev mode (see `lib/email.ts`) — no Resend account needed to test
the login flow end-to-end. You'll need at least one `User` row in the DB to actually log in past OTP
(Prisma Studio — `npx prisma studio` — is the quickest way to add yourself as an ADMIN for testing).

## Still open

- Verify agrisenseandcontrol.in as a sending domain in Resend (or keep the shared `onboarding@resend.dev`
  sender until that's done — works immediately, just less branded)
- Whether TH Monitor and FG1's existing MQTT topics migrate immediately or at their next firmware update
