"""Capture screenshots of the app UI against the simulated board.

Dev tool, not part of the published site or its build. Run by hand to
(re)generate docs/assets/screenshots/*.png after a UI change:

    ~/.pwvenv/bin/python3 docs-tools/capture_screenshots.py

Requires ~/.pwvenv (Playwright + Chromium, see CLAUDE.md) AND app/'s own deps
(uvicorn, fastapi, ...) importable -- run against app/.venv's site-packages,
not ~/.pwvenv's own, hence the sys.path juggling below rather than a venv
switch.

The device is the `sim://board` simulator, whose schema is copied from the
firmware's golden fixture -- so these screenshots track the real parameter set
without a second copy of it living here.
"""
import sys
import threading
import time
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
APP_DIR = REPO_ROOT / "app"
sys.path.insert(0, str(APP_DIR))
sys.path.insert(0, str(APP_DIR / ".venv" / "lib" /
                       f"python{sys.version_info.major}.{sys.version_info.minor}" /
                       "site-packages"))

import uvicorn  # noqa: E402  (path setup above must run first)
import backend.main as backend_main  # noqa: E402
from backend.device import DeviceModel  # noqa: E402
from backend.link import SIM_PORT, SerialLink  # noqa: E402
from backend.main import create_app  # noqa: E402

# The real /api/ports route enumerates whatever serial devices are physically
# attached to the machine running this script. Left alone, that leaks the
# capture machine's real hardware into the navbar's port picker, so the
# screenshots differ per machine. Pin it to the simulator alone.
backend_main.list_candidate_ports = lambda: [
    {"port": SIM_PORT, "desc": "Simulated board (no hardware)", "vid": "",
     "pid": "", "match": True, "board": "Simulator"}
]

PORT = 8099

# data-page values straight from app/web/index.html's nav buttons.
PAGES = ("home", "config", "controller", "modes", "terminal", "firmware",
         "wiring", "help")

# Pages whose content runs past one 1280x900 viewport: the parameter form, the
# two curated setup pages, the wiring diagram and the help text. The rest fit,
# and leaving them viewport-only keeps thumbnail sizes consistent.
FULL_PAGE = {"config", "controller", "modes", "wiring", "help"}


def main():
    device = DeviceModel(SerialLink())
    app = create_app(device)

    server = uvicorn.Server(uvicorn.Config(app, host="127.0.0.1", port=PORT, log_level="warning"))
    server_thread = threading.Thread(target=server.run, daemon=True)
    server_thread.start()
    while not server.started:
        time.sleep(0.05)

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

        for name in PAGES:
            page.click(f"[data-page='{name}']")
            page.wait_for_timeout(300)

            if name == "terminal":
                # An empty terminal makes for a useless screenshot -- type a
                # real command and submit it so the capture shows actual
                # command/response content.
                page.fill("#term-input", "get device.name")
                page.click("#term-send")
                page.wait_for_timeout(300)

            page.screenshot(path=str(out_dir / f"{name}.png"),
                            full_page=name in FULL_PAGE)

            if name == "wiring":
                # The docs need the diagram without the app's chrome around
                # it. Capturing the element here rather than copying the SVG
                # into the page keeps the board's own stylesheet applied --
                # the markup carries ~60 class attributes and renders
                # unstyled without it.
                page.locator(".diagram-card").screenshot(
                    path=str(out_dir / "wiring-diagram.png"))
        browser.close()

    server.should_exit = True
    server_thread.join(timeout=2)


if __name__ == "__main__":
    main()
