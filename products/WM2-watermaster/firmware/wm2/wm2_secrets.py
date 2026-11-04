# PlatformIO pre-script: compile in the fixed AgriSense broker password for
# the wm2-device login. Never commit the password (the repo is public).
# Source, first found: env WM2_AG_PASS, then ~/.config/agrisense/wm2_mqtt_pass.
import os

Import("env")  # noqa: F821  (provided by PlatformIO)

pw = os.environ.get("WM2_AG_PASS", "").strip()
f = os.path.expanduser("~/.config/agrisense/wm2_mqtt_pass")
if not pw and os.path.exists(f):
    pw = open(f).read().strip()
if pw:
    env.Append(CPPDEFINES=[("WM2_AG_PASS", env.StringifyMacro(pw))])  # noqa: F821
    print("wm2_secrets: built-in AgriSense login included")
else:
    print("wm2_secrets: WARNING no WM2_AG_PASS: nodes will need the broker login entered by hand")
