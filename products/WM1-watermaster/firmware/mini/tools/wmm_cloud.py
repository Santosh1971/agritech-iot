#!/usr/bin/env python3
"""Talk to a WM1-Mini / WM-Mini C3 board through the AgriSense MQTT broker,
from anywhere (the board only needs internet on its own WiFi).

    python3 wmm_cloud.py WM1_1A2B3C4D status            # latest retained status
    python3 wmm_cloud.py WM1_1A2B3C4D watch             # print status/programs as they arrive
    python3 wmm_cloud.py WM1_1A2B3C4D cmd '{"cmd":"manual_set","channel":"valve1","state":true}'
    python3 wmm_cloud.py WM1_1A2B3C4D ota https://host/path/firmware.bin [md5]

Topics: agrisense/WM1/<id>/{status,programs,command,lwt}. Login: user wm1-device,
password WM1_MQTT_PASS (env) or ~/.config/agrisense/wm1_mqtt_pass.
The ota command adds the update key from WMM_OTA_PASS / ~/.config/agrisense/wmm_ota_pass.
"""
import json
import os
import sys
import time

import paho.mqtt.client as mqtt

HOST, PORT, USER = "mqtt.agrisenseandcontrol.in", 1883, "wm1-device"


def secret(var, fname):
    v = os.environ.get(var, "").strip()
    f = os.path.expanduser("~/.config/agrisense/" + fname)
    if not v and os.path.exists(f):
        v = open(f).read().strip()
    return v


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    dev, action = sys.argv[1], sys.argv[2]
    base = f"agrisense/WM1/{dev}/"
    pw = secret("WM1_MQTT_PASS", "wm1_mqtt_pass")
    if not pw:
        sys.exit("no broker password: set WM1_MQTT_PASS or ~/.config/agrisense/wm1_mqtt_pass")

    got = {}

    def on_message(c, u, m):
        sub = m.topic[len(base):]
        try:
            body = json.loads(m.payload)
        except ValueError:
            body = m.payload.decode(errors="replace")
        got[sub] = body
        if action == "watch":
            print(time.strftime("%H:%M:%S"), sub, json.dumps(body)[:600], flush=True)

    c = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=f"wmm-cli-{os.getpid()}")
    c.username_pw_set(USER, pw)
    c.on_message = on_message
    login = {}
    c.on_connect = lambda cl, u, f, rc, p=None: login.setdefault("rc", rc)
    c.connect(HOST, PORT, 30)
    c.subscribe(base + "#")
    c.loop_start()
    for _ in range(50):
        if "rc" in login:
            break
        time.sleep(0.1)
    if login.get("rc") is None or login["rc"].is_failure:
        sys.exit(f"broker login failed: {login.get('rc', 'no answer')}")

    if action in ("cmd", "ota"):
        if action == "cmd":
            payload = json.loads(sys.argv[3])
        else:
            key = secret("WMM_OTA_PASS", "wmm_ota_pass")
            payload = {"cmd": "ota_url", "url": sys.argv[3], "key": key}
            if len(sys.argv) > 4:
                payload["md5"] = sys.argv[4]
        payload.setdefault("ts", int(time.time()))
        c.publish(base + "command", json.dumps(payload), qos=1).wait_for_publish()
        print("sent", payload.get("cmd"))
        time.sleep(4)
    elif action == "watch":
        try:
            while True:
                time.sleep(1)
        except KeyboardInterrupt:
            pass
    else:
        time.sleep(3)
    for k in ("lwt", "status"):
        if k in got:
            print(k, json.dumps(got[k], indent=1) if action == "status" else json.dumps(got[k])[:400])
    if not got:
        print("nothing received: is the device ID right and the board online?")
    c.loop_stop()
    c.disconnect()


if __name__ == "__main__":
    main()
