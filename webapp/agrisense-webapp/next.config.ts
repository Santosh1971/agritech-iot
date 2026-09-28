import type { NextConfig } from "next";

const nextConfig: NextConfig = {
  // Serve the static student labs pages (public/workshop/...) at /workshop and /workshop/practical.
  async rewrites() {
    return [
      { source: "/workshop", destination: "/workshop/index.html" },
      { source: "/workshop/practical", destination: "/workshop/practical/index.html" },
    ];
  },
};

export default nextConfig;
