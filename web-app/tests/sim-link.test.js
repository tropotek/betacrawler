import test from 'node:test';
import assert from 'node:assert/strict';
import { SimLink } from '../js/sim-link.js';
import { NotConnected } from '../js/webserial-link.js';
import { SIM_SCHEMA } from '../js/sim-schema.js';

async function connected() {
  const link = new SimLink();
  await link.connect(null);
  return link;
}

test('hello identifies itself as a simulator with no dfu/wifi capability', async () => {
  const link = await connected();
  const { response } = await link.requestRaw('hello');
  assert.equal(response.ok, true);
  assert.equal(response.proto, 1);
  assert.equal(response.board, 'simulator');
  assert.deepEqual(response.caps, []);
  await link.disconnect();
});

test('schema matches the generated sim schema', async () => {
  const link = await connected();
  const { response } = await link.requestRaw('schema');
  assert.deepEqual(response.params, SIM_SCHEMA.params);
  assert.deepEqual(response.tlm, SIM_SCHEMA.tlm);
  await link.disconnect();
});

test('getall and get agree', async () => {
  const link = await connected();
  const vals = (await link.requestRaw('getall')).response.vals;
  const one = (await link.requestRaw('get', { key: 'tlm.rate' })).response;
  assert.equal(one.val, vals['tlm.rate']);
  await link.disconnect();
});

test('set validates the way the firmware does', async () => {
  const link = await connected();
  assert.equal((await link.requestRaw('set', { key: 'tlm.rate', val: 20 })).response.ok, true);
  assert.equal((await link.requestRaw('set', { key: 'tlm.rate', val: 99 })).response.err, 'range');
  assert.equal((await link.requestRaw('set', { key: 'rx.protocol', val: 'nope' })).response.err, 'enum');
  assert.equal((await link.requestRaw('set', { key: 'tlm.rate', val: 'ten' })).response.err, 'badtype');
  assert.equal((await link.requestRaw('set', { key: 'device.name', val: 'x'.repeat(40) })).response.err, 'toolong');
  assert.equal((await link.requestRaw('set', { key: 'no.such', val: 1 })).response.err, 'nokey');
  await link.disconnect();
});

test('revert reports defaults before a save and flash after', async () => {
  const link = await connected();
  await link.requestRaw('set', { key: 'tlm.rate', val: 20 });
  assert.equal((await link.requestRaw('revert')).response.src, 'defaults');
  await link.requestRaw('set', { key: 'tlm.rate', val: 20 });
  await link.requestRaw('save');
  await link.requestRaw('set', { key: 'tlm.rate', val: 40 });
  assert.equal((await link.requestRaw('revert')).response.src, 'flash');
  assert.equal((await link.requestRaw('get', { key: 'tlm.rate' })).response.val, 20);
  await link.disconnect();
});

test('unsupported ops answer the way a board without them does', async () => {
  const link = await connected();
  assert.equal((await link.requestRaw('dfu')).response.err, 'nodfu');
  assert.equal((await link.requestRaw('wifiscan')).response.err, 'nowifi');
  assert.equal((await link.requestRaw('nonsense')).response.err, 'badop');
  await link.disconnect();
});

test('a boot log line marks the values as fabricated', async () => {
  const link = await connected();
  const lines = [];
  link.subscribe((msg) => lines.push(msg));
  await link.requestRaw('hello');
  const logs = lines.filter((msg) => 'log' in msg);
  assert.ok(logs.length > 0);
  assert.match(logs[0].log.toLowerCase(), /simulated/);
  await link.disconnect();
});

test('telemetry frames arrive on their own without a request', async () => {
  const link = await connected();
  await link.requestRaw('set', { key: 'tlm.rate', val: 50 }); // 20ms period
  const frames = [];
  link.subscribe((msg) => { if ('tlm' in msg) frames.push(msg.tlm); });
  await new Promise((r) => setTimeout(r, 100));
  await link.disconnect();
  assert.ok(frames.length > 0, 'expected at least one telemetry frame');
  assert.ok(frames[0].ch1 > 0);
});

test('requests before connect() are refused like a closed port', async () => {
  const link = new SimLink();
  await assert.rejects(() => link.requestRaw('hello'), NotConnected);
});

test('reconnecting starts a fresh session at the schema defaults', async () => {
  const link = await connected();
  await link.requestRaw('set', { key: 'tlm.rate', val: 40 });
  await link.disconnect();
  await link.connect(null);
  assert.equal((await link.requestRaw('get', { key: 'tlm.rate' })).response.val, 10);
  await link.disconnect();
});
