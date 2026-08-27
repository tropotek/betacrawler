import os
import re
import sys
from playwright.sync_api import sync_playwright

BASE = os.environ.get("BETACRAWLER_BASE", "http://localhost:9091")

# The dev and release configurators are the same tree served from two paths, so
# the only way to exercise the dev one is to serve it from an /app-dev/ URL:
# every request under it is proxied back to the real files.
def serve_as_app_dev(context):
    def handler(route):
        target = route.request.url.replace("/app-dev/", "/")
        resp = context.request.get(target)
        route.fulfill(status=resp.status, body=resp.body(),
                      headers={k: v for k, v in resp.headers.items()
                               if k.lower() not in ("content-encoding", "content-length")})
    context.route("**/app-dev/**", handler)


errors = []
with sync_playwright() as p:
    browser = p.chromium.launch(headless=True)
    context = browser.new_context(viewport={"width": 1280, "height": 800})
    serve_as_app_dev(context)
    page = context.new_page()
    page.on("pageerror", lambda exc: errors.append(str(exc)))
    page.on("console", lambda msg: errors.append(msg.text) if msg.type == "error" else None)

    # The release deployment must say nothing at all.
    page.goto(BASE, wait_until="networkidle")
    assert page.is_hidden("#dev-banner"), "the release build must not show the dev banner"
    version = page.inner_text("#app-version").strip()
    assert re.fullmatch(r"v\d+\.\d+\.\d+", version), version

    page.goto(f"{BASE}/app-dev/index.html", wait_until="networkidle")
    banner = page.locator("#dev-banner")
    assert banner.is_visible(), "the dev build must show the banner"
    assert "Development build" in banner.inner_text(), banner.inner_text()
    assert page.inner_text("#app-version").strip().endswith("-dev"), \
        page.inner_text("#app-version")

    # It sits above the navbar, not below it.
    top = banner.bounding_box()["y"]
    assert top < page.locator("header.navbar").bounding_box()["y"], "banner is below the navbar"
    assert top == 0, f"banner is not at the top of the page: y={top}"

    # One line at every width -- counting line boxes rather than comparing
    # heights, which differ between the tiers that include the <code> tag.
    # Total vertical span over the tallest rect: ~1 on one line, ~2 once it
    # wraps. Rect tops are not compared directly -- the <code> tag's own box
    # sits a fraction above the text it shares a line with.
    count_lines = """() => {
      const r = document.createRange();
      r.selectNodeContents(document.getElementById('dev-banner'));
      const boxes = [...r.getClientRects()].filter((b) => b.width > 0.5);
      const span = Math.max(...boxes.map((b) => b.bottom))
                 - Math.min(...boxes.map((b) => b.top));
      return span / Math.max(...boxes.map((b) => b.height));
    }"""
    for width in (1280, 992, 768, 576, 400, 320):
        page.set_viewport_size({"width": width, "height": 800})
        page.wait_for_timeout(50)
        text = banner.inner_text().replace("\n", " ")
        lines = page.evaluate(count_lines)
        assert lines < 1.5, f"banner wraps onto {lines:.2f} lines at {width}px: {text}"
        assert "Development build" in text and "stable version" in text, f"{width}px: {text}"

    browser.close()

if errors:
    print("console/page errors:", errors, file=sys.stderr)
    sys.exit(1)
print("OK")
