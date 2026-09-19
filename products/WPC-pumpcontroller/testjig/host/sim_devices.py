"""Software model of the jig + DUT serial consoles, so wpc_test.py can be run
(and its failure detection proven) without any hardware:

    python wpc_test.py master --simulate
    python wpc_test.py pump   --simulate --sim-fault norelay

It models just enough behaviour to be a faithful stand-in for the firmware's
serial protocol -- it is NOT a re-implementation of the firmware logic beyond
what the tests observe. Faults (comma separated, via --sim-fault):
  master: noinput (INPUTS all 0)   nolevel (level logic ignored)   nojoin
          lowrssi                  noadc   (ADC telemetry never arrives)
  pump:   norelay (contact never closes)   crosstalk (IN4 follows AOUT1)
          nofailsafe               lowrssi   nojoin
"""
import time

_worlds = {}
BOOT_S = 0.8


class World:
    def __init__(self, dut_type, faults):
        self.t = dut_type
        self.f = set(x for x in faults.split(",") if x)
        # jig side
        self.relays = 0
        self.aout = {1: 0, 2: 0}
        self.emu_adc = (0, 0)
        self.noack = False
        self.noack_since = 0.0
        self.jig_started = False
        self.jig_master = None
        self.pump_cmds = 0
        self.pump_last_cmd = -1
        self.last_hb = time.time()
        # master DUT side
        self.m_known = False
        self.m_mask = 0
        self.m_override = "auto"
        self.m_testmode = False
        self.m_saved_mask = 0
        self.m_saved_known = False
        # pump DUT side
        self.p_relay = 0
        self.p_master = "68A99B20"
        self.p_joined = False
        self.p_failsafes = 0
        self.p_last_cmd_t = 0.0
        self.p_testmode = False

    # ------- master DUT derived state
    def raw_inputs(self):
        if "noinput" in self.f:
            return [0, 0, 0, 0]
        return [(self.relays >> i) & 1 for i in range(4)]

    def desired(self):
        if self.m_override == "on":
            return 1
        if self.m_override == "off":
            return 0
        if "nolevel" in self.f:
            return 1
        raw = self.raw_inputs()
        return 1 if any((self.m_mask >> i) & 1 and raw[i] == 0 for i in range(3)) else 0

    def bump(self):
        """Something that changes the commanded state happened -> the Master sends a fresh command."""
        if self.jig_started and self.m_known:
            self.pump_cmds += 1
            self.pump_last_cmd = self.desired()

    def heartbeat(self):
        if self.jig_started and self.m_known and time.time() - self.last_hb > 1.0:
            self.last_hb = time.time()
            self.pump_cmds += 1
            self.pump_last_cmd = self.desired()

    def m_online(self):
        return not (self.noack and time.time() - self.noack_since > 1.5)

    # ------- pump DUT derived state
    def mv(self, ch):
        duty = self.aout[ch]
        if "crosstalk" in self.f and ch == 2:
            duty = self.aout[1]
        return min(1100.0, 420.0 + 8.0 * duty)

    def p_check_failsafe(self):
        limit = 8.0 if self.p_testmode else 60.0
        if (self.p_joined and self.p_relay and "nofailsafe" not in self.f
                and time.time() - self.p_last_cmd_t > limit):
            self.p_relay = 0
            self.p_failsafes += 1
            self.p_joined = False
            self.p_rejoin_at = time.time() + 1.0
        if not self.p_joined and self.jig_started and self.p_master == self.jig_master and \
                "nojoin" not in self.f and time.time() >= getattr(self, "p_rejoin_at", 0):
            self.p_joined = True
            self.p_last_cmd_t = time.time()


class SimPort:
    def __init__(self, world, role):
        self.w = world
        self.role = role
        self.q = []
        self.boot_until = 0.0

    # pyserial-ish API
    def write(self, data):
        for line in data.decode().splitlines():
            self._handle(line.strip())

    def reset_input_buffer(self):
        self.q.clear()

    def readline(self):
        if self.q:
            return (self.q.pop(0) + "\n").encode()
        time.sleep(0.02)
        return b""

    def _r(self, tag, text):
        self.q.append(f"@{tag} {text}")

    def _ok(self, text):
        self._r("OK", text)

    def _err(self, text):
        self._r("ERR", text)

    def _handle(self, line):
        if time.time() < self.boot_until:
            return                                      # board still booting: no answer
        self.q.append("[LOG] unrelated firmware chatter that must be ignored")
        cmd, _, args = line.partition(" ")
        cmd = cmd.upper()
        (self._jig if self.role == "jig" else self._dut)(cmd, args.strip())

    # ------------------------------------------------------------------ jig
    def _jig(self, cmd, args):
        w = self.w
        w.heartbeat()
        if cmd == "ID":
            self._ok("board=WPC-JIG fw=0.1.0 mac=AABBCCDD0001")
        elif cmd == "RESET":
            w.relays = 0
            w.aout = {1: 0, 2: 0}
            w.jig_started = False
            self._ok("jig idle")
        elif cmd == "RELAY":
            n, v = (int(x) for x in args.split())
            w.relays = (w.relays | (1 << (n - 1))) if v else (w.relays & ~(1 << (n - 1)))
            w.bump()
            self._ok(f"relays={w.relays}")
        elif cmd == "RELAYS":
            w.relays = int(args) & 0x3F
            w.bump()
            self._ok(f"relays={w.relays}")
        elif cmd == "SENSE":
            if w.t == "pump":
                w.p_check_failsafe()
            closed = w.p_relay and "norelay" not in w.f
            self._ok(f"contact={1 if closed else 0}")
        elif cmd == "AOUT":
            ch, d = (int(x) for x in args.split())
            w.aout[ch] = d
            self._ok(f"aout{ch}={d}")
        elif cmd == "PUMPEMU":
            sub, _, rest = args.partition(" ")
            sub = sub.upper()
            if sub == "START":
                w.jig_started = True
                w.jig_master = rest.split()[0]
                w.pump_cmds = 0
                w.pump_last_cmd = -1
                if "nojoin" not in w.f:
                    w.m_known = w.m_known or False
                self._ok("pumpemu")
            elif sub == "STATUS":
                joined = w.jig_started and "nojoin" not in w.f
                if joined and not w.m_known:
                    w.m_known = True            # the Master accepts the join
                    w.bump()
                rssi = -120 if "lowrssi" in w.f else -55
                self._ok(f"mode=pumpemu joined={1 if joined else 0} slot=0 cmds={w.pump_cmds} acks={w.pump_cmds} "
                         f"lastCmd={w.pump_last_cmd} rssi={rssi} snr=9.5 badCrc=0 foreign=0")
            elif sub == "ADC":
                a, b = rest.split()
                w.emu_adc = (int(a), int(b))
                self._ok("adc set")
            elif sub == "NOACK":
                w.noack = rest.strip() == "1"
                w.noack_since = time.time()
                self._ok(f"noack={int(w.noack)}")
            else:
                self._ok("stopped")
        elif cmd == "MASTEREMU":
            sub, _, rest = args.partition(" ")
            sub = sub.upper()
            if sub == "START":
                w.jig_started = True
                w.jig_master = rest.strip()
                self._ok("masteremu")
            elif sub == "STATUS":
                w.p_check_failsafe()
                self._ok(f"mode=masteremu joined={1 if w.p_joined else 0} pumpId=1234 badCrc=0 foreign=0")
            elif sub == "CMD":
                w.p_check_failsafe()
                if not w.p_joined:
                    self._err("not joined")
                    return
                state = int(rest.split()[0])
                w.p_relay = state
                w.p_last_cmd_t = time.time()
                rssi = -120 if "lowrssi" in w.f else -48
                self._ok(f"ack=1 seq=1 relay={state} in1dig=0 in4dig=0 adc1={int(w.mv(1) * 4095 / 1100)} "
                         f"adc4={int(w.mv(2) * 4095 / 1100)} rssi={rssi} snr=10.0 attempt=1")
            else:
                self._ok("stopped")
        elif cmd in ("WIFISCAN", "WIFICONNECT", "WIFIDISCONNECT"):
            self._ok("found=1 rssi=-40 ip=192.168.4.2 gw=192.168.4.1")
        elif cmd == "HTTPGET":
            self._r("DATA", '{"masterId":"0x68A99B20","fw":"0.4.0"}')
            self._ok("status=200 len=40")
        else:
            self._err("unknown")

    # ------------------------------------------------------------------ DUT
    def _dut(self, cmd, args):
        w = self.w
        if cmd == "ID":
            if w.t == "master":
                self._ok("board=WPC-MASTER fw=0.4.0 mac=112233445566 masterId=68A99B20 ap=WPC-Master-68A99B20")
            else:
                self._ok("board=WPC-PUMP fw=0.4.0 mac=665544332211 pumpId=2368 ap=WPC-Pump-2368 targetMaster=" + w.p_master)
            return
        if cmd in ("FACTORYRESET", "REBOOT"):
            self._ok("rebooting")
            self.boot_until = time.time() + BOOT_S
            if cmd == "FACTORYRESET":
                if w.t == "master":
                    w.m_known = False
                    w.m_mask = 0
                    w.m_saved_known = False
                    w.m_saved_mask = 0
                    w.m_override = "auto"
                    w.m_testmode = False
                else:
                    w.p_master = "68A99B20"
                    w.p_joined = False
                    w.p_relay = 0
                    w.p_testmode = False
            return
        if cmd == "TESTMODE":
            if w.t == "master":
                w.m_testmode = args == "1"
            else:
                w.p_testmode = args == "1"
            self._ok(f"testMode={args}")
            return
        (self._dut_master if w.t == "master" else self._dut_pump)(cmd, args)

    def _dut_master(self, cmd, args):
        w = self.w
        w.heartbeat()
        if cmd == "INPUTS":
            raw = w.raw_inputs()
            self._ok("raw=" + ",".join(map(str, raw)) + " state=" + ",".join(map(str, raw)))
        elif cmd == "PUMPS":
            n = 0
            if w.m_known:
                n = 1
                a1, a4 = (0, 0) if "noadc" in w.f else w.emu_adc
                self._r("DATA", f"slot=0 pumpId=9999 online={1 if w.m_online() else 0} relay={w.desired()} "
                                f"desired={w.desired()} levels={w.m_mask} adc1={a1} adc4={a4} "
                                f"override={0 if w.m_override == 'auto' else 1}")
            self._ok(f"count={n}")
        elif cmd == "ASSIGN":
            slot, mask = (int(x) for x in args.split())
            w.m_mask = mask
            w.m_saved_mask = mask
            w.bump()
            self._ok(f"slot={slot} mask={mask}")
        elif cmd == "OVERRIDE":
            _, mode = args.split()
            w.m_override = mode.lower()
            w.bump()
            self._ok("ok")
        elif cmd == "FORGETALL":
            w.m_known = False
            self._ok("all pumps forgotten")
        elif cmd == "WIFI":
            self._ok("wifi saved")
        elif cmd == "WIFISTAT":
            self._ok("configured=1 connected=1 mqtt=1")
        else:
            self._err("unknown")

    def _dut_pump(self, cmd, args):
        w = self.w
        w.p_check_failsafe()
        if cmd == "RELAY":
            w.p_relay = int(args)
            self._ok(f"relay={w.p_relay}")
        elif cmd == "ADC":
            m1, m4 = w.mv(1), w.mv(2)
            self._ok(f"in1raw={int(m1 * 4095 / 1100)} in1mv={int(m1)} in4raw={int(m4 * 4095 / 1100)} in4mv={int(m4)} "
                     "in1dig=0 in4dig=0")
        elif cmd == "MASTER":
            w.p_master = args.strip().upper()
            w.p_joined = False
            w.p_rejoin_at = time.time() + 1.0
            self._ok(f"master={w.p_master}")
        elif cmd == "STATE":
            self._ok(f"joined={1 if w.p_joined else 0} slot=0 relay={w.p_relay} master={w.p_master} "
                     f"failsafes={w.p_failsafes} cmds=1")
        elif cmd == "LEDTEST":
            self._ok("ledtest")
        else:
            self._err("unknown")


def make_sim_port(path):
    _, role, dut_type, faults = (path.split(":") + [""])[:4]
    key = (dut_type, faults)
    if key not in _worlds:
        _worlds[key] = World(dut_type, faults)
    return SimPort(_worlds[key], role)
