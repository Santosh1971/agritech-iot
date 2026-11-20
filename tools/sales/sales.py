#!/usr/bin/env python3
"""Sales register client for Claude: read and import /dashboard/testing/sales.

Token: SALES_TOKEN env var or ~/.config/agrisense/sales_token (same value as
SALES_TOKEN in the server's .env; never commit it).

  sales.py list [--q 0DA8]          sales and their devices (filter by text)
  sales.py add sales.json           create sales from a JSON list
  sales.py import-xlsx file.xlsx [--dry-run]
        Avinash's ASAC_Sales_Record.xlsx: one sheet per product, columns
        Sr. No. | Device Name | Dev. ID | Seller Name | Location | Dispatch Date |
        Payment Status | Invoice Name; merged cells are filled down. Rows with
        the same seller/location/date/payment/invoice become one sale.
"""
import argparse, datetime, json, os, ssl, sys, urllib.error, urllib.request

BASE = os.environ.get("SALES_URL", "https://agrisenseandcontrol.in")


def tls():
    ctx = ssl.create_default_context()
    if not ctx.cert_store_stats().get("x509_ca"):
        for f in ("/etc/ssl/cert.pem", "/etc/ssl/certs/ca-certificates.crt"):
            if os.path.exists(f):
                ctx.load_verify_locations(f)
                break
    return ctx


def token():
    t = os.environ.get("SALES_TOKEN")
    p = os.path.expanduser("~/.config/agrisense/sales_token")
    if not t and os.path.exists(p):
        t = open(p).read().strip()
    if not t:
        sys.exit("No token: set SALES_TOKEN or write ~/.config/agrisense/sales_token")
    return t


def call(method, path, body=None):
    req = urllib.request.Request(BASE + path, method=method, data=None if body is None else json.dumps(body).encode(),
                                 headers={"Authorization": "Bearer " + token(), "Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(req, context=tls(), timeout=60) as r:
            return json.load(r)
    except urllib.error.HTTPError as e:
        sys.exit(f"{e.code}: {e.read().decode()[:400]}")


PRODUCT_BY_SHEET = {"FG1": "FG1", "FG1-TH": "FG1-TH", "WM2": "WM2", "WM1": "WM1-MINI", "TH2": "TH2"}
PAY = {"paid": "PAID", "partial": "PARTIAL", "pending": "PENDING", "unpaid": "PENDING", "due": "PENDING"}


def product_for(sheet, device_name):
    n = (device_name or sheet or "").upper().replace(" ", "")
    if "WPC" in n or sheet.upper() == "WPC":
        if "PN" in n or "PUMP" in n or "NODE" in n:
            return "WPC-PN"
        if "GW" in n or "GATEWAY" in n or "MASTER" in n:
            return "WPC-GW"
        return "WPC-PN"
    for k, v in PRODUCT_BY_SHEET.items():
        if n.startswith(k.replace("-", "")) or sheet.upper() == k:
            return v
    return "OTHER"


def read_xlsx(path):
    import openpyxl
    wb = openpyxl.load_workbook(path, data_only=True)
    sales = {}
    for ws in wb.worksheets:
        # fill merged ranges with their top-left value
        grid = {}
        for row in ws.iter_rows():
            for c in row:
                grid[(c.row, c.column)] = c.value
        for rng in ws.merged_cells.ranges:
            v = ws.cell(rng.min_row, rng.min_col).value
            for r in range(rng.min_row, rng.max_row + 1):
                for col in range(rng.min_col, rng.max_col + 1):
                    grid[(r, col)] = v
        head_row, cols = None, {}
        for r in range(1, min(ws.max_row, 10) + 1):
            names = {str(grid.get((r, c)) or "").strip().lower(): c for c in range(1, ws.max_column + 1)}
            if any("dev" in k and "id" in k for k in names):
                head_row = r
                for k, c in names.items():
                    if "dev" in k and "id" in k: cols["id"] = c
                    elif "device" in k: cols["name"] = c
                    elif "seller" in k or "customer" in k or "buyer" in k: cols["customer"] = c
                    elif "location" in k: cols["location"] = c
                    elif "dispatch" in k or "date" in k: cols["date"] = c
                    elif "payment" in k: cols["pay"] = c
                    elif "invoice" in k: cols["invoice"] = c
                break
        if not head_row:
            continue
        for r in range(head_row + 1, ws.max_row + 1):
            dev = grid.get((r, cols["id"]))
            if dev is None or str(dev).strip() == "":
                continue
            g = lambda k: grid.get((r, cols[k])) if k in cols else None
            d = g("date")
            if isinstance(d, datetime.datetime):
                d = d.date().isoformat()
            elif isinstance(d, str) and d.strip():
                try:
                    d = datetime.datetime.strptime(d.strip(), "%d-%b-%Y").date().isoformat()
                except ValueError:
                    d = None
            else:
                d = None
            key = (str(g("customer") or "Unknown").strip(), str(g("location") or "").strip(), d, str(g("pay") or "").strip(), str(g("invoice") or "").strip())
            s = sales.setdefault(key, {"customer": key[0], "customerType": "Dealer", "location": key[1] or None, "dispatchDate": d,
                                       "paymentStatus": PAY.get(key[3].lower(), "PENDING"), "invoiceNo": key[4] or None,
                                       "notes": f"Imported from {os.path.basename(path)} ({ws.title} sheet)", "units": []})
            s["units"].append({"product": product_for(ws.title, str(g("name") or "")), "deviceId": str(dev).strip().upper(), "warrantyMonths": 12})
    return list(sales.values())


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    l = sub.add_parser("list"); l.add_argument("--q")
    a = sub.add_parser("add"); a.add_argument("file")
    x = sub.add_parser("import-xlsx"); x.add_argument("file"); x.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    if args.cmd == "list":
        r = call("GET", "/api/sales")
        q = (args.q or "").upper()
        for s in r["sales"]:
            units = [u for u in s["units"] if not q or q in u["deviceId"]] or (s["units"] if q and q in (s["customer"] + str(s["invoiceNo"])).upper() else [])
            if q and not units:
                continue
            print(f'{s["id"]}  {s["customer"]} · {s["location"] or "-"} · inv {s["invoiceNo"] or "-"} · {(s["dispatchDate"] or "")[:10]} · {s["paymentStatus"]}')
            for u in units:
                d = r["devices"].get(u["deviceId"])
                print(f'    {u["product"]:<9} {u["deviceId"]:<14} {"last seen " + (d["lastSeenAt"] or "never")[:10] if d else "not on server"}')
    elif args.cmd == "add":
        data = json.load(open(args.file))
        print(call("POST", "/api/sales", {"sales": data if isinstance(data, list) else [data]}))
    elif args.cmd == "import-xlsx":
        sales = read_xlsx(args.file)
        for s in sales:
            print(f'{s["customer"]} · {s["location"]} · {s["dispatchDate"]} · {s["paymentStatus"]} · inv {s["invoiceNo"]}: {len(s["units"])} units '
                  + ", ".join(f'{u["product"]}:{u["deviceId"]}' for u in s["units"]))
        if not args.dry_run:
            print(call("POST", "/api/sales", {"sales": sales}))


if __name__ == "__main__":
    main()
