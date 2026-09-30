export const metadata = {
  title: "Agri Sensors and Controls",
  description: "Device monitoring & control for AgriTech IoT products",
};

export default function RootLayout({ children }: { children: React.ReactNode }) {
  return (
    <html lang="en">
      <body>{children}</body>
    </html>
  );
}
