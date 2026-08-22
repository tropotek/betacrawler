import test from 'node:test';
import assert from 'node:assert/strict';
import { DeviceModel, DeviceError, ProtoMismatch } from '../js/device-model.js';
import { NotConnected } from '../js/webserial-link.js';

const SCHEMA = {
  params: [
    { key: 'led.mode', type: 'enum', options: ['on', 'off'], def: 'off', label: 'Mode' },
    { key: 'rx.deadband_us', type: 'u8', min: 0, max: 200, def: 0, label: 'Deadband' },
  ],
  tlm: [{ key: 'up', label: 'Uptime' }],
};

function makeFakeLink(overrides = {}) {
  const subs = [];
  return {
    state: 'connected',
    subscribe(cb) { subs.push(cb); return () => {}; },
    async connect() {},
    async disconnect() {},
    async requestRaw(op, fields) {
      if (overrides[op]) return overrides[op](fields);
      throw new Error(`fake link has no handler for op '${op}'`);
    },
    _emit(msg) { subs.forEach((cb) => cb(msg)); },
  };
}

function connectedLink() {
  return makeFakeLink({
    hello: () => ({ sent: '', raw: '', response: { id: 1, proto: 1, fw: 'betacrawler 1.0.0', board: 'blackpill_f411ce' } }),
    schema: () => ({ sent: '', raw: '', response: { id: 2, ...SCHEMA } }),
    getall: () => ({ sent: '', raw: '', response: { id: 3, vals: { 'led.mode': 'off', 'rx.deadband_us': 0 } } }),
  });
}

test('connect() populates schema, values, and info from hello/schema/getall', async () => {
  const device = new DeviceModel(connectedLink());
  await device.connect({}, 'USB 0483:5740');
  assert.deepEqual(device.schema(), SCHEMA);
  assert.deepEqual(device.values(), { 'led.mode': 'off', 'rx.deadband_us': 0 });
  assert.equal(device.status().board, 'blackpill_f411ce');
  assert.equal(device.status().port, 'USB 0483:5740');
  assert.equal(device.lastRealBoard(), 'blackpill_f411ce');
});

test('connect() rejects a protocol version mismatch', async () => {
  const link = makeFakeLink({
    hello: () => ({ sent: '', raw: '', response: { id: 1, proto: 99 } }),
  });
  const device = new DeviceModel(link);
  await assert.rejects(() => device.connect({}, 'p'), ProtoMismatch);
});

test('set() validates against the cached schema before sending', async () => {
  const device = new DeviceModel(connectedLink());
  await device.connect({}, 'p');
  await assert.rejects(() => device.set('led.mode', 'blink'), (exc) => {
    assert.equal(exc.code, 'enum');
    return true;
  });
  await assert.rejects(() => device.set('rx.deadband_us', 999), (exc) => {
    assert.equal(exc.code, 'range');
    return true;
  });
  await assert.rejects(() => device.set('no.such.key', 1), (exc) => {
    assert.equal(exc.code, 'nokey');
    return true;
  });
});

test('set() sends the value and updates the local cache on success', async () => {
  const base = connectedLink();
  const device = new DeviceModel({
    ...base,
    requestRaw: async (op, fields) => {
      if (op === 'set') return { sent: '', raw: '', response: { ok: true } };
      return base.requestRaw(op, fields);
    },
  });
  await device.connect({}, 'p');
  await device.set('led.mode', 'on');
  assert.equal(device.values()['led.mode'], 'on');
});

test('revert() reports which source the firmware fell back to', async () => {
  const base = connectedLink();
  const device = new DeviceModel({
    ...base,
    requestRaw: async (op, fields) => {
      if (op === 'revert') return { sent: '', raw: '', response: { ok: true, src: 'defaults' } };
      return base.requestRaw(op, fields);
    },
  });
  await device.connect({}, 'p');
  const src = await device.revert();
  assert.equal(src, 'defaults');
});

test('terminalSet() coerces a u8 string and rejects a non-integer one', async () => {
  const base = connectedLink();
  const device = new DeviceModel({
    ...base,
    requestRaw: async (op, fields) => {
      if (op === 'set') return { sent: 'sent-line', raw: 'recv-line', response: { ok: true } };
      return base.requestRaw(op, fields);
    },
  });
  await device.connect({}, 'p');
  const { val } = await device.terminalSet('rx.deadband_us', '12');
  assert.equal(val, 12);
  await assert.rejects(() => device.terminalSet('rx.deadband_us', '3.5'), (exc) => {
    assert.equal(exc.code, 'badtype');
    return true;
  });
});

test('a disconnected op maps a link RequestTimeout to DeviceError("timeout")', async () => {
  const base = connectedLink();
  const device = new DeviceModel({
    ...base,
    requestRaw: async (op, fields) => {
      if (op === 'save') {
        const { RequestTimeout } = await import('../js/webserial-link.js');
        throw new RequestTimeout('no response to save within 5000ms');
      }
      return base.requestRaw(op, fields);
    },
  });
  await device.connect({}, 'p');
  await assert.rejects(() => device.save(), (exc) => {
    assert.ok(exc instanceof DeviceError);
    assert.equal(exc.code, 'timeout');
    return true;
  });
});

// --- entering DFU -------------------------------------------------------------

function dfuLink(dfuResponse) {
  const link = makeFakeLink({
    hello: () => ({ sent: '', raw: '', response: { id: 1, proto: 1, fw: 'betacrawler 1.0.0', board: 'blackpill_f411ce', caps: ['dfu'] } }),
    schema: () => ({ sent: '', raw: '', response: { id: 2, ...SCHEMA } }),
    getall: () => ({ sent: '', raw: '', response: { id: 3, vals: {} } }),
    dfu: () => ({ sent: '', raw: '', response: { id: 4, ...dfuResponse } }),
  });
  link.disconnect = async () => { link.state = 'disconnected'; };
  return link;
}

test('enterDfu() disconnects once the device accepts', async () => {
  const link = dfuLink({ ok: true });
  const device = new DeviceModel(link);
  await device.connect({}, 'p');
  await device.enterDfu();
  assert.equal(link.state, 'disconnected');
});

test('enterDfu() reports firmware built without DFU support', async () => {
  const device = new DeviceModel(dfuLink({ ok: false, err: 'nodfu' }));
  await device.connect({}, 'p');
  await assert.rejects(() => device.enterDfu(), (exc) => {
    assert.equal(exc.code, 'nodfu');
    assert.match(exc.message, /BOOT0 button/);
    return true;
  });
});

test('enterDfu() treats an unknown op the same as no DFU support', async () => {
  const device = new DeviceModel(dfuLink({ ok: false, err: 'badop' }));
  await device.connect({}, 'p');
  await assert.rejects(() => device.enterDfu(), /BOOT0 button/);
});

test('enterDfu() surfaces any other device error', async () => {
  const device = new DeviceModel(dfuLink({ ok: false, err: 'busy' }));
  await device.connect({}, 'p');
  await assert.rejects(() => device.enterDfu(), (exc) => exc.code === 'busy');
});

test('enterDfu() leaves the link connected when the device refuses', async () => {
  const link = dfuLink({ ok: false, err: 'nodfu' });
  const device = new DeviceModel(link);
  await device.connect({}, 'p');
  await assert.rejects(() => device.enterDfu());
  assert.equal(link.state, 'connected');
});

// Which side of this race a board lands on varies -- an F411 gets its ack out,
// an F401 resets first -- so the ack cannot be a precondition for success.
test('enterDfu() accepts a board that resets before its ack flushes', async () => {
  const link = dfuLink({ ok: true });
  const device = new DeviceModel(link);
  await device.connect({}, 'p');
  link.requestRaw = async () => {
    link.state = 'disconnected';
    const exc = new NotConnected('connection lost while waiting');
    exc.afterSend = true;   // the request was on the wire when the port died
    throw exc;
  };
  await device.enterDfu();
  assert.equal(link.state, 'disconnected');
});

// The opposite case must still fail: nothing reached the board, so nothing
// rebooted, and reporting success would strand the caller waiting for a
// bootloader that is never coming.
test('enterDfu() still fails when the request never left', async () => {
  const link = dfuLink({ ok: true });
  const device = new DeviceModel(link);
  await device.connect({}, 'p');
  link.requestRaw = async () => { throw new NotConnected('write failed: port gone'); };
  await assert.rejects(() => device.enterDfu(), (exc) => {
    assert.equal(exc.code, 'disconnected');
    assert.equal(exc.afterSend, false, 'a write that never left is not a reboot');
    return true;
  });
});
