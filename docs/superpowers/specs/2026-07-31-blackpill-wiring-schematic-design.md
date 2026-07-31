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
- Real manufacturer symbols for the MCU or the ESP-01. KiCad 9's bundled libraries only have the
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
exactly (not generic numbered pins):

| Symbol | Pins |
|---|---|
| `BLACKPILL_F411CE` | PA0, PA2, PA3, PA5, PA6, PA7, PA9, PA10, PA13, PA14, PB0, PB1, PB6, PC13, USB_DM, USB_DP, 3V3, 5V, GND |
| `ST7789_DISPLAY` | SCK, MOSI, DC, RST, VCC_BLK, GND |
| `HOBBY_SERVO` | SIGNAL, PLUS, GND |
| `CRSF_RECEIVER` | TX, RX, PLUS, GND |
| `BRUSHLESS_ESC` | SIGNAL, GND |
| `ESP01_WIFI` | TXD, RXD, VCC, GND |
| `USB_HOST` | DM, DP |

## Connections

Implemented as net labels (matching text at each pin), not point-to-point wire routing — the
symbols sit wherever placement lands them, and labels avoid needing collision-free wire paths
across the sheet. One net label pair per SVG stub, plus three shared rails (`3V3`, `5V`, `GND`)
carrying the same label to every pin that shares that rail in the SVG:

- **Display**: PA5→SCK, PA7→MOSI, PB1→DC, PB0→RST, 3V3→VCC_BLK, GND→GND
- **Servo**: PB6→SIGNAL, 5V→PLUS, GND→GND
- **Receiver**: PA10→TX, PA9→RX (reserved, see Annotations), 5V→PLUS, GND→GND
- **ESC**: PA6→SIGNAL, GND→GND — **no power pin wired**, matching the SVG's "own supply only,
  never board 5V"
- **WiFi**: PA3→TXD, PA2→RXD, 3V3→VCC, GND→GND
- **USB**: USB_DM→DM, USB_DP→DP

Shared rails: a single `GND` net ties every module's GND together with the Black Pill's GND. A
single `5V` net ties the Black Pill, servo and receiver (not the ESC). A single `3V3` net ties the
Black Pill, display and WiFi module.

`PC13`, `PA0`, `PA13`, `PA14` get net labels only (`LED_PC13`, `KEY_PA0`, `SWDIO`, `SWCLK`) and no
wire to another symbol — these are onboard Black Pill features, not external wiring, matching how
the SVG shows them as text badges rather than stub connections.

## Annotations

Schematic text notes carrying over the SVG's callouts verbatim:
- Display: "No CS, no MISO — driver is write-only; panel presence can't be detected."
- Servo: "Never 3V3. Add a 470–1000 µF bulk cap at the connector — a moving servo can droop VBUS
  enough to reset the MCU."
- ESC: "Own supply only — never board 5V. Common ground with the board is still required, even
  so."
- WiFi: "CH_PD, GPIO0, GPIO2, RST: pulled high locally on the module (10k to 3V3) — no STM32 pin
  needed for any of them."
- Receiver: PA9 marked reserved — wired but not required by firmware today (CRSF telemetry
  back-channel, reserved for future use).

## Validation

- `run_erc` comes back clean, aside from expected "unused pin" notices on the four onboard-only
  Black Pill pins (they intentionally have no partner symbol).
- Visual check: export the schematic and compare side-by-side against the Examples-page SVG to
  confirm every stub and rail matches.

## Testing

No automated test suite applies — this is reference documentation, not code. Validation is the
ERC run plus the visual side-by-side above.
