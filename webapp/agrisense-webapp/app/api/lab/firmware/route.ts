import { NextRequest, NextResponse } from "next/server";
import { readFile } from "fs/promises";
import { join } from "path";
import { checkAccess } from "@/lib/firmwareBuild";
import { LAB_FIRMWARE_DIR } from "@/lib/labStation";

// GET (workshop access code): the Lab Station firmware as a merged image, for
// restoring the unit over USB from the flasher page. It contains the device
// token, so it is not a public static file.
export async function GET(req: NextRequest) {
  if (!checkAccess(req.headers.get("x-access-code"))) {
    return NextResponse.json({ error: "Wrong or missing workshop access code." }, { status: 401 });
  }
  try {
    const bin = await readFile(join(LAB_FIRMWARE_DIR, "labstation.merged.bin"));
    const version = (await readFile(join(LAB_FIRMWARE_DIR, "version.txt"), "utf8").catch(() => "")).trim();
    return new NextResponse(new Uint8Array(bin), {
      headers: { "Content-Type": "application/octet-stream", "Cache-Control": "no-store", "X-Lab-Version": version },
    });
  } catch {
    return NextResponse.json({ error: "Lab Station firmware not built yet." }, { status: 404 });
  }
}
