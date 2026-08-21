import test from 'node:test';
import assert from 'node:assert/strict';

function makeFakeSerialPort(scriptedResponses) {
  let controller, readable, writable;
  // Fresh streams on every open(), like a real SerialPort. Without this the
  // first disconnect() would cancel this port's readable for good and every
  // later test would run against a dead stream.
  const rebuild = () => {
    readable = new ReadableStream({ start(c) { controller = c; } });
    writable = new WritableStream({
      write(chunk) {
        const msg = JSON.parse(new TextDecoder().decode(chunk));
        const handler = scriptedResponses[msg.op];
        if (handler) {
          const resp = { id: msg.id, ...handler(msg) };
          // CRLF, the way the firmware actually terminates a line.
          controller.enqueue(new TextEncoder().encode(JSON.stringify(resp) + '\r\n'));
        }
      },
    });
  };
  rebuild();
  return {
    getInfo: () => ({ usbVendorId: 0x0483, usbProductId: 0x5740 }),
    open: async () => { rebuild(); },
    setSignals: async () => {},
    close: async () => {},
    get readable() { return readable; },
    get writable() { return writable; },
    pushLine(line) { controller.enqueue(new TextEncoder().encode(line)); },
  };
}

const BOARD = {
  hello: () => ({ ok: true, proto: 1, fw: 'betacrawler 1.0.0', board: 'blackpill_f411ce' }),
  schema: () => ({ ok: true, params: [{ key: 'led.mode', type: 'enum', options: ['on', 'off'], def: 'off' }], tlm: [] }),
  getall: () => ({ ok: true, vals: { 'led.mode': 'off' } }),
  get: () => ({ ok: true, val: 'off' }),
  set: () => ({ ok: true }),
};

const fakePort = makeFakeSerialPort(BOARD);

// A minimal stand-in for navigator.serial's EventTarget-ness: just enough
// for api.js's addEventListener('disconnect', ...) and this file's own
// fireSerialDisconnect() helper to talk to each other.
const disconnectListeners = [];
// defineProperty, not assignment: Node has its own read-only global
// `navigator`, and `globalThis.navigator = ...` throws a TypeError there.
Object.defineProperty(globalThis, 'navigator', {
  configurable: true,
  value: {
    serial: {
      requestPort: async () => fakePort,
      getPorts: async () => [fakePort],
      addEventListener: (type, cb) => { if (type === 'disconnect') disconnectListeners.push(cb); },
      removeEventListener: (type, cb) => {
        if (type === 'disconnect') {
          const i = disconnectListeners.indexOf(cb);
          if (i !== -1) disconnectListeners.splice(i, 1);
        }
      },
    },
  },
});
function fireSerialDisconnect(port) {
  for (const cb of disconnectListeners) cb({ target: port });
}

const { Api } = await import('../js/api.js');

test('isSupported() reports whether this browser has Web Serial at all', () => {
  assert.equal(Api.isSupported(), true);
  const saved = navigator.serial;
  delete navigator.serial;
  try {
    assert.equal(Api.isSupported(), false);
  } finally {
    navigator.serial = saved;
  }
});

test('requestPort() and connect() bring the device online', async () => {
  const port = await Api.requestPort();
  const status = await Api.connect(port);
  assert.equal(status.state, 'connected');
  assert.equal(status.board, 'blackpill_f411ce');
  assert.equal(status.port, 'USB 0483:5740');
});

test('schema() and params() reflect the connected device', async () => {
  assert.deepEqual((await Api.schema()).params[0].key, 'led.mode');
  assert.deepEqual(await Api.params(), { 'led.mode': 'off' });
});

test('setParam() returns the same {ok, key, val} shape as the REST route did', async () => {
  const result = await Api.setParam('led.mode', 'on');
  assert.deepEqual(result, { ok: true, key: 'led.mode', val: 'on' });
});

test('sendTerminalCommand() returns snake_case fields matching the REST route', async () => {
  const result = await Api.sendTerminalCommand('get led.mode');
  assert.equal(result.ok, true);
  assert.ok('raw_sent' in result);
  assert.ok('raw_recv' in result);
});

test('knownPorts() lists previously-granted ports with a display label', async () => {
  const known = await Api.knownPorts();
  assert.equal(known.length, 1);
  assert.equal(known[0].label, 'USB 0483:5740');
});

test('subscribe() delivers the {type, data} frames app.js expects', async () => {
  const frames = [];
  const unsubscribe = Api.subscribe((f) => frames.push(f));
  fakePort.pushLine('{"tlm":{"up":100}}\r\n');
  fakePort.pushLine('{"log":"boot: ready"}\r\n');
  await new Promise((r) => setTimeout(r, 10));
  assert.deepEqual(frames, [
    { type: 'tlm', data: { up: 100 } },
    { type: 'log', data: 'boot: ready' },
  ]);
  unsubscribe();
});

test('disconnect() tears the connection down', async () => {
  const status = await Api.disconnect();
  assert.equal(status.state, 'disconnected');
});

test('a global navigator.serial "disconnect" event marks it disconnected and publishes a state frame', async () => {
  const port = await Api.requestPort();
  await Api.connect(port);
  assert.equal((await Api.status()).state, 'connected');
  const frames = [];
  const unsubscribe = Api.subscribe((f) => frames.push(f));
  fireSerialDisconnect(port);
  await new Promise((r) => setTimeout(r, 10));
  assert.equal((await Api.status()).state, 'disconnected');
  assert.deepEqual(frames, [{ type: 'state', data: 'disconnected' }]);
  unsubscribe();
});

test('a "disconnect" event for a different port is ignored', async () => {
  const port = await Api.requestPort();
  await Api.connect(port);
  fireSerialDisconnect(makeFakeSerialPort({}));
  await new Promise((r) => setTimeout(r, 10));
  assert.equal((await Api.status()).state, 'connected');
  await Api.disconnect();
});
