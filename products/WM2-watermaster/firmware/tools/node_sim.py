#!/usr/bin/env python3
"""WM2 node simulator, for testing the web page and mobile app without hardware.

Serves the real control page (extracted from src/webpage.h) and answers the
same JSON commands over WebSocket /ws and POST /api/cmd. Water and flow are
simulated; valve logic is simplified (manual runs, bypass, interlock).

    python3 node_sim.py [--port 8080]
    open http://localhost:8080
    water on/off:  curl 'http://localhost:8080/sim?water=1'
"""
import argparse
import asyncio
import json
import re
import time
from pathlib import Path

from aiohttp import web, WSMsgType

HERE = Path(__file__).resolve().parent
PAGE = re.search(r'R"HTML\((.*)\)HTML"', (HERE.parent / "wm2/src/webpage.h").read_text(), re.S).group(1)

DAYS = ["mon", "tue", "wed", "thu", "fri", "sat", "sun"]
S = {
    "water": False, "ps": False, "bypass": False, "bypass_until": 0,
    "valves": [
        {"open": False, "mode": "program", "manual": False, "left": 0, "run": 0, "litres": 0.0,
         "program": {"enabled": True, "type": "timer", "days": ["mon", "wed", "fri"], "minutes": 45,
                     "window": ["00:00", "23:59"], "carry_over": False}},
        {"open": False, "mode": "manual", "manual": False, "left": 0, "run": 0, "litres": 0.0,
         "program": {"enabled": False, "type": "timer", "days": [], "minutes": 30,
                     "window": ["00:00", "23:59"], "carry_over": False}},
    ],
    "flow_today": [0.0] * 4, "events": [],
}
CFG = {"farm": "girish", "zone": "z1", "node": "n1", "ssid": "FarmWiFi", "broker": "agrisense",
       "ag_host": "mqtt.agrisenseandcontrol.in", "ag_port": 1883, "ag_user": "", "ag_id": "WM2_A98803E8", "mqtt_host": "", "mqtt_port": 1883,
       "mqtt_user": "", "tz_min": 330, "active_s": 60, "heartbeat_s": 3600, "idle_delay_s": 300, "ps_settle_s": 2,
       "profile": "responsive", "check_min": 1, "gap_s": 2, "safe_close": True, "bypass_default_min": 60,
       "chemistry": "lifepo4", "capacity_ah": 18, "v_warn": 12.8, "v_save": 12.5, "v_crit": 12.0, "daily_ah": 0.6,
       "sequential": True, "day_start_hour": 0, "low_pct": 50, "high_pct": 150, "imbalance_pct": 40,
       "alarm_delay_s": 180,
       "flow": [{"input": i + 1, "valve": 1 if i < 2 else 2, "ppl": 450} for i in range(4)],
       "valves": [{"valve": v + 1, "mode": S["valves"][v]["mode"], "pulse_ms": 50, "invert": False,
                   "never_close": False, "on_water_loss": "keep", "close_after_min": 10} for v in range(2)]}
CLIENTS = set()


def lpm(i):
    v = 0 if i < 2 else 1
    return 10.4 if S["valves"][v]["open"] and (S["water"] or S["bypass"]) else 0.0


def state():
    now = int(time.time())
    return {
        "v": 1, "ts": now, "time_ok": True, "local": time.strftime("%Y-%m-%d %H:%M:%S"), "node": "z1/n1",
        "fw": "sim", "state": "active" if S["water"] or any(v["open"] for v in S["valves"]) else "idle",
        "water": S["water"], "ps": S["ps"],
        "bypass": {"on": S["bypass"], "left_s": max(0, S["bypass_until"] - now) if S["bypass"] else 0},
        "flow": [{"in": i + 1, "valve": 1 if i < 2 else 2, "lpm": lpm(i), "l_today": round(S["flow_today"][i], 1),
                  "l_total": round(S["flow_today"][i], 1)} for i in range(4)],
        "valves": [{"v": i + 1, "open": v["open"], "target": v["open"], "pending": False,
                    "waiting": v["manual"] and not v["open"], "mode": v["mode"],
                    "due": v["mode"] == "program" and v["program"]["enabled"], "done": False,
                    "planned_min": v["program"].get("minutes", 0), "run_min": v["run"] // 60,
                    "litres": round(v["litres"], 1), "manual": v["manual"], "manual_left_s": v["left"],
                    "never_close": False} for i, v in enumerate(S["valves"])],
        "batt": {"v": 13.21, "soc": 72, "trend": "charging", "days": 24.5, "level": "ok"},
        "wifi": {"ip": "192.168.1.50", "rssi": -58, "ap": False, "ap_clients": 0}, "mqtt": False,
        "alarms": [], "uptime_s": now % 100000,
    }


async def broadcast(obj):
    msg = json.dumps(obj)
    for ws in list(CLIENTS):
        try:
            await ws.send_str(msg)
        except Exception:
            CLIENTS.discard(ws)


async def event(e):
    e.update({"v": 1, "ts": int(time.time()), "node": "z1/n1"})
    S["events"].append(e)
    S["events"] = S["events"][-200:]
    await broadcast({"type": "event", "data": e})


async def dispatch(req):
    cmd = req.get("cmd", "")
    resp = {"v": 1, "cmd": cmd, "ok": True}
    if "id" in req:
        resp["id"] = req["id"]
    data = {}
    v = req.get("valve", 0) - 1
    if cmd == "get_state":
        data = state()
    elif cmd == "device_info":
        data = {"fw": "sim", "chip": "ESP32-D0WD-V3", "mac": "20:9B:A9:88:03:E8", "flash_mb": 4, "fs_used": 40960,
                "fs_total": 1441792, "heap": 180000, "uptime_s": 1234, "host": "wm2-z1-n1",
                "base": "agrisense/WM2/WM2_A98803E8/" if CFG["broker"] == "agrisense" else "girish/wm2/z1/n1/",
                "broker": CFG["broker"], "broker_host": CFG["ag_host"] if CFG["broker"] == "agrisense" else CFG["mqtt_host"],
                "device_id": CFG["ag_id"], "mqtt": False,
                "ip": "192.168.1.50", "ap": False, "ap_ssid": "WM2-z1-n1"}
    elif cmd == "get_config":
        data = CFG
    elif cmd == "mqtt_config":
        ag = req.get("broker", CFG["broker"]) != "girish"
        CFG["broker"] = "agrisense" if ag else "girish"
        pre = "ag_" if ag else "mqtt_"
        for k in ("host", "port", "user"):
            if k in req:
                CFG[pre + k] = req[k]
        for k in ("ag_id", "farm", "zone", "node"):
            if k in req:
                CFG[k] = req[k]
        data = {"broker": CFG["broker"], "base": "agrisense/WM2/%s/" % CFG["ag_id"] if ag else "girish/wm2/z1/n1/"}
    elif cmd in ("set_config", "set_report", "set_options", "set_battery", "set_power",
                 "wifi_config", "set_flow", "set_valve_hw", "rtc_sync", "reboot", "clear_history", "softap_on"):
        CFG.update(req.get("config", {}))
        for k in ("active_s", "heartbeat_s", "idle_delay_s"):
            if k in req:
                CFG[k] = req[k]
    elif cmd == "valve_open":
        if v not in (0, 1):
            resp.update(ok=False, err="valve must be 1 or 2")
        else:
            S["valves"][v].update(manual=True, left=int(req.get("minutes", 0) * 60))
            data["state"] = "opening" if S["water"] or S["bypass"] else "waiting_for_water"
    elif cmd == "valve_close":
        S["valves"][v]["manual"] = False
    elif cmd == "set_mode":
        S["valves"][v]["mode"] = req.get("mode", "program")
    elif cmd == "set_program":
        S["valves"][v]["program"] = req.get("program", {})
        data["program"] = S["valves"][v]["program"]
    elif cmd == "get_program":
        data["programs"] = [{"valve": i + 1, "mode": x["mode"], "program": x["program"]} for i, x in enumerate(S["valves"])]
    elif cmd == "bypass":
        S["bypass"] = bool(req.get("on", True))
        S["bypass_until"] = int(time.time()) + int(req.get("minutes", 60)) * 60
        await event({"event": "bypass", "on": S["bypass"], "src": req.get("src", "app")})
    elif cmd == "get_events":
        data["events"] = S["events"][-int(req.get("limit", 50)):]
    elif cmd == "get_days":
        today = int(time.time() // 86400)
        data["days"] = [{"event": "day_result", "valve": 1, "day": today - d, "date": time.strftime("%Y-%m-%d", time.gmtime((today - d) * 86400)),
                         "planned_min": 45, "run_min": 45 if d % 3 else 30, "litres": [230.5, 228.1], "normal_litres": [234, 234],
                         "water_min": 300, "status": "done" if d % 3 else "partial", "alarms": []} for d in range(1, 6)]
    elif cmd == "get_history":
        f, t = int(req.get("from", time.time() - 86400)), int(req.get("to", time.time()))
        res = req.get("res") or ("raw" if t - f <= 2 * 86400 else "hour" if t - f <= 31 * 86400 else "day")
        step = {"raw": 60, "hour": 3600, "day": 86400}[res]
        pts = []
        for ts in range(f, t, step)[:1500]:
            on = (ts // 3600) % 24 in (2, 3, 4, 5, 6, 7)
            if res == "raw":
                pts.append([ts, 10.4 if on else 0, 10.2 if on else 0, 0, 0, 39 if on else 0, 13.1])
            else:
                k = step / 60
                pts.append([ts, round(10.4 * k * 0.25, 1), round(10.2 * k * 0.25, 1), 0, 0, int(k * 0.25), 0, int(k * 0.25), 12.9])
        data = {"from": f, "to": t, "res": res, "points": pts,
                "columns": "ts,lpm1,lpm2,lpm3,lpm4,flags,batt" if res == "raw" else "ts,l1,l2,l3,l4,v1_min,v2_min,water_min,min_batt",
                "totals": {"litres": [512.4, 503.0, 0, 0], "valve_litres": [1015.4, 0], "valve_open_min": [98, 0],
                           "water_min": 310, "min_batt": 12.95, "records": len(pts)}, "truncated": False}
    elif cmd == "wifi_scan":
        data["networks"] = [{"ssid": "FarmWiFi", "rssi": -52, "open": False}, {"ssid": "Neighbour", "rssi": -80, "open": False}]
    else:
        resp.update(ok=False, err="unknown command")
    if data:
        resp["data"] = data
    return resp


async def ws_handler(request):
    ws = web.WebSocketResponse()
    await ws.prepare(request)
    CLIENTS.add(ws)
    await ws.send_str(json.dumps({"type": "state", "data": state()}))
    async for msg in ws:
        if msg.type == WSMsgType.TEXT:
            try:
                await ws.send_str(json.dumps(await dispatch(json.loads(msg.data))))
            except Exception as e:  # keep the socket alive on bad input
                await ws.send_str(json.dumps({"ok": False, "err": str(e)}))
    CLIENTS.discard(ws)
    return ws


async def api_cmd(request):
    return web.json_response(await dispatch(await request.json()))


async def api_state(request):
    return web.json_response(state())


async def sim(request):
    if "water" in request.query:
        w = request.query["water"] == "1"
        if w != S["water"]:
            S["water"] = S["ps"] = w
            await event({"event": "water", "present": w})
    return web.json_response({"water": S["water"]})


async def ticker():
    while True:
        await asyncio.sleep(1)
        now = int(time.time())
        if S["bypass"] and now >= S["bypass_until"]:
            S["bypass"] = False
        for i, v in enumerate(S["valves"]):
            want = v["manual"]
            flowing = S["water"] or S["bypass"]
            if want and not v["open"] and flowing:
                v["open"] = True
                await event({"event": "valve", "valve": i + 1, "open": True, "reason": "manual"})
            if not want and v["open"]:
                v["open"] = False
                await event({"event": "valve", "valve": i + 1, "open": False, "reason": "manual"})
            if v["open"] and flowing:
                v["run"] += 1
                if v["manual"] and v["left"]:
                    v["left"] -= 1
                    if v["left"] <= 0:
                        v["manual"] = False
                        await event({"event": "manual_done", "valve": i + 1})
        for i in range(4):
            S["flow_today"][i] += lpm(i) / 60
        for i in range(2):
            S["valves"][i]["litres"] = S["flow_today"][2 * i] + S["flow_today"][2 * i + 1]
        if now % 2 == 0:
            await broadcast({"type": "state", "data": state()})


async def main(port):
    app = web.Application()
    app.add_routes([web.get("/", lambda r: web.Response(text=PAGE, content_type="text/html")),
                    web.get("/ws", ws_handler), web.post("/api/cmd", api_cmd), web.get("/api/state", api_state),
                    web.get("/sim", sim)])
    runner = web.AppRunner(app)
    await runner.setup()
    await web.TCPSite(runner, "0.0.0.0", port).start()
    print(f"WM2 simulator on http://localhost:{port}  (water: /sim?water=1)")
    await ticker()


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=8080)
    asyncio.run(main(ap.parse_args().port))
