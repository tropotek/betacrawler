import { Api } from './api.js';
import { assessBrowser } from './browser-support.js';

// The app (backend + this UI) is versioned independently of the firmware:
// they are separate projects that happen to live in one repo, and their
// numbers are not meant to track each other. betacrawler is a template, so this
// stays 1.0.0 -- a fork bumps it. The firmware's own version lives in
// firmware/include/config.h and arrives over the wire in `hello`.
const APP_VERSION = '1.0.0';

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

// pages/config.html calls these from Alpine expressions, which resolve
// against window. This file is a module, so its top level is not that.
Object.assign(window, { tlmLabel, perCellText, faultText, faultIsError });

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
  // Unlike config and telemetry, this store is NOT schema-driven -- there is no
  // device to ask. It reflects the images this site ships with plus whatever
  // WebUSB can see, which is why the whole page keeps working while
  // disconnected.
  Alpine.store('firmware', {
    images: [],
    recommended: null,
    board: null,          // board of the last device connected, or null
    selected: null,
    dfuPresent: false,
    busy: false,
    phase: 'idle',        // idle | waiting | needsdevice | flashing | done | error
    pct: 0,
    op: null,

    // Connection state is mirrored in here rather than read from the
    // module-level `connected`/`deviceInfo`. Those are plain variables, and
    // Alpine only re-renders on changes to reactive store properties.
    deviceConnected: false,
    device: {},

    syncDevice(isConnected, info) {
      const next = info || {};
      // Connecting is what makes a recommendation possible at all -- it is
      // derived from the board in `hello`. Without re-reading here, connecting
      // while already sitting on this page leaves it permanently null.
      const changed = this.deviceConnected !== isConnected
        || this.device.board !== next.board
        || this.device.built !== next.built;
      this.deviceConnected = isConnected;
      this.device = next;
      if (changed) this.refresh();
    },

    get deviceSummary() {
      if (!this.deviceConnected) {
        return this.board ? `not connected (last seen: ${this.board})` : 'not connected';
      }
      const d = this.device;
      return [d.fw, d.board, d.built && `built ${d.built}`].filter(Boolean).join(' · ');
    },

    // An exact match, not a version-number comparison: the manifest records the
    // same build timestamp the device reports in `hello`. Board is part of it,
    // or two boards built in the same second each claim to be running the other.
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

    // A connected board is flashable: Api.flashBundled/flashUpload reboot it
    // into DFU themselves and wait for the bootloader, so the page needs no
    // reboot/select choreography on the normal path.
    get canFlash() {
      return !!this.selectedImage && !this.busy
        && (this.dfuPresent || this.deviceConnected);
    },

    get canUpload() {
      return !this.busy && (this.dfuPresent || this.deviceConnected);
    },

    get statusText() {
      switch (this.phase) {
        case 'waiting':  return 'waiting for a device in DFU mode…';
        case 'needsdevice': return 'waiting for access to the bootloader…';
        // Two passes, erase then download, each 0-100%. Naming the current one
        // is why the bar reaching 100% twice isn't confusing.
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

    // Read once on entering the page: the browser announces every arrival and
    // departure after that, so there is nothing to poll for.
    async readDfu() {
      try {
        this.applyDfu(await Api.dfuStatus());
      } catch { /* the next visit retries */ }
    },

    applyDfu(st) {
      this.dfuPresent = st.present;
      this.busy = st.busy;
    },

    onFlashEvent(ev) {
      this.phase = ev.phase;
      if (ev.op) this.op = ev.op;
      if (typeof ev.pct === 'number') this.pct = ev.pct;
      if (ev.phase === 'error') this.pct = 100;
      if (ev.line) firmwareLog(ev.line);
      // The flash is parked on a permission the browser will only grant to a
      // click of its own, so put a button in front of the user to click.
      showDfuGrantModal(ev.phase === 'needsdevice');
      if (ev.phase === 'done' || ev.phase === 'error') {
        this.busy = false;
        this.op = null;
        // A finished flash changes what is on the board, so the "currently
        // running" marker is stale until it is reconnected and re-identified.
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

});

// setState()'s form-disable loop runs synchronously right after loadDevice()
// returns, but Alpine applies store-triggered DOM writes on the next
// microtask -- this lets loadDevice() wait for that flush so the elements it
// expects to find and enable actually exist yet.
function alpineNextTick() {
  return new Promise((resolve) => Alpine.nextTick(resolve));
}

async function loadDevice() {
  const [schema, values] = await Promise.all([Api.schema(), Api.params()]);
  Alpine.store('config').load(schema.params, values);
  Alpine.store('telemetry').load(schema.tlm);
  await alpineNextTick();
  setTelemetryPeriodFrom(values);
}

// Guards the click while a connect is in flight. Opening a port can take
// seconds on a board that is slow to enumerate, and a second open() on the same
// port throws "a call to open() is already in progress" -- so the button says
// what it is doing and refuses to start twice.
let connecting = false;

el('connect').addEventListener('click', async () => {
  if (connecting) return;
  try {
    if (connected) {
      setState((await Api.disconnect()).state);
      return;
    }
    connecting = true;
    el('connect').disabled = true;
    el('connect').textContent = 'Connecting…';
    // Web Serial remembers a granted port, so a board connected once
    // reconnects on one click. Anything else -- nothing granted yet, or
    // several -- still goes through the browser's own picker.
    //
    // Phase timings for a connect, at console.debug so DevTools hides them
    // unless Verbose is on. The first connect after a browser restart is
    // slower than later ones; this is what says which phase pays for it.
    const t0 = performance.now();
    const known = await Api.knownPorts();
    const tPorts = performance.now();
    const port = known.length === 1 ? known[0].port : await Api.requestPort();
    const tPick = performance.now();
    const st = await Api.connect(port);
    const tOpen = performance.now();
    setState(st.state, st);
    await loadDevice();
    console.debug(`connect: getPorts ${(tPorts - t0).toFixed(0)}ms`
      + ` | pick ${(tPick - tPorts).toFixed(0)}ms`
      + ` | open+handshake ${(tOpen - tPick).toFixed(0)}ms`
      + ` | loadDevice ${(performance.now() - tOpen).toFixed(0)}ms`);
    setState(st.state, st);
    // Deliberately stays on whatever page you were on. Connecting enables
    // the gated nav items (updateNavAvailability) and that is enough --
    // jumping to Configuration threw away the page you had chosen, which is
    // wrong when you connected in order to watch Telemetry or the Terminal.
  } catch (e) {
    // Dismissing the picker rejects with NotFoundError. That is a choice,
    // not a failure, and must not raise an error banner.
    if (e.name !== 'NotFoundError') showError(e.message);
  } finally {
    connecting = false;
    el('connect').disabled = false;
    el('connect').textContent = connected ? 'Disconnect' : 'Connect';
  }
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
  if (page === 'firmware') enterFirmwarePage();
  // Save/Discard/Load defaults act on the device's config -- only the pages
  // that edit it need the bar at all. It stays mounted in the shell rather
  // than each page's own fragment so it survives navigation without losing
  // Alpine's dirty/revertNote state.
  el('save-bar').classList.toggle('d-none', !CONNECTION_REQUIRED_PAGES.has(page));
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
  btn.addEventListener('click', (event) => {
    // preventDefault() is a no-op for the plain <button> nav items; it's
    // here for the navbar-brand link (an <a href="#">), so clicking it
    // doesn't also jump the page to the top or append "#" to the URL.
    event.preventDefault();
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

// --- firmware page -----------------------------------------------------------
const FW_MAX_LINES = 400;

const fwLog = makeLogBuffer(FW_MAX_LINES);   // #fw-log's content, kept alive across unmount

function firmwareLog(text) {
  fwLog.append(text);
  flushLogBuffer(fwLog, 'fw-log');
}

// The grant prompt is markup in index.html, not on the Firmware page: a flash
// runs on while another page is mounted, so the modal has to exist wherever the
// frame that opens it arrives. Wired once at startup for the same reason.
let dfuGrantModal = null;

function showDfuGrantModal(show) {
  if (!show) {
    dfuGrantModal?.hide();
    return;
  }
  const node = el('dfu-grant-modal');
  if (!node) return;
  dfuGrantModal ||= new window.bootstrap.Modal(node);
  dfuGrantModal.show();
}

function initDfuGrantModal() {
  const grant = el('dfu-grant-ok');
  if (!grant) return;
  grant.addEventListener('click', async () => {
    // Hidden first, in the same task as the click: the chooser this opens is
    // the point of the modal, and the flash it resumes runs to completion
    // inside the await below.
    showDfuGrantModal(false);
    grant.disabled = true;
    try {
      await Api.grantDfuAndResume();
    } catch (e) {
      showError(e.message);
    } finally {
      grant.disabled = false;
    }
  });
  el('dfu-grant-cancel').addEventListener('click', () => {
    Api.cancelDfuGrant();
    showDfuGrantModal(false);
  });
}

// Entering the page reads the catalog and the current DFU state once. A
// bootloader arriving or leaving afterwards comes through as a `dfu` frame, so
// nothing here polls -- and nothing opens a device to find out, which on a
// stateful bootloader is interference rather than observation.
function enterFirmwarePage() {
  const store = window.Alpine?.store('firmware');
  if (!store) return;
  store.syncDevice(connected, deviceInfo);
  store.refresh();
  store.readDfu();
}

function initFirmwarePage() {
  flushLogBuffer(fwLog, 'fw-log');

  el('fw-select-dfu').addEventListener('click', async () => {
    try {
      const chosen = await Api.requestDfuDevice();
      firmwareLog(`selected ${chosen.label}`);
      Alpine.store('firmware').readDfu();
    } catch (e) {
      // A cancelled chooser is a NotFoundError, not a fault worth alerting on.
      if (e.name !== 'NotFoundError') showError(e.message);
    }
  });

  el('fw-enter-dfu').addEventListener('click', async () => {
    const store = Alpine.store('firmware');
    try {
      await Api.enterDfu();
      // The device acked and is now resetting; its port is already gone, so
      // reflect that immediately rather than waiting for the watchdog to
      // notice and report it as a fault.
      setState('disconnected');
      firmwareLog('device acknowledged the DFU request and is rebooting');
      // A bootloader this browser has never been granted cannot announce
      // itself, so waiting alone would dead-end here. Api asks for it instead,
      // and publishes the presence it settles on.
      if (await Api.ensureDfuDevice()) return;
      firmwareLog('no bootloader this browser can reach; use "Select DFU device" '
                  + 'below to grant access once');
      store.readDfu();
    } catch (e) {
      showError(e.message);
      firmwareLog(`ERROR: ${e.message}`);
    }
  });

  el('fw-flash').addEventListener('click', async () => {
    const store = Alpine.store('firmware');
    const img = store.selectedImage;
    if (!img) return;
    if (!window.confirm(
        `Flash ${img.name} ${img.version} (${img.board}) to the board?\n\n`
        + 'This overwrites the firmware currently on it.')) return;
    store.begin();
    firmwareLog(`> flash ${img.id}`);
    try {
      await Api.flashBundled(img.id);
    } catch (e) {
      // Progress arrives as frames, but a request rejected outright (bad
      // checksum, another flash already running) never gets that far.
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
      // Read here rather than handing the File to Api: the seam takes bytes.
      await Api.flashUpload(await file.arrayBuffer(), file.name);
    } catch (e) {
      store.onFlashEvent({ phase: 'error', line: e.message });
      showError(e.message);
    }
  });
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

// One handler for every frame the backend pushes. Nothing here knows there is
// a socket underneath -- Api.subscribe owns the transport and its reconnection
// across backend restarts -- which is what lets an Electron build swap it for
// IPC without this function changing at all.
function subscribeEvents() {
  Api.subscribe((msg) => {
    if (msg.type === 'tlm') {
      Alpine.store('telemetry').render(msg.data);
    }
    else if (msg.type === 'flash') {
      Alpine.store('firmware').onFlashEvent(msg.data);
    }
    else if (msg.type === 'dfu') {
      Alpine.store('firmware').applyDfu(msg.data);
    }
    else if (msg.type === 'state') {
      const d = msg.data;
      setState(typeof d === 'string' ? d : d.state, typeof d === 'object' ? d : null);
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
  initDfuGrantModal();
  // Home is always the landing page, including on a reload while a device is
  // still connected server-side. Nothing navigates for you any more -- page
  // choice is the user's, and connecting only enables the gated nav items.
  // Not awaited (connect status shouldn't wait on it), but .catch()'d like
  // every other async path in this file -- otherwise a failed fetch of
  // pages/home.html surfaces only as an unhandled rejection.
  showPage('home').catch((e) => showError(`Failed to load the app: ${e.message}`));

  // A modal rather than showError(): that banner clears itself after 5 seconds,
  // which is no way to deliver "nothing on this page will work for you".
  const browser = assessBrowser({
    userAgent: navigator.userAgent,
    hasSerial: Api.isSupported(),
    hasUsb: Api.dfuSupported(),
    isSecureContext: window.isSecureContext,
  });
  if (browser.level !== 'ok') {
    el('browser-title').textContent = browser.title;
    el('browser-message').textContent = browser.message;
    new window.bootstrap.Modal(el('browser-modal')).show();
  }
  if (browser.level === 'block') {
    el('connect').disabled = true;
    el('connect').textContent = 'Web Serial unavailable';
    el('connect').title = browser.message;
    return;
  }

  const st = await Api.status();
  setState(st.state, st);
  if (st.state === 'connected') await loadDevice();
  subscribeEvents();
  startWatchdog();
})();
