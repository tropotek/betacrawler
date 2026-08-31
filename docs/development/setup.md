# Setup

This page is for working on BetaCrawler itself — firmware, the web app, or these docs. If you
just want to build and drive one, you don't need any of this: flash it from
**[the app](https://tropotek.github.io/betacrawler/app/)** and follow **[Choose your build](../build/index.md)**.

## Clone and open the workspace

```bash
git clone git@github.com:tropotek/betacrawler.git
cd betacrawler
code betacrawler.code-workspace
```

VS Code will offer the **PlatformIO IDE** extension — install it. It manages its own Python
environment and toolchain under `~/.platformio`, so no separate PlatformIO install is needed.

## Firmware

The PlatformIO extension's sidebar builds, uploads and tests firmware directly. From a terminal,
`pio` isn't on `PATH` — invoke it through the extension's own environment, from `firmware/`:

```bash
~/.platformio/penv/bin/pio test -e native                     # no board needed
~/.platformio/penv/bin/pio run -e blackpill_f411ce             # compile for the real board
~/.platformio/penv/bin/pio run -e blackpill_f411ce -t upload   # flash it over ST-Link/SWD
```

## web-app

No build step and no npm dependencies — vanilla ES modules served as static files. From
`web-app/`:

```bash
python3 -m http.server 9091   # then open http://localhost:9091
node --test                   # unit tests and browser suites, needs Node 18+
```

Web Serial and WebUSB require a secure context (`localhost` works, a LAN IP over plain HTTP
doesn't) and a Chromium-based browser.

## Docs site

The docs are built with [MkDocs Material](https://squidfunk.github.io/mkdocs-material/). Set up a
venv once, from the repo root:

```bash
python3 -m venv docs/.venv
docs/.venv/bin/pip install -r tools/requirements.txt
docs/.venv/bin/mkdocs serve   # live-reloading preview at http://localhost:8000
```

`docs/.venv/` is gitignored — it's a local tool environment, not part of the site.

## Headless browser testing

The rendered UI is covered by the browser suites in `web-app/tests/*.playwright.py`, which drive a
real headless Chromium. `node --test` runs them alongside the unit tests, reusing a server on port
9091 or starting one for the duration. They need a Playwright + Chromium venv, set up once:

```bash
python3 -m venv ~/.pwvenv
~/.pwvenv/bin/pip install playwright
~/.pwvenv/bin/playwright install --with-deps chromium
```

Without it the suites skip visibly rather than passing silently, so a `node --test` run that
reports a skip here has not checked the UI at all.

Run a single suite against a server you already have up:

```bash
~/.pwvenv/bin/python3 tests/config-page.playwright.py
```

Each suite reads its base URL from `BETACRAWLER_BASE` (default `http://localhost:9091`; set
`BETACRAWLER_PORT` to move the port `node --test` uses), and reports a failure by printing it and
exiting non-zero. Add one by dropping a new `tests/<name>.playwright.py` into `web-app/tests/` —
suites are discovered, not listed anywhere.

Use them to read rendered text, click through a page and collect console and `pageerror` events
before calling a UI change done. For a device state real hardware can't produce, install a fake
`navigator.serial` with `add_init_script()` before the page loads and answer the wire protocol
from it — see [Protocol](protocol.md) for the message shapes.
