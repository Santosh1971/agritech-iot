"""
Talks to agrisense-webapp's NB Agri Flasher API (see
webapp/agrisense-webapp/app/api/flasher and /api/auth) -- email+OTP login
(shared with the main AgriSense dashboard), then grant/builds/download/
report. Ported line-for-line from the Android app's ApiClient.kt -- same
endpoints, same request/response shapes, just requests instead of
HttpURLConnection and a local JSON file instead of SharedPreferences for
session persistence.
"""
from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Optional

import requests

DEFAULT_BASE_URL = "https://agrisenseandcontrol.in"
SESSION_COOKIE_NAME = "agrisense_session"

# One file per operator machine, mirrors the Android app's SharedPreferences
# role: remembers the session cookie across runs so re-launching the CLI
# doesn't require logging in again every time.
CONFIG_DIR = Path.home() / ".nbagri_production_tester"
SESSION_FILE = CONFIG_DIR / "session.json"


@dataclass
class Grant:
    label: str
    products: list[str]


@dataclass
class Build:
    id: str
    product: str
    version: str
    variant: str
    size_bytes: int
    notes: Optional[str]


class ApiError(RuntimeError):
    pass


class ApiClient:
    def __init__(self, base_url: str = DEFAULT_BASE_URL):
        self.base_url = base_url.rstrip("/")
        self._session_cookie: Optional[str] = None
        self._load_session()

    # ---------- session persistence ----------

    def _load_session(self) -> None:
        if SESSION_FILE.exists():
            try:
                data = json.loads(SESSION_FILE.read_text())
                self._session_cookie = data.get("session_cookie")
                self.base_url = data.get("base_url", self.base_url)
            except (json.JSONDecodeError, OSError):
                pass

    def _save_session(self) -> None:
        CONFIG_DIR.mkdir(parents=True, exist_ok=True)
        SESSION_FILE.write_text(json.dumps({
            "session_cookie": self._session_cookie,
            "base_url": self.base_url,
        }))

    @property
    def is_logged_in(self) -> bool:
        return self._session_cookie is not None

    def logout(self) -> None:
        self._session_cookie = None
        self._save_session()

    # ---------- auth ----------

    def request_otp(self, email: str) -> None:
        self._post("/api/auth/request-otp", {"email": email})

    def verify_otp(self, email: str, code: str) -> None:
        resp = requests.post(
            self.base_url + "/api/auth/verify-otp",
            json={"email": email, "code": code},
            timeout=10,
        )
        self._raise_for_status(resp)
        cookie = resp.cookies.get(SESSION_COOKIE_NAME)
        if not cookie:
            raise ApiError("Login succeeded but no session was returned")
        self._session_cookie = cookie
        self._save_session()

    # ---------- flasher API ----------

    def fetch_grant(self) -> Grant:
        data = self._get("/api/flasher/grant")
        return Grant(label=data["label"], products=data["products"])

    def fetch_builds(self, product: str) -> list[Build]:
        data = self._get(f"/api/flasher/builds?product={product}")
        return [
            Build(
                id=b["id"], product=b["product"], version=b["version"],
                variant=b["variant"], size_bytes=b["sizeBytes"],
                notes=(b.get("notes") or None),
            )
            for b in data["builds"]
        ]

    def download_build(
        self, build_id: str, mac: Optional[str] = None,
        expected_size: Optional[int] = None,
        on_progress: Optional[Callable[[int], None]] = None,
    ) -> bytes:
        """`mac` is the connected chip's raw MAC (hex, no separators) -- the
        server checks it against provisioned Device rows. `expected_size`
        should be the build's already-known sizeBytes (from fetch_builds) --
        the server always responds chunked with no Content-Length, so that
        can't be relied on; the size we already know from the listing can."""
        path = f"/api/flasher/download/{build_id}"
        if mac:
            path += f"?mac={mac}"
        resp = self._authed_request("GET", path, stream=True)
        self._raise_for_status(resp)
        total = expected_size or int(resp.headers.get("Content-Length", 0)) or -1
        chunks = bytearray()
        last_percent = -1
        for chunk in resp.iter_content(chunk_size=8192):
            chunks.extend(chunk)
            if total > 0 and on_progress:
                percent = len(chunks) * 100 // total
                if percent != last_percent:
                    last_percent = percent
                    on_progress(percent)
        return bytes(chunks)

    def report_result(
        self, build_id: str, result: str, mac: Optional[str] = None,
        device_id: Optional[str] = None, detail: Optional[str] = None,
    ) -> None:
        """Best-effort -- a failed report shouldn't itself be treated as a flash failure."""
        try:
            self._post("/api/flasher/report", {
                "buildId": build_id, "result": result, "mac": mac,
                "deviceId": device_id, "detail": detail,
            }, authed=True)
        except Exception:
            pass

    # ---------- internals ----------

    def _headers(self) -> dict:
        headers = {}
        if self._session_cookie:
            headers["Cookie"] = f"{SESSION_COOKIE_NAME}={self._session_cookie}"
        return headers

    def _authed_request(self, method: str, path: str, **kwargs) -> requests.Response:
        if not self.is_logged_in:
            raise ApiError("Not logged in.")
        return requests.request(
            method, self.base_url + path, headers=self._headers(),
            timeout=kwargs.pop("timeout", 15), **kwargs,
        )

    def _get(self, path: str, authed: bool = True) -> dict:
        resp = self._authed_request("GET", path) if authed else requests.get(
            self.base_url + path, timeout=10)
        self._raise_for_status(resp)
        return self._safe_json(resp)

    def _post(self, path: str, data: dict, authed: bool = False) -> dict:
        clean = {k: v for k, v in data.items() if v is not None}
        if authed:
            resp = self._authed_request("POST", path, json=clean)
        else:
            resp = requests.post(self.base_url + path, json=clean, headers=self._headers(), timeout=10)
        self._raise_for_status(resp)
        return self._safe_json(resp)

    @staticmethod
    def _safe_json(resp: requests.Response) -> dict:
        try:
            return resp.json()
        except ValueError:
            return {}

    @staticmethod
    def _raise_for_status(resp: requests.Response) -> None:
        if not (200 <= resp.status_code < 300):
            message = f"HTTP {resp.status_code}"
            try:
                err = resp.json().get("error")
                if err:
                    message = err
            except ValueError:
                pass
            raise ApiError(message)
