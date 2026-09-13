// Mirrors each product's own computeDeviceId() (see e.g.
// products/FG1-flowguard/firmware/src/main.cpp) — DEVICE_ID prefix plus the
// last 2 bytes of the chip's WiFi MAC, uppercased. Lets the flasher identify
// which physical unit is connected from its raw MAC (readable even on a
// blank chip) and match it against already-provisioned Device rows, without
// needing the chip to have ever run firmware before.
const PRODUCT_ID_PREFIX: Record<string, string> = {
  FG1: "SWC_001",
};

export function deriveDeviceId(product: string, macHex: string): string | null {
  const prefix = PRODUCT_ID_PREFIX[product];
  if (!prefix) return null;

  const clean = macHex.replace(/[^0-9a-fA-F]/g, "");
  if (clean.length !== 12) return null;

  const suffix = clean.slice(-4).toUpperCase();
  return `${prefix}_${suffix}`;
}
