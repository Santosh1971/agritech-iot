import type { NextConfig } from "next";

const nextConfig: NextConfig = {
  // Default is 10MB -- too small for an app APK upload (/api/admin/app-builds,
  // /api/admin/builds). A release APK is routinely 20-25MB (WPC's own app build
  // is ~23MB); middleware runs in front of every request, including these
  // uploads, so its own body-size cap is what actually bites, not any limit in
  // the route handler itself. Found 2026-10-03 when the first WPC app release
  // upload failed with "Request body exceeded 10MB" / "Failed to parse body as
  // FormData" (CI's curl got a 4xx, not a useful error message either).
  experimental: {
    middlewareClientMaxBodySize: "40mb",
  },
  // Serve the static student labs pages (public/workshop/...) at /workshop and /workshop/practical (+ /hi, /kn).
  async rewrites() {
    return [
      { source: "/workshop", destination: "/workshop/index.html" },
      { source: "/workshop/practical", destination: "/workshop/practical/index.html" },
      { source: "/workshop/practical/hi", destination: "/workshop/practical/hi/index.html" },
      { source: "/workshop/practical/kn", destination: "/workshop/practical/kn/index.html" },
      { source: "/workshop/agr322", destination: "/workshop/agr322/index.html" },
      { source: "/workshop/survey", destination: "/workshop/survey/index.html" },
      { source: "/workshop/insights", destination: "/workshop/insights/index.html" },
      { source: "/workshop/feedback", destination: "/workshop/feedback/index.html" },
      { source: "/workshop/flash", destination: "/workshop/flash/index.html" },
      { source: "/workshop/mqtt", destination: "/workshop/mqtt/index.html" },
      { source: "/workshop/lab", destination: "/workshop/lab/index.html" },
      { source: "/workshop/app", destination: "/workshop/app/index.html" },
      { source: "/workshop/myapp", destination: "/workshop/myapp/index.html" },
      { source: "/workshop/station", destination: "/workshop/station/index.html" },
      { source: "/workshop/facilitators", destination: "/workshop/facilitators/index.html" },
    ];
  },
};

export default nextConfig;
