// The Api object: same method names/shapes as app/web/app.js's Api, backed by
// a Web-Serial DeviceModel instead of fetch/WebSocket. Three methods have no
// backend equivalent -- requestPort()/knownPorts()/isSupported() -- because
// Web Serial cannot enumerate ports before one is granted, and does not
// exist at all outside Chromium.
import { SerialLink } from './webserial-link.js';
import { DeviceModel, DeviceError } from './device-model.js';
import { run as terminalRun } from './terminal.js';
import { parseIni } from './settings-ini.js';

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

const link = new SerialLink();
const device = new DeviceModel(link);
let currentPort = null;

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

// The link publishes what the device said; app.js consumes {type, data}
// frames. A port of main.py's Broadcaster.publish_threadsafe, which is the
// same translation on the same seam.
function toFrame(msg) {
  if ('tlm' in msg) return { type: 'tlm', data: msg.tlm };
  if ('log' in msg) return { type: 'log', data: msg.log };
  if ('state' in msg) return { type: 'state', data: msg.state };
  return { type: 'raw', data: msg };
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
  subscribe(handler) { return device.subscribe((msg) => handler(toFrame(msg))); },
};
