import test from 'node:test';
import assert from 'node:assert/strict';

function makeFakeSerialPort(scriptedResponses) {
  let controller, readable, writable;
  const rebuild = () => {
    readable = new ReadableStream({ start(c) { controller = c; } });
    writable = new WritableStream({
      write(chunk) {
        const msg = JSON.parse(new TextDecoder().decode(chunk));
        const handler = scriptedResponses[msg.op];
        if (handler) {
          const resp = { id: msg.id, ...handler(msg) };
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
  };
}

const BOARD = {
  hello: () => ({ ok: true, proto: 1, fw: 'betacrawler 4.0.0', board: 'blackpill_f411ce' }),
  schema: () => ({ ok: true, params: [], tlm: [] }),
  getall: () => ({ ok: true, vals: {} }),
};

const fakePort = makeFakeSerialPort(BOARD);

Object.defineProperty(globalThis, 'navigator', {
  configurable: true,
  value: {
    usb: {
      getDevices: async () => [],
      addEventListener: () => {},
      removeEventListener: () => {},
      requestDevice: async () => {
        throw Object.assign(new Error('no device'), { name: 'NotFoundError' });
      },
    },
    serial: {
      requestPort: async () => fakePort,
      getPorts: async () => [fakePort],
      addEventListener: () => {},
      removeEventListener: () => {},
    },
  },
});

// Node 22 ships a real (read-only) global `localStorage`; assigning over it
// throws, so it is replaced the same way api.test.js replaces `navigator`.
const store = new Map();
Object.defineProperty(globalThis, 'localStorage', {
  configurable: true,
  value: {
    getItem: (k) => (store.has(k) ? store.get(k) : null),
    setItem: (k, v) => store.set(k, String(v)),
    removeItem: (k) => store.delete(k),
  },
});

const { Api } = await import('../js/api.js');

test('connectSim() brings a simulated board online', async () => {
  const status = await Api.connectSim();
  assert.equal(status.state, 'connected');
  assert.equal(status.board, 'simulator');
  assert.equal((await Api.status()).board, 'simulator');
  await Api.disconnect();
});

test('the simulator publishes telemetry through the same subscribe() channel', async () => {
  await Api.connectSim();
  const frames = [];
  const off = Api.subscribe((f) => { if (f.type === 'tlm') frames.push(f.data); });
  await new Promise((r) => setTimeout(r, 150));
  off();
  await Api.disconnect();
  assert.ok(frames.length > 0);
});

test('connectSim() never writes the remembered-board key', async () => {
  store.clear();
  await Api.connectSim();
  assert.equal(store.has('betacrawler.lastBoard'), false);
  await Api.disconnect();
});

test('a sim session does not erase a previously remembered real board', async () => {
  store.clear();
  const port = await Api.requestPort();
  await Api.connect(port);
  await Api.disconnect();
  assert.equal(store.get('betacrawler.lastBoard'), 'blackpill_f411ce');

  await Api.connectSim();
  assert.equal((await Api.status()).board, 'simulator');
  assert.equal(store.get('betacrawler.lastBoard'), 'blackpill_f411ce',
    'a sim session must not overwrite the remembered real board');
  await Api.disconnect();
});

test('disconnecting from the simulator returns to the idle default device', async () => {
  await Api.connectSim();
  await Api.disconnect();
  assert.equal((await Api.status()).state, 'disconnected');
  const port = await Api.requestPort();
  const status = await Api.connect(port);
  assert.equal(status.board, 'blackpill_f411ce');
  await Api.disconnect();
});
