import { NextRequest, NextResponse } from "next/server";

// Web-app manifest for a student's personal farm app (/workshop/myapp/), so Chrome can
// install it on the phone's home screen with the student's app name, colour and initial.
// Query: n = app name, d = device (FarmIoT-XXXX), c = colour, l = icon letter.

const COLORS: Record<string, string> = {
  green: "#0E5E43", blue: "#2466A8", orange: "#C2601E", purple: "#6B3FA0", red: "#B3261E", teal: "#0F7C80",
};

export function GET(req: NextRequest) {
  const q = req.nextUrl.searchParams;
  const name = (q.get("n") || "My Farm App").replace(/[<>"\\]/g, "").trim().slice(0, 30) || "My Farm App";
  const device = /^FarmIoT-[0-9A-F]{4}$/.test(q.get("d") || "") ? q.get("d")! : "";
  const color = q.get("c") && COLORS[q.get("c")!] ? q.get("c")! : "green";
  const letter = /^[A-Z]$/.test(q.get("l") || "") ? q.get("l")! : (name.match(/[A-Za-z]/)?.[0] || "F").toUpperCase();

  const params = new URLSearchParams({ n: name, d: device, c: color, l: letter });
  const manifest = {
    id: `/workshop/myapp/?d=${device || "none"}&n=${encodeURIComponent(name)}`,
    name,
    short_name: name.slice(0, 12),
    description: `${name}: live farm readings and pump control${device ? ` for ${device}` : ""}`,
    start_url: `/workshop/myapp/index.html?${params}`,
    scope: "/workshop/myapp/",
    display: "standalone",
    orientation: "portrait",
    background_color: "#F3F6F1",
    theme_color: COLORS[color],
    icons: [192, 512].map((size) => ({
      src: `/workshop/myapp/icons/${letter}-${color}-${size}.png`,
      sizes: `${size}x${size}`,
      type: "image/png",
      purpose: "any maskable",
    })),
  };
  return NextResponse.json(manifest, { headers: { "Content-Type": "application/manifest+json", "Cache-Control": "no-store" } });
}
