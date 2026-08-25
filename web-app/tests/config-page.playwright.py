"""The Configuration page's vehicle-shaped behaviour, against the simulator.

Serve web-app/ on 9091 first, then: ~/.pwvenv/bin/python3 tests/config-page.playwright.py
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
    page.click("[data-page='config']")
    page.wait_for_selector("#f-drive-mode", timeout=10000)
    page.wait_for_timeout(500)

    def servo_mode():
        return page.evaluate("() => Alpine.store('config').values['servo.mode']")

    def card_visible():
        return page.is_visible("fieldset:has-text('Steering Servo')")

    # The steering servo belongs to the car build alone. drive.mode already
    # says whether one exists, so it is never asked about separately.
    check(not page.is_visible("#f-servo-mode"),
          "a servo mode control is on the page; drive.mode already decides this")

    check(not card_visible(), "the Steering Servo card shows on a skid build")
    check(servo_mode() == "off", f"skid left servo.mode at {servo_mode()!r}, want 'off'")

    page.select_option("#f-drive-mode", "car")
    page.wait_for_timeout(600)
    check(card_visible(), "the Steering Servo card is hidden on a car build")
    check(servo_mode() == "input", f"car left servo.mode at {servo_mode()!r}, want 'input'")
    check(page.is_visible("#f-servo-invert"), "Invert is missing from the car build")
    check(page.is_visible("#f-servo-trim"), "Trim is missing from the car build")

    page.select_option("#f-drive-mode", "skid")
    page.wait_for_timeout(600)
    check(not card_visible(), "the Steering Servo card survives going back to skid")
    check(servo_mode() == "off", f"back to skid left servo.mode at {servo_mode()!r}, want 'off'")

    browser.close()

for f in failures:
    print(f"FAIL: {f}")
for e in errors:
    print(f"CONSOLE: {e}")
if failures or errors:
    sys.exit(1)
print("config page OK")
