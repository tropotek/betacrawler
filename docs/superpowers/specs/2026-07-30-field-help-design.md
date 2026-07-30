# Config-page field help text

## Problem

The Configuration page's `.form-text` line under each input currently shows only an
auto-generated bound (`1–20`, `max 16 chars`) computed from the field's `type`/`min`/`max`/
`maxlen` — no description of what the field actually does. `_notes/todo.md` asks for real help
text under each input.

## Constraint this has to respect

`docs/architecture.md`'s schema-driven-UI rule: adding a firmware parameter must need **zero**
changes to `app.js`. There is deliberately no field-label map or field-order table in the app —
those come from the wire schema. This feature must not become a de-facto label/order map; it is
purely supplementary prose that a field renders fine without.

## Design

**New file: `app/web/field_help.js`.** One flat object, keyed by the param's dotted key exactly
as the schema carries it:

```js
const FIELD_HELP = {
  'device.name': "...",
  'led.mode': "...",
  // one entry per current param, see Content below
};
```

Loaded via a new `<script src="field_help.js"></script>` tag in `index.html`, placed before
`app.js`. Neither file is an ES module, so this is a plain top-level `const` in the shared global
scope — `app.js` reads `FIELD_HELP` directly, no `window.` prefix, no bundler, consistent with how
`vendor/*.js` is already loaded.

**Consumption.** In `Alpine.store('config')`'s `groups` getter (`app.js`, currently around
line 257-264), the per-field `help` value changes from:

```js
help: p.type === 'u8' ? `${p.min}–${p.max}`
    : p.type === 'str' ? `max ${p.maxlen} chars` : '',
```

to:

```js
help: FIELD_HELP[p.key] ?? (p.type === 'u8' ? `${p.min}–${p.max}`
    : p.type === 'str' ? `max ${p.maxlen} chars` : ''),
```

A mapped key's authored text fully replaces the old auto-generated hint in the existing
`.form-text` element (`id="h-" + field.key`, unchanged). An unmapped key falls back to today's
bound-only text — this is the load-bearing part: a newly-added firmware param renders correctly
with zero `app.js` changes and zero required edit to `field_help.js`, exactly preserving the
existing rule. Someone adds a `field_help.js` entry for it whenever they get to it; nothing breaks
or looks wrong in the meantime.

**Content.** Because the help text *replaces* rather than sits beside the bound, each entry is
authored to fold in the relevant unit/range/behavior where it matters (e.g. `servo.angle` →
"Commanded servo position, 0–180°." rather than a bare behavioral description with the range
silently dropped). One entry is written for every param currently in the schema (22 keys across
`device`, `tlm`, `led`, `servo`, `esc`, `rx`/`crossfire`/`elrs`, `st7789_240x240` — see
`firmware/test/golden/schema.json` for the authoritative current list), sourced from each
module's `ParamDef` comments and `docs/architecture.md`/`_notes/` reasoning already on file, so
the page ships fully populated.

**Scope.** Config page only. Telemetry cards keep their current unit-only caption — the user's
larger idea of app-configurable field maps spanning config *and* telemetry for custom pages is
explicitly future work, not part of this change.

## No backend or firmware changes

Purely `app/web/` — a new static file plus a two-line change to one getter and one `<script>` tag.

## Testing

No JS test framework exists in this repo by design (`CLAUDE.md`: "no automated UI test suite
exists ... but 'therefore you cannot check the UI' does not follow"). Verify with a
headless-Playwright pass (`~/.pwvenv`) against a backend served with a fake device
(`app/tests/fake_serial.py` + `DeviceModel`, the pattern `app/tests/` already establishes):

- A couple of mapped fields (e.g. `servo.angle`, `led.blink_hz`) show the new authored text in
  their `.form-text` element instead of the old bound-only hint.
- With one entry temporarily removed from `FIELD_HELP`, that field's `.form-text` falls back to
  the old auto-generated bound — proving the fallback path, not just the happy path.

## Out of scope (explicitly deferred)

- Telemetry-page help text / captions.
- The broader "project-configurable field maps" idea (custom pages mixing config + telemetry
  fields) — this spec is scoped to being a solid first step toward it, not the thing itself.
