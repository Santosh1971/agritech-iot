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

## Not yet decided / open questions for next session

- Auth approach: NextAuth (same as Bridge) vs something simpler, given this app also needs role-based
  dealer access (Bridge only had Admin/User)
- Whether TH Monitor and FG1's existing MQTT topics get migrated to the new convention immediately,
  or keep running as-is until each product's next firmware update
- Backend service that subscribes to `agrisense/#` and writes Reading/Device.lastStatus rows —
  a small always-running Node process (PM2-managed) separate from the Next.js app itself, or a
  Next.js API route triggered by the MQTT client — needs a decision once we're actually on the VPS
