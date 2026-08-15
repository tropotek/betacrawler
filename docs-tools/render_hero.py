"""Render app/web/tank-hero.svg to docs/assets/tank-hero.png.

GitHub's markdown sanitiser will not render the SVG reliably, so the README
uses a raster copy. Re-run after editing the SVG:

    ~/.pwvenv/bin/python3 docs-tools/render_hero.py
"""

from pathlib import Path

from playwright.sync_api import sync_playwright

REPO_ROOT = Path(__file__).resolve().parent.parent
SRC = REPO_ROOT / "app" / "web" / "tank-hero.svg"
OUT = REPO_ROOT / "docs" / "assets" / "tank-hero.png"
WIDTH, HEIGHT, SCALE = 1200, 500, 2


def main() -> None:
    OUT.parent.mkdir(parents=True, exist_ok=True)
    with sync_playwright() as p:
        browser = p.chromium.launch(headless=True)
        page = browser.new_page(
            viewport={"width": WIDTH, "height": HEIGHT},
            device_scale_factor=SCALE,
        )
        page.goto(SRC.as_uri())
        page.screenshot(path=str(OUT))
        browser.close()
    print(f"wrote {OUT} ({OUT.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
