import test from 'node:test';
import assert from 'node:assert/strict';
import { run } from '../js/terminal.js';
import { DeviceError } from '../js/device-model.js';

function makeFakeDevice(overrides = {}) {
  return {
    status: () => ({ state: 'connected' }),
    schema: () => ({ params: [
      { key: 'led.mode', type: 'enum', options: ['on', 'off'], def: 'off', label: 'Mode', group: 'LED' },
    ] }),
    terminalGet: async () => ({ sent: 's', recv: 'r', val: 'off' }),
    terminalGetAll: async () => ({ sent: 's', recv: 'r', vals: { 'led.mode': 'off' } }),
    terminalSet: async () => ({ sent: 's', recv: 'r', val: 'on' }),
    terminalSave: async () => ({ sent: 's', recv: 'r' }),
    terminalDefaults: async () => ({ sent: 's', recv: 'r' }),
    terminalRevert: async () => ({ sent: 's', recv: 'r', src: 'flash' }),
    ...overrides,
  };
}

test('an empty command is rejected without touching the device', async () => {
  const result = await run(makeFakeDevice(), '   ');
  assert.equal(result.ok, false);
  assert.match(result.friendly, /empty command/);
});

test('help works even while disconnected', async () => {
  const device = makeFakeDevice({ status: () => ({ state: 'disconnected' }) });
  const result = await run(device, 'help');
  assert.equal(result.ok, true);
  assert.match(result.friendly, /get <key>/);
});

test('any other command while disconnected is rejected', async () => {
  const device = makeFakeDevice({ status: () => ({ state: 'disconnected' }) });
  const result = await run(device, 'get led.mode');
  assert.equal(result.ok, false);
  assert.match(result.friendly, /not connected/);
});

test('get <key> reports the value and raw wire lines', async () => {
  const result = await run(makeFakeDevice(), 'get led.mode');
  assert.equal(result.ok, true);
  assert.equal(result.friendly, 'led.mode = off');
  assert.equal(result.rawSent, 's');
  assert.equal(result.rawRecv, 'r');
});

test('set <key> <value> marks the result dirty', async () => {
  const result = await run(makeFakeDevice(), 'set led.mode on');
  assert.equal(result.ok, true);
  assert.equal(result.friendly, 'OK: led.mode = on');
  assert.equal(result.dirty, true);
});

test('save clears dirty', async () => {
  const result = await run(makeFakeDevice(), 'save');
  assert.equal(result.dirty, false);
});

test('revert reports which source the firmware used', async () => {
  const device = makeFakeDevice({
    terminalRevert: async () => ({ sent: 's', recv: 'r', src: 'defaults' }),
  });
  const result = await run(device, 'revert');
  assert.match(result.friendly, /no saved settings/);
  assert.equal(result.dirty, true);
});

test('a wrong argument count reports a usage error', async () => {
  const result = await run(makeFakeDevice(), 'set led.mode');
  assert.equal(result.ok, false);
  assert.match(result.friendly, /usage: set <key> <value>/);
});

test('an unknown command is reported by name', async () => {
  const result = await run(makeFakeDevice(), 'frobnicate');
  assert.equal(result.ok, false);
  assert.match(result.friendly, /unknown command 'frobnicate'/);
});

test('a DeviceError from the device surfaces as ERROR: <message>', async () => {
  const device = makeFakeDevice({
    terminalGet: async () => { throw new DeviceError('nokey', "unknown parameter 'x'"); },
  });
  const result = await run(device, 'get x');
  assert.equal(result.ok, false);
  assert.equal(result.friendly, "ERROR: unknown parameter 'x'");
});

test('dump renders the schema and values as INI text', async () => {
  const result = await run(makeFakeDevice(), 'dump');
  assert.equal(result.ok, true);
  assert.match(result.friendly, /\[led\]\nmode = off/);
});

test('list renders the schema as a settings reference', async () => {
  const result = await run(makeFakeDevice(), 'list');
  assert.equal(result.ok, true);
  assert.match(result.friendly, /led\.mode/);
  assert.match(result.friendly, /on\|off/);
});
