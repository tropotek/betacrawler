import test from 'node:test';
import assert from 'node:assert/strict';
import { encode, decode, isResponse, isTelemetry, isLog, ProtocolError } from '../js/protocol.js';

test('encode drops undefined/null fields and appends a newline', () => {
  const line = encode(1, 'set', { key: 'led.mode', val: 'on', extra: undefined });
  assert.equal(line.endsWith('\n'), true);
  const obj = JSON.parse(line);
  assert.deepEqual(obj, { id: 1, op: 'set', key: 'led.mode', val: 'on' });
});

test('decode parses a well-formed line', () => {
  assert.deepEqual(decode('{"id":1,"ok":true}\n'), { id: 1, ok: true });
});

test('decode rejects an empty line', () => {
  assert.throws(() => decode('   \n'), ProtocolError);
});

test('decode rejects malformed json', () => {
  assert.throws(() => decode('{not json'), ProtocolError);
});

test('decode rejects a non-object json value', () => {
  assert.throws(() => decode('[1,2,3]'), ProtocolError);
});

test('is* classify a response, telemetry, and log message', () => {
  const resp = { id: 1, ok: true };
  const tlm = { tlm: { up: 100 } };
  const log = { log: 'boot: ready' };
  assert.equal(isResponse(resp), true);
  assert.equal(isTelemetry(resp), false);
  assert.equal(isLog(resp), false);
  assert.equal(isTelemetry(tlm), true);
  assert.equal(isResponse(tlm), false);
  assert.equal(isLog(log), true);
  assert.equal(isResponse(log), false);
});
