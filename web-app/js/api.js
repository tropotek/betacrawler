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
const DFU_FILTERS = [{ vendorId: DFU_VID, productId: DFU_PID }];

// How long to wait for a rebooted board's bootloader to turn up among the
// devices this origin may already see, before asking the browser for
// permission instead. Deliberately short: requestDevice() needs user
// activation, and the click that started the flash only counts for a few
// seconds, so this budget has to fit inside it.
const DFU_WAIT_MS = 4000;

// Which board was last connected, remembered across reloads. A bootloader
// cannot be asked what board it is -- every STM32F4 in DFU reports 0483:df11 --
// so without this a page loaded with the board already in DFU can recommend
// nothing, and the Flash button stays disabled with nothing selected.
const LAST_BOARD_KEY = 'betacrawler.lastBoard';

function rememberBoard(board) {
  if (!board) return;
  try { localStorage.setItem(LAST_BOARD_KEY, board); } catch { /* private mode */ }
}

function lastBoard() {
  const live = device.lastRealBoard();
  if (live) return live;
  try { return localStorage.getItem(LAST_BOARD_KEY); } catch { return null; }
}

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

// The chooser, for a bootloader this origin has never been granted: WebUSB
// cannot see one at all, and getDevices() never prompts, so polling alone would
// dead-end on a browser profile that has not flashed before. Returns null when
// the browser refuses the prompt or the user dismisses it -- neither is a
// fault, both just mean no device.
async function promptForDfuDevice() {
  if (!navigator.usb) return null;
  let picked;
  try {
    picked = await navigator.usb.requestDevice({ filters: DFU_FILTERS });
  } catch {
    return null;
  }
  return await isOnTheBus(picked) ? picked : null;
}

async function sha256Hex(bytes) {
  const digest = await crypto.subtle.digest('SHA-256', bytes);
  return [...new Uint8Array(digest)].map((b) => b.toString(16).padStart(2, '0')).join('');
}

// Pre-flight failures (no device, busy, bad image, bad checksum) throw, the way
// the backend build answers a rejected POST. A failure once writing has begun
// arrives as an error frame instead, the way its WS progress does.
// Betaflight's shape: poll for the bootloader only while a reboot we triggered
// is in flight, bounded, and never as a background habit. "Arrived" means a
// granted device that actually opens -- ground truth, not the grant list.
async function waitForDfuArrival(timeoutMs) {
  const deadline = Date.now() + timeoutMs;
  for (;;) {
    const dev = await deviceForFlash();
    if (dev) return dev;
    if (Date.now() >= deadline) return null;
    await new Promise((resolve) => setTimeout(resolve, 400));
  }
}

// One flash, start to finish. From a connected board this is the whole
// Betaflight-style sequence -- reboot into DFU, wait for the bootloader,
// write -- so the page needs no reboot/select choreography on the normal path.
// From a board already in DFU it just writes. Failures before anything has
// been published throw, like a rejected POST; failures after that arrive as
// error frames, like the backend build's FlashSession.
async function runFlash(bytes, label, { waitMs = DFU_WAIT_MS } = {}) {
  if (flashBusy) throw new DeviceError('busy', 'a firmware flash is already in progress');

  if (device.status().state !== 'connected') {
    // Nothing was rebooted, so nothing new is coming: resolve fast, and if the
    // handle has gone stale ask for one rather than sending the user away.
    const dev = await deviceForFlash() || await promptForDfuDevice();
    if (!dev) {
      throw new DeviceError(
        'nodfu',
        'no device in DFU mode could be opened. If one was listed, it may be left '
        + 'over from an earlier DFU session -- put the board back into DFU (hold '
        + 'BOOT0, tap NRST, release BOOT0) and pick it again.');
    }
    return writeAndReport(dev, bytes, label);
  }

  flashBusy = true;
  usbBusy = true;
  try {
    publish({ type: 'flash', data: { phase: 'waiting', pct: 0, line: 'asking the board to reboot into DFU mode' } });
    await device.enterDfu();
    // enterDfu() closes the port deliberately, which publishes no state frame;
    // mid-flash the page still needs to know the serial connection is gone.
    publish({ type: 'state', data: 'disconnected' });
    publish({ type: 'flash', data: { phase: 'waiting', pct: 0, line: 'waiting for the bootloader to appear' } });
    let dev = await waitForDfuArrival(waitMs);
    if (!dev) {
      // Nothing this origin may see -- which on a browser that has never
      // flashed before is the expected answer, not a failure. Ask for it.
      publish({ type: 'flash', data: { phase: 'waiting', pct: 0, line:
        'no bootloader this browser can reach; asking for permission' } });
      dev = await promptForDfuDevice();
    }
    if (!dev) {
      publish({ type: 'flash', data: { phase: 'error', line:
        'the board rebooted but no bootloader this browser can open appeared. '
        + 'If no chooser opened, click "Select DFU device" below to grant access '
        + 'once; otherwise try BOOT0 + NRST by hand, and rule out a different '
        + 'USB port or cable.' } });
      return;
    }
    dfuDevice = dev;
    await writeBody(dev, bytes, label);
  } catch (exc) {
    publish({ type: 'flash', data: { phase: 'error', line: exc.message } });
  } finally {
    finishFlash();
  }
}

async function writeAndReport(dev, bytes, label) {
  flashBusy = true;
  usbBusy = true;
  try {
    await writeBody(dev, bytes, label);
  } catch (exc) {
    publish({ type: 'flash', data: { phase: 'error', line: exc.message } });
  } finally {
    finishFlash();
  }
}

async function writeBody(dev, bytes, label) {
  publish({ type: 'flash', data: { phase: 'flashing', pct: 0, line: `writing ${label}` } });
  await dfuFlash(dev, bytes, {
    onProgress: (ev) => publish({ type: 'flash', data: { phase: 'flashing', ...ev } }),
  });
  publish({ type: 'flash', data: { phase: 'done', pct: 100, line: 'flash complete' } });
}

function finishFlash() {
  // The handle dies with the reset that ends a successful flash. A failed one
  // leaves the board in DFU, and its disconnect event never fires -- so the
  // next status call probes afresh rather than assuming either way.
  flashBusy = false;
  usbBusy = false;
  dfuDevice = null;
  publishDfu();
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
    rememberBoard(device.status().board);
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
    const picked = await navigator.usb.requestDevice({ filters: DFU_FILTERS });
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
  // Settles whether a bootloader is in hand after something was asked to reboot
  // into one: wait for an arrival first, and only prompt if none this origin can
  // already see turns up. Reports presence through the usual `dfu` frame.
  async ensureDfuDevice({ waitMs = DFU_WAIT_MS } = {}) {
    if (!this.dfuSupported()) return false;
    const dev = await waitForDfuArrival(waitMs) || await promptForDfuDevice();
    if (!dev) return false;
    dfuDevice = dev;
    publishDfu();
    return true;
  },
  async enterDfu() { await device.enterDfu(); },
  async firmwareCatalog() {
    const r = await fetch('firmware/manifest.json', { cache: 'no-cache' });
    if (!r.ok) {
      throw new DeviceError('firmware', `could not read the firmware manifest (${r.status})`);
    }
    const data = await r.json();
    const board = lastBoard();
    const match = board && data.images.find((img) => img.board === board);
    return {
      app_version: data.app_version,
      images: data.images.map((img) => ({ ...img, available: true })),
      recommended: match ? match.id : null,
      board,
    };
  },
  async flashBundled(id, opts = {}) {
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
    await runFlash(bytes, `${img.name} ${img.version} (${img.board})`, opts);
  },
  async flashUpload(bytes, name, opts = {}) {
    const data = bytes instanceof Uint8Array ? bytes : new Uint8Array(bytes);
    validateImage(data);
    await runFlash(data, name, opts);
  },
  subscribe(handler) {
    subscribers.add(handler);
    const off = device.subscribe((msg) => handler(toFrame(msg)));
    return () => { subscribers.delete(handler); off(); };
  },
};
