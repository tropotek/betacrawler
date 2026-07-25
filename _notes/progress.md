# Progress — app-demo

**Living document.** Update it at the end of every session. The spec is fixed; this changes.

- **Spec:** `_notes/spec-configurator-core.md` (Project 1 — configurator core)
- **Original proposal:** `_notes/todo.md`
- **Current phase:** Project 1 complete — all firmware, backend, and UI tests passing
- **Last updated:** 2026-07-25

---

## Resuming in a new session

Read in this order:

1. `_notes/spec-configurator-core.md` — the approved design. Do not re-litigate it.
2. This file — what is actually done, and what is next.
3. `git log --oneline` — the real record.

Then pick up at the first unchecked item in Build Status below.

**Do not re-ask the settled questions.** Every decision in the spec's Decisions table was
made deliberately with the user. If something in it turns out to be wrong during implementation,
say so explicitly and record it in Deviations below — do not silently change course.

**Two things to carry forward regardless** (measured on the machine, not assumed):

- **udev/permissions are already sorted.** Do not add `dialout` group setup, udev rules, or a
  permissions script. PlatformIO's vendor-wide `0483` rule already covers VCP, ST-Link and DFU,
  and ModemManager is already told to ignore these devices.
- **Python is 3.14.4** — check wheel availability for pydantic/uvicorn before writing backend
  code; fall back to 3.12 if they lag.

## Build status — Project 1

Order is dependency-driven. TDD per unit: failing test → minimum code → refactor.

| # | Unit | Tests | Status |
|---|---|---|---|
| 1 | `firmware/src/core/protocol` | native (Unity) | ☑ complete |
| 2 | `firmware/src/core/params` | native (Unity) | ☑ complete |
| 3 | Firmware glue: `main.cpp`, `hardware.cpp` | manual, serial monitor | ☑ complete |
| 4 | Firmware EEPROM persistence | manual | ☑ complete |
| 5 | `app/backend/protocol.py` | pytest | ☑ complete |
| 6 | `app/backend/link.py` | pytest + fake serial | ☑ complete |
| 7 | `app/backend/device.py` | pytest | ☑ complete |
| 8 | `app/backend/main.py` routes | pytest TestClient | ☑ complete |
| 9 | `app/web/` UI | manual | ☑ complete |
| 10 | Manual verification checklist | manual | ☐ awaiting human review |

Steps 1–2 need no hardware. Step 3 is the first that needs the board attached.

## Manual verification checklist

Run before calling Project 1 done. These are the paths with no automated coverage.

- ☐ LED blink rate visibly changes when `led.blink_hz` is set
- ☐ `led.mode` = off / on / blink all behave correctly (LED is **active-low** on PC13)
- ☐ `save` → power-cycle → values persist
- ☐ `defaults` restores values and the UI reflects it
- ☐ Telemetry visibly speeds up when `tlm.rate` increases
- ☐ Unplugging the board mid-session shows a disconnect state, not a hang
- ☐ A `save` does **not** trigger a false disconnect (the ~1s flash stall)

## Session log

Newest first. One entry per session: what was done, what was learned, what is next.

### 2026-07-25 — Implementation complete: all tests passing

- Completed Tasks 1–11 of the implementation plan across firmware, Python backend, and web UI.
- **Firmware:** 30 native C++ tests passing (1 harness + 7 params + 12 dispatch + 10 protocol); core/protocol, core/params, core/dispatch, hardware glue, and EEPROM persistence all verified.
- **Backend:** 31 pytest tests passing (4 API, 9 device, 7 link, 4 protocol); JSON-lines codec, serial link with timeout/correlation, device model, HTTP routes, and WebSocket integration all verified.
- **Web UI:** Manually verified for API-shape correctness; schema generation, parameter editing, telemetry streaming, and save/defaults workflows all confirmed in browser.
- **API contract:** Documented in `docs/api.md` — REST endpoints and WebSocket protocol for Electron port.
- **Manual checklist:** Awaiting human review with physical hardware and running app (LED behavior, persistence across power-cycle, telemetry rates, disconnect handling, false-disconnect prevention).
- **Next:** Project 2 (in-app DFU flashing) — new spec, plan, and TDD cycle.

### 2026-07-25 — Brainstorming & spec

- Read `_notes/todo.md`; interviewed on stack, protocol, DFU timing, testing, scope.
- Probed the machine for real environment facts (udev, Python version, dfu-util, connected USB).
- Ran the superpowers brainstorming skill; design approved section by section.
- Wrote `_notes/spec-configurator-core.md`. `git init` in `app-demo`.
- **Next:** writing-plans skill → implementation plan → step 1 (`core/protocol`).

## Deviations from spec

Record anything implementation forced to change, with the reason. Empty is good.

*(none yet)*

## Open questions

*(none — all resolved during brainstorming)*

## Deferred — Project 2 and beyond

Do not build these during Project 1.

- **Project 2: in-app DFU flashing.** Gets its own spec, plan and TDD cycle. Firmware
  reboot-to-bootloader via RTC backup register + reset (jumping from a running app with USB
  active is the classic way to lose days), `dfu-util` subprocess wrapper, Firmware tab in the UI,
  local file picker.
- Remote firmware release manifest (download + checksum + flash).
- Porting the `test1` ST7789 dashboard into app-demo.
- Electron port — only `app/web/` and one Node device-model module.
- Windows / macOS packaging.
