# PlatformIO pre-script: compile in the firmware-update password. Never commit
# it (the repo is public). Env var first, then a file:
#   WMM_OTA_PASS  /  ~/.config/agrisense/wmm_ota_pass
# Without it, firmware update over WiFi (push and pull) is disabled.
import os

Import("env")  # noqa: F821  (provided by PlatformIO)

v = os.environ.get("WMM_OTA_PASS", "").strip()
f = os.path.expanduser("~/.config/agrisense/wmm_ota_pass")
if not v and os.path.exists(f):
    v = open(f).read().strip()
if v:
    env.Append(CPPDEFINES=[("OTA_PASS", env.StringifyMacro(v))])  # noqa: F821
    print("wmm_secrets: firmware update password included")
else:
    print("wmm_secrets: WARNING no WMM_OTA_PASS: firmware update over WiFi disabled")
