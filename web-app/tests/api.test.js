import test from 'node:test';
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';

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
// WebUSB stand-in for the Firmware page's DFU paths. Chrome only ever hands
// back devices this origin has been granted, so a test sets that list directly.
let fakeUsbDevices = [];
const setFakeUsbDevices = (devices) => { fakeUsbDevices = devices; };

Object.defineProperty(globalThis, 'navigator', {
  configurable: true,
  value: {
    usb: {
      getDevices: async () => fakeUsbDevices,
      requestDevice: async () => {
        if (!fakeUsbDevices.length) {
          const err = new Error('No device selected.');
          err.name = 'NotFoundError';
          throw err;
        }
        return fakeUsbDevices[0];
      },
    },
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


// --- firmware bundle and DFU flashing ----------------------------------------

const F411 = {
  id: 'blackpill_f411ce-betacrawler-1.0.0', board: 'blackpill_f411ce',
  name: 'betacrawler', version: '1.0.0', built: 'Aug 22 2026 10:30:42',
  proto: 1, method: 'dfu', file: 'blackpill_f411ce/betacrawler-1.0.0.bin',
};

function validBytes(len = 2048) {
  const bytes = new Uint8Array(len);
  const view = new DataView(bytes.buffer);
  view.setUint32(0, 0x20020000, true);
  view.setUint32(4, 0x08000201, true);
  return bytes;
}

// A fake bundle: an image plus the manifest entry describing it, so the
// checksum path can be exercised both ways.
function fakeBundle({ corrupt = false, missing = false } = {}) {
  const blob = validBytes();
  const img = {
    ...F411,
    size: blob.length,
    sha256: createHash('sha256').update(corrupt ? new Uint8Array(8) : blob).digest('hex'),
  };
  const manifest = { app_version: '1.0.0', fw_source_sha256: 'x'.repeat(64), images: [img] };
  globalThis.fetch = async (url) => {
    if (String(url).endsWith('manifest.json')) return { ok: true, json: async () => manifest };
    if (missing) return { ok: false, status: 404 };
    return { ok: true, arrayBuffer: async () => blob.buffer.slice(0) };
  };
  return { blob, img };
}

function fakeDfuDevice() {
  return {
    vendorId: 0x0483, productId: 0xdf11, serialNumber: 'FAKE123',
    configuration: {
      interfaces: [{ interfaceNumber: 0, alternates: [{ alternateSetting: 0, interfaceName: null }] }],
    },
    open: async () => {},
    selectConfiguration: async () => {},
    claimInterface: async () => {},
    selectAlternateInterface: async () => {},
    close: async () => {},
    controlTransferOut: async () => ({ status: 'ok', bytesWritten: 0 }),
    controlTransferIn: async () => ({
      status: 'ok', data: new DataView(new Uint8Array([0, 0, 0, 0, 5, 0]).buffer),
    }),
  };
}

function collectFlashFrames() {
  const frames = [];
  const off = Api.subscribe((f) => { if (f.type === 'flash') frames.push(f.data); });
  return { frames, off };
}

test('dfuSupported() reports whether this browser has WebUSB at all', () => {
  assert.equal(Api.dfuSupported(), true);
});

test('firmwareCatalog() reads the committed manifest', async () => {
  fakeBundle();
  const cat = await Api.firmwareCatalog();
  assert.equal(cat.app_version, '1.0.0');
  assert.equal(cat.images.length, 1);
  assert.equal(cat.images[0].board, 'blackpill_f411ce');
  assert.equal(cat.images[0].available, true);
});

test('firmwareCatalog() recommends the image matching the last board seen', async () => {
  fakeBundle();
  const port = await Api.requestPort();
  await Api.connect(port);
  const cat = await Api.firmwareCatalog();
  assert.equal(cat.board, 'blackpill_f411ce');
  assert.equal(cat.recommended, F411.id);
  await Api.disconnect();
});

test('dfuStatus() reports a granted bootloader as present', async () => {
  setFakeUsbDevices([]);
  assert.deepEqual(await Api.dfuStatus(), { present: false, busy: false });
  setFakeUsbDevices([fakeDfuDevice()]);
  assert.deepEqual(await Api.dfuStatus(), { present: true, busy: false });
});

test('dfuStatus() ignores a granted device that is not a bootloader', async () => {
  setFakeUsbDevices([{ vendorId: 0x0483, productId: 0x5740 }]);
  assert.equal((await Api.dfuStatus()).present, false);
});

test('requestDfuDevice() returns a plain descriptor, not the USBDevice', async () => {
  setFakeUsbDevices([fakeDfuDevice()]);
  const chosen = await Api.requestDfuDevice();
  assert.deepEqual(Object.keys(chosen).sort(), ['label', 'serial']);
  assert.equal(chosen.serial, 'FAKE123');
});

test('flashBundled() refuses an image that fails its checksum', async () => {
  fakeBundle({ corrupt: true });
  setFakeUsbDevices([fakeDfuDevice()]);
  await assert.rejects(() => Api.flashBundled(F411.id), /failed its checksum/);
});

test('flashBundled() refuses an image the manifest lists but the site lacks', async () => {
  fakeBundle({ missing: true });
  setFakeUsbDevices([fakeDfuDevice()]);
  await assert.rejects(() => Api.flashBundled(F411.id), /missing from the bundle/);
});

test('flashBundled() refuses an id the manifest does not list', async () => {
  fakeBundle();
  setFakeUsbDevices([fakeDfuDevice()]);
  await assert.rejects(() => Api.flashBundled('nope'), /no firmware image/);
});

test('flashBundled() publishes flash frames and completes', async () => {
  fakeBundle();
  setFakeUsbDevices([fakeDfuDevice()]);
  const { frames, off } = collectFlashFrames();
  await Api.flashBundled(F411.id);
  off();
  assert.equal(frames[0].phase, 'flashing');
  assert.equal(frames.at(-1).phase, 'done');
  assert.equal(frames.at(-1).pct, 100);
  assert.ok(frames.some((f) => f.op === 'erase'));
  assert.ok(frames.some((f) => f.op === 'download'));
});

test('flashing with no DFU device throws before publishing anything', async () => {
  fakeBundle();
  setFakeUsbDevices([]);
  const { frames, off } = collectFlashFrames();
  await assert.rejects(() => Api.flashBundled(F411.id), /no device in DFU mode/);
  off();
  assert.deepEqual(frames, []);
});

test('flashUpload() rejects a file that is not a firmware binary', async () => {
  setFakeUsbDevices([fakeDfuDevice()]);
  await assert.rejects(() => Api.flashUpload(new Uint8Array(64), 'notes.txt'),
                       /too small to be firmware/);
});

test('flashUpload() accepts an ArrayBuffer as well as a Uint8Array', async () => {
  setFakeUsbDevices([fakeDfuDevice()]);
  const { frames, off } = collectFlashFrames();
  await Api.flashUpload(validBytes().buffer, 'local.bin');
  off();
  assert.equal(frames.at(-1).phase, 'done');
});

test('a second concurrent flash is refused', async () => {
  let release;
  const dev = fakeDfuDevice();
  dev.open = () => new Promise((r) => { release = r; });
  setFakeUsbDevices([dev]);
  const first = Api.flashUpload(validBytes(), 'a.bin');
  // A macrotask, not a microtask: runFlash awaits getDevices() before it sets
  // the busy flag, so Promise.resolve() would race it.
  await new Promise((r) => setTimeout(r, 0));
  assert.equal((await Api.dfuStatus()).busy, true);
  await assert.rejects(() => Api.flashUpload(validBytes(), 'b.bin'), /already in progress/);
  release();
  await first;
  assert.equal((await Api.dfuStatus()).busy, false);
});

test('a failure once writing has begun arrives as an error frame, not a rejection', async () => {
  const dev = fakeDfuDevice();
  dev.controlTransferOut = async () => ({ status: 'stall' });
  setFakeUsbDevices([dev]);
  const { frames, off } = collectFlashFrames();
  await Api.flashUpload(validBytes(), 'a.bin');
  off();
  assert.equal(frames.at(-1).phase, 'error');
  assert.match(frames.at(-1).line, /DFU_DNLOAD/);
});

test('device frames still reach subscribers alongside flash frames', async () => {
  fakeBundle();
  setFakeUsbDevices([fakeDfuDevice()]);
  const seen = [];
  const off = Api.subscribe((f) => seen.push(f.type));
  const port = await Api.requestPort();
  await Api.connect(port);
  await Api.flashBundled(F411.id);
  // An unsolicited unplug, not Api.disconnect(): a disconnect the caller asked
  // for deliberately publishes no state frame.
  fireSerialDisconnect(port);
  await new Promise((r) => setTimeout(r, 10));
  off();
  assert.ok(seen.includes('flash'), 'flash frames reach a subscriber');
  assert.ok(seen.includes('state'), 'device frames still reach the same subscriber');
});

test('unsubscribing stops both frame sources', async () => {
  fakeBundle();
  setFakeUsbDevices([fakeDfuDevice()]);
  const seen = [];
  Api.subscribe((f) => seen.push(f.type))();
  await Api.flashBundled(F411.id);
  assert.deepEqual(seen, []);
});
