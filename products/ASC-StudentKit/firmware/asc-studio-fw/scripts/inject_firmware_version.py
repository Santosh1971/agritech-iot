"""Pre-build: override FW_VERSION for CI's auto dev builds.

main.cpp's FW_VERSION default is bumped by hand to match whatever was last
cut as an actual release (e.g. "0.4.2"). For CI's per-push dev builds, and
for a tagged release build, we want the serial boot log (and the /status
JSON, which already sends this same define) to show exactly which commit
or release produced it. The workflow sets FIRMWARE_VERSION_OVERRIDE before
calling `pio run`; a local/manual build (no env var set) is untouched and
keeps main.cpp's hand-set default. Mirrors FG1's identical script.
"""
Import("env")  # noqa: F821 -- provided by PlatformIO's SCons environment

import os

override = os.environ.get("FIRMWARE_VERSION_OVERRIDE")
if override:
    env.Append(BUILD_FLAGS=[f'-DFW_VERSION=\\"{override}\\"'])
    print(f"[inject_firmware_version] FW_VERSION overridden to \"{override}\"")
