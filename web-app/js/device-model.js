// Schema cache, value cache, connection state. Mirrors app/backend/device.py.
// Validates locally before sending so bad input fails fast; the firmware
// validates again regardless -- it must never trust its host.
import { NotConnected, RequestTimeout } from './webserial-link.js';

const PROTO_VERSION = 1;

export class ProtoMismatch extends Error {}

export class DeviceError extends Error {
  constructor(code, message = '') {
    super(message || code);
    this.code = code;
  }
}

function revertError(resp) {
  const err = resp.err || 'err';
  if (err === 'badop') {
    return ['badop', 'this firmware is too old to know `revert`. Update the firmware with the desktop app, then try again.'];
  }
  return [err, 'revert failed'];
}

export class DeviceModel {
  constructor(link) {
    this._link = link;
    this._schema = [];
    this._tlmSchema = [];
    this._byKey = new Map();
    this._values = {};
    this._info = {};
    this._portLabel = null;
    this._lastRealBoard = null;
  }

  subscribe(callback) { return this._link.subscribe(callback); }

  async connect(serialPort, portLabel) {
    try {
      await this._link.connect(serialPort);
    } catch (exc) {
      throw new DeviceError('connect_failed', exc.message);
    }
    const t0 = performance.now();
    try {
      const hello = await this._send('hello');
      console.debug(`connect: hello ${(performance.now() - t0).toFixed(0)}ms`);
      if (hello.proto !== PROTO_VERSION) {
        throw new ProtoMismatch(
          `device speaks proto ${hello.proto}, this app speaks ${PROTO_VERSION}`);
      }
      this._info = {
        fw: hello.fw, proto: hello.proto, board: hello.board, name: hello.name,
        ver: hello.ver, built: hello.built, mods: hello.mods || [], caps: hello.caps || [],
      };
      const schema = await this._send('schema');
      this._schema = schema.params;
      this._tlmSchema = schema.tlm || [];
      this._byKey = new Map(this._schema.map((p) => [p.key, p]));
      this._values = (await this._send('getall')).vals;
      this._portLabel = portLabel;
      this._lastRealBoard = this._info.board;
      console.debug(`connect: handshake total ${(performance.now() - t0).toFixed(0)}ms`);
    } catch (exc) {
      await this._link.disconnect();
      this._schema = []; this._tlmSchema = [];
      this._byKey = new Map(); this._values = {}; this._info = {};
      this._portLabel = null;
      throw exc;
    }
  }

  async disconnect() { await this._link.disconnect(); }

  status() { return { state: this._link.state, port: this._portLabel, ...this._info }; }

  lastRealBoard() { return this._lastRealBoard; }

  schema() { return { params: this._schema, tlm: this._tlmSchema }; }

  values() { return { ...this._values }; }

  async set(key, val) {
    const spec = this._byKey.get(key);
    if (!spec) throw new DeviceError('nokey', `unknown parameter '${key}'`);
    this._validate(spec, val);
    const resp = await this._send('set', { key, val });
    if (!resp.ok) throw new DeviceError(resp.err || 'err', `device rejected ${key}`);
    this._values[key] = val;
  }

  async save() {
    const resp = await this._send('save', {}, 5000);
    if (!resp.ok) throw new DeviceError(resp.err || 'err', 'save failed');
  }

  async loadDefaults() {
    const resp = await this._send('defaults');
    if (!resp.ok) throw new DeviceError(resp.err || 'err', 'defaults failed');
    this._values = (await this._send('getall')).vals;
  }

  async revert() {
    const resp = await this._send('revert');
    if (!resp.ok) throw new DeviceError(...revertError(resp));
    this._values = (await this._send('getall')).vals;
    return resp.src || 'defaults';
  }

  // The firmware answers BEFORE it resets, so `ok` means the request was
  // accepted and the port disappearing a moment later is expected. Disconnect
  // here rather than letting the read loop find a dead port and report it as a
  // fault at the exact moment things are working.
  async enterDfu() {
    let resp;
    try {
      resp = await this._send('dfu', {}, 2000);
    } catch (exc) {
      // The port dying under a request that was already on the wire is this
      // op's success signature, not a failure: the board reset before its ack
      // could flush, which is the reboot that was asked for. Boards differ in
      // which side of that race they land on, so neither may be relied on.
      if (exc.code === 'disconnected' && exc.afterSend) {
        await this._link.disconnect();
        return;
      }
      throw exc;
    }
    if (!resp.ok) {
      const err = resp.err || 'err';
      if (err === 'nodfu' || err === 'badop') {
        throw new DeviceError(
          'nodfu',
          'this firmware does not support rebooting to DFU. Use the BOOT0 button '
          + 'method instead.');
      }
      throw new DeviceError(err, 'could not enter DFU mode');
    }
    await this._link.disconnect();
  }

  async terminalGet(key) {
    if (!this._byKey.has(key)) throw new DeviceError('nokey', `unknown parameter '${key}'`);
    const { sent, raw, response } = await this._sendRaw('get', { key });
    if (!response.ok) throw new DeviceError(response.err || 'err', `device rejected get ${key}`);
    this._values[key] = response.val;
    return { sent, recv: raw, val: response.val };
  }

  async terminalGetAll() {
    const { sent, raw, response } = await this._sendRaw('getall');
    if (!response.ok) throw new DeviceError(response.err || 'err', 'getall failed');
    this._values = response.vals;
    return { sent, recv: raw, vals: { ...this._values } };
  }

  async terminalSet(key, rawValue) {
    const spec = this._byKey.get(key);
    if (!spec) throw new DeviceError('nokey', `unknown parameter '${key}'`);
    let val = rawValue;
    if (spec.type === 'u8') {
      val = Number.parseInt(rawValue, 10);
      if (!Number.isInteger(val) || String(val) !== rawValue.trim()) {
        throw new DeviceError('badtype', 'expected an integer');
      }
    }
    this._validate(spec, val);
    const { sent, raw, response } = await this._sendRaw('set', { key, val });
    if (!response.ok) throw new DeviceError(response.err || 'err', `device rejected ${key}`);
    this._values[key] = val;
    return { sent, recv: raw, val };
  }

  async terminalSave() {
    const { sent, raw, response } = await this._sendRaw('save', {}, 5000);
    if (!response.ok) throw new DeviceError(response.err || 'err', 'save failed');
    return { sent, recv: raw };
  }

  async terminalDefaults() {
    const { sent, raw, response } = await this._sendRaw('defaults');
    if (!response.ok) throw new DeviceError(response.err || 'err', 'defaults failed');
    this._values = (await this._send('getall')).vals;
    return { sent, recv: raw };
  }

  async terminalRevert() {
    const { sent, raw, response } = await this._sendRaw('revert');
    if (!response.ok) throw new DeviceError(...revertError(response));
    this._values = (await this._send('getall')).vals;
    return { sent, recv: raw, src: response.src || 'defaults' };
  }

  _validate(spec, val) {
    const kind = spec.type;
    if (kind === 'u8') {
      if (!Number.isInteger(val)) throw new DeviceError('badtype', 'expected an integer');
      if (val < spec.min || val > spec.max) {
        throw new DeviceError('range', `must be ${spec.min}..${spec.max}`);
      }
    } else if (kind === 'enum') {
      if (!spec.options.includes(val)) {
        throw new DeviceError('enum', `must be one of ${spec.options.join(', ')}`);
      }
    } else if (kind === 'str') {
      if (typeof val !== 'string') throw new DeviceError('badtype', 'expected a string');
      if (val.length > spec.maxlen) throw new DeviceError('toolong', `max ${spec.maxlen} characters`);
    }
  }

  async _send(op, fields = {}, timeoutMs = 1000) {
    return (await this._sendRaw(op, fields, timeoutMs)).response;
  }

  async _sendRaw(op, fields = {}, timeoutMs = 1000) {
    try {
      return await this._link.requestRaw(op, fields, timeoutMs);
    } catch (exc) {
      if (exc instanceof RequestTimeout) throw new DeviceError('timeout', exc.message);
      if (exc instanceof NotConnected) {
        const err = new DeviceError('disconnected', exc.message);
        err.afterSend = !!exc.afterSend;   // whether the request reached the wire
        throw err;
      }
      throw exc;
    }
  }
}
