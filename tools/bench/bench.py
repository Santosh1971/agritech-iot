#!/usr/bin/env python3
"""Remote bench: talk to a board plugged into the tester's PC.

The tester opens https://agrisenseandcontrol.in/dashboard/testing/bench in
Chrome/Edge and connects the board; this tool then reads its serial log,
sends it commands, resets it and flashes firmware through the server.

Token: BENCH_TOKEN env var, or the file ~/.config/agrisense/bench_token
(same value as BENCH_TOKEN in the server's .env; never commit it).

  bench.py status
  bench.py log [-n 50] [-f]                 last lines; -f keeps following
  bench.py send '{"cmd":"get_state"}' [-w 3] send a line, show the reply lines
  bench.py reset [-w 8]                     reset the board, show the boot log
  bench.py flash image.bin [--offset 0x0]   queue a flash, follow its progress
"""
import argparse, json, os, re, sys, time, urllib.error, urllib.request, uuid

BASE = os.environ.get("BENCH_URL", "https://agrisenseandcontrol.in")


def token():
    t = os.environ.get("BENCH_TOKEN")
    if not t:
        p = os.path.expanduser("~/.config/agrisense/bench_token")
        if os.path.exists(p):
            t = open(p).read().strip()
    if not t:
        sys.exit("No token: set BENCH_TOKEN or write it to ~/.config/agrisense/bench_token")
    return t


def req(method, path, body=None, ctype=None):
    r = urllib.request.Request(BASE + path, method=method, data=body)
    r.add_header("Authorization", "Bearer " + token())
    r.add_header("User-Agent", "agrisense-bench/1")
    if ctype:
        r.add_header("Content-Type", ctype)
    try:
        with urllib.request.urlopen(r, timeout=30) as res:
            if "/login" in res.url:
                sys.exit("Unauthorized: the server did not accept the bench token")
            return json.loads(res.read())
    except urllib.error.HTTPError as e:
        sys.exit(f"{e.code}: {e.read().decode(errors='replace')[:300]}")


def snap(after=0):
    return req("GET", f"/api/bench/cli?after={after}")


CTRL = re.compile(r"[\x00-\x08\x0b-\x1f\x7f-\x9f]")


def clean(text):
    """Board output is untrusted: show control characters as \\xNN, never send them to the terminal."""
    return CTRL.sub(lambda m: "\\x%02x" % ord(m.group()), text)


def print_lines(lines):
    for l in lines:
        print(time.strftime("%H:%M:%S", time.localtime(l["t"] / 1000)), clean(l["text"]), flush=True)


def head():
    s = snap(10**12)
    st = s["status"]
    return s, (f"page {'ONLINE' if s['online'] else 'offline'} (seen {s['lastSeenS']} s ago)"
               f", board {'connected' if st.get('connected') else 'not connected'}"
               f"{', tester ' + st['who'] if st.get('who') else ''}{', chip ' + st['chip'] if st.get('chip') else ''}"
               f"{', flash ' + st['flash']['state'] + ' ' + str(st['flash']['pct']) + '%' if st.get('flash') else ''}")


def follow(after, seconds, until=None):
    end = time.time() + seconds
    while time.time() < end:
        s = snap(after)
        print_lines(s["lines"])
        if s["lines"]:
            after = s["lines"][-1]["seq"]
        if until and until(s):
            return s, after
        time.sleep(0.7)
    return None, after


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("status")
    p = sub.add_parser("log"); p.add_argument("-n", type=int, default=50); p.add_argument("-f", action="store_true")
    p = sub.add_parser("send"); p.add_argument("line"); p.add_argument("-w", type=float, default=3)
    p = sub.add_parser("reset"); p.add_argument("-w", type=float, default=8)
    p = sub.add_parser("flash"); p.add_argument("file"); p.add_argument("--offset", default="0x0")
    p.add_argument("--timeout", type=float, default=600)
    a = ap.parse_args()

    if a.cmd == "status":
        print(head()[1])
    elif a.cmd == "log":
        s = snap(0)
        print_lines(s["lines"][-a.n:])
        if a.f:
            follow(s["nextSeq"] - 1, 10**9)
    elif a.cmd in ("send", "reset"):
        s, line = head()
        if not s["online"]:
            print("warning:", line)
        start = s["nextSeq"] - 1
        if a.cmd == "send":
            req("POST", "/api/bench/cli", json.dumps({"type": "write", "data": a.line + "\n"}).encode(), "application/json")
        else:
            req("POST", "/api/bench/cli", json.dumps({"type": "reset"}).encode(), "application/json")
        follow(start, a.w)
    elif a.cmd == "flash":
        data = open(a.file, "rb").read()
        b = uuid.uuid4().hex
        name = os.path.basename(a.file)
        body = (f"--{b}\r\nContent-Disposition: form-data; name=\"offset\"\r\n\r\n{int(a.offset, 0)}\r\n"
                f"--{b}\r\nContent-Disposition: form-data; name=\"file\"; filename=\"{name}\"\r\n"
                f"Content-Type: application/octet-stream\r\n\r\n").encode() + data + f"\r\n--{b}--\r\n".encode()
        s, line = head()
        print(line)
        cmd = req("POST", "/api/bench/cli", body, f"multipart/form-data; boundary={b}")["cmd"]
        print(f"queued {name} ({len(data)} bytes) at {a.offset}; the tester must hold BOOT, tap RESET, then click Start flash")
        last = None

        def done(s):
            nonlocal last
            f = s["status"].get("flash") or {}
            if f.get("id") != cmd["id"]:
                return False
            if (f["state"], f["pct"] // 10) != last:
                last = (f["state"], f["pct"] // 10)
                print(f"  flash {f['state']} {f['pct']}% {f.get('msg', '')}", flush=True)
            return f["state"] == "done"

        s, _ = follow(s["nextSeq"] - 1, a.timeout, done)
        sys.exit(0 if s else "flash did not finish in time")


if __name__ == "__main__":
    main()
