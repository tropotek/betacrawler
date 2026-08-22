'use strict';

// The app (backend + this UI) is versioned independently of the firmware:
// they are separate projects that happen to live in one repo, and their
// numbers are not meant to track each other. betacrawler is a template, so this
// stays 1.0.0 -- a fork bumps it. The firmware's own version lives in
// firmware/include/config.h and arrives over the wire in `hello`.
const APP_VERSION = '1.0.0';

// The single seam between this UI and whatever is on the other end: every
// request and every pushed frame that talks to the device or backend goes
// through here. That is what makes an Electron build a rewrite of this one
// object rather than of the app -- swap these bodies for IPC calls and
// nothing below changes.
//
// Two rules keep that true, and both have been broken before: nothing
// outside here may construct a request TO THE DEVICE/BACKEND, and nothing in
// here may take or return a browser-only type (a File, a WebSocket) that an
// IPC transport could not produce.
//
// One documented exception: showPage()'s fetch('pages/<name>.html') below
// loads this app's OWN static markup, not a device/backend request -- it
// isn't part of the porting surface this seam exists to isolate, so it's
// exempt (see CLAUDE.md's "The Api seam"). Any fetch that talks to the
// device or backend still has no excuse to live outside Api.
const Api = {
  // Prefixed onto every request path. '' is same-origin, which is the browser
  // build. A shell that loads this page from file:// -- where a leading '/'
  // resolves to the filesystem root, not the backend -- sets this to the
  // backend's origin instead, with no trailing slash.
  base: '',

  async get(path) {
    const r = await fetch(Api.base + path);
    if (!r.ok) throw await Api._err(r);
    return r.json();
  },
  async send(method, path, body) {
    const r = await fetch(Api.base + path, {
      method,
      headers: { 'Content-Type': 'application/json' },
      body: body === undefined ? undefined : JSON.stringify(body),
    });
    if (!r.ok) throw await Api._err(r);
    return r.json();
  },
  async sendBody(path, body) {
    const r = await fetch(Api.base + path, {
      method: 'POST',
      headers: { 'Content-Type': 'application/octet-stream' },
      body,
    });
    if (!r.ok) throw await Api._err(r);
    return r.json();
  },
  async _err(r) {
    let detail = r.statusText;
    try { const j = await r.json(); detail = j.detail || j.err || detail; } catch {}
    return new Error(detail);
  },
  ports:     ()          => Api.get('/api/ports'),
  status:    ()          => Api.get('/api/status'),
  schema:    ()          => Api.get('/api/schema'),
  params:    ()          => Api.get('/api/params'),
  connect:   (port)      => Api.send('POST', '/api/connect', { port }),
  disconnect:()          => Api.send('POST', '/api/disconnect'),
  setParam:  (key, val)  => Api.send('PUT', `/api/params/${encodeURIComponent(key)}`, { val }),
  save:      ()          => Api.send('POST', '/api/params/save'),
  defaults:  ()          => Api.send('POST', '/api/params/defaults'),
  revert:    ()          => Api.send('POST', '/api/params/revert'),
  sendTerminalCommand: (command) => Api.send('POST', '/api/terminal', { command }),
  restoreIni: (ini)      => Api.send('POST', '/api/params/restore', { ini }),
  firmwareCatalog: ()    => Api.get('/api/firmware/catalog'),
  dfuStatus: ()          => Api.get('/api/firmware/dfu-status'),
  enterDfu:  ()          => Api.send('POST', '/api/firmware/enter-dfu'),
  flashBundled: (id, port) => Api.send('POST', '/api/firmware/flash', port ? { id, port } : { id }),
  // The image goes up as the raw request body, not multipart: that keeps the
  // backend free of a python-multipart dependency. Takes bytes rather than the
  // File itself even though fetch would accept a File as a body directly --
  // a File cannot cross an IPC boundary, and this would otherwise be the one
  // method whose signature pins the UI to an HTTP transport.
  flashUpload: (bytes, filename, method = 'dfu', port = null) => {
    const params = new URLSearchParams({ filename, method });
    if (port) params.set('port', port);
    return Api.sendBody(`/api/firmware/flash-upload?${params}`, bytes);
  },
  wifiScan:  ()          => Api.send('POST', '/api/wifi/scan'),

  // The push channel, exposed as a subscription rather than as a socket.
  // `handler` receives one parsed {type, data} frame per message; reconnection
  // lives in here, not at the call site. Returns an unsubscribe function.
  //
  // Deliberately does not hand the WebSocket back. A transport with no socket
  // to hold -- Electron IPC -- has to be able to satisfy this same contract,
  // and the moment a caller reaches for .onclose it cannot.
  subscribe(handler) {
    let ws = null, retry = null, stopped = false;
    const open = () => {
      ws = new WebSocket(Api._wsUrl());
      ws.onmessage = (ev) => handler(JSON.parse(ev.data));
      ws.onclose = () => { if (!stopped) retry = setTimeout(open, 1000); };
    };
    open();
    return () => { stopped = true; clearTimeout(retry); ws?.close(); };
  },

  _wsUrl() {
    // From `base` when it is set, from the page's own origin otherwise --
    // `new URL` resolves both, and an empty base yields the current origin.
    const origin = new URL(Api.base || './', location.href);
    return `${origin.protocol === 'https:' ? 'wss:' : 'ws:'}//${origin.host}/ws`;
  },
};

const el = (id) => document.getElementById(id);
let connected = false;
let stale = false;
// Last `hello` payload, kept because more than the navbar needs it now: the
// Firmware page reads `caps` to decide whether the device can reboot itself
// into DFU, and `built`/`ver` to mark which bundled image is already running.
let deviceInfo = {};

function showError(msg) {
  const a = el('alert');
  a.textContent = msg;
  a.classList.remove('d-none');
  setTimeout(() => a.classList.add('d-none'), 5000);
}

function setState(state, info) {
  connected = state === 'connected';
  stale = false;   // any ground-truth state transition supersedes staleness
  // Only replace it on a connect: a disconnect keeps the last identity around
  // so the Firmware page can still say which board it was.
  if (connected && info) deviceInfo = info;
  // Unsaved RAM-only changes died with the connection; leaving the Save
  // button armed would just produce a "disconnected" error on click.
  if (!connected) setDirty(false);
  const badge = el('state');
  badge.textContent = state;
  badge.className = 'badge ' + (connected ? 'text-bg-success' : 'text-bg-secondary');
  el('connect').textContent = connected ? 'Disconnect' : 'Connect';
  el('fw').textContent = connected && info && info.fw ? `${info.fw} · proto ${info.proto}` : '';
  el('fw').title = connected && info && info.built ? `built ${info.built}` : '';
  updateNavAvailability();
  // Optional-chained: setState can run before Alpine has initialised (this
  // script is not deferred, Alpine's is), and the Firmware page re-syncs on
  // entry anyway.
  window.Alpine?.store('firmware')?.syncDevice(connected, deviceInfo);
  window.Alpine?.store('wifi')?.syncDevice(connected, deviceInfo);
  window.Alpine?.store('app') && Object.assign(window.Alpine.store('app'), {
    connected,
    fwSummary: connected && info && info.fw
      ? [info.fw, info.board, info.built && `built ${info.built}`,
         info.mods && info.mods.length && `modules: ${info.mods.join(', ')}`]
        .filter(Boolean).join(' · ')
      : 'not connected',
  });
}

// Telemetry-staleness: distinct from a hard disconnect. The port is still
// open and /api/status still says "connected", but no telemetry frame has
// arrived for 3 missed intervals -- the firmware may have wedged. Shown as
// an amber "stale" badge rather than silently re-affirming green
// "connected". Does not touch `connected` or disable the form: as far as
// the OS/serial port is concerned the device is still there.
function setStale() {
  if (!connected || stale) return;
  stale = true;
  const badge = el('state');
  badge.textContent = 'stale';
  badge.className = 'badge text-bg-warning';
}

function clearStale() {
  if (!stale) return;
  stale = false;
  const badge = el('state');
  badge.textContent = 'connected';
  badge.className = 'badge text-bg-success';
}

// --- schema-driven form (Alpine) --------------------------------------------
// Display order is the firmware's registration order -- there is deliberately
// no override table here. The device decides which module comes first, and
// the UI follows, so reordering modules in firmware/src/modules.cpp reorders
// the form with no change on this side.

// --- telemetry ---------------------------------------------------------------
// There is no hardcoded field list any more: labels, units and formatting all
// arrive in the schema's `tlm` descriptor, so a firmware module that publishes
// a new reading shows up here with no JavaScript change.
//
// `div`/`dec` keep the wire honest: the device sends vdd as integer
// millivolts (see docs/api.md) and only this display divides by 1000. What is
// sent to, and validated by, the device is never touched.
//
// `fmt` is the same idea for readings a divisor and a decimal count cannot
// express. The descriptor names a renderer rather than supplying a format
// string, so the firmware can never ask for something this side has no way to
// honour -- and an unknown name degrades to the plain number instead of
// blanking the card. A formatter receives the RAW wire value; div/dec do not
// apply to it, exactly as in the firmware's own formatUptime().
const TLM_FORMATTERS = {
  // Uptime milliseconds as HH:MM:SS. Hours are deliberately not clamped to two
  // digits -- millis() wraps at ~49.7 days, and the real figure just before
  // the wrap is more use than a truncated one. Mirrors core/tlm_format.cpp.
  hms(ms) {
    const s = Math.floor(ms / 1000);
    const pad = (n) => String(n).padStart(2, '0');
    return `${pad(Math.floor(s / 3600))}:${pad(Math.floor(s / 60) % 60)}:${pad(s % 60)}`;
  },

  // Dotted-decimal from the packed u32 the wire carries. Mirrors
  // core/tlm_format.cpp's formatIp() exactly -- both must agree on byte
  // order (big-endian: a in the high byte) or the two renderers disagree
  // about the same frame.
  ip(packed) {
    return [(packed >>> 24) & 0xFF, (packed >>> 16) & 0xFF,
            (packed >>> 8) & 0xFF, packed & 0xFF].join('.');
  },
};

function formatTelemetryValue(def, value) {
  if (typeof value !== 'number') return value;
  const fmt = TLM_FORMATTERS[def.fmt];
  if (fmt) return fmt(value);
  const scaled = def.div ? value / def.div : value;
  return scaled.toFixed(def.dec || 0);
}

// core::Fault codes, in the firmware's own order. The wire carries the code
// and the Configuration page names it here, so no named renderer is needed.
// Both helpers take the store's already-formatted value, i.e. a string.
const FAULT_NAMES = ['None', 'Registry overflow', 'Panic'];

function faultText(value) {
  if (value === null || value === undefined) return '–';
  return FAULT_NAMES[Number(value)] ?? `Unknown (${value})`;
}

function faultIsError(value) {
  return value !== null && value !== undefined && Number(value) > 0;
}

// "Label (unit)" for a telemetry key, or just the label when it carries no
// unit. The descriptor owns both; this only assembles them.
function tlmLabel(key) {
  const def = Alpine.store('telemetry').field(key).def;
  if (!def) return '';
  return def.unit ? `${def.label} (${def.unit})` : def.label;
}

// Volts per cell, derived rather than published: an exact division of two
// readings the schema already carries, where `pct` is a firmware policy and so
// comes over the wire. Dashes until a cell count has been latched.
function perCellText() {
  const t = Alpine.store('telemetry');
  const mv = t.raw['vbat'];
  const cells = t.raw['cells'];
  if (!mv || !cells) return '\u2013';
  return (mv / cells / 1000).toFixed(2);
}

// The config form and telemetry cards are the two most repetitive,
// DOM-construction-heavy regions of this file -- markup for both now lives in
// index.html as <template x-for> blocks. These two stores are the only
// bridge the surrounding plain JS (loadDevice/subscribeEvents/watchdog) needs to
// push data in or read state back out: Alpine.store() is reachable from
// anywhere with no element handle, unlike Alpine.$data()/refs.
document.addEventListener('alpine:init', () => {
  // Cross-page state that used to live as ad-hoc DOM writes, reachable only
  // while the page holding that DOM happened to be mounted. showPage() (see
  // below) now destroys and recreates each page's DOM on every navigation,
  // so anything that must survive navigating away and back -- or be read by
  // more than one page at once, like the Terminal Save button mirroring the
  // Configuration page's dirty flag -- has to live here instead.
  Alpine.store('app', {
    version: APP_VERSION,
    connected: false,
    dirty: false,
    revertNote: false,
    fwSummary: 'not connected',
  });

  Alpine.store('config', {
    schema: [],
    values: {},
    invalid: {},

    // Looked up by key, for the hand-curated pages -- `def: null` when the
    // connected board's schema doesn't carry this key at all (e.g. esc1.* on
    // a board with FEATURE_ESC1 0). A curated page names specific keys, so it
    // must handle a missing one explicitly.
    field(key) {
      const def = this.schema.find((p) => p.key === key);
      return { def: def || null, value: def ? this.values[key] : undefined };
    },

    load(schema, values) {
      this.schema = schema;
      this.values = { ...values };
      this.invalid = {};
    },

    async commit(field) {
      const raw = this.values[field.key];
      const val = field.type === 'u8' ? Number(raw) : raw;
      try {
        await Api.setParam(field.key, val);
        this.invalid[field.key] = false;
        setDirty(true);
        if (field.key === 'tlm.rate') tlmPeriodMs = 1000 / Number(val);
      } catch (e) {
        this.invalid[field.key] = true;
        showError(`${field.label}: ${e.message}`);
      }
    },

    // Throttle's row alone dual-writes esc0.direction/esc1.direction together
    // -- a frontend convenience the Modes page uses, not a new firmware param.
    // esc0/esc1 keep their own independent .direction params exactly as
    // designed; nothing stops them being set differently via the Controller
    // page, this is just the one control meant to keep them in sync in the
    // common case.
    async setBothDirections(v) {
      this.values['esc0.direction'] = v;
      this.values['esc1.direction'] = v;
      try {
        await Api.setParam('esc0.direction', v);
        await Api.setParam('esc1.direction', v);
        this.invalid['esc0.direction'] = false;
        this.invalid['esc1.direction'] = false;
        setDirty(true);
      } catch (e) {
        this.invalid['esc0.direction'] = true;
        this.invalid['esc1.direction'] = true;
        showError(`Direction: ${e.message}`);
      }
    },

    // Same dual-write convenience as setBothDirections, for the Configuration
    // page's single PWM Rate control. esc0/esc1 keep independent .rate params
    // -- notify() only ever reaches the owning module, so one shared param
    // would leave the other ESC un-notified -- but two motors on one vehicle
    // running different frame rates has no use case, so the UI offers one.
    async setBothEscRates(v) {
      const keys = ['esc0.rate', 'esc1.rate'].filter((k) => this.field(k).def);
      keys.forEach((k) => { this.values[k] = v; });
      try {
        for (const k of keys) await Api.setParam(k, v);
        keys.forEach((k) => { this.invalid[k] = false; });
        setDirty(true);
      } catch (e) {
        keys.forEach((k) => { this.invalid[k] = true; });
        showError(`PWM Rate: ${e.message}`);
      }
    },

    // Betaflight's own formula: new = old * (measured / reported). The
    // firmware cannot do this itself -- onParamChanged() takes a const Params,
    // so a driver cannot write a parameter -- and does not need to, since one
    // multiplier absorbs every systematic error in the divider chain at once.
    async calibrateVbat(measuredVolts) {
      if (!this.field('vbat.scale').def) {
        showError('Calibrate: this board has no battery sensor');
        return;
      }
      if (this.values['vbat.source'] !== 'adc') {
        showError('Calibrate: set Source to adc first \u2014 a simulated pack cannot be calibrated');
        return;
      }
      // raw, not field().value: the latter is the FORMATTED string ("16.78"),
      // already through the div/dec display hints. The comparison and the
      // formula below are both in millivolts, which is what raw carries.
      // Matches the firmware's own validity floor: a USB-powered board wired
      // to a PDB reads ~4600mV with no pack, which must not be calibrated on.
      const reported = Alpine.store('telemetry').raw['vbat'];
      if (!reported || reported < 6000) {
        showError('Calibrate: no valid reading \u2014 connect a pack first');
        return;
      }
      // Entered in volts because that is what a multimeter reads; the wire and
      // vbat.scale both stay in millivolts.
      const measured = Math.round(Number(measuredVolts) * 1000);
      if (!Number.isFinite(measured) || measured < 5000 || measured > 30000) {
        showError('Calibrate: enter the measured pack voltage in volts (5\u201330)');
        return;
      }
      const next = Math.round((measured * this.values['vbat.scale']) / reported);
      if (next < 1000 || next > 30000) {
        showError(`Calibrate: computed scale ${next} is outside 1000\u201330000 \u2014 check the divider`);
        return;
      }
      this.values['vbat.scale'] = next;
      try {
        await Api.setParam('vbat.scale', next);
        this.invalid['vbat.scale'] = false;
        setDirty(true);
      } catch (e) {
        this.invalid['vbat.scale'] = true;
        showError(`Calibrate: ${e.message}`);
      }
    },
  });

  // Dual-handle range slider over two config keys, built from plain elements
  // so the track, the selected-range fill and the ruler are all styleable.
  // Args: the µs bounds, the [minKey, maxKey] pair it edits (null for a
  // function that has no range), and the config key naming its RC channel.
  Alpine.data('rangeSlider', (min, max, keys, channelKey) => ({
    min,
    max,
    keys,
    channelKey,
    dragging: null,

    // A row with no range still draws its track, ruler and live marker --
    // only the fill and the two handles need a range to exist.
    get hasRange() {
      return !!this.keys;
    },
    get enabled() {
      return this.hasRange && this.$store.app.connected;
    },
    get lo() {
      return this.hasRange ? Number(this.$store.config.values[this.keys[0]]) : this.min;
    },
    get hi() {
      return this.hasRange ? Number(this.$store.config.values[this.keys[1]]) : this.max;
    },
    pct(v) {
      return ((v - this.min) / (this.max - this.min)) * 100;
    },

    // Where the RC stick currently sits, so the marker tracks the radio. The
    // channel may be unset ("none") or the board may be sending nothing yet;
    // mid-stick is the neutral standing in for "no reading".
    get live() {
      const v = this.$store.telemetry.raw[this.$store.config.values[this.channelKey]];
      return typeof v === 'number' ? v : 1500;
    },
    // Clamped: a receiver may legally read outside the slider's own bounds.
    get livePct() {
      return Math.min(Math.max(this.pct(this.live), 0), 100);
    },

    // A labelled tick every tenth, drawn taller at both ends and the midpoint.
    get ticks() {
      const span = this.max - this.min;
      const out = [];
      for (let v = this.min; v <= this.max; v += span / 10) {
        out.push({ v: Math.round(v), major: (v - this.min) % (span / 2) === 0 });
      }
      return out;
    },

    valueAt(clientX) {
      const r = this.$refs.track.getBoundingClientRect();
      const t = Math.min(Math.max((clientX - r.left) / r.width, 0), 1);
      return Math.round(this.min + t * (this.max - this.min));
    },

    // The thumb keeps pointer capture for the whole drag, so move/up keep
    // firing on it after the pointer has left its 1rem box.
    down(e, which) {
      if (!this.enabled) return;
      this.dragging = which;
      e.target.setPointerCapture(e.pointerId);
    },
    move(e) {
      if (this.dragging === null) return;
      this.apply(this.dragging, this.valueAt(e.clientX));
    },
    up() {
      if (this.dragging === null) return;
      const which = this.dragging;
      this.dragging = null;
      this.push(which);
    },

    // Held inside the µs bounds first (a keyboard nudge has no track to clamp
    // it against), then against the other handle: the two may meet, never cross.
    apply(which, v) {
      const inRange = Math.min(Math.max(v, this.min), this.max);
      const bounded = which === 0
        ? Math.min(inRange, this.hi)
        : Math.max(inRange, this.lo);
      this.$store.config.values[this.keys[which]] = bounded;
    },
    push(which) {
      const def = this.$store.config.field(this.keys[which]).def;
      if (def) this.$store.config.commit(def);
    },
    nudge(which, delta) {
      if (!this.enabled) return;
      this.apply(which, (which === 0 ? this.lo : this.hi) + delta);
      this.push(which);
    },
  }));

  Alpine.store('telemetry', {
    schema: [],
    data: {},
    // The formatted value in `data` is a STRING -- "1500", "01:23:45". A bar
    // needs the number, so both are kept rather than reparsing text that a
    // formatter may have made unparseable.
    raw: {},

    // Same lookup-by-key mechanism as $store.config.field() above. `value` is
    // the formatted string (what render() already put in `data`), matching
    // what every existing x-text binding displays.
    field(key) {
      const def = this.schema.find((d) => d.key === key);
      return { def: def || null, value: def ? this.data[key] : undefined };
    },

    load(tlmSchema) {
      this.schema = tlmSchema;
      this.data = {};
      this.raw = {};
    },

    render(rawFrame) {
      noteTelemetry();
      for (const def of this.schema) {
        if (rawFrame[def.key] !== undefined) {
          this.raw[def.key] = rawFrame[def.key];
          this.data[def.key] = formatTelemetryValue(def, rawFrame[def.key]);
        }
      }
    },

    // Position of a reading within its declared lo..hi range, as a
    // percentage. The range comes from the descriptor, never from here:
    // this file must not know the concrete bounds of any given field --
    // that's the whole reason lo/hi exist on TlmDef.
    //
    // Clamped, because the firmware deliberately does not clamp -- a receiver
    // may legally send outside its nominal range and the number stays true
    // even when the bar has run out of room.
    barPct(def) {
      const v = this.raw[def.key];
      if (typeof v !== 'number' || !(def.hi > def.lo)) return 0;
      const pct = ((v - def.lo) / (def.hi - def.lo)) * 100;
      return Math.max(0, Math.min(100, pct));
    },
  });

  // --- firmware / DFU --------------------------------------------------------
  // Unlike config and telemetry, this store is NOT schema-driven — there is no
  // device to ask. It reflects the app's own bundle plus whatever dfu-util can
  // see, which is why the whole page keeps working while disconnected.
  Alpine.store('firmware', {
    images: [],
    recommended: null,
    board: null,          // board of the last device connected, or null
    selected: null,
    dfuPresent: false,
    busy: false,
    phase: 'idle',        // idle | waiting | flashing | done | error
    pct: 0,
    op: null,
    ports: [],
    selectedPort: null,
    uploadTarget: 'dfu',
    uploadPort: null,

    // Connection state is mirrored in here rather than read from the
    // module-level `connected`/`deviceInfo`. Those are plain variables, and
    // Alpine only re-renders on changes to reactive store properties -- the
    // getters below would render once and then never update.
    deviceConnected: false,
    device: {},

    syncDevice(isConnected, info) {
      const next = info || {};
      // Connecting is what makes a recommendation possible at all -- the
      // catalog's `recommended` is derived from the board in `hello`. Without
      // re-fetching here, connecting while already sitting on this page
      // leaves the recommendation permanently null.
      const changed = this.deviceConnected !== isConnected
        || this.device.board !== next.board
        || this.device.built !== next.built;
      this.deviceConnected = isConnected;
      this.device = next;
      if (changed) this.refresh();
    },

    get deviceSummary() {
      if (!this.deviceConnected) {
        return this.board
          ? `not connected (last seen: ${this.board})`
          : 'not connected';
      }
      const d = this.device;
      return [d.fw, d.board, d.built && `built ${d.built}`]
        .filter(Boolean).join(' · ');
    },

    // An exact match, not a version-number comparison: the manifest records
    // the same build timestamp the device reports in `hello`. Board is part
    // of it, or two boards built in the same second each claim to be running
    // the other.
    isRunning(img) {
      return this.deviceConnected && !!this.device.built
        && img.board === this.device.board
        && img.built === this.device.built && img.version === this.device.ver;
    },

    get canEnterDfu() {
      return this.deviceConnected && !this.dfuPresent && !this.busy
        && (this.device.caps || []).includes('dfu');
    },

    get selectedImage() {
      return this.images.find((i) => i.id === this.selected) || null;
    },

    get selectedMethod() {
      return this.selectedImage?.method || 'dfu';
    },

    // Hides the DFU-mode badge/polling text when the catalog has no
    // dfu-method image at all -- an ESP32-only bundle, say -- rather than
    // showing DFU-specific chrome that can never apply to anything selected.
    get hasDfuImages() {
      return this.images.some((i) => i.method !== 'esptool');
    },

    get canFlash() {
      const img = this.selectedImage;
      if (!img || !img.available || this.busy) return false;
      return img.method === 'esptool' ? !!this.selectedPort : this.dfuPresent;
    },

    get canUpload() {
      if (this.busy) return false;
      return this.uploadTarget === 'esptool' ? !!this.uploadPort : this.dfuPresent;
    },

    get statusText() {
      switch (this.phase) {
        case 'waiting':  return 'waiting for a device in DFU mode…';
        // dfu-util runs two passes, erase then download, each 0-100%. Naming
        // the current one is why the bar reaching 100% twice isn't confusing.
        case 'flashing': return this.op ? `${this.op}…` : 'flashing…';
        case 'done':     return 'flash complete';
        case 'error':    return 'flash failed';
        default:         return '';
      }
    },

    async refresh() {
      try {
        const cat = await Api.firmwareCatalog();
        this.images = cat.images;
        this.recommended = cat.recommended;
        this.board = cat.board;
        // Only preselect; never override a choice already made by hand.
        if (!this.selected && cat.recommended) this.selected = cat.recommended;
      } catch (e) { showError(e.message); }
    },

    portLabel(p) {
      return portOptionLabel(p);
    },

    async refreshPorts() {
      try {
        // The simulator is not a flash target; it exists only to connect to.
        const ports = (await Api.ports()).filter((p) => !p.sim);
        // Only reassign when the list actually changed. This runs on the
        // Firmware page's 1.5s poll now, and swapping the array every tick
        // would re-render both port <select>s continuously -- which, apart
        // from the churn, can drop the option the user just chose.
        if (JSON.stringify(ports) !== JSON.stringify(this.ports)) this.ports = ports;
        // A port is only PRESELECTED when `match` recognizes it (link.py's
        // _KNOWN_BOARDS). Nothing recognized means nothing selected -- never
        // ports[0] as a fallback: flashing is destructive, and an honest
        // disabled button plus a "pick a port" hint beats silently aiming at
        // whatever happened to enumerate first. Any listed port can still be
        // picked by hand, recognized or not.
        //
        // '' is the placeholder option, i.e. a deliberate "none" -- left
        // alone, since this now re-runs every 1.5s and would otherwise undo
        // the user's choice a moment after they made it. null (never chosen)
        // and a port that has since vanished both get a fresh preselection.
        const keep = (port) => port === '' || this.ports.some((p) => p.port === port);
        if (!keep(this.selectedPort)) {
          this.selectedPort = this.ports.find((p) => p.match)?.port || null;
        }
        if (!keep(this.uploadPort)) {
          this.uploadPort = this.ports.find((p) => p.match)?.port || null;
        }
      } catch { /* backend restarting; the next page-enter retries */ }
    },

    async pollDfu() {
      try {
        const st = await Api.dfuStatus();
        this.dfuPresent = st.present;
        this.busy = st.busy;
      } catch { /* backend restarting; the next tick retries */ }
    },

    onFlashEvent(ev) {
      this.phase = ev.phase;
      if (ev.op) this.op = ev.op;
      if (typeof ev.pct === 'number') this.pct = ev.pct;
      if (ev.phase === 'error') this.pct = 100;
      if (ev.line) firmwareLog(ev.line);
      if (ev.phase === 'done' || ev.phase === 'error') {
        this.busy = false;
        this.op = null;
        // A finished flash changes what is on the board, so the "currently
        // running" marker is stale until the device is reconnected and
        // re-identified. Re-reading the catalog costs nothing and keeps the
        // recommendation honest.
        this.refresh();
      } else {
        this.busy = true;
      }
    },

    begin() {
      this.phase = 'waiting';
      this.pct = 0;
      this.op = null;
      this.busy = true;
      fwLog.clear();
      const out = el('fw-log');
      if (out) out.value = '';
    },
  });

  // SSID scan is the one bespoke, non-schema-driven bit this module needs
  // (see docs/api.md's "WiFi network scan") -- results arrive over the
  // `scan` WS frame, handled in subscribeEvents() below, not in scan()
  // itself, which only confirms the firmware started scanning.
  Alpine.store('wifi', {
    results: [],
    scanning: false,
    deviceConnected: false,
    device: {},
    // Firmware's wifi::STATUS_CONNECTED (wifi_params.h) -- the wire carries
    // this telemetry field as a plain number with no schema-driven name
    // (no `fmt` renderer), so 2 has to be hardcoded here same as it is
    // wherever the raw value already renders as-is on the Telemetry page.
    wifiStatus: null,

    syncDevice(isConnected, info) {
      this.deviceConnected = isConnected;
      this.device = info || {};
      if (!isConnected) {
        this.scanning = false;
        this.results = [];
        this.wifiStatus = null;
      }
    },

    // Real-hardware-verified: scanning while the radio is already
    // associated to an AP (wifi.status telemetry == 2) doesn't cleanly
    // fail or wedge -- it starves the device's main loop badly enough that
    // telemetry drops from its configured rate to roughly one frame every
    // few seconds, and the scan itself never completes. There's no
    // legitimate reason to scan while already joined to a network anyway,
    // so the button is simply unavailable rather than trying to make that
    // combination work.
    noteWifiStatus(status) {
      this.wifiStatus = status;
    },

    get canScan() {
      return this.deviceConnected && (this.device.caps || []).includes('wifiscan')
        && this.wifiStatus !== 2;
    },

    async scan() {
      this.scanning = true;
      this.results = [];
      try {
        await Api.wifiScan();
      } catch (e) {
        this.scanning = false;
        showError(`Scan: ${e.message}`);
      }
      // scanning stays true until the `scan` WS frame arrives (onScanEvent
      // below) or the connection drops -- there is no separate "scan
      // finished with nothing found" signal, an empty array IS that signal.
    },

    onScanEvent(nets) {
      this.scanning = false;
      this.results = nets;
    },

    pick(net) {
      this.results = [];
      const cfg = Alpine.store('config');
      cfg.values['wifi.ssid'] = net.ssid;
      cfg.commit({ key: 'wifi.ssid', type: 'str', label: 'SSID' });
    },
  });
});

// setState()'s form-disable loop runs synchronously right after loadDevice()
// returns, but Alpine applies store-triggered DOM writes on the next
// microtask -- this lets loadDevice() wait for that flush so the elements it
// expects to find and enable actually exist yet.
function alpineNextTick() {
  return new Promise((resolve) => Alpine.nextTick(resolve));
}

// --- wiring ----------------------------------------------------------------
function portOptionLabel(p) {
  if (p.sim) return 'Simulated board';
  // Every board this template's `match` heuristic doesn't recognize (not
  // one of link.py's _KNOWN_BOARDS) used to render as a bare path,
  // indistinguishable from this environment's own placeholder serial
  // ports (or any other port with nothing plugged in). Any port with a
  // real USB descriptor at least proves *something* is actually
  // connected there, which is worth surfacing even without a name for it.
  return p.board ? `${p.port} (${p.board})`
    : p.vid ? `${p.port} (USB ${p.vid}:${p.pid})`
    : p.port;
}

// Some USB devices genuinely blip out of the OS's port enumeration for a
// poll or two at a time -- confirmed on an ESP32-C3's CDC descriptor, which
// /api/ports drops for exactly one 1s watchdog tick before it's back.
// Reacting to that as a real disappearance is what made the picker itself
// flicker: the port's <option> got removed and recreated, and the selection
// bounced to a fallback and back. A port that goes missing is kept in the
// effective list for a few missed polls before being treated as actually
// gone -- the same "don't trust a single miss" principle the telemetry
// disconnect watchdog already uses (see startWatchdog()'s three-interval
// rule).
const PORT_MISS_GRACE_MS = 3000;
const portSeenAt = new Map(); // port -> { data, lastSeenAt }

function stabilizePorts(rawPorts) {
  const now = Date.now();
  for (const p of rawPorts) portSeenAt.set(p.port, { data: p, lastSeenAt: now });
  for (const [port, rec] of portSeenAt) {
    if (now - rec.lastSeenAt > PORT_MISS_GRACE_MS) portSeenAt.delete(port);
  }
  // Sorted here, once, before anything downstream sees the list -- known
  // boards and any port with a real USB descriptor first, then
  // alphabetically within each group -- so the change-detection compare
  // below and the render diff both work off one canonical order instead of
  // whatever order the OS/pyserial happened to enumerate in this poll.
  return Array.from(portSeenAt.values())
    .map((r) => r.data)
    .sort((a, b) => {
      const rank = (p) => (p.match || p.vid ? 0 : 1);
      const d = rank(a) - rank(b);
      // {numeric: true} makes this a natural sort -- ttyS2 before ttyS10,
      // not the lexicographic "ttyS10" < "ttyS2" plain string comparison
      // gives.
      return d !== 0 ? d : a.port.localeCompare(b.port, undefined, { numeric: true });
    });
}

// Compared against on every refreshPorts() call so an unchanged list is a
// no-op -- see the comment inside for why rebuilding unconditionally broke
// the picker.
let lastPortsJSON = null;

// The port the user actually wants selected -- deliberately NOT read back
// from sel.value at the top of each refresh. Some USB devices (an ESP32-C3's
// CDC descriptor, for one) briefly drop out of the OS's port enumeration and
// reappear a tick later; when that happens mid-selection, refreshPorts falls
// back to some other port for that one tick. Reading sel.value as "the
// previous selection" on the NEXT tick would then latch onto that fallback
// permanently, because the fallback port (unlike the flaky device) is never
// itself missing. Tracking intent separately, updated only by an explicit
// user pick, means a forced fallback tick never overwrites it, so the real
// port reappearing is recognized and restored instead of staying stuck.
let desiredPort = null;
el('port').addEventListener('change', () => {
  desiredPort = el('port').value || null;
});

async function refreshPorts() {
  const ports = stabilizePorts(await Api.ports());
  const portsJSON = JSON.stringify(ports);
  // The watchdog calls this every second while disconnected so a replugged
  // board reappears -- but destroying and recreating every <option> on a
  // timer, even when nothing changed, fights the browser's native dropdown:
  // opening it, then losing it to a same-second rebuild mid-click, reads as
  // "won't let me select a port". Skip the work entirely when the list is
  // byte-for-byte the same as last time.
  if (portsJSON === lastPortsJSON) return;
  lastPortsJSON = portsJSON;

  const sel = el('port');
  // Recognized boards AND any port with a real USB descriptor float to the
  // top -- the port list is a pyserial enumeration order, which has nothing
  // to do with which entries are actually worth looking at first, and on
  // Linux it is dominated by dozens of vid-less /dev/ttySN platform ports
  // that are never a real device. A disabled option separates the top group
  // from genuinely bare ports below, only when both groups are non-empty:
  // nothing to separate from if every port qualifies, or none did.
  const sims = ports.filter((p) => p.sim);
  const known = ports.filter((p) => !p.sim && (p.match || p.vid));
  const other = ports.filter((p) => !p.sim && !p.match && !p.vid);

  // Reuse each port's existing <option> element rather than tearing every
  // one down and recreating it -- an option's selectedness is a property of
  // the element itself, so one that survives this diff keeps its selected
  // state automatically (appendChild() on a node already in the DOM MOVES
  // it, it doesn't reset it), and a port whose label didn't change never
  // touches the DOM at all. Only ports that actually appeared or vanished
  // since last time cause any mutation.
  const existing = new Map(
    Array.from(sel.options).filter((o) => !o.disabled).map((o) => [o.value, o])
  );
  const touched = new Set();
  let matched = null;

  const place = (p) => {
    let o = existing.get(p.port);
    const label = portOptionLabel(p);
    if (o) {
      if (o.textContent !== label) o.textContent = label;
    } else {
      o = document.createElement('option');
      o.value = p.port;
      o.textContent = label;
    }
    sel.appendChild(o);
    touched.add(o);
  };

  const separate = () => {
    // Stateless (never selected, always disabled) -- cheaper to recreate
    // than to track across a diff.
    const sep = document.createElement('option');
    sep.disabled = true;
    sep.textContent = '──────────';
    sel.appendChild(sep);
    touched.add(sep);
  };

  for (const p of sims) place(p);
  if (sims.length && (known.length || other.length)) separate();
  for (const p of known) {
    place(p);
    // First recognized-board match wins -- not just the first port with a
    // vid, so an unrecognized-but-real device never outranks an actual known
    // board for auto-select. "first" should mean the first one actually
    // found, not whichever matched last.
    if (!matched && p.match) matched = p.port;
  }
  if (known.length && other.length) separate();
  for (const p of other) place(p);

  // Whatever wasn't placed this round belonged to a port that dropped out
  // of the list.
  for (const o of Array.from(sel.options)) {
    if (!touched.has(o)) sel.removeChild(o);
  }

  // First load / nothing picked yet: adopt the recognized-board guess as the
  // desired port too, so it's what a later flicker-and-recover restores.
  // Only a real board is adopted -- the simulator stays a provisional
  // fallback below, so a board appearing later displaces it.
  if (!desiredPort && matched) desiredPort = matched;
  const desiredPresent = ports.some((p) => p.port === desiredPort);
  // Desired port missing this tick (e.g. mid-flicker): a recognized board
  // wins, and the simulator is the last resort when no real board is there.
  // Picking it by hand sets desiredPort, which keeps it selected regardless.
  if (desiredPresent) sel.value = desiredPort;
  else if (matched) sel.value = matched;
  else if (sims.length) sel.value = sims[0].port;
  growPortSelectWidth(sel);
}

// #port is `w-auto`, so its rendered width tracks whatever option is
// currently showing -- a rebuild that swaps in a longer or shorter label
// (e.g. a device with a long "(USB vvvv:pppp)" suffix appearing or
// disappearing) visibly resizes the control and shifts the Connect button
// next to it. Only ever grow the floor, never shrink it, so the width
// settles at the widest label seen this session instead of jittering on
// every plug/unplug.
let portSelectMinWidth = 0;

function growPortSelectWidth(sel) {
  sel.style.minWidth = '';
  const natural = sel.getBoundingClientRect().width;
  if (natural > portSelectMinWidth) portSelectMinWidth = natural;
  sel.style.minWidth = `${portSelectMinWidth}px`;
}

async function loadDevice() {
  const [schema, values] = await Promise.all([Api.schema(), Api.params()]);
  Alpine.store('config').load(schema.params, values);
  Alpine.store('telemetry').load(schema.tlm);
  await alpineNextTick();
  setTelemetryPeriodFrom(values);
}

el('connect').addEventListener('click', async () => {
  try {
    if (connected) {
      setState((await Api.disconnect()).state);
    } else {
      const st = await Api.connect(el('port').value);
      setState(st.state, st);
      await loadDevice();
      setState(st.state, st);
      // Deliberately stays on whatever page you were on. Connecting enables
      // the gated nav items (updateNavAvailability) and that is enough --
      // jumping to Configuration threw away the page you had chosen, which is
      // wrong when you connected in order to watch Telemetry or the Terminal.
    }
  } catch (e) { showError(e.message); }
});

// One dirty flag, two readouts: the Configuration page's "applied — not saved"
// note and the Terminal's Save button. Anything that changes the device's RAM
// -- a form field, a restore -- goes through here, so both pages agree about
// whether there is something worth writing to flash.
function setDirty(dirty) {
  // Optional-chained: setState() calls this unconditionally on every
  // disconnect (see above), and setState() itself can run before Alpine has
  // initialised (this script is not deferred, Alpine's is).
  const app = window.Alpine?.store('app');
  if (!app) return;
  app.dirty = dirty;
  // Any subsequent action supersedes the fallback note, so it is cleared here
  // rather than tracked separately. The revert handler re-shows it after
  // calling setDirty().
  app.revertNote = false;
}

async function saveToFlash() {
  // Flash erase stalls the board ~1s; telemetry will gap. That is expected.
  await Api.save();
  setDirty(false);
}

// --- global actions (shell-level: save/discard/defaults live in every page
// once connected, not just one) --------------------------------------------
el('save').addEventListener('click', async () => {
  try {
    await saveToFlash();
  } catch (e) { showError(e.message); }
});

el('defaults').addEventListener('click', async () => {
  try {
    await Api.defaults();
    await loadDevice();
    // deviceInfo, not nothing: setState() blanks the navbar identity and the
    // Help page whenever `info` is falsy, and the device is still connected --
    // reloading its parameters told us nothing new about who it is.
    setState('connected', deviceInfo);
    // The firmware's `defaults` op reloads RAM and re-notifies the modules but
    // never touches flash (core/dispatch.cpp, Op::Defaults), so the device is
    // now exactly as unsaved as after editing a field by hand.
    setDirty(true);
  } catch (e) { showError(e.message); }
});

el('revert').addEventListener('click', async () => {
  try {
    const res = await Api.revert();
    await loadDevice();
    setState('connected', deviceInfo);   // see the defaults handler above
    // "flash" means RAM now matches what is stored, so there is nothing left
    // to save -- this is the ONLY action that leaves the device clean.
    // "defaults" means the board had nothing valid stored and the firmware
    // fell back, which is both worth saving and worth saying out loud.
    setDirty(res.src !== 'flash');
    if (res.src !== 'flash') Alpine.store('app').revertNote = true;
  } catch (e) { showError(e.message); }
});

// --- page init functions -----------------------------------------------------

// --- page fragments -----------------------------------------------------------
const pageCache = new Map();   // page name -> fetched fragment HTML text, cached after first use

// One-time (per page) DOM wiring, run in full on EVERY mount of that page --
// see "Correction to the design doc" at the top of this plan for why a
// once-only guard would be wrong here: #page-mount's innerHTML is replaced on
// every navigation (below), which destroys the previous mount's nodes and
// every listener attached to them, so there is nothing to "double-attach" by
// re-running this every time.
// Every page gets an explicit entry -- `null` for "pure Alpine-driven
// content, nothing here needs imperative wiring beyond what Alpine.initTree
// already does when the fragment mounts" -- rather than an omission, so a
// missing entry is distinguishable from an intentional no-op. The check
// right below turns "forgot to add a page here" into a loud console error
// instead of a page with silently dead buttons.
const PAGE_INIT = {
  home:       null,
  config:     null,
  controller: null,
  modes:      null,
  terminal:   initTerminalPage,
  firmware:   initFirmwarePage,
  wiring:     null,
  help:       null,
};

// app.js is a plain (non-deferred) trailing <script>, so the DOM -- and with
// it every [data-page] nav button, which live in the shell, not a fragment
// -- is already fully parsed by the time this runs.
document.querySelectorAll('[data-page]').forEach((btn) => {
  if (!(btn.dataset.page in PAGE_INIT)) {
    console.error(`PAGE_INIT is missing an entry for page "${btn.dataset.page}"`);
  }
});

// --- side-menu navigation ---------------------------------------------------
// Terminal is deliberately not connection-gated. It is readable while
// disconnected so you can sit on it and watch the device's boot record
// arrive when you connect -- the firmware replays that after `hello`, and
// being forced to connect first and navigate second is exactly the moment
// you would miss it. Its controls are disabled instead (Alpine-bound to
// $store.app.connected, see pages/terminal.html).
//
// Firmware is not gated either, for a stronger version of the same reason: a
// board that needs re-flashing is frequently a board that cannot be talked
// to, and gating the recovery tool on a working device would be exactly
// backwards.
const CONNECTION_REQUIRED_PAGES = new Set(['config', 'controller', 'modes']);

// Bumped on every call, checked after the (possibly slow, first-visit-only)
// fragment fetch below -- two overlapping navigations otherwise let whichever
// fetch resolves LAST win #page-mount and the nav highlight, regardless of
// which page the user actually clicked last, and can leave e.g. DFU polling
// running for a page that isn't even displayed any more.
let showPageGeneration = 0;

async function showPage(page) {
  const generation = ++showPageGeneration;
  let html = pageCache.get(page);
  if (html === undefined) {
    // 'no-cache' revalidates rather than serving from the HTTP cache: a
    // fragment held there while index.html/app.js reload is markup skewed
    // against the CSS and components it relies on. A 304 costs nothing here.
    const r = await fetch(`pages/${page}.html`, { cache: 'no-cache' });
    if (!r.ok) {
      showError(`Failed to load the ${page} page (${r.status})`);
      return;
    }
    html = await r.text();
    // Only a successful fetch is cached -- a transient error must not poison
    // every later visit to this page for the rest of the session.
    pageCache.set(page, html);
  }
  // A newer navigation started (and, on a slow connection, may already have
  // finished) while this fetch was in flight -- drop this stale result
  // rather than clobbering whatever the user actually navigated to since.
  if (generation !== showPageGeneration) return;
  el('page-mount').innerHTML = html;
  // Safe unguarded: this always runs after an awaited fetch, and even a
  // same-origin static-file fetch resolves as a browser task, never
  // synchronously -- by the time it does, Alpine's own deferred script has
  // long since run and registered every store, the same guarantee
  // loadDevice() already relies on for its own direct Alpine.store() calls.
  Alpine.initTree(el('page-mount'));
  PAGE_INIT[page]?.();
  // Polling for a DFU device costs a `dfu-util -l` subprocess per tick, so it
  // runs only while the page that displays it is actually on screen.
  setDfuPolling(page === 'firmware');
  document.querySelectorAll('[data-page]').forEach((btn) => {
    const isActive = btn.dataset.page === page;
    btn.classList.toggle('active', isActive);
    if (isActive) btn.setAttribute('aria-current', 'page');
    else btn.removeAttribute('aria-current');
  });
  // On narrow viewports the sidebar is a Bootstrap offcanvas overlay; close
  // it once a page is chosen. A no-op when it isn't currently shown (desktop
  // widths, or the offcanvas was never opened).
  const sidebar = document.getElementById('sidebarMenu');
  window.bootstrap?.Offcanvas.getInstance(sidebar)?.hide();
}

function updateNavAvailability() {
  document.querySelectorAll('[data-page]').forEach((btn) => {
    const needsConnection = CONNECTION_REQUIRED_PAGES.has(btn.dataset.page);
    const disabled = needsConnection && !connected;
    btn.classList.toggle('disabled', disabled);
    btn.setAttribute('aria-disabled', disabled ? 'true' : 'false');
  });
}

document.querySelectorAll('[data-page]').forEach((btn) => {
  btn.addEventListener('click', () => {
    if (btn.classList.contains('disabled')) return;
    showPage(btn.dataset.page);
  });
});

// Shared by Terminal's #term-output and Firmware's #fw-log: both are a
// bounded, newline-joined text buffer that must outlive the owning page's
// DOM -- log lines (a device's replayed boot record, "show device traffic"
// frames, a flash's WS progress events) can arrive while that page isn't
// even the mounted one -- plus the trim/scroll logic for writing the
// buffer into a <textarea> on the ticks where it IS mounted.
function makeLogBuffer(maxLines) {
  let text = '';
  return {
    append(line) {
      const lines = (text ? text.split('\n') : []).concat(line.split('\n'));
      text = lines.slice(-maxLines).join('\n');
    },
    clear() { text = ''; },
    get value() { return text; },
  };
}

// A no-op when `elId` isn't mounted -- see makeLogBuffer's comment above for
// why that has to be silent rather than an error.
function flushLogBuffer(buf, elId) {
  const out = el(elId);
  if (!out) return;
  out.value = buf.value;
  out.scrollTop = out.scrollHeight;
}

// --- terminal ----------------------------------------------------------------
const TERM_MAX_LINES = 500;   // caps growth when "show device traffic" streams tlm at up to 50Hz
const termHistory = [];
let termHistoryIdx = 0;

const termLog = makeLogBuffer(TERM_MAX_LINES);   // #term-output's content, kept alive across unmount/remount

function termAppend(text) {
  termLog.append(text);
  flushLogBuffer(termLog, 'term-output');
}

async function termRun(text) {
  if (!text.trim()) return;
  termHistory.push(text);
  termHistoryIdx = termHistory.length;
  termAppend(`> ${text}`);
  el('term-send').disabled = true;
  try {
    const r = await Api.sendTerminalCommand(text);
    termAppend(r.friendly);
    if (el('term-raw')?.checked) {
      if (r.raw_sent) termAppend(`  >> ${r.raw_sent}`);
      if (r.raw_recv) termAppend(`  << ${r.raw_recv}`);
    }
    if (r.ok) {
      // Unlike restore/defaults/revert, this command line can be anything --
      // `set rx.protocol elrs` changes which settings group and telemetry
      // fields are live. Without this, the form stays on the old protocol's
      // group and an edit there writes a parameter that has no effect.
      await loadDevice();
      // r.dirty is null for read-only commands (get/list/dump/help) -- only
      // set/save/defaults/revert carry a verdict. Without this, a `set` typed
      // here never enables the Save button, so the change looks applied but
      // is RAM-only and silently gone on the next reboot.
      if (r.dirty !== null) {
        setDirty(r.dirty);
        if (r.dirty && text.trim().split(/\s+/)[0].toLowerCase() === 'revert') {
          Alpine.store('app').revertNote = true;
        }
      }
    }
  } catch (e) {
    termAppend(`ERROR: ${e.message}`);
  } finally {
    // Not an unconditional re-enable: the command may well have been what
    // revealed the device is gone. Guarded: Terminal may no longer be the
    // mounted page by the time a slow command's response comes back.
    const sendBtn = el('term-send');
    if (sendBtn) sendBtn.disabled = !connected;
  }
}

function initTerminalPage() {
  // Replay whatever accumulated while Terminal wasn't mounted -- the
  // textarea itself is brand new, termLog is not.
  flushLogBuffer(termLog, 'term-output');

  el('term-send').addEventListener('click', () => {
    const input = el('term-input');
    termRun(input.value);
    input.value = '';
  });

  el('term-input').addEventListener('keydown', (ev) => {
    if (ev.key === 'Enter') {
      el('term-send').click();
    } else if (ev.key === 'ArrowUp') {
      if (termHistoryIdx > 0) { termHistoryIdx--; el('term-input').value = termHistory[termHistoryIdx]; }
      ev.preventDefault();
    } else if (ev.key === 'ArrowDown') {
      if (termHistoryIdx < termHistory.length - 1) {
        termHistoryIdx++;
        el('term-input').value = termHistory[termHistoryIdx];
      } else {
        termHistoryIdx = termHistory.length;
        el('term-input').value = '';
      }
      ev.preventDefault();
    }
  });

  el('term-clear').addEventListener('click', () => {
    termLog.clear();
    el('term-output').value = '';
  });

  // --- restore from INI -------------------------------------------------------
  // The other half of `dump`. The report text is built here from the backend's
  // applied/skipped lists rather than sent as prose, so this stays the only
  // place that decides how the terminal reads.
  el('term-restore').addEventListener('click', () => el('term-restore-file').click());

  el('term-restore-file').addEventListener('change', async (ev) => {
    const file = ev.target.files[0];
    // Reset immediately so re-picking the same file after an edit still fires
    // a `change` event.
    ev.target.value = '';
    if (!file) return;
    termAppend(`> restore from ${file.name}`);
    try {
      const r = await Api.restoreIni(await file.text());
      termAppend(`applied ${r.applied.length} setting(s)`
        + (r.applied.length ? `: ${r.applied.join(', ')}` : ''));
      for (const s of r.skipped) termAppend(`  skipped ${s.key}: ${s.reason}`);
      if (r.applied.length) {
        // Values are live on the device but not in flash — same state as
        // editing a field by hand, so surface the same reminder.
        await loadDevice();
        setDirty(true);
        termAppend('Not saved to flash yet — click "Save to flash".');
      }
    } catch (e) {
      termAppend(`ERROR: ${e.message}`);
    }
  });

  el('term-save').addEventListener('click', async () => {
    termAppend('> save to flash');
    el('term-save').disabled = true;      // the write stalls the board ~1s
    try {
      await saveToFlash();
      termAppend('OK: saved to flash');
    } catch (e) {
      termAppend(`ERROR: ${e.message}`);
      setDirty(true);                     // still unsaved — let them retry
      // setDirty(true) above is a same-value write when dirty was already
      // true (it was, or Save couldn't have been clicked) -- Alpine's
      // :disabled="!$store.app.dirty" binding skips re-firing on an
      // unchanged value, so the imperative disable set above has to be
      // cleared by hand too. Guarded: Terminal may no longer be the mounted
      // page by the time a save that stalls the board ~1s comes back.
      const saveBtn = el('term-save');
      if (saveBtn) saveBtn.disabled = false;
    }
  });
}

// --- firmware page -----------------------------------------------------------
const FW_MAX_LINES = 400;
let dfuPollTimer = null;

const fwLog = makeLogBuffer(FW_MAX_LINES);   // #fw-log's content, kept alive across unmount/remount

function firmwareLog(text) {
  fwLog.append(text);
  flushLogBuffer(fwLog, 'fw-log');
}

function setDfuPolling(on) {
  const store = window.Alpine?.store('firmware');
  if (on && store && !dfuPollTimer) {
    store.syncDevice(connected, deviceInfo);
    store.refresh();
    store.refreshPorts();
    store.pollDfu();
    // Ports are polled alongside DFU status, not just on page entry: plugging
    // a board in AFTER opening this page is the normal sequence, and without
    // this the picker would keep showing the old list until you navigated
    // away and back. /api/ports is a pyserial enumeration -- no subprocess,
    // unlike the dfu-util call behind pollDfu() -- so it rides the same
    // cadence for free.
    dfuPollTimer = setInterval(() => {
      store.pollDfu();
      store.refreshPorts();
    }, 1500);
  } else if (!on && dfuPollTimer) {
    clearInterval(dfuPollTimer);
    dfuPollTimer = null;
  }
}

function initFirmwarePage() {
  flushLogBuffer(fwLog, 'fw-log');

  el('fw-enter-dfu').addEventListener('click', async () => {
    const store = Alpine.store('firmware');
    try {
      await Api.enterDfu();
      // The device acked and is now resetting; its port is already gone, so
      // reflect that immediately rather than waiting for the watchdog to
      // notice and report it as a fault.
      setState('disconnected');
      firmwareLog('device acknowledged the DFU request and is rebooting');
      // The ROM bootloader takes a moment to enumerate.
      setTimeout(() => store.pollDfu(), 1200);
    } catch (e) {
      showError(e.message);
      firmwareLog(`ERROR: ${e.message}`);
    }
  });

  el('fw-flash').addEventListener('click', async () => {
    const store = Alpine.store('firmware');
    const img = store.images.find((i) => i.id === store.selected);
    if (!img) return;
    if (!window.confirm(
        `Flash ${img.name} ${img.version} (${img.board}) to the board?\n\n`
        + 'This overwrites the firmware currently on it.')) return;
    store.begin();
    firmwareLog(`> flash ${img.id}`);
    try {
      await Api.flashBundled(img.id, img.method === 'esptool' ? store.selectedPort : undefined);
    } catch (e) {
      // Progress arrives over the WebSocket, but a request rejected outright
      // (bad checksum, another flash already running) never gets that far.
      store.onFlashEvent({ phase: 'error', line: e.message });
      showError(e.message);
    }
  });

  el('fw-upload').addEventListener('click', () => el('fw-upload-file').click());

  el('fw-upload-file').addEventListener('change', async (ev) => {
    const file = ev.target.files[0];
    ev.target.value = '';   // so re-picking the same file fires `change` again
    if (!file) return;
    const store = Alpine.store('firmware');
    if (!window.confirm(
        `Flash ${file.name} to the board?\n\n`
        + 'Nothing verifies this file matches your board.')) return;
    store.begin();
    firmwareLog(`> flash ${file.name} (${file.size} bytes)`);
    try {
      // Read here rather than handing the File to Api: the transport seam takes
      // bytes, so that it can be something other than fetch one day.
      await Api.flashUpload(await file.arrayBuffer(), file.name,
                            store.uploadTarget,
                            store.uploadTarget === 'esptool' ? store.uploadPort : null);
    } catch (e) {
      store.onFlashEvent({ phase: 'error', line: e.message });
      showError(e.message);
    }
  });
}

// One handler for every frame the backend pushes. Nothing here knows there is
// a socket underneath -- Api.subscribe owns the transport and its reconnection
// across backend restarts -- which is what lets an Electron build swap it for
// IPC without this function changing at all.
function subscribeEvents() {
  Api.subscribe((msg) => {
    if (msg.type === 'tlm') {
      Alpine.store('telemetry').render(msg.data);
      // Not every board has a wifi module -- only update when the field
      // is actually present in this frame.
      if ('wifi.status' in msg.data) Alpine.store('wifi').noteWifiStatus(msg.data['wifi.status']);
    }
    else if (msg.type === 'state') {
      const d = msg.data;
      setState(typeof d === 'string' ? d : d.state, typeof d === 'object' ? d : null);
    } else if (msg.type === 'flash') {
      // Its own frame type, not `log`: log frames are the DEVICE talking and
      // render in the Terminal as `[device] …`. This is dfu-util running on
      // the host, and putting it in the Terminal would misattribute it.
      Alpine.store('firmware').onFlashEvent(msg.data);
    } else if (msg.type === 'scan') {
      Alpine.store('wifi').onScanEvent(msg.data);
    } else if (msg.type === 'log') {
      // Device log lines are unprompted, so they are marked to distinguish
      // them from a reply to something the user typed. The firmware replays
      // its boot record after every `hello`, which is what puts boot health
      // here on connect — those lines were produced long before the browser
      // existed, and showing them only under "device traffic" buried them
      // among telemetry frames.
      termAppend(`[device] ${msg.data}`);
    }
    if (el('term-traffic')?.checked) {
      termAppend(`<< [${msg.type}] ${JSON.stringify(msg.data)}`);
    }
  });
}

// --- disconnect watchdog ---------------------------------------------------
// Spec rule: declare a disconnect only after THREE missed telemetry intervals.
// Deliberately slack — a flash save stalls the MCU ~1s and telemetry will gap.
// A tighter threshold would report a false disconnect on every save.
let lastTlmAt = 0;
let tlmPeriodMs = 100;

function noteTelemetry() {
  lastTlmAt = Date.now();
  clearStale();   // a fresh frame proves the firmware is alive again
}

function setTelemetryPeriodFrom(values) {
  const hz = Number(values['tlm.rate']) || 10;
  tlmPeriodMs = 1000 / hz;
}

function startWatchdog() {
  setInterval(async () => {
    // Rescan on every tick, connected or not. Being connected no longer
    // implies the picker is settled: a board plugged in while the simulator
    // is connected still has to appear in it.
    try { await refreshPorts(); } catch { /* ignore */ }
    if (!connected) return;
    if (!lastTlmAt || Date.now() - lastTlmAt <= tlmPeriodMs * 3) return;
    try {
      const st = await Api.status();
      if (st.state === 'connected') {
        // Port-level state is fine but telemetry has gone quiet -- the
        // firmware itself may be wedged. Distinct "stale" signal; a true
        // disconnect (port lost) still falls through to setState() below.
        setStale();
      } else {
        setState(st.state, st);
      }
    } catch {
      setState('disconnected');
    }
  }, 1000);
}

(async function init() {
  el('app-version').textContent = `v${APP_VERSION}`;
  // Home is always the landing page, including on a reload while a device is
  // still connected server-side. Nothing navigates for you any more -- page
  // choice is the user's, and connecting only enables the gated nav items.
  // Not awaited (refreshPorts()/connect status shouldn't wait on it), but
  // .catch()'d like every other async path in this file -- otherwise a
  // failed fetch of pages/home.html surfaces only as an unhandled rejection.
  showPage('home').catch((e) => showError(`Failed to load the app: ${e.message}`));
  await refreshPorts();
  const st = await Api.status();
  setState(st.state, st);
  if (st.state === 'connected') await loadDevice();
  subscribeEvents();
  startWatchdog();
})();
