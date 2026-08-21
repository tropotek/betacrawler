import json
import re
import sys
import urllib.request
from playwright.sync_api import sync_playwright

BASE = "http://localhost:9091"


def fetch(path):
    # Bytes, not text: the shell includes binary assets (icons, favicon.ico),
    # and forcing a UTF-8 decode on those raises before status is even checked.
    with urllib.request.urlopen(f"{BASE}/{path.lstrip('./')}") as resp:
        assert resp.status == 200, path
        return resp.read()


manifest = json.loads(fetch("manifest.json"))
assert manifest["display"] == "standalone", manifest
assert manifest["icons"], "manifest has no icons"
for icon in manifest["icons"]:
    fetch(icon["src"])

# Read the shell list out of the worker rather than repeating it here: a file
# named there but missing on disk makes cache.addAll() reject as a whole, and
# the app silently loses its offline shell.
sw = fetch("service-worker.js").decode()
shell = re.findall(r"'(\./[^']*)'", sw.split("SHELL_FILES = [")[1].split("];")[0])
assert len(shell) > 10, shell
for path in shell:
    fetch(path)

errors = []
with sync_playwright() as p:
    browser = p.chromium.launch(headless=True)
    page = browser.new_page()
    page.on("pageerror", lambda exc: errors.append(str(exc)))
    page.on("console", lambda msg: errors.append(msg.text) if msg.type == "error" else None)
    page.goto(BASE, wait_until="networkidle")

    manifest_href = page.eval_on_selector("link[rel=manifest]", "el => el.getAttribute('href')")
    assert manifest_href == "manifest.json", manifest_href

    page.wait_for_function(
        "() => navigator.serviceWorker.controller !== null "
        "|| navigator.serviceWorker.getRegistrations().then(r => r.length > 0)",
        timeout=5000,
    )

    nav_pages = page.eval_on_selector_all(
        "[data-page]", "els => els.map(el => el.dataset.page)")
    assert "firmware" not in nav_pages, nav_pages
    assert set(["home", "config", "controller", "modes", "terminal", "wiring", "help"]) <= set(nav_pages), nav_pages

    # app.js is a module, so its top-level functions are NOT globals. These
    # four have to be put back on window by hand because pages/config.html
    # calls them from Alpine expressions, which look there and nowhere else.
    missing = page.evaluate(
        "() => ['tlmLabel', 'perCellText', 'faultText', 'faultIsError']"
        ".filter((n) => typeof window[n] !== 'function')")
    assert not missing, f"not exported to window: {missing}"

    # Navigating a page fragment in is where a broken Alpine expression
    # surfaces, so visit one that has them before judging `errors`.
    page.click("[data-page=help]")
    page.wait_for_timeout(500)

    browser.close()

if errors:
    print("console/page errors:", errors, file=sys.stderr)
    sys.exit(1)
print("OK")
