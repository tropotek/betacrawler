# Black Pill Wiring KiCad Schematic Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a KiCad schematic-capture project at `hardware/blackpill_wiring/` that reproduces every connection shown in the Examples page's "Wiring a Black Pill" SVG (`app/web/index.html`), as real symbols and nets.

**Architecture:** One flat schematic sheet, one project-local symbol library holding 7 custom connector-style symbols (no real manufacturer parts — see spec for why), wired with net labels (not point-to-point wires). All work happens through the `kicad` MCP server's tools (`mcp__kicad__*`) — there is no source code and no pytest/Unity suite here; "tests" are the MCP server's own read-back and ERC tools.

**Tech Stack:** KiCad 9 (`.kicad_pro`/`.kicad_sch`/`.kicad_sym`), driven entirely via the `kicad` MCP server.

**Spec:** `docs/superpowers/specs/2026-07-31-blackpill-wiring-schematic-design.md` — read it first; this plan implements it task-by-task and doesn't repeat the reasoning behind each decision.

## Global Constraints

- Schematic capture only. Never create board outlines, footprints, or run PCB-editor tools (`add_via`, `route_trace`, `add_copper_pour`, etc.) — if a task here ever tempts you toward one of those, stop, that's scope creep.
- Every symbol is custom (project-local `blackpill_wiring.kicad_sym`), not a real manufacturer part. Do not substitute `MCU_ST_STM32F4:STM32F411CEUx` or any `MCU_Espressif`/`RF_Module` ESP8266 symbol for the ones defined in Task 2 — the spec rejects those explicitly (bare-die pinouts, wrong abstraction).
- All connections are net labels with matching names at each pin, not `add_schematic_wire` calls (spec: "Connections" section).
- All file paths below are absolute. Use them exactly as given — the `kicad` MCP server has no fixed working directory of its own.
- Project root: `/home/godar/Projects/stm32/silkscreen/hardware/blackpill_wiring/`
  - Project: `blackpill_wiring.kicad_pro`
  - Schematic: `blackpill_wiring.kicad_sch`
  - Symbol library: `blackpill_wiring.kicad_sym`

---

### Task 1: Bootstrap the KiCad project and schematic

**Files:**
- Create: `hardware/blackpill_wiring/blackpill_wiring.kicad_pro`
- Create: `hardware/blackpill_wiring/blackpill_wiring.kicad_sch`

**Interfaces:**
- Produces: an openable KiCad project at the path every later task writes into.

- [ ] **Step 1: Create the project**

Call:
```
mcp__kicad__create_project
  path: "/home/godar/Projects/stm32/silkscreen/hardware/blackpill_wiring"
  name: "blackpill_wiring"
```

- [ ] **Step 2: Create the schematic**

Call:
```
mcp__kicad__create_schematic
  name: "blackpill_wiring"
  path: "/home/godar/Projects/stm32/silkscreen/hardware/blackpill_wiring"
```

If this errors because `create_project` already made a root schematic of the same name, that's fine — skip to Step 3 using the existing file.

- [ ] **Step 3: Verify**

Call:
```
mcp__kicad__list_schematic_components
  schematicPath: "/home/godar/Projects/stm32/silkscreen/hardware/blackpill_wiring/blackpill_wiring.kicad_sch"
```
Expected: succeeds, returns an empty component list. If a stub `.kicad_pcb` was also created by Step 1, leave it untouched and empty — that's normal KiCad project boilerplate, not scope creep, as long as nothing ever gets drawn on it.

---

### Task 2: Create the 7 custom symbols and register the library

**Files:**
- Create: `hardware/blackpill_wiring/blackpill_wiring.kicad_sym`
- Modify: project's `sym-lib-table` (written by `register_symbol_library`)

**Interfaces:**
- Consumes: `blackpill_wiring.kicad_pro` from Task 1.
- Produces: 7 symbols placeable as `blackpill_wiring:<SYMBOL_NAME>` in Task 3.

Every pin uses `type: "passive"`, `shape: "line"`, default 2.54 mm length, and sits on the left edge (`at.x: -2.54`, `angle: 0`) at 2.54 mm vertical steps starting at `y: 0` and decreasing. Body rectangle is `x1: 0, y1: 1.27` to `x2: <width>, y2: <last_pin_y - 1.27>`. All coordinates are mm, in the symbol's own frame — do not adjust for the schematic sheet.

- [ ] **Step 1: Create `BLACKPILL_F411CE`**

Call:
```
mcp__kicad__create_symbol
  libraryPath: "/home/godar/Projects/stm32/silkscreen/hardware/blackpill_wiring/blackpill_wiring.kicad_sym"
  name: "BLACKPILL_F411CE"
  referencePrefix: "U"
  description: "WeAct STM32F411CE Black Pill -- pins used by silkscreen firmware (boards/blackpill_f411ce.h). Not the bare MCU die; see design spec."
  footprint: "~"
  datasheet: "~"
  rectangles: [{ x1: 0, y1: 1.27, x2: 25.4, y2: -46.99 }]
  pins:
    - { name: "PA0",    number: "1",  at: {x: -2.54, y: 0,      angle: 0}, type: "passive" }
    - { name: "PA2",    number: "2",  at: {x: -2.54, y: -2.54,  angle: 0}, type: "passive" }
    - { name: "PA3",    number: "3",  at: {x: -2.54, y: -5.08,  angle: 0}, type: "passive" }
    - { name: "PA5",    number: "4",  at: {x: -2.54, y: -7.62,  angle: 0}, type: "passive" }
    - { name: "PA6",    number: "5",  at: {x: -2.54, y: -10.16, angle: 0}, type: "passive" }
    - { name: "PA7",    number: "6",  at: {x: -2.54, y: -12.70, angle: 0}, type: "passive" }
    - { name: "PA9",    number: "7",  at: {x: -2.54, y: -15.24, angle: 0}, type: "passive" }
    - { name: "PA10",   number: "8",  at: {x: -2.54, y: -17.78, angle: 0}, type: "passive" }
    - { name: "PA13",   number: "9",  at: {x: -2.54, y: -20.32, angle: 0}, type: "passive" }
    - { name: "PA14",   number: "10", at: {x: -2.54, y: -22.86, angle: 0}, type: "passive" }
    - { name: "PB0",    number: "11", at: {x: -2.54, y: -25.40, angle: 0}, type: "passive" }
    - { name: "PB1",    number: "12", at: {x: -2.54, y: -27.94, angle: 0}, type: "passive" }
    - { name: "PB6",    number: "13", at: {x: -2.54, y: -30.48, angle: 0}, type: "passive" }
    - { name: "PC13",   number: "14", at: {x: -2.54, y: -33.02, angle: 0}, type: "passive" }
    - { name: "USB_DM", number: "15", at: {x: -2.54, y: -35.56, angle: 0}, type: "passive" }
    - { name: "USB_DP", number: "16", at: {x: -2.54, y: -38.10, angle: 0}, type: "passive" }
    - { name: "3V3",    number: "17", at: {x: -2.54, y: -40.64, angle: 0}, type: "passive" }
    - { name: "5V",     number: "18", at: {x: -2.54, y: -43.18, angle: 0}, type: "passive" }
    - { name: "GND",    number: "19", at: {x: -2.54, y: -45.72, angle: 0}, type: "passive" }
```

- [ ] **Step 2: Create `ST7789_DISPLAY`**

```
mcp__kicad__create_symbol
  libraryPath: "/home/godar/Projects/stm32/silkscreen/hardware/blackpill_wiring/blackpill_wiring.kicad_sym"
  name: "ST7789_DISPLAY"
  referencePrefix: "J"
  description: "ST7789 240x240 IPS display module (GMT130) -- module pins only, no MISO/CS."
  footprint: "~"
  datasheet: "~"
  rectangles: [{ x1: 0, y1: 1.27, x2: 15.24, y2: -13.97 }]
  pins:
    - { name: "SCK",     number: "1", at: {x: -2.54, y: 0,      angle: 0}, type: "passive" }
    - { name: "MOSI",    number: "2", at: {x: -2.54, y: -2.54,  angle: 0}, type: "passive" }
    - { name: "DC",      number: "3", at: {x: -2.54, y: -5.08,  angle: 0}, type: "passive" }
    - { name: "RST",     number: "4", at: {x: -2.54, y: -7.62,  angle: 0}, type: "passive" }
    - { name: "VCC_BLK", number: "5", at: {x: -2.54, y: -10.16, angle: 0}, type: "passive" }
    - { name: "GND",     number: "6", at: {x: -2.54, y: -12.70, angle: 0}, type: "passive" }
```

- [ ] **Step 3: Create `HOBBY_SERVO`**

```
mcp__kicad__create_symbol
  libraryPath: "/home/godar/Projects/stm32/silkscreen/hardware/blackpill_wiring/blackpill_wiring.kicad_sym"
  name: "HOBBY_SERVO"
  referencePrefix: "J"
  description: "Hobby servo, TIM4 CH1, 50 Hz -- module pins."
  footprint: "~"
  datasheet: "~"
  rectangles: [{ x1: 0, y1: 1.27, x2: 15.24, y2: -6.35 }]
  pins:
    - { name: "SIGNAL", number: "1", at: {x: -2.54, y: 0,     angle: 0}, type: "passive" }
    - { name: "PLUS",   number: "2", at: {x: -2.54, y: -2.54, angle: 0}, type: "passive" }
    - { name: "GND",    number: "3", at: {x: -2.54, y: -5.08, angle: 0}, type: "passive" }
```

- [ ] **Step 4: Create `CRSF_RECEIVER`**

```
mcp__kicad__create_symbol
  libraryPath: "/home/godar/Projects/stm32/silkscreen/hardware/blackpill_wiring/blackpill_wiring.kicad_sym"
  name: "CRSF_RECEIVER"
  referencePrefix: "J"
  description: "TBS/ELRS CRSF receiver -- module pins. RX switched to CRSF mode first."
  footprint: "~"
  datasheet: "~"
  rectangles: [{ x1: 0, y1: 1.27, x2: 15.24, y2: -8.89 }]
  pins:
    - { name: "TX",   number: "1", at: {x: -2.54, y: 0,     angle: 0}, type: "passive" }
    - { name: "RX",   number: "2", at: {x: -2.54, y: -2.54, angle: 0}, type: "passive" }
    - { name: "PLUS", number: "3", at: {x: -2.54, y: -5.08, angle: 0}, type: "passive" }
    - { name: "GND",  number: "4", at: {x: -2.54, y: -7.62, angle: 0}, type: "passive" }
```

- [ ] **Step 5: Create `BRUSHLESS_ESC`**

```
mcp__kicad__create_symbol
  libraryPath: "/home/godar/Projects/stm32/silkscreen/hardware/blackpill_wiring/blackpill_wiring.kicad_sym"
  name: "BRUSHLESS_ESC"
  referencePrefix: "J"
  description: "Brushless ESC, TIM3 CH1 PWM out -- module pins. Own supply only, never board 5V."
  footprint: "~"
  datasheet: "~"
  rectangles: [{ x1: 0, y1: 1.27, x2: 15.24, y2: -3.81 }]
  pins:
    - { name: "SIGNAL", number: "1", at: {x: -2.54, y: 0,     angle: 0}, type: "passive" }
    - { name: "GND",    number: "2", at: {x: -2.54, y: -2.54, angle: 0}, type: "passive" }
```

- [ ] **Step 6: Create `ESP01_WIFI`**

```
mcp__kicad__create_symbol
  libraryPath: "/home/godar/Projects/stm32/silkscreen/hardware/blackpill_wiring/blackpill_wiring.kicad_sym"
  name: "ESP01_WIFI"
  referencePrefix: "J"
  description: "ESP-01 (ESP8266, stock AT firmware) -- module pins. CH_PD/GPIO0/GPIO2/RST pulled high locally."
  footprint: "~"
  datasheet: "~"
  rectangles: [{ x1: 0, y1: 1.27, x2: 15.24, y2: -8.89 }]
  pins:
    - { name: "TXD", number: "1", at: {x: -2.54, y: 0,     angle: 0}, type: "passive" }
    - { name: "RXD", number: "2", at: {x: -2.54, y: -2.54, angle: 0}, type: "passive" }
    - { name: "VCC", number: "3", at: {x: -2.54, y: -5.08, angle: 0}, type: "passive" }
    - { name: "GND", number: "4", at: {x: -2.54, y: -7.62, angle: 0}, type: "passive" }
```

- [ ] **Step 7: Create `USB_HOST`**

```
mcp__kicad__create_symbol
  libraryPath: "/home/godar/Projects/stm32/silkscreen/hardware/blackpill_wiring/blackpill_wiring.kicad_sym"
  name: "USB_HOST"
  referencePrefix: "J"
  description: "USB-C to host PC -- Configurator link, serial console, one-click DFU (PA11/PA12, built in)."
  footprint: "~"
  datasheet: "~"
  rectangles: [{ x1: 0, y1: 1.27, x2: 15.24, y2: -3.81 }]
  pins:
    - { name: "DM", number: "1", at: {x: -2.54, y: 0,     angle: 0}, type: "passive" }
    - { name: "DP", number: "2", at: {x: -2.54, y: -2.54, angle: 0}, type: "passive" }
```

- [ ] **Step 8: Register the library**

```
mcp__kicad__register_symbol_library
  libraryPath: "/home/godar/Projects/stm32/silkscreen/hardware/blackpill_wiring/blackpill_wiring.kicad_sym"
  libraryName: "blackpill_wiring"
  scope: "project"
  projectPath: "/home/godar/Projects/stm32/silkscreen/hardware/blackpill_wiring/blackpill_wiring.kicad_pro"
```

- [ ] **Step 9: Verify**

```
mcp__kicad__list_symbols_in_library
  libraryPath: "/home/godar/Projects/stm32/silkscreen/hardware/blackpill_wiring/blackpill_wiring.kicad_sym"
```
Expected: all 7 names — `BLACKPILL_F411CE`, `ST7789_DISPLAY`, `HOBBY_SERVO`, `CRSF_RECEIVER`, `BRUSHLESS_ESC`, `ESP01_WIFI`, `USB_HOST`.

---

### Task 3: Place and wire all 7 components

**Files:**
- Modify: `hardware/blackpill_wiring/blackpill_wiring.kicad_sch`

**Interfaces:**
- Consumes: the 7 symbols from Task 2, referenced as `blackpill_wiring:<NAME>`.
- Produces: 19 nets (listed in the verify step) that Task 4's annotations sit next to.

Net names used below are the plan's own naming (the spec's Connections section describes the pairing, not exact net-label text) — use them exactly as written so nets line up:

| Net | Blackpill pin | Other pin(s) |
|---|---|---|
| `DISP_SCK` | PA5 | ST7789_DISPLAY.SCK |
| `DISP_MOSI` | PA7 | ST7789_DISPLAY.MOSI |
| `DISP_DC` | PB1 | ST7789_DISPLAY.DC |
| `DISP_RST` | PB0 | ST7789_DISPLAY.RST |
| `SERVO_SIG` | PB6 | HOBBY_SERVO.SIGNAL |
| `RX_TX` | PA10 | CRSF_RECEIVER.TX |
| `RX_RX` | PA9 | CRSF_RECEIVER.RX |
| `ESC_SIG` | PA6 | BRUSHLESS_ESC.SIGNAL |
| `WIFI_TXD` | PA3 | ESP01_WIFI.TXD |
| `WIFI_RXD` | PA2 | ESP01_WIFI.RXD |
| `USB_DM` | USB_DM | USB_HOST.DM |
| `USB_DP` | USB_DP | USB_HOST.DP |
| `3V3` | 3V3 | ST7789_DISPLAY.VCC_BLK, ESP01_WIFI.VCC |
| `5V` | 5V | HOBBY_SERVO.PLUS, CRSF_RECEIVER.PLUS |
| `GND` | GND | ST7789_DISPLAY.GND, HOBBY_SERVO.GND, CRSF_RECEIVER.GND, BRUSHLESS_ESC.GND, ESP01_WIFI.GND |
| `KEY_PA0` | PA0 | (none — onboard button) |
| `LED_PC13` | PC13 | (none — onboard LED) |
| `SWDIO` | PA13 | (none — onboard SWD) |
| `SWCLK` | PA14 | (none — onboard SWD) |

- [ ] **Step 1: Place and connect everything in one call**

```
mcp__kicad__batch_add_and_connect
  schematicPath: "/home/godar/Projects/stm32/silkscreen/hardware/blackpill_wiring/blackpill_wiring.kicad_sch"
  labelType: "label"
  components:
    - symbol: "blackpill_wiring:BLACKPILL_F411CE"
      reference: "U1"
      position: {x: 120, y: 80}
      nets: { PA0: "KEY_PA0", PA2: "WIFI_RXD", PA3: "WIFI_TXD", PA5: "DISP_SCK", PA6: "ESC_SIG",
              PA7: "DISP_MOSI", PA9: "RX_RX", PA10: "RX_TX", PA13: "SWDIO", PA14: "SWCLK",
              PB0: "DISP_RST", PB1: "DISP_DC", PB6: "SERVO_SIG", PC13: "LED_PC13",
              USB_DM: "USB_DM", USB_DP: "USB_DP", "3V3": "3V3", "5V": "5V", GND: "GND" }
    - symbol: "blackpill_wiring:ST7789_DISPLAY"
      reference: "J1"
      position: {x: 40, y: 20}
      nets: { SCK: "DISP_SCK", MOSI: "DISP_MOSI", DC: "DISP_DC", RST: "DISP_RST", VCC_BLK: "3V3", GND: "GND" }
    - symbol: "blackpill_wiring:HOBBY_SERVO"
      reference: "J2"
      position: {x: 40, y: 140}
      nets: { SIGNAL: "SERVO_SIG", PLUS: "5V", GND: "GND" }
    - symbol: "blackpill_wiring:CRSF_RECEIVER"
      reference: "J3"
      position: {x: 220, y: 20}
      nets: { TX: "RX_TX", RX: "RX_RX", PLUS: "5V", GND: "GND" }
    - symbol: "blackpill_wiring:BRUSHLESS_ESC"
      reference: "J4"
      position: {x: 220, y: 90}
      nets: { SIGNAL: "ESC_SIG", GND: "GND" }
    - symbol: "blackpill_wiring:ESP01_WIFI"
      reference: "J5"
      position: {x: 220, y: 140}
      nets: { TXD: "WIFI_TXD", RXD: "WIFI_RXD", VCC: "3V3", GND: "GND" }
    - symbol: "blackpill_wiring:USB_HOST"
      reference: "J6"
      position: {x: 120, y: 190}
      nets: { DM: "USB_DM", DP: "USB_DP" }
```

- [ ] **Step 2: Verify components landed**

```
mcp__kicad__list_schematic_components
  schematicPath: "/home/godar/Projects/stm32/silkscreen/hardware/blackpill_wiring/blackpill_wiring.kicad_sch"
```
Expected: 7 components — `U1`, `J1`..`J6` — each with the lib IDs above.

- [ ] **Step 3: Verify nets**

```
mcp__kicad__list_schematic_nets
  schematicPath: "/home/godar/Projects/stm32/silkscreen/hardware/blackpill_wiring/blackpill_wiring.kicad_sch"
```
Expected: 19 nets matching the table above exactly — `GND` with 6 pins, `3V3` and `5V` with 3 pins each, `DISP_*`/`RX_*`/`ESC_SIG`/`WIFI_*`/`USB_*` with 2 pins each, `KEY_PA0`/`LED_PC13`/`SWDIO`/`SWCLK` with 1 pin each. If any net is missing a pin or has an extra one, re-check the `nets` map on the relevant component in Step 1 and re-run `batch_add_and_connect` for just that component (or delete and redo — don't hand-patch with raw wires).

- [ ] **Step 4: Save**

```
mcp__kicad__save_project
```

---

### Task 4: Add the caveat annotations

**Files:**
- Modify: `hardware/blackpill_wiring/blackpill_wiring.kicad_sch`

**Interfaces:**
- Consumes: component positions from Task 3 (J1 @ 40,20 / J2 @ 40,140 / J3 @ 220,20 / J4 @ 220,90 / J5 @ 220,140).

- [ ] **Step 1: Display note**

```
mcp__kicad__add_schematic_text
  schematicPath: "/home/godar/Projects/stm32/silkscreen/hardware/blackpill_wiring/blackpill_wiring.kicad_sch"
  text: "No CS, no MISO -- driver is write-only; panel presence can't be detected."
  position: [40, 45]
```

- [ ] **Step 2: Servo note**

```
mcp__kicad__add_schematic_text
  schematicPath: "/home/godar/Projects/stm32/silkscreen/hardware/blackpill_wiring/blackpill_wiring.kicad_sch"
  text: "Never 3V3. Add a 470-1000 uF bulk cap at the connector -- a moving servo can droop VBUS enough to reset the MCU."
  position: [40, 165]
```

- [ ] **Step 3: Receiver PA9 note**

```
mcp__kicad__add_schematic_text
  schematicPath: "/home/godar/Projects/stm32/silkscreen/hardware/blackpill_wiring/blackpill_wiring.kicad_sch"
  text: "RX_RX (PA9): reserved for CRSF telemetry back-channel -- wired but not required by firmware today."
  position: [220, 45]
```

- [ ] **Step 4: ESC note**

```
mcp__kicad__add_schematic_text
  schematicPath: "/home/godar/Projects/stm32/silkscreen/hardware/blackpill_wiring/blackpill_wiring.kicad_sch"
  text: "Own supply only -- never board 5V. Common ground with the board is still required, even so."
  position: [220, 115]
```

- [ ] **Step 5: WiFi note**

```
mcp__kicad__add_schematic_text
  schematicPath: "/home/godar/Projects/stm32/silkscreen/hardware/blackpill_wiring/blackpill_wiring.kicad_sch"
  text: "CH_PD, GPIO0, GPIO2, RST: pulled high locally on the module (10k to 3V3) -- no STM32 pin needed for any of them."
  position: [220, 165]
```

- [ ] **Step 6: Save, then visual check**

```
mcp__kicad__save_project
```
```
mcp__kicad__get_schematic_view
  schematicPath: "/home/godar/Projects/stm32/silkscreen/hardware/blackpill_wiring/blackpill_wiring.kicad_sch"
  width: 1600
  height: 1200
```
Look at the returned image. If any text overlaps a symbol body or another note, nudge the offending `add_schematic_text` call's `position` (or the component's position back in Task 3) and re-save — don't leave overlapping text in place.

---

### Task 5: ERC and export

**Files:**
- Create: `hardware/blackpill_wiring/blackpill_wiring.svg` (visual reference export, not committed — see Task 6)

**Interfaces:**
- Consumes: the completed schematic from Tasks 3-4.

- [ ] **Step 1: Run ERC**

```
mcp__kicad__run_erc
  schematicPath: "/home/godar/Projects/stm32/silkscreen/hardware/blackpill_wiring/blackpill_wiring.kicad_sch"
```
Expected: the only violations are single-pin-net notices on `KEY_PA0`, `LED_PC13`, `SWDIO`, `SWCLK` (4 onboard-only pins with no partner symbol, per spec). Anything else — unresolved pin, duplicate reference, missing library symbol — is a real problem: stop and fix it (check Task 2/3 first) rather than proceeding.

- [ ] **Step 2: Export SVG for the side-by-side check**

```
mcp__kicad__export_schematic_svg
  schematicPath: "/home/godar/Projects/stm32/silkscreen/hardware/blackpill_wiring/blackpill_wiring.kicad_sch"
  outputPath: "/home/godar/Projects/stm32/silkscreen/hardware/blackpill_wiring/blackpill_wiring.svg"
```

- [ ] **Step 3: Compare against the Examples page**

Open `blackpill_wiring.svg` and compare it stub-by-stub against the wiring diagram in `app/web/index.html` (search for `Wiring a Black Pill`, the `<svg viewBox="0 0 1200 1130"...>` block around line 585). Confirm every pin name and every net pairing in the table under Task 3 has a visible match. This is a manual check — there's no automated diff between a hand-drawn illustrative SVG and a schematic export.

- [ ] **Step 4: Delete the throwaway export**

The exported SVG was only for this comparison; the checked-in project stays schematic-source-only.
```bash
rm /home/godar/Projects/stm32/silkscreen/hardware/blackpill_wiring/blackpill_wiring.svg
```

---

### Task 6: Commit

**Files:**
- Create (git-tracked): `hardware/blackpill_wiring/blackpill_wiring.kicad_pro`, `.kicad_sch`, `.kicad_sym`, and the project's `sym-lib-table` (plus an empty `.kicad_pcb` if Task 1 generated one).

- [ ] **Step 1: Review what's staged**

```bash
cd /home/godar/Projects/stm32/silkscreen && git status --short hardware/
```
Expected: only new files under `hardware/blackpill_wiring/`. Nothing matching the KiCad-cache patterns added to `.gitignore` earlier (`*.kicad_prl`, `*-backups/`, `_autosave-*`, `fp-info-cache`) should show up — if one does, the ignore pattern didn't match; don't force-add it, fix the pattern instead.

- [ ] **Step 2: Commit**

```bash
git add hardware/blackpill_wiring/
git commit -m "$(cat <<'EOF'
feat(hardware): add Black Pill wiring KiCad schematic

Schematic-capture reference matching every connection in the Examples
page's SVG wiring diagram: 7 custom connector symbols (display, servo,
receiver, ESC, WiFi, USB, plus the Black Pill itself), wired with net
labels, with the SVG's own caveats carried over as schematic text.

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
)"
```

- [ ] **Step 3: Verify**

```bash
git log -1 --stat
```
Expected: the commit includes exactly the files from Step 1's review.
