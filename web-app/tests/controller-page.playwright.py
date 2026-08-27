"""The Controller page's Drive Mixer readouts, against the simulator.

Serve web-app/ on 9091 first, then: ~/.pwvenv/bin/python3 tests/controller-page.playwright.py
"""
import os
import sys
from playwright.sync_api import sync_playwright

BASE = os.environ.get("BETACRAWLER_BASE", "http://localhost:9091")

errors = []
failures = []


def check(cond, msg):
    if not cond:
        failures.append(msg)


with sync_playwright() as p:
    browser = p.chromium.launch(headless=True)
    page = browser.new_page(viewport={"width": 1440, "height": 1000})
    page.on("pageerror", lambda exc: errors.append(str(exc)))
    page.on("console", lambda m: errors.append(m.text) if m.type == "error" else None)

    page.goto(BASE)
    page.wait_for_timeout(1200)
    page.click("button:has-text('Try the simulator')")
    page.wait_for_timeout(1500)
    page.click("[data-page='controller']")
    page.wait_for_selector("fieldset:has-text('Drive Mixer')", timeout=10000)
    page.wait_for_timeout(1200)

    def mixer():
        return page.inner_text("fieldset:has-text('Drive Mixer')")

    check("Left Output" in mixer() and "Right Output" in mixer(),
          "skid mode does not label the two motor slots Left and Right")

    page.evaluate("() => Alpine.store('config').setDriveMode('car')")
    page.wait_for_timeout(1500)
    body = mixer()

    # Both motor slots carry throttle on a car, so the second readout follows
    # drive's steering slot instead of duplicating the first.
    check("Throttle Output" in body, "car mode does not label the motor slots Throttle")
    check("Steer Output" in body, "car mode does not show the steering output")

    steer = page.evaluate("() => Alpine.store('telemetry').field('drv_s').value")
    left = page.evaluate("() => Alpine.store('telemetry').field('drv_l').value")
    check(steer is not None, "drv_s is not published")
    check(steer != left, "drv_s repeats the throttle reading instead of the steering slot")

    browser.close()

for f in failures:
    print(f"FAIL: {f}")
for e in errors:
    print(f"CONSOLE: {e}")
if failures or errors:
    sys.exit(1)
print("controller-page: ok")
