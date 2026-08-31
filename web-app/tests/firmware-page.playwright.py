"""The Firmware page, and the third rung of the DFU permission ladder.

Rungs one and two live in api.js and are covered by tests/api.test.js. Rung
three is markup plus a listener -- the modal in index.html and
initDfuGrantModal() in app.js -- so only a rendered browser reaches it. It is
the recovery path for a flash that has already stranded the user, which is
exactly when a silent break costs the most.

Serve web-app/ on 9091 first, then: ~/.pwvenv/bin/python3 tests/firmware-page.playwright.py
"""
import os
import sys
from playwright.sync_api import sync_playwright

BASE = os.environ.get("BETACRAWLER_BASE", "http://localhost:9091")

# A WebUSB stand-in for the state that puts the flash on rung three: one DFU
# entry the browser has been granted, whose device is no longer on the bus, and
# a chooser that refuses to open because the click behind it aged out.
#
# `open()` answers from __dfuOnBus so the test can pull the device out from
# under the page between the page's presence probe and the flash -- the probe
# needs it present (or the Flash button stays disabled), the flash needs it
# gone (or it flashes for real instead of parking).
#
# requestDevice() reports SecurityError, which api.js reads as "blocked" rather
# than "no device", and counts its calls so the modal button can be shown to
# reach grantDfuAndResume() at all. The second call answers NotFoundError: a
# granted device would start a real DFU write, and the abandon path proves the
# wiring just as well without faking transfers.
FAKE_USB = """
window.__dfuOnBus = true;
window.__requestCalls = 0;
const stale = {
  vendorId: 0x0483,
  productId: 0xdf11,
  serialNumber: 'STALE',
  open: () => window.__dfuOnBus ? Promise.resolve()
                                : Promise.reject(new Error('not on the bus')),
  close: () => Promise.resolve(),
};
Object.defineProperty(navigator, 'usb', {
  configurable: true,
  value: {
    getDevices: () => Promise.resolve([stale]),
    requestDevice: () => {
      window.__requestCalls += 1;
      const err = new Error('permission blocked');
      err.name = window.__requestCalls === 1 ? 'SecurityError' : 'NotFoundError';
      return Promise.reject(err);
    },
    addEventListener: () => {},
    removeEventListener: () => {},
  },
});
"""

errors = []
failures = []


def check(cond, msg):
    if not cond:
        failures.append(msg)


with sync_playwright() as p:
    browser = p.chromium.launch(headless=True)
    context = browser.new_context(viewport={"width": 1440, "height": 1000})
    context.add_init_script(FAKE_USB)
    page = context.new_page()
    page.on("pageerror", lambda exc: errors.append(str(exc)))
    page.on("console", lambda m: errors.append(m.text) if m.type == "error" else None)
    # The flash asks first. Accepting is the path under test.
    page.on("dialog", lambda d: d.accept())

    page.goto(BASE)
    page.wait_for_timeout(1200)

    # The page is reachable with nothing connected, on purpose: gating the
    # recovery tool on a working device is backwards.
    page.click("[data-page='firmware']")
    page.wait_for_selector("#fw-flash", timeout=10000)
    page.wait_for_timeout(800)

    # The log block is behind x-show="phase !== 'idle'", so at rest it exists
    # but is not shown; it appears once a flash starts, asserted below.
    check(page.locator("#fw-log").count() == 1, "the flash log is missing")
    check(not page.is_visible("#fw-log"), "the flash log shows before any flash")
    # The badge is knowable with no serial connection at all, which is the
    # whole reason this page is not gated on one.
    check(page.evaluate("() => Alpine.store('firmware').dfuPresent") is True,
          "the granted bootloader was not detected")
    check(page.is_visible("span.badge:has-text('DFU mode')"),
          "the DFU badge does not render the detected state")

    # Every image in the shipped manifest is offered.
    manifest = page.evaluate(
        "async () => (await (await fetch('firmware/manifest.json')).json()).images")
    check(len(manifest) > 0, "the manifest ships no images")
    # Selected by attribute, not #id: an image id carries the version, so it
    # contains dots and is not a usable CSS id selector.
    def image(img_id):
        return f'[id="fw-{img_id}"]'

    for img in manifest:
        check(page.is_visible(image(img["id"])),
              f"{img['id']} is missing from the picker")

    # --- rung three -----------------------------------------------------------
    page.check(image(manifest[0]["id"]))
    page.wait_for_timeout(300)
    check(page.is_enabled("#fw-flash"),
          "Flash stayed disabled with an image chosen and a bootloader present")

    # Gone by the time the flash looks for it, so no device can be opened and
    # the chooser is the only way on.
    page.evaluate("() => { window.__dfuOnBus = false; }")
    page.click("#fw-flash")
    page.wait_for_selector("#dfu-grant-modal.show", timeout=10000)

    modal = page.locator("#dfu-grant-modal")
    check(modal.is_visible(), "the grant modal never appeared")
    # Both are clicked below, so a missing one has to stop the run here rather
    # than time out on a locator 30 seconds later with no message.
    for button in ("#dfu-grant-ok", "#dfu-grant-cancel"):
        if not page.is_visible(button):
            print(f"FAIL: {button} is missing from the grant modal")
            browser.close()
            sys.exit(1)
    check("STM32" in modal.inner_text(),
          "the modal does not say what to pick in the chooser")
    check(page.evaluate("() => window.__requestCalls") == 1,
          "the flash never tried the chooser before parking")
    check(page.is_visible("#fw-log"), "the flash log stayed hidden during a flash")

    # --- the button carries a fresh activation into the chooser ---------------
    page.click("#dfu-grant-ok")
    page.wait_for_timeout(800)
    check(page.evaluate("() => window.__requestCalls") == 2,
          "clicking Select DFU device never reached grantDfuAndResume()")
    check(not page.is_visible("#dfu-grant-modal.show"),
          "the modal stayed up after its button was clicked")
    # A textarea's text lives in its value, not its child nodes.
    log = page.input_value("#fw-log")
    check("abandoned" in log, f"the dismissed chooser was not reported: {log!r}")

    # --- cancelling instead ---------------------------------------------------
    # The init script runs again on reload, so the fake comes back with the
    # device on the bus and the call count at zero.
    page.reload()
    page.wait_for_timeout(1200)
    page.click("[data-page='firmware']")
    page.wait_for_selector("#fw-flash", timeout=10000)
    page.wait_for_timeout(800)
    page.check(image(manifest[0]["id"]))
    page.wait_for_timeout(300)
    page.evaluate("() => { window.__dfuOnBus = false; }")
    page.click("#fw-flash")
    page.wait_for_selector("#dfu-grant-modal.show", timeout=10000)

    page.click("#dfu-grant-cancel")
    page.wait_for_timeout(600)
    check(not page.is_visible("#dfu-grant-modal.show"),
          "Cancel flash left the modal up")
    check(page.evaluate("() => window.__requestCalls") == 1,
          "Cancel flash opened the chooser instead of dropping the flash")

    browser.close()

for f in failures:
    print(f"FAIL: {f}")
for e in errors:
    print(f"CONSOLE: {e}")
if failures or errors:
    sys.exit(1)
print("firmware page OK")
