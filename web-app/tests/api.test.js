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
  dfu: () => ({ ok: true }),
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
// What the chooser offers, which is NOT the granted list: WebUSB shows the user
// devices this origin has never been granted, and getDevices() cannot see those
// at all. `undefined` means "whatever is granted", the common case; setting it
// models a bootloader only a pick can reach, and `null` an empty/dismissed
// chooser. Reset by setFakeUsbDevices() so it cannot leak between tests.
let fakeChooserDevice;
// Chrome refusing to open the chooser at all, because the click the call
// descends from has aged out of its 5s transient activation window. Distinct
// from an empty chooser: nothing was shown to dismiss.
let chooserBlocked = false;
const setFakeUsbDevices = (devices) => {
  fakeUsbDevices = devices;
  fakeChooserDevice = undefined;
  chooserBlocked = false;
};
const setFakeChooserDevice = (dev) => { fakeChooserDevice = dev; };
const setChooserBlocked = (blocked) => { chooserBlocked = blocked; };
const usbDisconnectListeners = [];
const usbConnectListeners = [];
const fireUsbDisconnect = (device) => {
  for (const cb of usbDisconnectListeners) cb({ device });
};
const fireUsbConnect = (device) => {
  for (const cb of usbConnectListeners) cb({ device });
};

Object.defineProperty(globalThis, 'navigator', {
  configurable: true,
  value: {
    usb: {
      getDevices: async () => fakeUsbDevices,
      addEventListener: (type, cb) => {
        if (type === 'disconnect') usbDisconnectListeners.push(cb);
        if (type === 'connect') usbConnectListeners.push(cb);
      },
      removeEventListener: () => {},
      requestDevice: async () => {
        if (chooserBlocked) {
          const err = new Error('Must be handling a user gesture to show a permission request.');
          err.name = 'SecurityError';
          throw err;
        }
        const offered = fakeChooserDevice !== undefined ? fakeChooserDevice : fakeUsbDevices[0];
        if (!offered) {
          const err = new Error('No device selected.');
          err.name = 'NotFoundError';
          throw err;
        }
        // A grant persists: the browser lists a picked device from getDevices()
        // from here on, which is what makes presence knowable without a re-pick.
        if (!fakeUsbDevices.includes(offered)) fakeUsbDevices = [...fakeUsbDevices, offered];
        return offered;
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

// `onBus: false` models what Brave actually returns: a device this origin was
// granted at some point, which is no longer plugged in. Its open() rejects the
// way a real absent device's does.
function fakeDfuDevice({ onBus = true, serial = 'FAKE123', openError = 'NotFoundError' } = {}) {
  const dev = {
    vendorId: 0x0483, productId: 0xdf11, serialNumber: serial,
    opens: 0, closes: 0, get open_() { return dev.opens; },
    configuration: {
      interfaces: [{ interfaceNumber: 0, alternates: [{ alternateSetting: 0, interfaceName: null }] }],
    },
    forgotten: 0,
    forget: async () => { dev.forgotten += 1; },
    open: async () => {
      dev.opens += 1;
      if (!onBus) {
        const err = new Error(openError === 'NotFoundError'
          ? 'The device was disconnected.' : 'Access denied.');
        err.name = openError;
        throw err;
      }
    },
    selectConfiguration: async () => {},
    claimInterface: async () => {},
    selectAlternateInterface: async () => {},
    close: async () => { dev.closes += 1; },
    controlTransferOut: async () => ({ status: 'ok', bytesWritten: 0 }),
    controlTransferIn: async () => ({
      status: 'ok', data: new DataView(new Uint8Array([0, 0, 0, 0, 5, 0]).buffer),
    }),
  };
  return dev;
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

// Waits for a condition rather than a fixed delay: how many awaits a flash
// takes before it marks itself busy is an implementation detail, and a timeout
// that encodes it silently turns into a race the next time that changes.
async function waitUntil(predicate, what, timeoutMs = 2000) {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    if (await predicate()) return;
    await new Promise((r) => setTimeout(r, 5));
  }
  throw new Error(`timed out waiting for ${what}`);
}

test('a second concurrent flash is refused', async () => {
  let release;
  let stalled = false;
  const dev = fakeDfuDevice();
  // Stalls the flash, not the liveness probe -- the probe only opens and
  // closes, so holding open() here would deadlock before the flash begins.
  // Only the first call stalls, so releasing it cannot be stolen by a second.
  dev.configuration = null;
  dev.selectConfiguration = () => new Promise((resolve) => {
    if (stalled) { resolve(); return; }
    stalled = true;
    release = resolve;
  });
  setFakeUsbDevices([dev]);

  const first = Api.flashUpload(validBytes(), 'a.bin');
  await waitUntil(async () => (await Api.dfuStatus()).busy, 'the flash to take the device');

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

// --- a bootloader that has left the bus ---------------------------------------

test('the board leaving the bus ends its presence', async () => {
  // Reset by NRST, by BOOT0, or by a finished flash: the grant survives, the
  // device does not, and the browser says so.
  const dev = fakeDfuDevice();
  setFakeUsbDevices([dev]);
  await Api.requestDfuDevice();
  assert.equal((await Api.dfuStatus()).present, true);

  setFakeUsbDevices([]);
  fireUsbDisconnect(dev);
  assert.equal((await Api.dfuStatus()).present, false);
});

test('a usb disconnect event drops the granted device immediately', async () => {
  setFakeUsbDevices([fakeDfuDevice()]);
  const granted = fakeUsbDevices[0];
  await Api.requestDfuDevice();
  assert.equal((await Api.dfuStatus()).present, true);

  setFakeUsbDevices([]);
  fireUsbDisconnect(granted);
  assert.equal((await Api.dfuStatus()).present, false);
});

test('a disconnect for the same board as a different object still counts', async () => {
  // The same physical bootloader can be handed back as a new USBDevice.
  const granted = fakeDfuDevice({ serial: 'BOARD1' });
  setFakeUsbDevices([granted]);
  await Api.requestDfuDevice();
  fireUsbDisconnect(fakeDfuDevice({ serial: 'BOARD1' }));
  setFakeUsbDevices([]);
  assert.equal((await Api.dfuStatus()).present, false);
});

test('a usb disconnect for a different device leaves the grant alone', async () => {
  setFakeUsbDevices([fakeDfuDevice()]);
  await Api.requestDfuDevice();
  fireUsbDisconnect({ vendorId: 0x1234, productId: 0x5678 });
  assert.equal((await Api.dfuStatus()).present, true);
});

test('flashing after the bootloader has gone throws instead of writing', async () => {
  fakeBundle();
  const dev = fakeDfuDevice();
  setFakeUsbDevices([dev]);
  await Api.requestDfuDevice();
  setFakeUsbDevices([]);
  fireUsbDisconnect(dev);
  await assert.rejects(() => Api.flashBundled(F411.id), /no device in DFU mode/);
});

test('a re-entered bootloader is picked up without a second grant', async () => {
  const first = fakeDfuDevice();
  setFakeUsbDevices([first]);
  await Api.requestDfuDevice();
  setFakeUsbDevices([]);
  fireUsbDisconnect(first);
  assert.equal((await Api.dfuStatus()).present, false);

  // Back into DFU: the browser announces it, no pick required.
  const again = fakeDfuDevice();
  setFakeUsbDevices([again]);
  fireUsbConnect(again);
  assert.equal((await Api.dfuStatus()).present, true);
});


// --- presence is "on the bus", not "granted once" -----------------------------
// Brave returns every device this origin was ever granted, connected or not --
// two stale 0483:df11 entries were observed on a board running its firmware.

test('dfuStatus() ignores a granted device that no longer opens', async () => {
  setFakeUsbDevices([fakeDfuDevice({ onBus: false })]);
  assert.equal((await Api.dfuStatus()).present, false);
});

test('dfuStatus() ignores several stale grants at once', async () => {
  setFakeUsbDevices([
    fakeDfuDevice({ onBus: false, serial: 'OLD1' }),
    fakeDfuDevice({ onBus: false, serial: 'OLD2' }),
  ]);
  assert.equal((await Api.dfuStatus()).present, false);
});

test('dfuStatus() finds the live bootloader among stale grants', async () => {
  const live = fakeDfuDevice({ serial: 'LIVE' });
  setFakeUsbDevices([fakeDfuDevice({ onBus: false, serial: 'OLD1' }), live]);
  assert.equal((await Api.dfuStatus()).present, true);
});

test('probing leaves the device closed', async () => {
  const live = fakeDfuDevice();
  setFakeUsbDevices([live]);
  await Api.dfuStatus();
  assert.equal(live.opens, live.closes, 'every probe open() must be closed again');
});

test('a flash writes to the live device, not a stale grant', async () => {
  const live = fakeDfuDevice({ serial: 'LIVE' });
  setFakeUsbDevices([fakeDfuDevice({ onBus: false, serial: 'OLD1' }), live]);
  const { frames, off } = collectFlashFrames();
  await Api.flashUpload(validBytes(), 'a.bin');
  off();
  assert.equal(frames.at(-1).phase, 'done');
  assert.ok(live.opens > 0, 'the live device is the one written to');
});

test('polling does not touch the device while a flash is running', async () => {
  let release;
  const live = fakeDfuDevice();
  setFakeUsbDevices([live]);
  // Stall inside the flash, then poll: a probe here would open (and close) the
  // device mid-write. configuration is nulled so flash() actually reaches
  // selectConfiguration().
  live.configuration = null;
  live.selectConfiguration = () => new Promise((r) => { release = r; });
  const flashing = Api.flashUpload(validBytes(), 'a.bin');
  await new Promise((r) => setTimeout(r, 0));
  // Baselines taken mid-flash: the liveness probe that ran BEFORE the flash
  // started legitimately opened and closed the device once.
  const opensBefore = live.opens;
  const closesBefore = live.closes;
  const status = await Api.dfuStatus();
  assert.equal(status.busy, true);
  assert.equal(live.opens, opensBefore, 'no probe while the flash holds the device');
  assert.equal(live.closes, closesBefore,
               'the flash\'s device must not be closed underneath it');
  release();
  await flashing;
});

test('presence goes false after a flash without a new grant', async () => {
  // The board resets into its application, so the bootloader stops opening --
  // but the grant, and getDevices()\'s entry for it, both remain.
  const dev = fakeDfuDevice();
  setFakeUsbDevices([dev]);
  const { off } = collectFlashFrames();
  await Api.flashUpload(validBytes(), 'a.bin');
  off();
  setFakeUsbDevices([fakeDfuDevice({ onBus: false })]);
  assert.equal((await Api.dfuStatus()).present, false);
});

// --- picking the handle that actually opens -----------------------------------
// A chooser can offer entries for bootloaders that no longer exist, and picking
// one opens with "Access denied". The badge's handle is a hint, not a promise.

test('a flash falls back when the tracked handle will not open', async () => {
  const dead = fakeDfuDevice({ onBus: false, openError: 'SecurityError', serial: 'DEAD' });
  const live = fakeDfuDevice({ serial: 'LIVE' });
  setFakeUsbDevices([dead, live]);
  // The browser announced this one, so it is tracked unverified -- and by the
  // time a flash starts it has gone (no disconnect event reached us).
  fireUsbConnect(dead);
  const { frames, off } = collectFlashFrames();
  await Api.flashUpload(validBytes(), 'a.bin');
  off();
  assert.equal(frames.at(-1).phase, 'done');
  assert.ok(live.opens > 0, 'the flash must land on the device that opens');
});

test('a flash explains itself when no candidate opens', async () => {
  setFakeUsbDevices([
    fakeDfuDevice({ onBus: false, openError: 'SecurityError', serial: 'A' }),
    fakeDfuDevice({ onBus: false, serial: 'B' }),
  ]);
  await assert.rejects(() => Api.flashUpload(validBytes(), 'a.bin'),
                       /could not be opened|no device in DFU mode/);
});

test('the tracked handle is tried first when it does open', async () => {
  const other = fakeDfuDevice({ serial: 'OTHER' });
  const picked = fakeDfuDevice({ serial: 'PICKED' });
  setFakeUsbDevices([other, picked]);
  // Track the second one, then confirm the flash follows it rather than simply
  // taking the first entry the browser lists.
  fireUsbConnect(picked);
  const { off } = collectFlashFrames();
  await Api.flashUpload(validBytes(), 'a.bin');
  off();
  assert.ok(picked.opens > 0);
  assert.equal(other.opens, 0, 'no need to touch other devices when the tracked one works');
});

// --- a pick is a claim to be checked, not believed --------------------------
// The chooser offers entries for bootloaders that no longer exist. Believing
// one puts the page in DFU mode with no board there, which used to disable
// Reboot to DFU and hide the picker -- a state only a reload escaped.

test('picking a device that is not there does not claim DFU mode', async () => {
  setFakeUsbDevices([fakeDfuDevice({ onBus: false, serial: 'STALE' })]);
  await assert.rejects(() => Api.requestDfuDevice(), /no longer connected|not connected/);
  assert.equal((await Api.dfuStatus()).present, false,
               'a pick that cannot be opened must not turn the badge on');
});

test('picking a device that is there claims DFU mode', async () => {
  setFakeUsbDevices([fakeDfuDevice({ serial: 'LIVE' })]);
  const chosen = await Api.requestDfuDevice();
  assert.equal(chosen.serial, 'LIVE');
  assert.equal((await Api.dfuStatus()).present, true);
});

test('a rejected pick leaves the previous state alone', async () => {
  setFakeUsbDevices([fakeDfuDevice({ serial: 'LIVE' })]);
  await Api.requestDfuDevice();
  assert.equal((await Api.dfuStatus()).present, true);

  // A second pick that is stale must not tear down a working one.
  const live = fakeUsbDevices[0];
  setFakeUsbDevices([fakeDfuDevice({ onBus: false, serial: 'STALE' }), live]);
  await assert.rejects(() => Api.requestDfuDevice());
  assert.equal((await Api.dfuStatus()).present, true);
});

test('verifying a pick leaves the device closed', async () => {
  const live = fakeDfuDevice({ serial: 'LIVE' });
  setFakeUsbDevices([live]);
  await Api.requestDfuDevice();
  assert.equal(live.opens, live.closes, 'the verification open() must be closed again');
});


// --- one click from a connected board -----------------------------------------
// Betaflight's flow, and the backend FlashSession's: Flash reboots the board
// into DFU itself, waits (bounded) for the bootloader to arrive, then writes.

test('flashing while connected reboots the board and waits for the bootloader', async () => {
  fakeBundle();
  setFakeUsbDevices([]);
  const port = await Api.requestPort();
  await Api.connect(port);
  const { frames, off } = collectFlashFrames();
  const flashing = Api.flashBundled(F411.id, { waitMs: 4000 });
  // The bootloader enumerates a moment after the reboot.
  setTimeout(() => setFakeUsbDevices([fakeDfuDevice()]), 150);
  await flashing;
  off();
  assert.equal((await Api.status()).state, 'disconnected',
               'the serial connection died with the reboot');
  assert.equal(frames[0].phase, 'waiting');
  assert.ok(frames.some((f) => f.phase === 'flashing'));
  assert.equal(frames.at(-1).phase, 'done');
});

test('a reboot that never produces a bootloader ends in an error frame', async () => {
  fakeBundle();
  setFakeUsbDevices([]);
  const port = await Api.requestPort();
  await Api.connect(port);
  const { frames, off } = collectFlashFrames();
  await Api.flashBundled(F411.id, { waitMs: 600 });
  off();
  assert.equal(frames[0].phase, 'waiting');
  assert.equal(frames.at(-1).phase, 'error');
  assert.match(frames.at(-1).line, /no bootloader this browser can open/);
});

// WebUSB cannot see a bootloader this origin has never been granted, and
// getDevices() never prompts -- so a browser profile that has not flashed
// before would dead-end after the reboot with the board sitting in DFU.
test('a bootloader this origin cannot see is asked for rather than given up on', async () => {
  fakeBundle();
  setFakeUsbDevices([]);          // never granted, so getDevices() stays blind
  const picked = fakeDfuDevice({ serial: 'FRESH' });
  setFakeChooserDevice(picked);   // only a pick can reach it
  const port = await Api.requestPort();
  await Api.connect(port);
  const { frames, off } = collectFlashFrames();
  await Api.flashBundled(F411.id, { waitMs: 300 });
  off();
  assert.ok(frames.some((f) => /asking for permission/.test(f.line || '')),
            'the user is told why a chooser is opening');
  assert.equal(frames.at(-1).phase, 'done');
  assert.ok(picked.opens > 0, 'the picked device is the one written to');
});

test('a chooser the user dismisses ends in the error frame, not a rejection', async () => {
  fakeBundle();
  setFakeUsbDevices([]);
  setFakeChooserDevice(null);     // dismissed: requestDevice() throws NotFoundError
  const port = await Api.requestPort();
  await Api.connect(port);
  const { frames, off } = collectFlashFrames();
  await Api.flashBundled(F411.id, { waitMs: 300 });
  off();
  assert.equal(frames.at(-1).phase, 'error');
  assert.match(frames.at(-1).line, /Select DFU device/);
});

// --- tier three: a chooser the browser will not open on its own ---------------
// Chrome only opens the device chooser inside a click's 5s transient activation
// window, and a flash spends more than that on its confirm, the reboot and the
// arrival wait -- so requestDevice() is refused outright, with no chooser shown.
// The image parks until a button of its own can carry a fresh click into it.

test('a chooser the browser refuses parks the flash instead of failing it', async () => {
  fakeBundle();
  setFakeUsbDevices([]);
  setChooserBlocked(true);
  const port = await Api.requestPort();
  await Api.connect(port);
  const { frames, off } = collectFlashFrames();
  await Api.flashBundled(F411.id, { waitMs: 300 });
  assert.equal(frames.at(-1).phase, 'needsdevice');
  assert.equal(Api.needsDfuGrant(), true);
  assert.equal((await Api.dfuStatus()).busy, true, 'a parked flash is not over');

  const picked = fakeDfuDevice({ serial: 'FRESH' });
  setChooserBlocked(false);        // the grant button's click is an activation
  setFakeChooserDevice(picked);
  assert.equal(await Api.grantDfuAndResume(), true);
  off();
  assert.equal(frames.at(-1).phase, 'done');
  assert.ok(picked.opens > 0, 'the parked image is written to the granted device');
  assert.equal(Api.needsDfuGrant(), false);
  assert.equal((await Api.dfuStatus()).busy, false);
});

test('cancelling the grant ends the parked flash rather than leaving it busy', async () => {
  fakeBundle();
  setFakeUsbDevices([]);
  setChooserBlocked(true);
  const port = await Api.requestPort();
  await Api.connect(port);
  const { frames, off } = collectFlashFrames();
  await Api.flashBundled(F411.id, { waitMs: 300 });
  Api.cancelDfuGrant();
  off();
  assert.equal(frames.at(-1).phase, 'error');
  assert.equal(Api.needsDfuGrant(), false);
  assert.equal((await Api.dfuStatus()).busy, false);
});

test('a grant prompt the user dismisses abandons the parked flash', async () => {
  fakeBundle();
  setFakeUsbDevices([]);
  setChooserBlocked(true);
  const port = await Api.requestPort();
  await Api.connect(port);
  const { frames, off } = collectFlashFrames();
  await Api.flashBundled(F411.id, { waitMs: 300 });
  setChooserBlocked(false);
  setFakeChooserDevice(null);      // dismissed: requestDevice() throws NotFoundError
  assert.equal(await Api.grantDfuAndResume(), false);
  off();
  assert.equal(frames.at(-1).phase, 'error');
  assert.match(frames.at(-1).line, /no bootloader was picked/);
  assert.equal((await Api.dfuStatus()).busy, false);
});

test('grantDfuAndResume() rejects when no flash is parked', async () => {
  setFakeUsbDevices([]);
  await assert.rejects(() => Api.grantDfuAndResume(), /no flash is waiting/);
});

test('a picked device that is not really there is not written to', async () => {
  fakeBundle();
  setFakeUsbDevices([]);
  const stale = fakeDfuDevice({ onBus: false, serial: 'STALE' });
  setFakeChooserDevice(stale);
  const port = await Api.requestPort();
  await Api.connect(port);
  const { frames, off } = collectFlashFrames();
  await Api.flashBundled(F411.id, { waitMs: 300 });
  off();
  assert.equal(frames.at(-1).phase, 'error', 'a pick that will not open is not a device');
});

test('ensureDfuDevice() prompts when nothing granted turns up, and reports presence', async () => {
  setFakeUsbDevices([]);
  setFakeChooserDevice(null);
  assert.equal(await Api.ensureDfuDevice({ waitMs: 100 }), false,
               'no device and an empty chooser is an honest false');

  setFakeUsbDevices([]);
  setFakeChooserDevice(fakeDfuDevice({ serial: 'FRESH2' }));
  assert.equal(await Api.ensureDfuDevice({ waitMs: 100 }), true);
  assert.equal((await Api.dfuStatus()).present, true);
});

test('firmware that cannot reboot itself ends in an error frame naming BOOT0', async () => {
  fakeBundle();
  setFakeUsbDevices([]);
  const noDfuPort = makeFakeSerialPort({ ...BOARD, dfu: () => ({ ok: false, err: 'nodfu' }) });
  await Api.connect(noDfuPort);
  const { frames, off } = collectFlashFrames();
  await Api.flashBundled(F411.id, { waitMs: 600 });
  off();
  assert.equal(frames.at(-1).phase, 'error');
  assert.match(frames.at(-1).line, /BOOT0 button/);
  await Api.disconnect();
});

test('the busy lock clears after a reboot-and-wait flash', async () => {
  fakeBundle();
  setFakeUsbDevices([]);
  const port = await Api.requestPort();
  await Api.connect(port);
  const flashing = Api.flashBundled(F411.id, { waitMs: 3000 });
  setTimeout(() => setFakeUsbDevices([fakeDfuDevice()]), 100);
  await flashing;
  assert.equal((await Api.dfuStatus()).busy, false);
});
