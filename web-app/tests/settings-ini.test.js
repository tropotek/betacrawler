import test from 'node:test';
import assert from 'node:assert/strict';
import { dumpIni, parseIni, GENERAL_SECTION } from '../js/settings-ini.js';

const SCHEMA = [
  { key: 'led.mode' }, { key: 'led.blink_hz' }, { key: 'rx.deadband_us' }, { key: 'proto' },
];

test('dumpIni groups keys by section in schema order, skipping keys absent from values', () => {
  const text = dumpIni(SCHEMA,
    { 'led.mode': 'on', 'led.blink_hz': 2, 'rx.deadband_us': 5 },
    { fw: 'betacrawler 1.0.0', board: 'blackpill_f411ce' });
  assert.match(text, /^; betacrawler settings dump/);
  assert.match(text, /; firmware: betacrawler 1\.0\.0 \(blackpill_f411ce\)/);
  assert.match(text, /\[led\]\nmode = on\nblink_hz = 2/);
  assert.match(text, /\[rx\]\ndeadband_us = 5/);
  assert.doesNotMatch(text, /proto/); // not present in values
});

test('parseIni parses simple sections back into [key, rawValue] pairs', () => {
  const pairs = parseIni('[led]\nmode = on\nblink_hz = 2\n');
  assert.deepEqual(pairs, [['led.mode', 'on'], ['led.blink_hz', '2']]);
});

test('parseIni ignores comments and blank lines', () => {
  const pairs = parseIni('; a comment\n\n[led]\n; another\nmode = on\n');
  assert.deepEqual(pairs, [['led.mode', 'on']]);
});

test('a dotless key in [general] stays bare only when known_keys does not claim it', () => {
  const pairs = parseIni(`[${GENERAL_SECTION}]\nproto = 1\n`, ['proto']);
  assert.deepEqual(pairs, [['proto', '1']]);
});

test('a dotless key in [general] becomes general.<key> when known_keys expects the prefix', () => {
  const pairs = parseIni(`[${GENERAL_SECTION}]\nwidget = 1\n`, ['general.widget']);
  assert.deepEqual(pairs, [['general.widget', '1']]);
});

test('a duplicate section header is rejected', () => {
  assert.throws(() => parseIni('[led]\nmode = on\n[led]\nmode = off\n'), /duplicate section/);
});

test('a duplicate option within a section is rejected', () => {
  assert.throws(() => parseIni('[led]\nmode = on\nmode = off\n'), /duplicate option/);
});

test('an option outside any section is rejected', () => {
  assert.throws(() => parseIni('mode = on\n'), /outside a section/);
});

test('a line that is not a section or an assignment is rejected', () => {
  assert.throws(() => parseIni('[led]\nnot an assignment\n'), /could not parse/);
});

test('a dump parses straight back in', () => {
  const values = { 'led.mode': 'on', 'rx.deadband_us': 5 };
  const text = dumpIni(SCHEMA, values, { fw: 'betacrawler 1.0.0' });
  const pairs = parseIni(text);
  assert.deepEqual(Object.fromEntries(pairs), { 'led.mode': 'on', 'rx.deadband_us': '5' });
});
