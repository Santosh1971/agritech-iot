#!/usr/bin/env python3
"""Leads tracker client for Claude: read and update /dashboard/testing/leads.

Token: LEADS_TOKEN env var or ~/.config/agrisense/leads_token (same value as
LEADS_TOKEN in the server's .env; never commit it).

  leads.py list [--status NEW]          show leads with their latest notes
  leads.py add leads.json               add/update leads (a list of objects)
  leads.py note <id> "text"             add a note as Claude
  leads.py status <id> CONTACTED        change status
"""
import argparse, json, os, ssl, sys, urllib.error, urllib.request

BASE = os.environ.get("LEADS_URL", "https://agrisenseandcontrol.in")


def tls():
    ctx = ssl.create_default_context()
    if not ctx.cert_store_stats().get("x509_ca"):
        for f in ("/etc/ssl/cert.pem", "/etc/ssl/certs/ca-certificates.crt"):
            if os.path.exists(f):
                ctx.load_verify_locations(f)
                break
    return ctx


def token():
    t = os.environ.get("LEADS_TOKEN")
    p = os.path.expanduser("~/.config/agrisense/leads_token")
    if not t and os.path.exists(p):
        t = open(p).read().strip()
    if not t:
        sys.exit("No token: set LEADS_TOKEN or write ~/.config/agrisense/leads_token")
    return t


def call(method, path, body=None):
    req = urllib.request.Request(BASE + path, method=method, data=None if body is None else json.dumps(body).encode(),
                                 headers={"Authorization": "Bearer " + token(), "Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(req, context=tls(), timeout=30) as r:
            return json.load(r)
    except urllib.error.HTTPError as e:
        sys.exit(f"{e.code}: {e.read().decode()[:300]}")


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    l = sub.add_parser("list"); l.add_argument("--status")
    a = sub.add_parser("add"); a.add_argument("file")
    n = sub.add_parser("note"); n.add_argument("id"); n.add_argument("text")
    s = sub.add_parser("status"); s.add_argument("id"); s.add_argument("status")
    args = ap.parse_args()

    if args.cmd == "list":
        for x in call("GET", "/api/leads")["leads"]:
            if args.status and x["status"] != args.status:
                continue
            print(f'{x["id"]}  {x["status"]:<9} {x["heat"]:<4} {x["kind"]:<9} @{x["handle"]}: {x["said"][:70]}')
            for nt in x["notes"][-3:]:
                print(f'      - {nt["author"]}: {nt["text"][:100]}')
    elif args.cmd == "add":
        data = json.load(open(args.file))
        print(call("POST", "/api/leads", {"leads": data if isinstance(data, list) else [data]}))
    elif args.cmd == "note":
        print(call("POST", f"/api/leads/{args.id}/notes", {"text": args.text})["note"]["id"])
    elif args.cmd == "status":
        print(call("PATCH", f"/api/leads/{args.id}", {"status": args.status})["lead"]["status"])


if __name__ == "__main__":
    main()
