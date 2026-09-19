#!/usr/bin/env python3
"""Local web UI for the FG1 guided production tester -- same underlying
production_tester package as cli.py (login, build picker, flash + full
SoftAP/MQTT test sequence), just with a browser front end instead of
terminal prompts: a live step checklist, a scrolling log console, and a
run-history table with CSV export. Single bench station, single test at
a time -- same as the CLI, just friendlier to watch.

Run with: python3 web_ui.py
Then open http://localhost:8420 in a browser (works the same on Mac and
Windows -- nothing platform-specific here, unlike a native GUI toolkit).
"""
from __future__ import annotations

import contextlib
import json
import re
import sys
import threading
import time
from pathlib import Path
from typing import Optional

from flask import Flask, Response, jsonify, request, send_from_directory

sys.path.insert(0, str(Path(__file__).resolve().parent))

from production_tester import cli as ptcli
from production_tester.api_client import ApiClient, ApiError, Build
from production_tester.config import BenchConfig
from production_tester.report import PRODUCTION_TEST_STEPS, REPORT_DIR, STEP_LABELS, export_csv

ANSI_RE = re.compile(r"\x1b\[[0-9;]*m")
WEB_DIR = Path(__file__).resolve().parent / "web"
PORT = 8420


def _strip_ansi(s: str) -> str:
    return ANSI_RE.sub("", s)


class QueueWriter:
    """A file-like object that splits writes into lines and hands each one
    to [on_line] -- used to redirect_stdout() during a run so the same
    print() calls cli.py already makes show up in the browser's log
    console, instead of duplicating every print site with a second
    "also emit to the UI" call."""

    def __init__(self, on_line):
        self._on_line = on_line
        self._buf = ""

    def write(self, s: str) -> int:
        self._buf += s
        while True:
            m = re.search(r"[\r\n]", self._buf)
            if not m:
                break
            line, self._buf = self._buf[:m.start()], self._buf[m.end():]
            if line:
                self._on_line(_strip_ansi(line))
        return len(s)

    def flush(self) -> None:
        pass


class RunManager:
    """Owns the one login session, the one bench config, the currently
    selected build, and at most one in-flight test run -- mirrors cli.py's
    single-loop design (plug in a unit, run it, repeat), just driven by
    HTTP requests instead of input())."""

    def __init__(self):
        self.api = ApiClient()
        self.config = BenchConfig.load()
        self.selected_build: Optional[Build] = None
        self.lock = threading.Lock()
        self.running = False
        self.events: list[dict] = []
        self.last_result: Optional[dict] = None
        self.stop_event: Optional[threading.Event] = None

    def status(self) -> dict:
        return {
            "logged_in": self.api.is_logged_in,
            "config": vars(self.config).copy(),
            "selected_build": _build_json(self.selected_build) if self.selected_build else None,
            "running": self.running,
            "step_order": PRODUCTION_TEST_STEPS,
            "step_labels": STEP_LABELS,
            "last_result": self.last_result,
        }

    def start_run(self, skip_flash: bool = False, device_id: Optional[str] = None) -> None:
        with self.lock:
            if self.running:
                raise ApiError("A test is already running.")
            if skip_flash:
                if not device_id:
                    raise ApiError("Skipping flash needs a device ID -- enter the unit's device ID.")
            elif self.selected_build is None:
                raise ApiError("No build selected -- pick a product and build first.")
            self.running = True
            self.events = []
            self.stop_event = threading.Event()
            # Skip-flash mode doesn't need a build picked at all -- fall
            # back to a placeholder just so build_report()'s build_label
            # has something to format instead of crashing on None.
            build = self.selected_build or Build(
                id="", product="(none)", version="skip-flash", variant="debug", size_bytes=0, notes=None,
            )

        def emit(event: dict) -> None:
            self.events.append(event)

        def on_step(name: str, passed: bool, detail: str) -> None:
            emit({"type": "step", "name": name, "passed": passed, "detail": detail})

        def on_jig_line(text: str) -> None:
            emit({"type": "jig_line", "text": _strip_ansi(text)})

        def on_dut_line(text: str) -> None:
            emit({"type": "dut_line", "text": _strip_ansi(text)})

        def worker() -> None:
            writer = QueueWriter(lambda line: emit({"type": "log", "text": line}))
            try:
                with contextlib.redirect_stdout(writer):
                    run = ptcli.run_one_unit(
                        self.api, self.config, build, on_step=on_step,
                        skip_flash=skip_flash, manual_device_id=device_id,
                        on_jig_line=on_jig_line, on_dut_line=on_dut_line,
                        stop_event=self.stop_event,
                    )
                report = run.build_report()
                self.last_result = report.to_json()
                if run.stopped:
                    emit({"type": "stopped"})
                else:
                    emit({"type": "done", "overall_passed": report.overall_passed})
            except Exception as e:  # noqa: BLE001 -- surface any failure to the browser instead of losing it
                emit({"type": "error", "message": str(e)})
            finally:
                with self.lock:
                    self.running = False
                    self.stop_event = None

        threading.Thread(target=worker, daemon=True).start()

    def stop_run(self) -> None:
        with self.lock:
            if self.stop_event is not None:
                self.stop_event.set()


def _build_json(b: Build) -> dict:
    return {
        "id": b.id, "product": b.product, "version": b.version,
        "variant": b.variant, "size_bytes": b.size_bytes, "notes": b.notes,
        "label": f"{b.product} {b.version} ({b.variant})",
    }


manager = RunManager()
app = Flask(__name__)


@app.route("/")
def index():
    return send_from_directory(WEB_DIR, "index.html")


@app.route("/api/state")
def api_state():
    return jsonify(manager.status())


@app.route("/api/login/request-otp", methods=["POST"])
def api_request_otp():
    email = request.json.get("email", "").strip()
    try:
        manager.api.request_otp(email)
        return jsonify({"ok": True})
    except ApiError as e:
        return jsonify({"ok": False, "error": str(e)}), 400


@app.route("/api/login/verify-otp", methods=["POST"])
def api_verify_otp():
    data = request.json
    try:
        manager.api.verify_otp(data.get("email", "").strip(), data.get("code", "").strip())
        return jsonify({"ok": True})
    except ApiError as e:
        return jsonify({"ok": False, "error": str(e)}), 400


@app.route("/api/products")
def api_products():
    try:
        grant = manager.api.fetch_grant()
        return jsonify({"products": grant.products})
    except ApiError as e:
        return jsonify({"error": str(e)}), 400


@app.route("/api/builds")
def api_builds():
    product = request.args.get("product", "")
    try:
        builds = manager.api.fetch_builds(product)
        return jsonify({"builds": [_build_json(b) for b in builds]})
    except ApiError as e:
        return jsonify({"error": str(e)}), 400


@app.route("/api/select-build", methods=["POST"])
def api_select_build():
    data = request.json
    product = data.get("product", "")
    build_id = data.get("build_id", "")
    try:
        builds = manager.api.fetch_builds(product)
    except ApiError as e:
        return jsonify({"ok": False, "error": str(e)}), 400
    match = next((b for b in builds if b.id == build_id), None)
    if match is None:
        return jsonify({"ok": False, "error": "Build not found -- refresh the list and try again."}), 404
    manager.selected_build = match
    return jsonify({"ok": True, "build": _build_json(match)})


@app.route("/api/settings", methods=["POST"])
def api_settings():
    data = request.json
    cfg = manager.config
    for field in ("office_wifi_ssid", "office_wifi_password", "operator_name", "station_name"):
        if field in data:
            setattr(cfg, field, str(data[field]))
    for field in ("expected_calibration_ppl", "flow_test_pulse_count"):
        if field in data and data[field]:
            setattr(cfg, field, int(data[field]))
    cfg.save()
    return jsonify({"ok": True, "config": vars(cfg)})


@app.route("/api/run/start", methods=["POST"])
def api_run_start():
    data = request.json or {}
    try:
        manager.start_run(
            skip_flash=bool(data.get("skip_flash")),
            device_id=(data.get("device_id") or "").strip() or None,
        )
        return jsonify({"ok": True})
    except ApiError as e:
        return jsonify({"ok": False, "error": str(e)}), 400


@app.route("/api/run/stop", methods=["POST"])
def api_run_stop():
    manager.stop_run()
    return jsonify({"ok": True})


@app.route("/api/run/stream")
def api_run_stream():
    def gen():
        idx = 0
        # Loop past the point the run actually finished too, briefly --
        # a client that connects a beat late (page load, EventSource
        # reconnect after a network blip) should still see the final
        # step/done events instead of an empty stream.
        idle_after_done = 0
        while True:
            events = manager.events
            while idx < len(events):
                yield f"data: {json.dumps(events[idx])}\n\n"
                idx += 1
            if not manager.running:
                idle_after_done += 1
                if idle_after_done > 20:  # ~2s of no new events after completion
                    break
            time.sleep(0.1)

    return Response(gen(), mimetype="text/event-stream", headers={
        "Cache-Control": "no-cache", "X-Accel-Buffering": "no",
    })


@app.route("/api/history")
def api_history():
    rows = []
    for f in sorted(REPORT_DIR.glob("*.json"), reverse=True)[:100]:
        try:
            rows.append(json.loads(f.read_text()))
        except (json.JSONDecodeError, OSError):
            continue
    return jsonify({"reports": rows})


@app.route("/api/history/export.csv")
def api_history_export():
    path = export_csv()
    return send_from_directory(path.parent, path.name, as_attachment=True)


if __name__ == "__main__":
    print(f"FG1 production tester -- open http://localhost:{PORT} in a browser.")
    app.run(host="127.0.0.1", port=PORT, threaded=True)
