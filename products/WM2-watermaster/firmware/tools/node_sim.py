#!/usr/bin/env python3
"""WM2 node simulator, for testing the web page and mobile app without hardware.

Each simulated node serves the real control page (extracted from
src/webpage.h) and answers the same JSON commands over WebSocket /ws and
POST /api/cmd. Water, flow and valve runs are simulated; the logic is a
simplified stand-in for the firmware (manual runs, bypass, interlock).

    python3 node_sim.py                        # one node on :8080
    python3 node_sim.py --port 8091 --count 4  # nodes on 8091..8094
    water on/off for one node:  curl 'http://localhost:8091/sim?water=1'
"""
import argparse
import asyncio
import json
import random
import re
import time
from pathlib import Path

from aiohttp import web, WSMsgType

HERE = Path(__file__).resolve().parent
PAGE = re.search(r'R"HTML\((.*)\)HTML"', (HERE.parent / "wm2/src/webpage.h").read_text(), re.S).group(1)
DAYS = ["mon", "tue", "wed", "thu", "fri", "sat", "sun"]
TZ = 330 * 60


def local_day(ts):
    return (ts + TZ) // 86400


class Node:
    def __init__(self, idx, port):
        self.idx = idx
        self.port = port
        mac8 = "SIM%05d" % (idx + 1)
        self.dev_id = "WM2_" + mac8
        self.ssid = "WM2-" + mac8
        now = int(time.time())
        # Node 1 has water and V1 irrigating; others idle with varied history.
        self.water = idx == 0
        self.bypass = False
        self.bypass_until = 0
        self.flow_today = [0.0] * 4
        self.events = []
        self.clients = set()
        self.valves = []
        for v in range(2):
            days = [["mon", "wed", "fri"], ["tue", "thu", "sat"], ["mon", "thu"], ["wed", "sat"]][(idx + v) % 4]
            mins = [45, 30, 40, 35][(idx + v) % 4]
            last_end = now - (idx * 2 + v + 1) * 86400 + random.randint(-3600, 3600)
            self.valves.append({
                "open": False, "mode": "program", "manual": False, "left": 0, "litres": 0.0,
                "run": None,
                "last": {"start": last_end - mins * 60, "end": last_end, "min": mins,
                         "l": round(mins * 20.8 * random.uniform(0.85, 1.05), 1)},
                "program": {"enabled": True, "type": "timer", "days": days, "minutes": mins,
                            "window": ["00:00", "23:59"], "carry_over": False},
            })
        if idx == 0:
            self.valves[0]["open"] = True
            self.valves[0]["run"] = {"since": now - 24 * 60, "sec": 24 * 60, "l": 499.0}
        self.cfg = {"farm": "girish", "zone": "z1", "node": "n%d" % (idx + 1), "ssid": "FarmWiFi",
                    "broker": "agrisense", "ag_host": "mqtt.agrisenseandcontrol.in", "ag_port": 1883,
                    "ag_user": "", "ag_id": self.dev_id, "mqtt_host": "mqtt.grty.co.in", "mqtt_port": 1883,
                    "mqtt_user": "", "tz_min": 330, "active_s": 60, "heartbeat_s": 3600, "idle_delay_s": 300,
                    "ps_settle_s": 2, "profile": "responsive", "check_min": 1, "gap_s": 2, "safe_close": True,
                    "bypass_default_min": 60, "chemistry": "lifepo4", "capacity_ah": 18, "v_warn": 12.8,
                    "v_save": 12.5, "v_crit": 12.0, "daily_ah": 0.6, "sequential": True, "day_start_hour": 0,
                    "low_pct": 50, "high_pct": 150, "imbalance_pct": 40, "alarm_delay_s": 180,
                    "flow": [{"input": i + 1, "valve": 1 if i < 2 else 2, "ppl": 450} for i in range(4)],
                    "valves": [{"valve": v + 1, "mode": "program", "pulse_ms": 50, "invert": False,
                                "never_close": False, "on_water_loss": "keep", "close_after_min": 10}
                               for v in range(2)]}

    # ------------------------------------------------------------ model
    def lpm(self, i):
        v = self.valves[0 if i < 2 else 1]
        return 10.4 if v["open"] and (self.water or self.bypass) else 0.0

    def next_due(self, v):
        p = self.valves[v]["program"]
        if self.valves[v]["mode"] != "program" or not p.get("enabled"):
            return None
        now = int(time.time())
        today = local_day(now)
        for d in range(0, 14):
            day = today + d
            wd = DAYS[(day + 3) % 7]
            if wd in p.get("days", []):
                if d == 0:
                    if self.valves[v]["open"] or self.valves[v]["last"]["end"] < now - 6 * 3600:
                        return now
                    continue
                return day * 86400 - TZ
        return None

    def state(self):
        now = int(time.time())
        vals = []
        for i, v in enumerate(self.valves):
            j = {"v": i + 1, "open": v["open"], "target": v["open"], "pending": False,
                 "waiting": v["manual"] and not v["open"], "mode": v["mode"], "due": bool(v["open"]),
                 "done": False, "planned_min": v["program"].get("minutes", 0),
                 "run_min": (v["run"]["sec"] // 60) if v["run"] else 0, "litres": round(v["litres"], 1),
                 "manual": v["manual"], "manual_left_s": v["left"], "never_close": False,
                 "prog": v["program"], "last": v["last"]}
            if v["run"]:
                j["run"] = {"since": v["run"]["since"], "min": v["run"]["sec"] // 60, "l": round(v["run"]["l"], 1)}
            nd = self.next_due(i)
            if nd:
                j["next_due"] = nd
            vals.append(j)
        return {
            "v": 1, "ts": now, "time_ok": True, "local": time.strftime("%Y-%m-%d %H:%M:%S"),
            "node": "%s/%s" % (self.cfg["zone"], self.cfg["node"]), "fw": "sim",
            "state": "active" if self.water or any(v["open"] for v in self.valves) else "idle",
            "water": self.water, "ps": self.water,
            "bypass": {"on": self.bypass, "left_s": max(0, self.bypass_until - now) if self.bypass else 0},
            "flow": [{"in": i + 1, "valve": 1 if i < 2 else 2, "lpm": self.lpm(i), "l_today": round(self.flow_today[i], 1),
                      "l_total": round(self.flow_today[i], 1)} for i in range(4)],
            "valves": vals,
            "batt": {"v": 13.21 - self.idx * 0.1, "soc": 72 - self.idx * 5, "trend": "charging", "days": 24.5, "level": "ok"},
            "wifi": {"ip": "192.168.1.%d" % (50 + self.idx), "rssi": -58 - self.idx * 4, "ap": False, "ap_clients": 0},
            "mqtt": False, "alarms": [], "uptime_s": now % 100000,
        }

    async def broadcast(self, obj):
        msg = json.dumps(obj)
        for ws in list(self.clients):
            try:
                await ws.send_str(msg)
            except Exception:
                self.clients.discard(ws)

    async def event(self, e):
        e.update({"v": 1, "ts": int(time.time()), "node": self.state()["node"]})
        self.events = (self.events + [e])[-200:]
        await self.broadcast({"type": "event", "data": e})

    # ------------------------------------------------------------ commands
    async def dispatch(self, req):
        cmd = req.get("cmd", "")
        resp = {"v": 1, "cmd": cmd, "ok": True}
        if "id" in req:
            resp["id"] = req["id"]
        data = {}
        v = req.get("valve", 0) - 1
        cfg = self.cfg
        if cmd == "get_state":
            data = self.state()
        elif cmd == "device_info":
            ag = cfg["broker"] == "agrisense"
            data = {"fw": "sim", "chip": "ESP32-D0WD-V3", "mac": "20:9B:A9:00:00:%02X" % self.idx, "flash_mb": 4,
                    "fs_used": 40960, "fs_total": 1441792, "heap": 180000, "uptime_s": 1234,
                    "host": self.ssid.lower(),
                    "base": "agrisense/WM2/%s/" % cfg["ag_id"] if ag else "girish/wm2/%s/%s/" % (cfg["zone"], cfg["node"]),
                    "broker": cfg["broker"], "broker_host": cfg["ag_host"] if ag else cfg["mqtt_host"],
                    "device_id": cfg["ag_id"], "mqtt": False, "ip": "192.168.1.%d" % (50 + self.idx),
                    "ap": True, "ap_ssid": self.ssid}
        elif cmd == "get_config":
            data = cfg
        elif cmd == "mqtt_config":
            ag = req.get("broker", cfg["broker"]) != "girish"
            cfg["broker"] = "agrisense" if ag else "girish"
            pre = "ag_" if ag else "mqtt_"
            for k in ("host", "port", "user"):
                if k in req:
                    cfg[pre + k] = req[k]
            for k in ("ag_id", "farm", "zone", "node"):
                if k in req:
                    cfg[k] = req[k]
            data = {"broker": cfg["broker"]}
        elif cmd in ("set_config", "set_report", "set_options", "set_battery", "set_power", "wifi_config",
                     "set_flow", "set_valve_hw", "rtc_sync", "reboot", "clear_history", "softap_on"):
            cfg.update(req.get("config", {}))
        elif cmd == "valve_open":
            if v not in (0, 1):
                resp.update(ok=False, err="valve must be 1 or 2")
            else:
                self.valves[v].update(manual=True, left=int(req.get("minutes", 0) * 60))
                data["state"] = "opening" if self.water or self.bypass else "waiting_for_water"
        elif cmd == "valve_close":
            self.valves[v]["manual"] = False
            self.valves[v]["force_close"] = True
        elif cmd == "set_mode":
            self.valves[v]["mode"] = req.get("mode", "program")
        elif cmd == "set_program":
            self.valves[v]["program"] = req.get("program", {})
            data["program"] = self.valves[v]["program"]
        elif cmd == "get_program":
            data["programs"] = [{"valve": i + 1, "mode": x["mode"], "program": x["program"]} for i, x in enumerate(self.valves)]
        elif cmd == "bypass":
            self.bypass = bool(req.get("on", True))
            self.bypass_until = int(time.time()) + int(req.get("minutes", 60)) * 60
            await self.event({"event": "bypass", "on": self.bypass, "src": req.get("src", "app")})
        elif cmd == "get_events":
            data["events"] = self.events[-int(req.get("limit", 50)):]
        elif cmd == "get_days":
            today = local_day(int(time.time()))
            data["days"] = [{"event": "day_result", "valve": vv + 1, "day": today - d,
                             "date": time.strftime("%Y-%m-%d", time.gmtime((today - d) * 86400)),
                             "planned_min": 45, "run_min": 45 if d % 3 else 30, "litres": [230.5, 228.1],
                             "normal_litres": [234, 234], "water_min": 300, "status": "done" if d % 3 else "partial",
                             "alarms": []} for d in range(int(req.get("days", 7)), 0, -1) for vv in range(2)
                            if DAYS[(today - d + 3) % 7] in self.valves[vv]["program"].get("days", [])]
        elif cmd == "get_history":
            data = self.history(req)
        elif cmd == "wifi_scan":
            data["networks"] = [{"ssid": "FarmWiFi", "rssi": -52, "open": False}]
        elif cmd == "diag_inputs":
            data["inputs"] = [{"name": n, "pin_falls": 0, "low_pct": 0} for n in ("flow1", "flow2", "flow3", "flow4", "ps")]
        else:
            resp.update(ok=False, err="unknown command")
        if data:
            resp["data"] = data
        return resp

    def history(self, req):
        f, t = int(req.get("from", time.time() - 86400)), int(req.get("to", time.time()))
        res = req.get("res") or ("raw" if t - f <= 2 * 86400 else "hour" if t - f <= 31 * 86400 else "day")
        step = {"raw": 60, "hour": 3600, "day": 86400}[res]
        start = f - (f + TZ) % step if step >= 3600 else f
        pts, tot, om = [], [0.0] * 4, [0, 0]
        for ts in list(range(start, t, step))[:1500]:
            day = local_day(ts)
            wd = DAYS[(day + 3) % 7]
            hr = ((ts + TZ) // 3600) % 24
            on = [wd in self.valves[x]["program"].get("days", []) for x in range(2)]
            if res == "raw":
                a, b = on[0] and hr in (6, 7), on[1] and hr == 8
                pts.append([ts, 10.4 if a else 0, 10.2 if a else 0, 9.9 if b else 0, 10.1 if b else 0, 0, 13.1])
                for i, x in enumerate([a, a, b, b]):
                    tot[i] += 10.3 / 60 * 60 if x else 0
                om[0] += 1 if a else 0
                om[1] += 1 if b else 0
            else:
                if res == "hour":
                    m = [45 if on[0] and hr == 6 else 0, 30 if on[1] and hr == 8 else 0]
                else:
                    m = [self.valves[x]["program"].get("minutes", 0) if on[x] else 0 for x in range(2)]
                l = [round(m[0] * 10.4, 1), round(m[0] * 10.2, 1), round(m[1] * 9.9, 1), round(m[1] * 10.1, 1)]
                pts.append([ts] + l + [m[0], m[1], max(m), 12.9])
                for i in range(4):
                    tot[i] += l[i]
                om[0] += m[0]
                om[1] += m[1]
        return {"from": f, "to": t, "res": res, "points": pts,
                "columns": "ts,lpm1,lpm2,lpm3,lpm4,flags,batt" if res == "raw" else "ts,l1,l2,l3,l4,v1_min,v2_min,water_min,min_batt",
                "totals": {"litres": [round(x, 1) for x in tot],
                           "valve_litres": [round(tot[0] + tot[1], 1), round(tot[2] + tot[3], 1)],
                           "valve_open_min": om, "water_min": max(om), "min_batt": 12.95, "records": len(pts)},
                "truncated": False}

    # ------------------------------------------------------------ http
    async def ws_handler(self, request):
        ws = web.WebSocketResponse()
        await ws.prepare(request)
        self.clients.add(ws)
        await ws.send_str(json.dumps({"type": "state", "data": self.state()}))
        async for msg in ws:
            if msg.type == WSMsgType.TEXT:
                try:
                    await ws.send_str(json.dumps(await self.dispatch(json.loads(msg.data))))
                except Exception as e:
                    await ws.send_str(json.dumps({"ok": False, "err": str(e)}))
        self.clients.discard(ws)
        return ws

    async def api_cmd(self, request):
        return web.json_response(await self.dispatch(await request.json()))

    async def sim(self, request):
        if "water" in request.query:
            w = request.query["water"] == "1"
            if w != self.water:
                self.water = w
                await self.event({"event": "water", "present": w})
        return web.json_response({"water": self.water})

    async def tick(self):
        now = int(time.time())
        if self.bypass and now >= self.bypass_until:
            self.bypass = False
        flowing = self.water or self.bypass
        for i, v in enumerate(self.valves):
            want = v["manual"] or (v["open"] and not v.pop("force_close", False))
            if want and not v["open"] and flowing:
                v["open"] = True
                v["run"] = {"since": now, "sec": 0, "l": 0.0}
                await self.event({"event": "valve", "valve": i + 1, "open": True, "reason": "manual"})
            if not want and v["open"]:
                v["open"] = False
                r = v["run"] or {"since": now, "sec": 0, "l": 0}
                v["last"] = {"start": r["since"], "end": now, "min": r["sec"] // 60, "l": round(r["l"], 1)}
                v["run"] = None
                await self.event({"event": "valve", "valve": i + 1, "open": False, "reason": "manual"})
            if v["open"] and flowing:
                v["run"]["sec"] += 1
                v["run"]["l"] += 20.6 / 60
                if v["manual"] and v["left"]:
                    v["left"] -= 1
                    if v["left"] <= 0:
                        v["manual"] = False
                        await self.event({"event": "manual_done", "valve": i + 1})
        for i in range(4):
            self.flow_today[i] += self.lpm(i) / 60
        for i in range(2):
            self.valves[i]["litres"] = self.flow_today[2 * i] + self.flow_today[2 * i + 1]
        if now % 2 == 0:
            await self.broadcast({"type": "state", "data": self.state()})

    async def start(self):
        app = web.Application()
        app.add_routes([web.get("/", lambda r: web.Response(text=PAGE, content_type="text/html")),
                        web.get("/ws", self.ws_handler), web.post("/api/cmd", self.api_cmd),
                        web.get("/api/state", lambda r: web.json_response(self.state())),
                        web.get("/sim", self.sim)])
        runner = web.AppRunner(app)
        await runner.setup()
        await web.TCPSite(runner, "0.0.0.0", self.port).start()
        print(f"{self.dev_id} on http://localhost:{self.port}  (water: /sim?water=1)")


async def main(port, count):
    nodes = [Node(i, port + i) for i in range(count)]
    for n in nodes:
        await n.start()
    while True:
        await asyncio.sleep(1)
        for n in nodes:
            await n.tick()


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=8080)
    ap.add_argument("--count", type=int, default=1)
    a = ap.parse_args()
    asyncio.run(main(a.port, a.count))
