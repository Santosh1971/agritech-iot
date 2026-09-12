"""Pre-build patch: raise ESPAsyncWebServer's hardcoded per-client RX timeout.

AsyncWebServer::AsyncWebServer() (src/WebServer.cpp) sets every new client's
RX timeout to 3 seconds at connect time via a bare `c->setRxTimeout(3)` --
a literal with no override hook, cleared only when the server starts
sending its own response (_send()) or explicitly pauses. That timeout stays
armed for the whole duration of an incoming request body, including a large
multipart upload: if the ESP32 goes quiet on the wire for more than 3s
(exactly what happens when Update.write()'s flash erase blocks the same
async_tcp task that would otherwise keep draining the socket), the
connection gets closed outright -- independent of the AsyncTCP ACK timeout
raised separately via -D CONFIG_ASYNC_TCP_MAX_ACK_TIME in platformio.ini.

Confirmed via FG1 bench testing, 2026-09-12: WiFi OTA over the board's
SoftAP kept dying mid-transfer (Broken pipe / unexpected end of stream) at
~40-45% even after the watchdog and ACK-timeout fixes, always after a
period of no forward progress -- consistent with this 3s window, not
either of the other two.

No build-flag override exists for this constant (unlike
CONFIG_ASYNC_TCP_MAX_ACK_TIME), so this patches PlatformIO's fetched copy
of the library directly. Idempotent -- safe to run on every build; only
warns (never fails the build) if upstream changes the line this looks for,
so a library bump doesn't silently leave the old 3s timeout in place
unnoticed.
"""
Import("env")  # noqa: F821 -- provided by PlatformIO's SCons environment

from pathlib import Path

OLD = "c->setRxTimeout(3);"
NEW = "c->setRxTimeout(30);  // patched by scripts/patch_asyncwebserver_rx_timeout.py -- see that file"


def patch_rx_timeout(*_args, **_kwargs):
    libdeps_dir = Path(env.subst("$PROJECT_LIBDEPS_DIR")) / env.subst("$PIOENV")
    target = libdeps_dir / "ESPAsyncWebServer" / "src" / "WebServer.cpp"

    if not target.exists():
        print(f"[patch_asyncwebserver_rx_timeout] {target} not found yet -- skipping (library not fetched?)")
        return

    text = target.read_text()
    if NEW in text:
        return  # already patched
    if OLD not in text:
        print(
            f"[patch_asyncwebserver_rx_timeout] WARNING: expected line not found in {target} -- "
            "ESPAsyncWebServer may have changed; RX timeout is still the library default."
        )
        return

    target.write_text(text.replace(OLD, NEW))
    print(f"[patch_asyncwebserver_rx_timeout] Raised RX timeout 3s -> 30s in {target}")


patch_rx_timeout()
