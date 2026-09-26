import type { NextConfig } from "next";

const nextConfig: NextConfig = {
  // Serve the static student labs page (public/workshop/index.html) at /workshop.
  async rewrites() {
    return [{ source: "/workshop", destination: "/workshop/index.html" }];
  },
};

export default nextConfig;
