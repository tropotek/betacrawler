// Descriptive help text for the Configuration page, shown in each field's
// .form-text line -- replaces the bound-only hint app.js falls back to when
// a key has no entry here. Keyed by the same dotted key the wire schema
// carries (firmware/test/golden/schema.json is the authoritative list of
// what currently exists). A param with no entry here still renders fine --
// see docs/architecture.md's schema-driven-UI rule -- it just shows the
// plain bound until someone adds a line here.
// Numeric bounds quoted in this copy are hand-copied from each param's ParamDef in firmware --
// if a bound ever changes there, update the matching line here too.
const FIELD_HELP = {
  'device.name': "A short name for this board, shown in the navbar, app title, and the on-board display's info page — cosmetic only, up to 31 characters.",
  'tlm.rate': 'How often telemetry pushes to the app over the WebSocket, 1–50 Hz. Higher rates cost more USB bandwidth; the Configuration form itself does not need this to be fast.',

  'led.mode': '"off" disables the LED; "on" holds it steady; "blink" pulses on/off; "fade" breathes smoothly. The LED Rate field controls blink/fade speed in both modes.',
  'led.blink_hz': 'Cycles per second in both blink and fade modes, 1–20 Hz.',

  'servo.mode': '"off" commands nothing; "hold" drives a fixed angle; "sweep" sends the servo back and forth continuously; "input" follows a selected receiver channel.',
  'servo.angle': 'Commanded position in hold mode, 0–180 degrees.',
  'servo.sweep_s': 'Seconds for one full sweep cycle (0° to 180° and back), 1–30 s. Not a rate in Hz — most hobby servos cannot physically track a full sweep faster than about a second.',
  'servo.min_us': "Pulse width commanded at the 0-degree end of travel, 500–1500 microseconds. Calibrate this to the servo's real endpoint.",
  'servo.max_us': "Pulse width commanded at the 180-degree end of travel, 1500–2500 microseconds. Calibrate this to the servo's real endpoint.",
  'servo.src': "Which receiver channel drives the servo in input mode (ch1–ch12, matching the RX module's own channel numbering). Only used in input mode.",

  'esc.direction': '"unidirectional" treats the low end of the throttle range as stop (the default, and safe for most ESCs); "bidirectional" treats the middle as stop, either side driving forward or reverse. Get this wrong and arming will not behave as expected.',
  'esc.mode': '"off" commands nothing; "armed" drives Throttle directly; "input" follows a selected receiver channel. Arming always starts at the safe/neutral position and holds briefly (~2s by default) before honouring a commanded value.',
  'esc.throttle_us': 'Commanded pulse width in armed mode, 1000–2000 microseconds.',
  'esc.min_us': "Lowest pulse the ESC will ever be commanded, 500–1500 microseconds — stop on a unidirectional ESC, full reverse on a bidirectional one. Calibrate this to the ESC's real endpoint.",
  'esc.max_us': "Pulse width commanded at the high end of the throttle range, 1500–2500 microseconds. Calibrate this to the ESC's real endpoint.",
  'esc.src': "Which receiver channel drives the ESC in input mode (ch1–ch12, matching the RX module's own channel numbering). Only shown while off — change it before arming, not while live.",

  'rx.protocol': '"crossfire" (TBS Crossfire) or "elrs" (ExpressLRS) — which receiver protocol to decode. Changing this switches which timeout setting applies.',
  'rx.source': '"uart" reads a real receiver wired to the board; "sim" generates fake channel data for testing without hardware.',
  'crossfire.timeout_ms': "How long without a valid frame before the link is considered lost, 100–2000 ms. TBS's own guidance is to wait about 1 second.",
  'elrs.timeout_ms': 'How long without a valid frame before the link is considered lost, 50–2000 ms. Kept far shorter than Crossfire\'s — this is a link monitor, not a failsafe trigger.',

  'disp.mode': 'Turns the on-board display on or off.',
  'disp.page': '"info" shows device identity, "stats" shows live telemetry, "cycle" rotates through both.',
  'disp.rate': "How often the display redraws, 1–10 Hz. Capped low because each refresh costs real SPI time; independent of the device's Telemetry Rate setting.",
};
