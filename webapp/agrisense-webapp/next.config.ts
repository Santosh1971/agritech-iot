import type { NextConfig } from "next";

const nextConfig: NextConfig = {
  // Serve the static student labs pages (public/workshop/...) at /workshop and /workshop/practical (+ /hi, /kn).
  async rewrites() {
    return [
      { source: "/workshop", destination: "/workshop/index.html" },
      { source: "/workshop/practical", destination: "/workshop/practical/index.html" },
      { source: "/workshop/practical/hi", destination: "/workshop/practical/hi/index.html" },
      { source: "/workshop/practical/kn", destination: "/workshop/practical/kn/index.html" },
      { source: "/workshop/agr322", destination: "/workshop/agr322/index.html" },
      { source: "/workshop/survey", destination: "/workshop/survey/index.html" },
    ];
  },
};

export default nextConfig;
