---
name: wiring-diagram-svg
description: Use when creating or extending a hardware wiring diagram for betacrawler's Wiring page — a new board, or a new peripheral card on an existing board's SVG diagram.
---

# Wiring Diagram SVG

## Overview

Betacrawler's Wiring page uses one hand-authored schematic SVG format for every board-wiring
diagram — a wiring map grouped by peripheral, not a photorealistic/Fritzing-style render (no such
tool is available). It was converged on over several iterative rounds; reuse it exactly rather
than re-deriving a layout.

## When to use

- Adding a new peripheral card to an existing board diagram
- Documenting wiring for a new board
- NOT for: circuit schematics with electronic components (resistors, ICs, logic gates) — this
  format is board-to-module pin wiring only

## Reference implementation

The full worked example (Black Pill + battery/PDB/ESC/motor/receiver/USB cards) lives in
`web-app/pages/wiring.html`, inside `<div class="wiring-diagram">`. CSS tokens are in
`web-app/index.html`'s `<style>` block under `.wiring-diagram { --wd-* }`. Copy an existing card
(the "Motor 0 card" is the smallest complete one) as your starting point — don't build a card from
a blank SVG.

The docs site carries the same diagram as two PNGs, captured off the live page by
`docs-tools/capture_screenshots.py` — one inline, one full-size for the reader to open and zoom.
Re-run that script after changing the SVG or the docs page goes stale.

## Anatomy (non-negotiable)

- One `<div class="wiring-diagram">` wrapper; one `<svg viewBox="0 0 W H">` inside a
  `.diagram-card`. Grow the viewBox to fit taller content — never shrink row spacing to cram more
  pins in.
- Board rectangle centered; peripheral cards to its left and right, ~260px wide.
- Every wire is a `<line class="wire-<peripheral>">` plus a `r="3"` `<circle class="fill-<peripheral>">`
  connector dot at **both** ends — the board edge and the card edge. Never only one end.
- Pin rows step 24px apart. Card structure top to bottom: header (12x12 icon-swatch `<rect>` +
  `lbl-heading` title + `lbl-mono fill-muted` subtitle) → hairline `stroke-border` divider → pin
  rows → hairline divider → muted footnote (only if there's a caveat worth calling out).
- STM pin labels sit **inside the board's own border**, right next to that pin's stub. Module pin
  labels sit **inside the module card's own border** — right-aligned (`text-anchor="end"`) for
  cards left of the board, left-aligned for cards right of the board — so each label hugs the wire
  it belongs to.
- Pins normally sit on a card's left or right edge with a horizontal wire. A card that also has to
  reach the power band below it puts those pins on its **bottom** edge instead: dot on the border,
  label centered just inside it, wire straight down. Every such lead runs in its own vertical
  channel down to the PDB's top edge, so no two leads cross.
- One color per peripheral (receiver/esc/motor/pdb/…), not per signal type — add a new
  `--wd-<name>` token plus matching `.fill-<name>` / `.wire-<name>` CSS classes when introducing a
  peripheral; reuse existing ones for a repeat appearance.
- A wired-but-not-currently-used pin (e.g. a reserved RX line) gets `wire-reserved`
  (dashed) on the line and `opacity="0.55"` on its dot and label — never drawn identically to an
  active pin.
- copper (`--wd-copper`) is the brand accent, used for the USB connector only. Power-red
  (`--wd-power`) is reserved for safety callouts in footnote text (e.g. "never board 5V") — never
  used as a wire color. Teal (`--wd-pdb`) carries the whole battery/PDB power chain, at a heavier
  `stroke-width`, so current-carrying leads read as one system.
- Type: `lbl-mono` (monospace) for all pin names and technical labels, `lbl-heading` (condensed
  sans) for card/board titles, plain body copy outside the SVG for prose.

## Adding a new card

1. Pick a `--wd-<name>` color: reuse if the peripheral already has one, otherwise add the token
   plus `.fill-<name>` / `.wire-<name>` classes next to the existing ones.
2. Size the card: header+dividers is ~56px, plus 24px per pin row, plus ~50px if a footnote is
   needed.
3. Draw stub wires from the board edge to the card edge with a connector dot at both ends.
4. Place STM-side labels inside the board rect, module-side labels inside the card rect,
   right/left-aligned toward the wire, matching the alignment rule above.
5. If the diagram no longer fits the current `viewBox`, expand it.
6. Check it in a browser (Wiring page) — confirm no label collides with a border or another
   card, and that new wires read as the same visual language as the existing ones.

## Common mistakes

- Connector dot on only one end of a wire.
- Coloring by signal type (red=power, gray=ground, blue=data) instead of by peripheral.
- Shrinking row spacing to fit more pins instead of growing the viewBox.
- Drawing a reserved/unused pin identically to an active one (missing the dashed + faded
  treatment).
- Inventing a new card layout instead of copying an existing card and swapping its content.
