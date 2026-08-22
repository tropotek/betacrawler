// Same public surface as SerialLink (webserial-link.js), backed by an
// in-process SimModel instead of a real serial port -- so DeviceModel runs
// against it unmodified. Requests round-trip through protocol.js's encode(),
// so Terminal's raw-sent/raw-recv display shows real JSON.
import { encode } from './protocol.js';
import { NotConnected } from './webserial-link.js';
import { SimModel } from './sim-model.js';
import { SIM_SCHEMA } from './sim-schema.js';

const SIM_FW = 'betacrawler 4.0.0 (sim)';
const SIM_NAME = 'betacrawler';
const SIM_VER = '4.0.0';
const SIM_BOARD = 'simulator';
const SIM_MODS = ['device', 'system', 'rx', 'tank_drive', 'esc0', 'esc1'];
const PROTO_VERSION = 1;
const BOOT_LOG = 'simulated board - every value below is fabricated';

function validate(spec, val) {
  const kind = spec.type;
  if (kind === 'u8') {
    if (!Number.isInteger(val)) return 'badtype';
    if (val < spec.min || val > spec.max) return 'range';
  } else if (kind === 'enum') {
    if (!spec.options.includes(val)) return 'enum';
  } else if (kind === 'str') {
    if (typeof val !== 'string') return 'badtype';
    if (val.length > spec.maxlen) return 'toolong';
  }
  return null;
}

export class SimLink {
  constructor() {
    this._state = 'disconnected';
    this._subs = [];
    this._nextId = 1;
    this._model = null;
    this._t0 = 0;
    this._tlmOn = true;
    this._timer = null;
  }

  get state() { return this._state; }

  subscribe(callback) {
    this._subs.push(callback);
    return () => { this._subs = this._subs.filter((cb) => cb !== callback); };
  }

  // A fresh session every time, at the schema defaults -- the honest analog
  // of a fresh boot, and it never carries stray state between test runs.
  async connect() {
    await this.disconnect();
    this._model = new SimModel(SIM_SCHEMA.params);
    this._t0 = performance.now();
    this._tlmOn = true;
    this._state = 'connected';
    this._startTelemetry();
  }

  async disconnect() {
    this._stopTelemetry();
    this._state = 'disconnected';
    this._model = null;
  }

  async request(op, fields = {}, timeoutMs = 1000) {
    return (await this.requestRaw(op, fields, timeoutMs)).response;
  }

  async requestRaw(op, fields = {}) {
    if (this._state !== 'connected') throw new NotConnected('no serial connection');
    const id = this._nextId;
    this._nextId = (this._nextId % 65535) + 1;
    const sent = encode(id, op, fields).trimEnd();
    const response = { id, ...this._handle(op, fields) };
    const raw = JSON.stringify(response);
    if (op === 'hello') this._publish({ log: BOOT_LOG });
    return { sent, raw, response };
  }

  _nowMs() { return Math.round(performance.now() - this._t0); }

  _handle(op, fields) {
    const now = this._nowMs();
    const model = this._model;
    switch (op) {
      case 'hello':
        return {
          ok: true, fw: SIM_FW, proto: PROTO_VERSION, board: SIM_BOARD,
          name: SIM_NAME, ver: SIM_VER, built: 'simulated', mods: SIM_MODS, caps: [],
        };
      case 'schema':
        return { ok: true, params: SIM_SCHEMA.params, tlm: SIM_SCHEMA.tlm };
      case 'getall':
        return { ok: true, vals: model.values() };
      case 'get': {
        const spec = model.spec(fields.key);
        if (!spec) return { ok: false, err: 'nokey' };
        return { ok: true, key: fields.key, val: model.get(fields.key) };
      }
      case 'set': {
        const spec = model.spec(fields.key);
        if (!spec) return { ok: false, err: 'nokey' };
        const err = validate(spec, fields.val);
        if (err) return { ok: false, err };
        model.set(fields.key, fields.val, now);
        return { ok: true };
      }
      case 'save':
        model.save();
        return { ok: true };
      case 'defaults':
        model.loadDefaults(now);
        return { ok: true };
      case 'revert':
        return { ok: true, src: model.revert(now) };
      case 'tlm':
        this._tlmOn = !!fields.on;
        return { ok: true };
      case 'dfu':
        return { ok: false, err: 'nodfu' };
      case 'wifiscan':
        return { ok: false, err: 'nowifi' };
      default:
        return { ok: false, err: 'badop' };
    }
  }

  _startTelemetry() {
    const tick = () => {
      if (this._state !== 'connected') return;
      if (this._tlmOn) this._publish({ tlm: this._model.telemetry(this._nowMs()) });
      const rate = Math.max(1, this._model.num('tlm.rate'));
      this._timer = setTimeout(tick, 1000 / rate);
    };
    tick();
  }

  _stopTelemetry() {
    if (this._timer) clearTimeout(this._timer);
    this._timer = null;
  }

  _publish(msg) {
    for (const cb of this._subs.slice()) cb(msg);
  }
}
