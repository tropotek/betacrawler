# Config-page field help text Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the Configuration page's auto-generated bound-only hint (`1–20`, `max 16 chars`) with real descriptive help text under each input, sourced from a new app-layer map file — with zero firmware or backend changes.

**Architecture:** A new static file, `app/web/field_help.js`, defines one flat object `FIELD_HELP` keyed by the param's dotted schema key. It loads before `app.js` via a plain `<script>` tag (no build step, no module system — matches how `vendor/*.js` already loads). `app.js`'s config store reads `FIELD_HELP[p.key]`, falling back to today's auto-generated bound when a key is absent, so a firmware param with no entry yet still renders correctly.

**Tech Stack:** Vanilla JS, Alpine.js store (`app/web/app.js`), Bootstrap `.form-text` (already present in `app/web/index.html`). Verification via headless Playwright (`~/.pwvenv`) against a fake-serial-backed FastAPI instance (`app/tests/fake_serial.py`), per this repo's established "no JS test framework, verify the UI live" pattern — no new automated test file is added, matching how the rest of the UI is verified in this repo.

## Global Constraints

- Zero changes to `firmware/` or `app/backend/` — this is `app/web/` only (design spec: "No backend or firmware changes").
- Adding a firmware parameter must still need zero `app.js` changes (`docs/architecture.md`'s schema-driven-UI rule) — an unmapped key must fall back gracefully, never break or blank the form-text line.
- Help text *replaces* the old bound-only hint per field, so it must fold in the relevant unit/range itself where that matters (e.g. "0–180°", "1000–2000 microseconds") rather than dropping it.
- One `FIELD_HELP` entry for every one of the 22 params currently in `firmware/test/golden/schema.json` (device, tlm, led, servo, esc, rx, crossfire, elrs, st7789_240x240 groups).
- Scope is the Configuration page only — no Telemetry-page changes.

---

### Task 1: Add `field_help.js`, wire it in, verify in the browser

**Files:**
- Create: `app/web/field_help.js`
- Modify: `app/web/index.html:852-854` (new `<script>` tag)
- Modify: `app/web/app.js:259-264` (the `groups` getter's `help:` line)

**Interfaces:**
- Produces: a global `const FIELD_HELP` object (plain key→string map, loaded before `app.js` runs), consumed directly by name (no import) inside `Alpine.store('config').groups`.

- [ ] **Step 1: Create `app/web/field_help.js` with the full content below**

```js
// Descriptive help text for the Configuration page, shown in each field's
// .form-text line -- replaces the bound-only hint app.js falls back to when
// a key has no entry here. Keyed by the same dotted key the wire schema
// carries (firmware/test/golden/schema.json is the authoritative list of
// what currently exists). A param with no entry here still renders fine --
// see docs/architecture.md's schema-driven-UI rule -- it just shows the
// plain bound until someone adds a line here.
const FIELD_HELP = {
  'device.name': 'A short name for this board, shown in the navbar and app title — cosmetic only, up to 31 characters.',
  'tlm.rate': 'How often telemetry pushes to the app over the WebSocket, 1–50 Hz. Higher rates cost more USB bandwidth; the Configuration form itself does not need this to be fast.',

  'led.mode': '"off" disables the LED; "on" holds it steady; "blink" pulses on/off; "fade" breathes smoothly. Rate below controls blink/fade speed.',
  'led.blink_hz': 'Cycles per second in both blink and fade modes, 1–20 Hz.',

  'servo.mode': '"off" commands nothing; "hold" drives a fixed angle; "sweep" sends the servo back and forth continuously; "input" follows a receiver channel (see Source).',
  'servo.angle': 'Commanded position in hold mode, 0–180 degrees.',
  'servo.sweep_s': 'Seconds for one full sweep cycle (0° to 180° and back), 1–30 s. Not a rate in Hz — most hobby servos cannot physically track a full sweep faster than about a second.',
  'servo.min_us': "Pulse width commanded at the 0-degree end of travel, 500–1500 microseconds. Calibrate this to the servo's real endpoint.",
  'servo.max_us': "Pulse width commanded at the 180-degree end of travel, 1500–2500 microseconds. Calibrate this to the servo's real endpoint.",
  'servo.src': "Which receiver channel drives the servo in input mode (ch1–ch12, matching the RX module's own channel numbering). Only used when Mode is input.",

  'esc.direction': '"unidirectional" treats the low end of the throttle range as stop — the safe default for most ESCs. "bidirectional" treats the middle of the range as stop, with either side driving forward or reverse. Get this wrong and arming will not behave the way you expect.',
  'esc.mode': '"off" commands nothing; "armed" drives Throttle directly; "input" follows a receiver channel (see Source). Arming always starts at the safe/neutral position and holds there briefly before honouring a commanded value.',
  'esc.throttle_us': 'Commanded pulse width in armed mode, 1000–2000 microseconds.',
  'esc.min_us': "Pulse width commanded at the low (or, in bidirectional mode, centre-adjacent) end of the throttle range, 500–1500 microseconds. Calibrate this to the ESC's real endpoint.",
  'esc.max_us': "Pulse width commanded at the high end of the throttle range, 1500–2500 microseconds. Calibrate this to the ESC's real endpoint.",
  'esc.src': "Which receiver channel drives the ESC in input mode (ch1–ch12, matching the RX module's own channel numbering). Only shown while off — change it before arming, not while live.",

  'rx.protocol': '"crossfire" (TBS Crossfire) or "elrs" (ExpressLRS) — which receiver protocol to decode. Changing this switches which settings group below applies.',
  'rx.source': '"uart" reads a real receiver wired to the board; "sim" generates fake channel data for testing without hardware.',
  'crossfire.timeout_ms': "How long without a valid frame before the link is considered lost, 100–2000 ms. TBS's own guidance is to wait about 1 second.",
  'elrs.timeout_ms': 'How long without a valid frame before the link is considered lost, 50–2000 ms. Kept far shorter than Crossfire\'s — this is a link monitor, not a failsafe trigger.',

  'disp.mode': 'Turns the on-board display on or off.',
  'disp.page': '"info" shows device identity, "stats" shows live telemetry, "cycle" rotates through both.',
  'disp.rate': 'How often the display redraws, 1–10 Hz. Capped low because each refresh costs real SPI time; independent of the Telemetry Rate above.',
};
```

- [ ] **Step 2: Wire the script tag into `app/web/index.html`**

Change:
```html
  <script src="vendor/bootstrap.bundle.min.js"></script>
  <script defer src="vendor/alpine.min.js"></script> <!-- Alpine.js v3.15.12, MIT, alpinejs.dev -->
  <script src="app.js"></script>
```
to:
```html
  <script src="vendor/bootstrap.bundle.min.js"></script>
  <script defer src="vendor/alpine.min.js"></script> <!-- Alpine.js v3.15.12, MIT, alpinejs.dev -->
  <script src="field_help.js"></script>
  <script src="app.js"></script>
```
(`field_help.js` must load before `app.js`, since `app.js` reads the `FIELD_HELP` global at store-init time.)

- [ ] **Step 3: Consume `FIELD_HELP` in `app/web/app.js`'s config store**

In the `groups` getter (`app/web/app.js`, currently lines 259-264), change:
```js
      return groupItems(visible, (p) => ({
        ...p,
        isSlider: p.type === 'u8' && SLIDER_FIELDS.has(p.key),
        help: p.type === 'u8' ? `${p.min}–${p.max}`
            : p.type === 'str' ? `max ${p.maxlen} chars` : '',
      }));
```
to:
```js
      return groupItems(visible, (p) => ({
        ...p,
        isSlider: p.type === 'u8' && SLIDER_FIELDS.has(p.key),
        // FIELD_HELP (field_help.js) fully replaces the auto-generated bound
        // when a field has an entry -- copy is written to fold the bound back
        // in where it matters. An unmapped key (a new firmware param nobody
        // has written copy for yet) falls back to the bound alone, so this
        // never needs to change when a param is added.
        help: FIELD_HELP[p.key] ?? (p.type === 'u8' ? `${p.min}–${p.max}`
            : p.type === 'str' ? `max ${p.maxlen} chars` : ''),
      }));
```

- [ ] **Step 4: Start a backend against a fake device for verification**

From `app/`:
```bash
.venv/bin/python3 -c "
import uvicorn
from backend.device import DeviceModel
from backend.link import SerialLink
from backend.main import create_app
import sys; sys.path.insert(0, 'tests')
from tests.fake_serial import FakeSerial
from tests.test_device import device_responder

fake = FakeSerial(responder=device_responder())
device = DeviceModel(SerialLink(open_port=lambda p: fake))
app = create_app(device)
device.connect('/dev/fake')
uvicorn.run(app, host='127.0.0.1', port=8099, log_level='warning')
" &
sleep 1.5
curl -s http://127.0.0.1:8099/api/status
```
Expected: a JSON status body with `"state":"connected"`.

- [ ] **Step 5: Write and run a Playwright verification script**

Save as a scratch file (this repo has no committed JS/UI test suite by design — see `CLAUDE.md`) and run with `~/.pwvenv/bin/python3`:

```python
from playwright.sync_api import sync_playwright

with sync_playwright() as p:
    browser = p.chromium.launch(headless=True)
    page = browser.new_page()
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)))
    page.goto("http://127.0.0.1:8099/")
    page.wait_for_timeout(500)
    page.click('[data-page="config"]')
    page.wait_for_timeout(300)

    checks = {
        "servo.angle": "Commanded position in hold mode, 0–180 degrees.",
        "led.blink_hz": "Cycles per second in both blink and fade modes, 1–20 Hz.",
    }
    for key, expected in checks.items():
        text = page.locator(f"#h-{key}").inner_text()
        status = "OK" if text == expected else f"MISMATCH: got {text!r}"
        print(key, "->", status)

    print("console/page errors:", errors)
    browser.close()
```

Expected output: `servo.angle -> OK`, `led.blink_hz -> OK`, `console/page errors: []`.

- [ ] **Step 6: Verify the fallback path (unmapped field)**

Temporarily comment out the `'servo.angle': ...,` line in `field_help.js`, reload, and re-run the Step 5 script with the `servo.angle` expectation changed to `"0–180"` (the old auto-generated bound for a `u8` field with `min=0, max=180`). Confirm it prints `OK`, proving a field with no `FIELD_HELP` entry still renders correctly. Then restore the `servo.angle` line (uncomment it) — this step is a proof, not a permanent change.

- [ ] **Step 7: Stop the verification server**

```bash
kill %1
```
(or `pkill -f "uvicorn.run"` if it was started differently — confirm with `ps aux | grep uvicorn` that nothing stray is left listening on :8099).

- [ ] **Step 8: Run the existing backend test suite as a regression check**

From `app/`:
```bash
.venv/bin/pytest -q
```
Expected: all tests pass (this task touches no Python — this just confirms nothing else was disturbed).

- [ ] **Step 9: Update `_notes/todo.md` and `CHANGELOG.md`**

Remove the "Look into adding help text below inputs on the app config page..." line from `_notes/todo.md` (it is gitignored, so this is a local-only edit, not part of the commit).

Add a `CHANGELOG.md` entry under the current version heading, following the existing entry format in that file:
```markdown
- **feat(app): Configuration page fields show real descriptive help text.** New
  `app/web/field_help.js` maps each param's dotted schema key to authored copy,
  replacing the old auto-generated bound-only hint (`1–20`, `max 16 chars`).
  An unmapped key falls back to that bound automatically, so adding a firmware
  parameter still needs zero `app.js` changes. Config page only — Telemetry
  keeps its existing unit-only caption.
```

- [ ] **Step 10: Commit**

```bash
git add app/web/field_help.js app/web/index.html app/web/app.js CHANGELOG.md
git commit -m "$(cat <<'EOF'
feat(app): add descriptive help text to Configuration page fields

New app/web/field_help.js maps each schema key to authored copy, read by
app.js's config store and falling back to the old auto-generated bound
(1-20, max N chars) when a key has no entry -- so adding a firmware
parameter still needs zero app.js changes.

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
)"
```

---

## Self-Review Notes

- **Spec coverage:** new file + location (spec ✓ Step 1/2), consumption/fallback logic (spec ✓ Step 3), content replaces-not-appends with bound folded in (spec ✓ Step 1 copy), all 22 current schema keys covered (spec ✓ Step 1 — count: device.name, tlm.rate, led.mode, led.blink_hz, servo.mode/angle/sweep_s/min_us/max_us/src, esc.direction/mode/throttle_us/min_us/max_us/src, rx.protocol/source, crossfire.timeout_ms, elrs.timeout_ms, disp.mode/page/rate = 22), no backend/firmware changes (spec ✓ — no `app/backend/` or `firmware/` file touched), scope is Config page only (spec ✓ — Telemetry page untouched), testing via headless Playwright against a fake device with both the happy path and the fallback path proven (spec ✓ Steps 5-6).
- **Placeholder scan:** none — every step has literal file content or a literal command with expected output.
- **Type consistency:** `FIELD_HELP` is referenced identically (bare global, `FIELD_HELP[p.key]`) in Steps 1 and 3; no signature drift across steps.
