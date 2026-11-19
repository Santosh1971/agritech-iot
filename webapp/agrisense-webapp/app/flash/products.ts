// Products the USB web flasher (/flash) can write. Shared by the server page
// and the browser code.
import { deriveDeviceId } from "@/lib/deviceIdentity";

// Every firmware product on the flasher. ASAC_DEMO is app-only (no hardware).
export const USB_PRODUCTS: Record<string, string> = {
  WM2: "WM2 WaterMaster",
  FG1: "FG1 FlowGuard",
  FG1_TH: "FG1 Greenhouse TH",
  WPC: "WPC Pump Controller",
  WM1_MINI: "WM1 Mini",
  WM1_PRO: "WM1 Pro",
  FM1: "FM1 FlowMaster",
  TH: "TH Monitor",
  ASC_KIT: "ASC Student Kit",
};

// The chip a build is for, from its variant (the PlatformIO env it was built
// with): *_c3 = ESP32-C3, *_s3 = ESP32-S3, anything else (esp32dev,
// master_node, pump_node, asc_gpsioam, ...) = classic ESP32. Names match
// esptool-js CHIP_NAME.
export function chipForVariant(variant: string): string {
  if (/c3/i.test(variant)) return "ESP32-C3";
  if (/s3/i.test(variant)) return "ESP32-S3";
  return "ESP32";
}

// Second-stage bootloader offset: 0x1000 on a classic ESP32, 0x0 on S3/C3.
// Partition table (0x8000), OTA data (0xe000) and the first app slot
// (0x10000) are the same in every product's layout (default.csv,
// min_spiffs.csv).
export function bootloaderOffset(chip: string): number {
  return chip === "ESP32" ? 0x1000 : 0x0;
}

// The ID the unit calls itself, where the product has a MAC-based scheme
// (lib/deviceIdentity.ts). null = this product has none, so the flash is not
// tied to a registered device.
export function deviceIdFor(product: string, mac: string): string | null {
  return deriveDeviceId(product, mac);
}
