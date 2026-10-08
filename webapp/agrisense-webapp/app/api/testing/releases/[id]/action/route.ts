import { NextRequest, NextResponse } from "next/server";
import { prisma } from "@/lib/prisma";
import { loadGate } from "@/lib/tracker/gate";
import { err, formFiles, formText, notify, requireMember, saveFiles } from "@/lib/tracker/server";

// multipart: action =
//   signoff | withdraw_signoff   tester; signoff only when the gate is clear
//   publish                      developer; only after sign-off. Optional final
//                                files: firmware (.bin), apk (release-signed), gerber (.zip)
export async function POST(req: NextRequest, { params }: { params: Promise<{ id: string }> }) {
  const auth = await requireMember();
  if ("res" in auth) return auth.res;
  const { user } = auth;
  const { id } = await params;
  const form = await req.formData();
  const action = formText(form, "action", 40);
  const loaded = await loadGate(id);
  if (!loaded) return err("Release not found", 404);
  const { release, gate } = loaded;
  const path = `/dashboard/testing/release/${id}`;
  const note = formText(form, "text", 2000);

  if (action === "signoff") {
    if (user.trackRole !== "TESTER") return err("Only the tester signs off", 403);
    if (release.status !== "OPEN") return err("Already signed off");
    if (!gate.readyForSignOff) return err("The gate is not clear yet: " + gate.checks.filter((c) => !c.ok && c.key !== "signoff").map((c) => c.label).join("; "));
    await prisma.$transaction([
      prisma.trackRelease.update({ where: { id }, data: { status: "SIGNED_OFF", signedOffById: user.userId, signedOffAt: new Date() } }),
      prisma.trackEvent.create({ data: { releaseId: id, buildId: release.candidateBuildId, actorId: user.userId, kind: "signoff", text: `Signed off${note ? `: ${note}` : ""}` } }),
    ]);
    await notify("DEVELOPER", `${release.name} signed off`, [`${user.name} signed off ${release.name}. You can publish it now.`], path, user.userId);
    return NextResponse.json({ ok: true });
  }

  if (action === "withdraw_signoff") {
    if (user.trackRole !== "TESTER") return err("Only the tester can withdraw a sign-off", 403);
    if (release.status !== "SIGNED_OFF") return err("Not signed off");
    if (!note) return err("Say why");
    await prisma.$transaction([
      prisma.trackRelease.update({ where: { id }, data: { status: "OPEN", signedOffById: null, signedOffAt: null } }),
      prisma.trackEvent.create({ data: { releaseId: id, actorId: user.userId, kind: "signoff", text: `Sign-off withdrawn: ${note}` } }),
    ]);
    await notify("DEVELOPER", `${release.name}: sign-off withdrawn`, [`${user.name}: ${note}`], path, user.userId);
    return NextResponse.json({ ok: true });
  }

  if (action === "publish") {
    if (user.trackRole !== "DEVELOPER") return err("Only the developer publishes", 403);
    if (release.status !== "SIGNED_OFF" || !gate.readyToPublish) return err("Needs a clear gate and the tester's sign-off first");
    const firmware = formFiles(form, "firmware");
    const apk = formFiles(form, "apk");
    const gerber = formFiles(form, "gerber");
    if (firmware.some((f) => !f.name.toLowerCase().endsWith(".bin"))) return err("Firmware must be a .bin file");
    if (apk.some((f) => !f.name.toLowerCase().endsWith(".apk"))) return err("App must be an .apk file");
    if (gerber.some((f) => !/\.(zip|rar|7z)$/i.test(f.name))) return err("Gerber must be a .zip file");
    try {
      await saveFiles(firmware, "FIRMWARE", { releaseId: id }, user.userId);
      await saveFiles(apk, "APK", { releaseId: id }, user.userId);
      await saveFiles(gerber, "GERBER", { releaseId: id }, user.userId);
    } catch (e) {
      return err((e as Error).message);
    }
    const c = release.candidateBuild;
    await prisma.$transaction([
      prisma.trackRelease.update({
        where: { id },
        data: {
          status: "RELEASED",
          releasedById: user.userId,
          releasedAt: new Date(),
          fwVersion: formText(form, "fwVersion", 40) ?? release.fwVersion ?? c?.fwVersion,
          appVersion: formText(form, "appVersion", 40) ?? release.appVersion ?? c?.appVersion,
          hwRev: formText(form, "hwRev", 40) ?? release.hwRev ?? c?.hwRev,
        },
      }),
      prisma.trackEvent.create({ data: { releaseId: id, actorId: user.userId, kind: "release", text: `Published${note ? `: ${note}` : ""}` } }),
    ]);
    await notify("ALL", `${release.name} released`, [`${release.name} is published. The changelog and final files are on the release page.`], path, user.userId);
    return NextResponse.json({ ok: true });
  }

  return err("Unknown action");
}
