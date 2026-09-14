"""Pre-build: override FIRMWARE_VERSION for CI's auto dev builds.

Config.h's FIRMWARE_VERSION default is bumped by hand to match whatever was
last cut as an actual dashboard release (e.g. "1.1.0") -- see that file.
For CI's per-push dev builds we want the serial boot log (and the MQTT
status payload, which already sends this same define) to show exactly
which commit produced it instead, same idea as the Android app's
appVersionName Gradle property. The workflow sets FIRMWARE_VERSION_OVERRIDE
before calling `pio run`; a local/manual build (no env var set) is
untouched and keeps Config.h's hand-set default.
"""
Import("env")  # noqa: F821 -- provided by PlatformIO's SCons environment

import os

override = os.environ.get("FIRMWARE_VERSION_OVERRIDE")
if override:
    env.Append(BUILD_FLAGS=[f'-DFIRMWARE_VERSION=\\"{override}\\"'])
    print(f"[inject_firmware_version] FIRMWARE_VERSION overridden to \"{override}\"")
