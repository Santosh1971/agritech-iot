# PlatformIO pre-script: compile in the fixed broker logins. Never commit
# them (the repo is public). Each value: env var first, then a file.
#   AgriSense password (user wm2-device): WM2_AG_PASS  / ~/.config/agrisense/wm2_mqtt_pass
#   Girish user + password:               WM2_GR_USER  / ~/.config/agrisense/wm2_girish_user
#                                         WM2_GR_PASS  / ~/.config/agrisense/wm2_girish_pass
import os

Import("env")  # noqa: F821  (provided by PlatformIO)


def secret(var, fname):
    v = os.environ.get(var, "").strip()
    f = os.path.expanduser("~/.config/agrisense/" + fname)
    if not v and os.path.exists(f):
        v = open(f).read().strip()
    if v:
        env.Append(CPPDEFINES=[(var, env.StringifyMacro(v))])  # noqa: F821
    return bool(v)


if secret("WM2_AG_PASS", "wm2_mqtt_pass"):
    print("wm2_secrets: AgriSense login included")
else:
    print("wm2_secrets: WARNING no WM2_AG_PASS: nodes cannot log in to the AgriSense broker")
gu = secret("WM2_GR_USER", "wm2_girish_user")
gp = secret("WM2_GR_PASS", "wm2_girish_pass")
print("wm2_secrets: Girish login " + ("included" if gu else "not set (connects without a login)"))
