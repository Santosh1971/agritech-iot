// Products the USB web flasher (/flash) can write. Shared by the server page
// and the browser code.
export type UsbProduct = {
  name: string;
  chip: string; // esptool-js CHIP_NAME the board must report, e.g. "ESP32"
  bootloaderAt: number; // 0x1000 on a classic ESP32, 0x0 on S3/C3
};

// Products that can be flashed from this page. Offsets are the Arduino-ESP32
// default layout (default.csv): partition table at 0x8000, OTA data at 0xe000,
// first app slot at 0x10000.
export const USB_PRODUCTS: Record<string, UsbProduct> = {
  WM2: { name: "WM2 WaterMaster", chip: "ESP32", bootloaderAt: 0x1000 },
};

export function deviceIdFor(product: string, mac: string): string | null {
  const hex = mac.replace(/[^0-9a-fA-F]/g, "").toUpperCase();
  if (hex.length !== 12) return null;
  if (product === "WM2") return `WM2_${hex.slice(4)}`; // firmware Settings::deviceId(): last 4 MAC bytes
  return null;
}
