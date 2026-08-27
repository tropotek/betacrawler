import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, readdirSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { parseIni } from '../js/settings-ini.js';

const here = fileURLToPath(new URL('.', import.meta.url));
const presetDir = `${here}../../docs/assets/presets`;
const golden = JSON.parse(readFileSync(`${here}../../firmware/test/golden/schema.json`, 'utf8'));
const byKey = new Map(golden.params.map((p) => [p.key, p]));

// Every value a preset carries has to survive a real device's validation, so
// each is checked against the golden schema's bounds for its own type.
function checkValue(spec, value) {
  if (spec.type === 'enum') {
    assert.ok(spec.options.includes(value),
      `${spec.key}: '${value}' is not one of ${spec.options.join('|')}`);
    return;
  }
  if (spec.type === 'str') {
    assert.ok(value.length <= spec.maxlen,
      `${spec.key}: '${value}' exceeds maxlen ${spec.maxlen}`);
    return;
  }
  const n = Number(value);
  assert.ok(Number.isFinite(n), `${spec.key}: '${value}' is not a number`);
  assert.ok(n >= spec.min && n <= spec.max,
    `${spec.key}: ${n} is outside ${spec.min}..${spec.max}`);
}

const presets = readdirSync(presetDir).filter((f) => f.endsWith('.ini')).sort();

test('there is a starter preset for every base build', () => {
  assert.deepEqual(presets, [
    'car-brushed.ini', 'car-brushless.ini',
    'skid-brushed.ini', 'skid-brushless.ini',
  ]);
});

for (const file of presets) {
  test(`${file} matches the firmware schema`, () => {
    const pairs = parseIni(readFileSync(`${presetDir}/${file}`, 'utf8'), [...byKey.keys()]);
    assert.ok(pairs.length > 0, `${file} sets nothing`);
    for (const [key, value] of pairs) {
      const spec = byKey.get(key);
      assert.ok(spec, `${file}: '${key}' is not a firmware parameter`);
      checkValue(spec, value);
    }
  });
}

test('a car preset selects the car mixer', () => {
  for (const file of ['car-brushed.ini', 'car-brushless.ini']) {
    const values = new Map(parseIni(readFileSync(`${presetDir}/${file}`, 'utf8'), [...byKey.keys()]));
    assert.equal(values.get('drive.mode'), 'car', `${file}: drive.mode`);
  }
});

// The firmware derives the servo from drive.mode, so a preset naming a servo
// mode describes a parameter this firmware does not have -- and an INI restore
// reports that as a skipped key rather than an error the user notices.
test('no preset carries a servo mode', () => {
  for (const file of presets) {
    const text = readFileSync(`${presetDir}/${file}`, 'utf8');
    const servoSection = text.split('[servo]')[1] ?? '';
    assert.ok(!/^\s*mode\s*=/m.test(servoSection), `${file}: [servo] must not set a mode`);
    assert.ok(!text.includes('servo.mode'), `${file}: no servo.mode`);
  }
});

test('every preset sets both motor types', () => {
  for (const file of presets) {
    const values = new Map(parseIni(readFileSync(`${presetDir}/${file}`, 'utf8'), [...byKey.keys()]));
    for (const key of ['motor0.type', 'motor1.type']) {
      const v = values.get(key);
      assert.ok(v === 'brushed' || v === 'brushless',
        `${file}: ${key} is '${v}' -- a preset that leaves it at none drives nothing`);
    }
  }
});
