// Where a product already has its own on-device convention for turning a MAC
// into an ID (see e.g. products/FG1-flowguard/firmware/src/main.cpp's
// computeDeviceId(), or WM1's DeviceIdentity.h), this mirrors it exactly, so
// the label shown here matches what the unit would call itself. Where one
// doesn't exist (WPC — its own internal LoRa sync word is derived from the
// MAC too, but that's purely internal pairing state, not a display/inventory
// ID, and doesn't need to match this), suffixBytes is just a fresh choice:
// FG1's 2-byte suffix turned out collision-prone across a dealer's stock in
// practice, which is why WM1 moved to 4 bytes — new products start there
// rather than repeating FG1's mistake.
//
// Either way, this only has to be unique and consistent per physical chip —
// it's how the flasher identifies which physical unit is connected from its
// raw MAC (readable even on a blank chip, before it's ever run firmware) and
// matches it against already-provisioned Device rows.
const PRODUCT_ID_SCHEME: Record<string, { prefix: string; suffixBytes: number }> = {
  FG1: { prefix: "SWC_001", suffixBytes: 2 },
  WM1_MINI: { prefix: "WM1", suffixBytes: 4 },
  WPC: { prefix: "WPC", suffixBytes: 4 },
};

export function deriveDeviceId(product: string, macHex: string): string | null {
  const scheme = PRODUCT_ID_SCHEME[product];
  if (!scheme) return null;

  const clean = macHex.replace(/[^0-9a-fA-F]/g, "");
  if (clean.length !== 12) return null;

  const suffix = clean.slice(-scheme.suffixBytes * 2).toUpperCase();
  return `${scheme.prefix}_${suffix}`;
}
