// The Api object: same method names/shapes as app/web/app.js's Api, backed by
// a Web-Serial DeviceModel instead of fetch/WebSocket. Three methods have no
// backend equivalent -- requestPort()/knownPorts()/isSupported() -- because
// Web Serial cannot enumerate ports before one is granted, and does not
// exist at all outside Chromium.
import { SerialLink } from './webserial-link.js';
import { DeviceModel, DeviceError } from './device-model.js';
import { run as terminalRun } from './terminal.js';
import { parseIni } from './settings-ini.js';
import { flash as dfuFlash, validateImage } from './dfu.js';

// STM32 Black Pill's native USB CDC, and the CP2102 bridge some ESP32
// devkits use -- the same pairs app/backend/link.py's _KNOWN_BOARDS lists.
const KNOWN_BOARD_FILTERS = [
  { usbVendorId: 0x0483, usbProductId: 0x5740 },
  { usbVendorId: 0x10c4, usbProductId: 0xea60 },
];

function portLabel(serialPort) {
  const info = serialPort.getInfo();
  if (info.usbVendorId != null && info.usbProductId != null) {
    const vid = info.usbVendorId.toString(16).padStart(4, '0');
    const pid = info.usbProductId.toString(16).padStart(4, '0');
    return `USB ${vid}:${pid}`;
  }
  return 'serial device';
}

// The STM32 ROM bootloader's USB identity. Every STM32F4 in DFU mode presents
// exactly this, which is why nothing here can tell one board from another and
// the UI has to carry the board identity forward from the last `hello`.
const DFU_VID = 0x0483, DFU_PID = 0xdf11;

const link = new SerialLink();
const device = new DeviceModel(link);
let currentPort = null;
// The granted bootloader, held here so no USBDevice crosses the Api seam.
let dfuDevice = null;
let flashBusy = false;
// Set for the whole flash, including the moment before it starts, so the
// presence poll never opens a device a write is about to take over.
let usbBusy = false;
const subscribers = new Set();

// The read loop already notices a physical unplug when the next read fails
// or the stream closes -- but navigator.serial's own "disconnect" event
// fires independently of that, and can arrive first. Both paths end up
// calling the same link.handleDisconnect(), so whichever notices first wins;
// handleDisconnect() is a no-op if the link is already disconnected.
//
// Optional-chained because Firefox and Safari have no navigator.serial:
// throwing here would take the whole app down before it rendered.
navigator.serial?.addEventListener('disconnect', (event) => {
  if (event.target === currentPort) link.handleDisconnect();
});

const isDfuDevice = (d) => d.vendorId === DFU_VID && d.productId === DFU_PID;

// Presence is what the browser tells us, not something to go asking about. A
// bootloader arriving or leaving is an event; between events the answer does
// not change, so nothing here polls or opens a device to find out.
// Optional-chained for the browsers with no WebUSB at all, exactly as the
// serial listener above is.
navigator.usb?.addEventListener('connect', (event) => {
  if (!isDfuDevice(event.device)) return;
  dfuDevice = event.device;
  publishDfu();
});

navigator.usb?.addEventListener('disconnect', (event) => {
  if (!isDfuDevice(event.device) || !dfuDevice) return;
  // Matched on serial as well as identity: the same board can come back as a
  // different USBDevice object.
  const ours = event.device === dfuDevice
    || (!!dfuDevice.serialNumber && event.device.serialNumber === dfuDevice.serialNumber);
  if (!ours) return;
  dfuDevice = null;
  publishDfu();
});

// The link publishes what the device said; app.js consumes {type, data}
// frames. A port of main.py's Broadcaster.publish_threadsafe, which is the
// same translation on the same seam.
function toFrame(msg) {
  if ('tlm' in msg) return { type: 'tlm', data: msg.tlm };
  if ('log' in msg) return { type: 'log', data: msg.log };
  if ('state' in msg) return { type: 'state', data: msg.state };
  return { type: 'raw', data: msg };
}

function publish(frame) {
  for (const handler of subscribers) handler(frame);
}

function publishDfu() {
  publish({ type: 'dfu', data: { present: !!dfuDevice, busy: flashBusy } });
}

// Whether a granted device is actually plugged in. getDevices() lists
// everything this origin has ever been granted, connected or not -- a board
// running its firmware was measured returning two stale 0483:df11 entries --
// and only a device that is really there will open.
async function isOnTheBus(device) {
  try {
    await device.open();
  } catch {
    return false;
  }
  // Left as we found it: opening is the test, not a claim on the device.
  try { await device.close(); } catch { /* it answered open(), that is enough */ }
  return true;
}

// Events cannot report a device that was already plugged in before the page
// loaded, and getDevices() alone cannot tell a live bootloader from a
// permission an old one left behind -- so exactly one probe resolves the
// starting position. Only ever runs while nothing is known to be present: once
// a device is in hand, events own the answer.
async function firstDfuDevice() {
  if (!navigator.usb) return null;
  if (usbBusy) return dfuDevice;

  const granted = (await navigator.usb.getDevices()).filter(isDfuDevice);
  // Free sanity check: a device the browser no longer lists at all is gone,
  // whatever events did or did not arrive. It cannot false-negative -- a device
  // that is present and granted is always listed.
  if (dfuDevice && !granted.includes(dfuDevice)) dfuDevice = null;
  if (dfuDevice) return dfuDevice;

  for (const device of granted) {
    if (await isOnTheBus(device)) {
      dfuDevice = device;
      break;
    }
  }
  return dfuDevice;
}

// The handle the badge tracks is a hint, not a promise: a chooser can offer an
// entry for a bootloader that no longer exists -- the one from the previous DFU
// session -- and picking it opens with "Access denied". So the device to write
// to is settled by opening one, at flash time only, where an open() was about
// to happen regardless.
async function deviceForFlash() {
  if (!navigator.usb) return null;
  const granted = (await navigator.usb.getDevices()).filter(isDfuDevice);
  // A handle the browser no longer lists is gone, so it is not a candidate at
  // all; the one being tracked goes first when it is still listed.
  const candidates = granted.includes(dfuDevice)
    ? [dfuDevice, ...granted.filter((d) => d !== dfuDevice)]
    : granted;
  for (const device of candidates) {
    if (await isOnTheBus(device)) return device;
  }
  return null;
}

async function sha256Hex(bytes) {
  const digest = await crypto.subtle.digest('SHA-256', bytes);
  return [...new Uint8Array(digest)].map((b) => b.toString(16).padStart(2, '0')).join('');
}

// Pre-flight failures (no device, busy, bad image, bad checksum) throw, the way
// the backend build answers a rejected POST. A failure once writing has begun
// arrives as an error frame instead, the way its WS progress does.
async function runFlash(bytes, label) {
  if (flashBusy) throw new DeviceError('busy', 'a firmware flash is already in progress');
  const dev = await deviceForFlash();
  if (!dev) {
    throw new DeviceError(
      'nodfu',
      'no device in DFU mode could be opened. If one was listed, it may be left '
      + 'over from an earlier DFU session -- put the board back into DFU (hold '
      + 'BOOT0, tap NRST, release BOOT0) and pick it again.');
  }
  flashBusy = true;
  usbBusy = true;
  publish({ type: 'flash', data: { phase: 'flashing', pct: 0, line: `writing ${label}` } });
  try {
    await dfuFlash(dev, bytes, {
      onProgress: (ev) => publish({ type: 'flash', data: { phase: 'flashing', ...ev } }),
    });
    publish({ type: 'flash', data: { phase: 'done', pct: 100, line: 'flash complete' } });
  } catch (exc) {
    publish({ type: 'flash', data: { phase: 'error', line: exc.message } });
  } finally {
    // The handle dies with the reset that ends a successful flash. A failed one
    // leaves the board in DFU, and its disconnect event never fires -- so the
    // next status call probes afresh rather than assuming either way.
    flashBusy = false;
    usbBusy = false;
    dfuDevice = null;
    publishDfu();
  }
}

export const Api = {
  isSupported() {
    return typeof navigator !== 'undefined' && 'serial' in navigator;
  },
  async requestPort() {
    return navigator.serial.requestPort({ filters: KNOWN_BOARD_FILTERS });
  },
  async knownPorts() {
    const ports = await navigator.serial.getPorts();
    return ports.map((port) => ({ port, label: portLabel(port) }));
  },
  async status() { return device.status(); },
  async schema() { return device.schema(); },
  async params() { return device.values(); },
  async connect(serialPort) {
    currentPort = serialPort;
    await device.connect(serialPort, portLabel(serialPort));
    return device.status();
  },
  async disconnect() {
    await device.disconnect();
    currentPort = null;
    return device.status();
  },
  async setParam(key, val) {
    await device.set(key, val);
    return { ok: true, key, val };
  },
  async save() { await device.save(); return { ok: true }; },
  async defaults() { await device.loadDefaults(); return { ok: true, vals: device.values() }; },
  async revert() {
    const src = await device.revert();
    return { ok: true, src, vals: device.values() };
  },
  async sendTerminalCommand(command) {
    const result = await terminalRun(device, command);
    return {
      ok: result.ok, friendly: result.friendly,
      raw_sent: result.rawSent, raw_recv: result.rawRecv, dirty: result.dirty,
    };
  },
  async restoreIni(ini) {
    if (device.status().state !== 'connected') throw new DeviceError('disconnected', 'not connected');
    let pairs;
    try {
      pairs = parseIni(ini, device.schema().params.map((p) => p.key));
    } catch (exc) {
      throw new DeviceError('badini', exc.message);
    }
    const applied = [];
    const skipped = [];
    for (const [key, raw] of pairs) {
      try {
        await device.terminalSet(key, raw);
        applied.push(key);
      } catch (exc) {
        skipped.push({ key, reason: exc.message });
      }
    }
    return { ok: applied.length > 0 && skipped.length === 0, applied, skipped, vals: device.values() };
  },
  dfuSupported() {
    return typeof navigator !== 'undefined' && 'usb' in navigator;
  },
  // A pick is a claim, not a fact: the chooser lists entries for bootloaders
  // that no longer exist, and believing one puts the page in DFU mode with no
  // board there. Verified before it is allowed to mean anything.
  async requestDfuDevice() {
    const picked = await navigator.usb.requestDevice({
      filters: [{ vendorId: DFU_VID, productId: DFU_PID }],
    });
    if (!await isOnTheBus(picked)) {
      throw new DeviceError(
        'nodfu',
        'that device is no longer connected -- the entry is left over from an '
        + 'earlier DFU session. Put the board into DFU mode and pick the one '
        + 'that appears.');
    }
    dfuDevice = picked;
    publishDfu();
    return { label: 'STM32 bootloader (0483:df11)', serial: picked.serialNumber || null };
  },
  async dfuStatus() {
    if (!this.dfuSupported()) return { present: false, busy: flashBusy };
    return { present: !!(await firstDfuDevice()), busy: flashBusy };
  },
  async enterDfu() { await device.enterDfu(); },
  async firmwareCatalog() {
    const r = await fetch('firmware/manifest.json', { cache: 'no-cache' });
    if (!r.ok) {
      throw new DeviceError('firmware', `could not read the firmware manifest (${r.status})`);
    }
    const data = await r.json();
    const board = device.lastRealBoard();
    const match = board && data.images.find((img) => img.board === board);
    return {
      app_version: data.app_version,
      images: data.images.map((img) => ({ ...img, available: true })),
      recommended: match ? match.id : null,
      board,
    };
  },
  async flashBundled(id) {
    const cat = await this.firmwareCatalog();
    const img = cat.images.find((i) => i.id === id);
    if (!img) throw new DeviceError('firmware', `no firmware image '${id}' in the bundle`);
    const r = await fetch(`firmware/${img.file}`, { cache: 'no-cache' });
    if (!r.ok) {
      throw new DeviceError(
        'firmware',
        `${img.file} is missing from the bundle -- the manifest lists it but the `
        + 'file is not there');
    }
    // Re-checked here rather than trusted from the manifest: the two are
    // separate files that a bad merge or a partial copy can drift apart.
    const bytes = new Uint8Array(await r.arrayBuffer());
    if (bytes.length !== img.size) {
      throw new DeviceError(
        'firmware', `${img.file} is ${bytes.length} bytes, manifest says ${img.size}`);
    }
    if (await sha256Hex(bytes) !== img.sha256) {
      throw new DeviceError(
        'firmware',
        `${img.file} failed its checksum -- the bundled file does not match the `
        + 'manifest and will not be flashed');
    }
    await runFlash(bytes, `${img.name} ${img.version} (${img.board})`);
  },
  async flashUpload(bytes, name) {
    const data = bytes instanceof Uint8Array ? bytes : new Uint8Array(bytes);
    validateImage(data);
    await runFlash(data, name);
  },
  subscribe(handler) {
    subscribers.add(handler);
    const off = device.subscribe((msg) => handler(toFrame(msg)));
    return () => { subscribers.delete(handler); off(); };
  },
};
