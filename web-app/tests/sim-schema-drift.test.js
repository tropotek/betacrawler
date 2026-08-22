import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { SIM_SCHEMA } from '../js/sim-schema.js';

const here = fileURLToPath(new URL('.', import.meta.url));
const golden = JSON.parse(readFileSync(`${here}../../firmware/test/golden/schema.json`, 'utf8'));

test('sim-schema.js matches the firmware golden schema', () => {
  assert.deepEqual(SIM_SCHEMA.params, golden.params,
    'run `node web-app/tools/gen-sim-schema.js` and commit web-app/js/sim-schema.js');
  assert.deepEqual(SIM_SCHEMA.tlm, golden.tlm,
    'run `node web-app/tools/gen-sim-schema.js` and commit web-app/js/sim-schema.js');
});
