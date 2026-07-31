# Black Pill wiring — KiCad schematic design

## Purpose

`app/web/index.html`'s Examples page carries one hand-drawn SVG wiring diagram ("Wiring a
Black Pill") documenting how the ST7789 display, hobby servo, CRSF receiver, brushless ESC and
ESP-01 WiFi module connect to a WeAct STM32F411CE Black Pill. That SVG is illustrative only —
there is no schematic-capture source behind it. This spec adds one: a KiCad project under
`hardware/` that captures the same connections as real symbols and nets, using the `kicad` MCP
server now registered for this project.

## Scope

**Schematic capture only** — no PCB layout, no footprints, nothing gets fabricated. This is
reference material for a bench of off-the-shelf modules wired point-to-point, not a board being
designed. One KiCad project per SVG diagram; today there is exactly one diagram, so exactly one
project.

Out of scope, deliberately:
- PCB layout / routing / footprints — nothing here is being fabricated.
- Real manufacturer symbols for the MCU or the ESP-01. KiCad's bundled libraries only have the
  bare-die parts (`MCU_ST_STM32F4:STM32F411CEUx`, `MCU_Espressif:ESP8266EX`), not the assembled
  modules actually on the bench (Black Pill dev board, ESP-01 module). Using the die symbols would
  pull in ~30 pins (VDD/VSS/VBAT/BOOT0/NRST/decoupling) that are already handled on those modules
  and aren't part of what a user wires — it would misrepresent the diagram's own abstraction
  level. Every part in this schematic is therefore a custom-drawn connector-style symbol with
  named pins, matching the SVG's own treatment of each module as a sealed unit exposing only its
  external pins.
- Automated generation of the SVG from the schematic, or vice versa. The two stay in sync by hand,
  same as today — this schematic is a cross-check reference, not a build input for `app/web`.

## Location

```
hardware/blackpill_wiring/
  blackpill_wiring.kicad_pro     project file
  blackpill_wiring.kicad_sch     single schematic sheet
  blackpill_wiring.kicad_sym     project-local symbol library (the 7 symbols below)
```

A future board or peripheral diagram added to the Examples page gets its own sibling folder under
`hardware/`, not a second sheet in this project.

## Symbols

Seven custom symbols in the project-local library, each pin named to match the SVG's own labels
exactly (not generic numbered pins). `BLACKPILL_F411CE`'s pins are split left/right, mirroring
the SVG's own left/right stub grouping, so every connecting wire is a straight horizontal run —
see Connections below. Onboard-only features (`PC13`/`PA0`/`PA13`/`PA14`) are **not** pins on this
symbol; the SVG draws those as plain text badges, not stubs, so they're schematic text instead
(see Annotations).

| Symbol | Pins | Side (on `BLACKPILL_F411CE`) |
|---|---|---|
| `BLACKPILL_F411CE` | PA5, PA7, PB1, PB0, 3V3, GND, PB6, 5V, GND, USB_DM, USB_DP (left) / PA10, PA9, 5V, GND, PA6, GND, PA3, PA2, 3V3, GND (right) | — |
| `ST7789_DISPLAY` | SCK, MOSI, DC, RST, VCC_BLK, GND | left |
| `HOBBY_SERVO` | SIGNAL, PLUS, GND | left |
| `USB_HOST` | DM, DP | left |
| `CRSF_RECEIVER` | TX, RX, PLUS, GND | right |
| `BRUSHLESS_ESC` | SIGNAL, GND | right |
| `ESP01_WIFI` | TXD, RXD, VCC, GND | right |

Note `BLACKPILL_F411CE` has **multiple pins named `GND`/`3V3`/`5V`** (one per module group, same
as the SVG draws a separate stub per group even where two stubs are electrically the same rail).
KiCad allows duplicate pin names on one symbol; each is wired independently.

## Connections

Implemented as real routed wires (`add_schematic_wire`), not net labels — a first version used
labels only and was electrically correct but unreadable as a wiring diagram when opened (no
visible line between components). Every module is placed at the Black Pill's own sheet Y, with
its local pin Y-offsets designed to exactly match the corresponding Black Pill pin, so every one
of the 21 wires below is a straight 2-point horizontal segment — no waypoint routing needed:

- **Display**: PA5–SCK, PA7–MOSI, PB1–DC, PB0–RST, 3V3–VCC_BLK, GND–GND
- **Servo**: PB6–SIGNAL, 5V–PLUS, GND–GND
- **USB**: USB_DM–DM, USB_DP–DP
- **Receiver**: PA10–TX, PA9–RX (reserved, see Annotations), 5V–PLUS, GND–GND
- **ESC**: PA6–SIGNAL, GND–GND — **no power pin wired**, matching the SVG's "own supply only,
  never board 5V"
- **WiFi**: PA3–TXD, PA2–RXD, 3V3–VCC, GND–GND

Each pair above is its own independent 2-pin net (21 total) — since each module gets its own
dedicated GND/3V3/5V pin on the Black Pill symbol rather than sharing one, there's no merged
"GND net with N members" the way a label-based version would have; that's expected, not a defect.

## Annotations

Schematic text notes. Three replace the onboard pins removed from `BLACKPILL_F411CE` (placed near
U1); five carry over the SVG's module callouts verbatim (placed near their module):
- "PC13: LED (LOW=on, onboard)"
- "PA0: KEY button (onboard)"
- "PA13/PA14: SWD (alt. flash)"
- Display: "No CS, no MISO — driver is write-only; panel presence can't be detected."
- Servo: "Never 3V3. Add a 470–1000 µF bulk cap at the connector — a moving servo can droop VBUS
  enough to reset the MCU."
- Receiver: "PA9 (receiver RX): reserved for CRSF telemetry back-channel — wired but not required
  by firmware today."
- ESC: "Own supply only — never board 5V. Common ground with the board is still required, even
  so."
- WiFi: "CH_PD, GPIO0, GPIO2, RST: pulled high locally on the module (10k to 3V3) — no STM32 pin
  needed for any of them."

## Validation

- `kicad-cli sch export netlist` (real KiCad, not the MCP server's own `list_schematic_nets` —
  that tool gave a false "no nets found" on this file; see
  `kicad-schematic-needs-real-wires` in project memory) shows all 21 point-to-point nets wired
  correctly.
- `kicad-cli sch erc` / `run_erc`: 0 errors, 21 warnings — 7 benign `lib_symbol_mismatch` cache
  nags and 14 benign `endpoint_off_grid` notices (cosmetic; connectivity independently confirmed
  via the netlist export above).
- Visual check: export the schematic and compare side-by-side against the Examples-page SVG to
  confirm every stub and rail matches, and that wires are actually visible when the project is
  opened in KiCad.

## Testing

No automated test suite applies — this is reference documentation, not code. Validation is the
ERC run plus the visual side-by-side above.
