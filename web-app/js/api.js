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

// Drops the granted bootloader the moment it leaves the bus, rather than
// waiting for the next poll to notice. Optional-chained for the browsers with
// no WebUSB at all, exactly as the serial listener above is.
navigator.usb?.addEventListener('disconnect', (event) => {
  if (event.device === dfuDevice) dfuDevice = null;
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

// Presence means "on the bus now", never "granted at some point". A WebUSB
// grant outlives the device it was given for, so a board that has been reset
// -- by NRST, by BOOT0, or by the flash that just finished -- leaves a handle
// behind that would otherwise report DFU mode forever.
async function firstDfuDevice() {
  if (!navigator.usb) return null;
  const live = (await navigator.usb.getDevices())
    .filter((d) => d.vendorId === DFU_VID && d.productId === DFU_PID);
  if (dfuDevice && !live.includes(dfuDevice)) dfuDevice = null;
  return dfuDevice || live[0] || null;
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
  const dev = await firstDfuDevice();
  if (!dev) {
    throw new DeviceError(
      'nodfu',
      'no device in DFU mode. Hold BOOT0, tap NRST, release BOOT0, then choose the device.');
  }
  flashBusy = true;
  publish({ type: 'flash', data: { phase: 'flashing', pct: 0, line: `writing ${label}` } });
  try {
    await dfuFlash(dev, bytes, {
      onProgress: (ev) => publish({ type: 'flash', data: { phase: 'flashing', ...ev } }),
    });
    publish({ type: 'flash', data: { phase: 'done', pct: 100, line: 'flash complete' } });
  } catch (exc) {
    publish({ type: 'flash', data: { phase: 'error', line: exc.message } });
  } finally {
    // The handle dies with the reset that ends a successful flash; getDevices()
    // still finds the board after a failed one, since the grant persists.
    flashBusy = false;
    dfuDevice = null;
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
  async requestDfuDevice() {
    dfuDevice = await navigator.usb.requestDevice({
      filters: [{ vendorId: DFU_VID, productId: DFU_PID }],
    });
    return { label: 'STM32 bootloader (0483:df11)', serial: dfuDevice.serialNumber || null };
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
