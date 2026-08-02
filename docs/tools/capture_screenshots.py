"""Capture screenshots of the app UI against a fake, fully-populated device.

Dev tool, not part of the published site or its build. Run by hand to
(re)generate docs/assets/screenshots/*.png after a UI change:

    ~/.pwvenv/bin/python3 docs/tools/capture_screenshots.py

Requires ~/.pwvenv (Playwright + Chromium, see CLAUDE.md) AND app/'s own deps
(uvicorn, fastapi, ...) importable -- run against app/.venv's site-packages,
not ~/.pwvenv's own, hence the sys.path juggling below rather than a venv
switch.
"""
import math
import sys
import threading
import time
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
APP_DIR = REPO_ROOT / "app"
sys.path.insert(0, str(APP_DIR))
sys.path.insert(0, str(APP_DIR / ".venv" / "lib" /
                       f"python{sys.version_info.major}.{sys.version_info.minor}" /
                       "site-packages"))

import uvicorn  # noqa: E402  (path setup above must run first)
from backend.device import DeviceModel  # noqa: E402
from backend.link import SerialLink  # noqa: E402
from backend.main import create_app  # noqa: E402
from tests.fake_serial import FakeSerial  # noqa: E402

PORT = 8099

# Every module enabled at once -- see the module docstring for why that's
# fine here despite no real board shipping this combination.
_SRC_CHANS = [f"ch{i}" for i in range(1, 13)]

SCHEMA_PARAMS = [
    {"key": "device.name", "type": "str", "label": "Device Name", "def": "silkscreen",
     "group": "Device"},
    {"key": "tlm.rate", "type": "u8", "label": "Rate", "unit": "Hz",
     "min": 1, "max": 50, "def": 10, "group": "Telemetry"},
    {"key": "led.mode", "type": "enum", "label": "LED Mode",
     "options": ["off", "on", "blink", "fade"], "def": "blink", "group": "LED"},
    {"key": "led.blink_hz", "type": "u8", "label": "Rate", "unit": "Hz",
     "min": 1, "max": 20, "def": 2, "group": "LED"},
    {"key": "servo.mode", "type": "enum", "label": "Servo",
     "options": ["off", "hold", "sweep", "input"], "def": "off", "group": "Servo"},
    {"key": "servo.angle", "type": "u8", "label": "Angle", "unit": "°",
     "min": 0, "max": 180, "def": 90, "group": "Servo"},
    {"key": "servo.sweep_s", "type": "u8", "label": "Sweep", "unit": "s",
     "min": 1, "max": 30, "def": 4, "group": "Servo"},
    {"key": "servo.min_us", "type": "u8", "label": "Min", "unit": "µs",
     "min": 500, "max": 1500, "def": 1000, "group": "Servo"},
    {"key": "servo.max_us", "type": "u8", "label": "Max", "unit": "µs",
     "min": 1500, "max": 2500, "def": 2000, "group": "Servo"},
    {"key": "servo.src", "type": "enum", "label": "Source", "options": _SRC_CHANS,
     "def": "ch2", "group": "Servo", "showIf": {"key": "servo.mode", "val": "input"}},
    {"key": "esc.direction", "type": "enum", "label": "Direction",
     "options": ["unidirectional", "bidirectional"], "def": "unidirectional", "group": "ESC"},
    {"key": "esc.mode", "type": "enum", "label": "ESC",
     "options": ["off", "armed", "input"], "def": "off", "group": "ESC"},
    {"key": "esc.throttle_us", "type": "u8", "label": "Throttle", "unit": "µs",
     "min": 1000, "max": 2000, "def": 1000, "group": "ESC"},
    {"key": "esc.min_us", "type": "u8", "label": "Min", "unit": "µs",
     "min": 500, "max": 1500, "def": 1000, "group": "ESC"},
    {"key": "esc.max_us", "type": "u8", "label": "Max", "unit": "µs",
     "min": 1500, "max": 2500, "def": 2000, "group": "ESC"},
    {"key": "esc.src", "type": "enum", "label": "Source", "options": _SRC_CHANS,
     "def": "ch1", "group": "ESC", "showIf": {"key": "esc.mode", "val": "off"}},
    {"key": "rx.protocol", "type": "enum", "label": "Protocol",
     "options": ["crossfire", "elrs"], "def": "crossfire", "group": "RX"},
    {"key": "rx.source", "type": "enum", "label": "Source",
     "options": ["uart", "sim"], "def": "uart", "group": "RX"},
    {"key": "crossfire.timeout_ms", "type": "u8", "label": "Timeout", "unit": "ms",
     "min": 100, "max": 2000, "def": 1000, "group": "Crossfire",
     "showIf": {"key": "rx.protocol", "val": "crossfire"}},
    {"key": "elrs.timeout_ms", "type": "u8", "label": "Timeout", "unit": "ms",
     "min": 50, "max": 2000, "def": 200, "group": "ELRS",
     "showIf": {"key": "rx.protocol", "val": "elrs"}},
    {"key": "disp.mode", "type": "enum", "label": "Display",
     "options": ["off", "on"], "def": "on", "group": "Display"},
    {"key": "disp.page", "type": "enum", "label": "Page",
     "options": ["info", "stats", "cycle"], "def": "info", "group": "Display"},
    {"key": "disp.rate", "type": "u8", "label": "Refresh", "unit": "Hz",
     "min": 1, "max": 10, "def": 2, "group": "Display"},
    {"key": "wifi.ssid", "type": "str", "label": "SSID", "group": "WiFi"},
    {"key": "wifi.password", "type": "str", "label": "Password", "group": "WiFi", "secret": True},
]

SCHEMA_TLM = [
    {"key": "up", "label": "Uptime", "fmt": "hms", "group": "System"},
    {"key": "clk", "label": "Clock", "unit": "MHz", "group": "System"},
    {"key": "ram", "label": "Free RAM", "unit": "kB", "div": 1024, "dec": 1, "group": "System"},
    {"key": "temp", "label": "Temp", "unit": "°C", "dec": 1, "group": "System"},
    {"key": "vdd", "label": "VDD", "unit": "V", "div": 1000, "dec": 2, "group": "System"},
    {"key": "btn", "label": "Button", "group": "Button"},
    {"key": "srv", "label": "Servo", "unit": "µs", "group": "Servo"},
    {"key": "esc", "label": "ESC", "unit": "µs", "group": "ESC"},
    {"key": "arm", "label": "Armed", "group": "ESC"},
    {"key": "wifi.status", "label": "Status", "group": "WiFi"},
    {"key": "wifi.rssi", "label": "RSSI", "unit": "dBm", "group": "WiFi"},
    {"key": "wifi.ip", "label": "IP", "fmt": "ip", "group": "WiFi"},
    {"key": "link", "label": "Link", "group": "RC Link"},
    {"key": "lq", "label": "LQ", "unit": "%", "group": "RC Link"},
    {"key": "rssi", "label": "RSSI", "unit": "dBm", "group": "RC Link"},
    {"key": "rate", "label": "Rate", "unit": "Hz", "group": "RC Link"},
    {"key": "err", "label": "Errors", "group": "RC Link"},
    {"key": "rfrate", "label": "RF Rate", "unit": "Hz", "group": "RC Link"},
    {"key": "pwr", "label": "TX Power", "unit": "mW", "group": "RC Link"},
] + [
    {"key": f"ch{i}", "label": f"CH{i}", "unit": "µs", "fmt": "bar",
     "lo": 988, "hi": 2012, "group": "RC Channels"}
    for i in range(1, 17)
]

VALUES = {p["key"]: p.get("def", p.get("defStr", "")) for p in SCHEMA_PARAMS}


def responder(req, emit):
    op, rid = req["op"], req["id"]
    if op == "hello":
        emit({"id": rid, "ok": True, "fw": "silkscreen 1.0.0", "proto": 1,
              "board": "blackpill_f411ce", "name": "silkscreen", "ver": "1.0.0",
              "built": "Aug  2 2026 12:00:00",
              "mods": ["device", "system", "button", "led", "servo", "esc", "rx",
                       "st7789_240x240", "wifi"],
              "caps": ["dfu", "wifiscan"]})
    elif op == "schema":
        emit({"id": rid, "ok": True, "params": SCHEMA_PARAMS, "tlm": SCHEMA_TLM})
    elif op == "getall":
        emit({"id": rid, "ok": True, "vals": dict(VALUES)})
    elif op == "get":
        emit({"id": rid, "ok": True, "key": req["key"], "val": VALUES.get(req["key"])})
    elif op in ("set", "save", "defaults"):
        emit({"id": rid, "ok": True})
    else:
        emit({"id": rid, "ok": False, "err": "badop"})


def push_telemetry(fake, stop_event):
    """Unsolicited tlm frames -- moving RC-channel bars are what make the
    Telemetry screenshot read as "live" rather than a static form.

    Pushed every 200ms rather than once a second: app.js's disconnect
    watchdog (app/web/app.js's startWatchdog()) flags the connection "stale"
    -- an amber badge -- after 3 missed intervals of the *declared*
    `tlm.rate` (10 Hz here, i.e. a 300ms threshold), regardless of what
    interval this fake actually pushes at. A once-a-second push blows past
    that 300ms budget on every cycle, so the badge flickered stale/connected
    and could land on "stale" for whichever screenshot happened to fire
    mid-gap. 200ms keeps every frame under the threshold so the badge reads
    "connected" throughout the capture run.
    """
    start = time.monotonic()
    while not stop_event.wait(0.2):
        t = time.monotonic() - start
        frame = {"up": int(t * 1000), "clk": 96, "ram": 42189, "temp": 34.5, "vdd": 3300,
                 "btn": 0, "srv": 1500, "esc": 1500, "arm": 2,
                 "wifi.status": 1, "wifi.rssi": -52, "wifi.ip": 0xC0A80042,
                 "link": 1, "lq": 99, "rssi": -61, "rate": 150, "err": 0,
                 "rfrate": 150, "pwr": 100}
        for i in range(1, 17):
            frame[f"ch{i}"] = 1500 + int(300 * math.sin(t / 3 + i))
        fake.emit({"tlm": frame})


def main():
    fake = FakeSerial(responder=responder)
    device = DeviceModel(SerialLink(open_port=lambda p: fake))
    app = create_app(device)

    server = uvicorn.Server(uvicorn.Config(app, host="127.0.0.1", port=PORT, log_level="warning"))
    server_thread = threading.Thread(target=server.run, daemon=True)
    server_thread.start()
    while not server.started:
        time.sleep(0.05)

    stop_event = threading.Event()
    threading.Thread(target=push_telemetry, args=(fake, stop_event), daemon=True).start()

    out_dir = REPO_ROOT / "docs" / "assets" / "screenshots"
    out_dir.mkdir(parents=True, exist_ok=True)

    from playwright.sync_api import sync_playwright

    with sync_playwright() as p:
        browser = p.chromium.launch(headless=True)
        page = browser.new_page(viewport={"width": 1280, "height": 900})
        page.goto(f"http://127.0.0.1:{PORT}/")
        page.click("#connect")
        page.wait_for_selector("#state:has-text('connected')")
        time.sleep(1.5)  # let at least one telemetry frame land

        # data-page values straight from app/web/index.html's nav buttons.
        for name in ("home", "config", "telemetry", "terminal", "firmware", "help"):
            page.click(f"[data-page='{name}']")
            page.wait_for_timeout(300)
            # config.png feeds readme.md's hero gallery and needs to sell the
            # project at a glance, so it captures the whole scrollable form
            # (all 9 module groups) rather than just the first screenful.
            # The other five fit their content within one 1280x900 viewport
            # already, so leaving them viewport-only keeps the gallery's
            # thumbnails a consistent size.
            full_page = name == "config"
            page.screenshot(path=str(out_dir / f"{name}.png"), full_page=full_page)
        browser.close()

    stop_event.set()
    server.should_exit = True
    server_thread.join(timeout=2)


if __name__ == "__main__":
    main()
