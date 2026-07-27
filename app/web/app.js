'use strict';

// The app (backend + this UI) is versioned independently of the firmware:
// they are separate projects that happen to live in one repo, and their
// numbers are not meant to track each other. silkscreen is a template, so this
// stays 1.0.0 -- a fork bumps it. The firmware's own version lives in
// firmware/include/config.h and arrives over the wire in `hello`.
const APP_VERSION = '1.0.0';

// The single seam between this UI and whatever is on the other end: every
// request and every pushed frame goes through here, and there is no fetch, no
// WebSocket and no URL anywhere else in this file. That is what makes an
// Electron build a rewrite of this one object rather than of the app -- swap
// these bodies for IPC calls and nothing below changes.
//
// Two rules keep that true, and both have been broken before: nothing outside
// here may construct a request, and nothing in here may take or return a
// browser-only type (a File, a WebSocket) that an IPC transport could not
// produce.
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
  flashBundled: (id)     => Api.send('POST', '/api/firmware/flash', { id }),
  // The image goes up as the raw request body, not multipart: that keeps the
  // backend free of a python-multipart dependency. Takes bytes rather than the
  // File itself even though fetch would accept a File as a body directly --
  // a File cannot cross an IPC boundary, and this would otherwise be the one
  // method whose signature pins the UI to an HTTP transport.
  flashUpload: (bytes, filename) => Api.sendBody(
    `/api/firmware/flash-upload?filename=${encodeURIComponent(filename)}`, bytes),

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
  el('help-fw').textContent = connected && info && info.fw
    ? [info.fw, info.board, info.built && `built ${info.built}`,
       info.mods && info.mods.length && `modules: ${info.mods.join(', ')}`]
      .filter(Boolean).join(' · ')
    : 'not connected';
  el('form').querySelectorAll('input,select').forEach((i) => { i.disabled = !connected; });
  updateNavAvailability();
  updateTerminalAvailability();
  // Optional-chained: setState can run before Alpine has initialised (this
  // script is not deferred, Alpine's is), and the Firmware page re-syncs on
  // entry anyway.
  window.Alpine?.store('firmware')?.syncDevice(connected, deviceInfo);
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

// u8 fields rendered as a range slider instead of a plain number input. Purely
// a presentation choice, so it is the one thing that stays keyed by name here.
// servo.angle earns it more than anything else here: a 0-180 sweep is a
// physical position, and dragging to it beats typing a number. Note the
// commit is on `change`, not `input`, so a drag sends one `set` on release
// rather than a stream of them -- which is also why the firmware carries a
// sweep mode instead of expecting the UI to scrub.
const SLIDER_FIELDS = new Set(['led.blink_hz', 'servo.angle']);

// Groups items that carry a `group` field into [{name, items}], preserving
// the order each group first appears. Shared by the config form and the
// telemetry page, which group identically.
function groupItems(items, decorate = (x) => x) {
  const order = [];
  const byName = new Map();
  for (const item of items) {
    const name = item.group || '';
    if (!byName.has(name)) { byName.set(name, []); order.push(name); }
    byName.get(name).push(decorate(item));
  }
  return order.map((name) => ({ name, items: byName.get(name) }));
}

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
};

function formatTelemetryValue(def, value) {
  if (typeof value !== 'number') return value;
  const fmt = TLM_FORMATTERS[def.fmt];
  if (fmt) return fmt(value);
  const scaled = def.div ? value / def.div : value;
  return scaled.toFixed(def.dec || 0);
}

// The config form and telemetry cards are the two most repetitive,
// DOM-construction-heavy regions of this file -- markup for both now lives in
// index.html as <template x-for> blocks. These two stores are the only
// bridge the surrounding plain JS (loadDevice/subscribeEvents/watchdog) needs to
// push data in or read state back out: Alpine.store() is reachable from
// anywhere with no element handle, unlike Alpine.$data()/refs.
document.addEventListener('alpine:init', () => {
  Alpine.store('config', {
    schema: [],
    values: {},
    invalid: {},

    get groups() {
      // showIf is a display hint (see core/params.h): a param whose condition
      // is unmet is not drawn, but it is still in the schema, still validated
      // by the device, and still settable from the Terminal or an INI
      // restore. Filtering BEFORE groupItems is what makes a group that has
      // been emptied disappear rather than render as a bare heading.
      //
      // Reading this.values[...] inside the getter is what makes Alpine
      // re-run it when the controlling selector changes. Hoisting that lookup
      // out breaks the reactivity while leaving the logic looking correct --
      // the group would simply stop updating.
      const visible = this.schema.filter(
        (p) => !p.showIf || this.values[p.showIf.key] === p.showIf.val);
      return groupItems(visible, (p) => ({
        ...p,
        isSlider: p.type === 'u8' && SLIDER_FIELDS.has(p.key),
        help: p.type === 'u8' ? `${p.min}–${p.max}`
            : p.type === 'str' ? `max ${p.maxlen} chars` : '',
      }));
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
  });

  Alpine.store('telemetry', {
    schema: [],
    data: {},
    // The formatted value in `data` is a STRING -- "1500", "01:23:45". A bar
    // needs the number, so both are kept rather than reparsing text that a
    // formatter may have made unparseable.
    raw: {},

    get groups() { return groupItems(this.schema); },

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
    // the same build timestamp the device reports in `hello`, so two builds
    // of the same version number are still told apart.
    isRunning(img) {
      return this.deviceConnected && !!this.device.built
        && img.built === this.device.built && img.version === this.device.ver;
    },

    get canEnterDfu() {
      return this.deviceConnected && !this.dfuPresent && !this.busy
        && (this.device.caps || []).includes('dfu');
    },

    get canFlash() {
      const img = this.images.find((i) => i.id === this.selected);
      return !!img && img.available && this.dfuPresent && !this.busy;
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
      el('fw-log').value = '';
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
async function refreshPorts() {
  const ports = await Api.ports();
  const sel = el('port');
  sel.innerHTML = '';
  for (const p of ports) {
    const o = document.createElement('option');
    o.value = p.port;
    o.textContent = p.match ? `${p.port} (STM32)` : p.port;
    if (p.match) o.selected = true;
    sel.appendChild(o);
  }
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
  el('dirty').classList.toggle('d-none', !dirty);
  el('term-save').disabled = !dirty;
  // Any subsequent action supersedes the fallback note, so it is cleared here
  // rather than tracked separately. The revert handler re-shows it after
  // calling setDirty().
  el('revert-note').classList.add('d-none');
}

async function saveToFlash() {
  // Flash erase stalls the board ~1s; telemetry will gap. That is expected.
  await Api.save();
  setDirty(false);
}

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
    if (res.src !== 'flash') el('revert-note').classList.remove('d-none');
  } catch (e) { showError(e.message); }
});

// --- side-menu navigation ---------------------------------------------------
const PAGES = ['home', 'config', 'telemetry', 'terminal', 'firmware', 'help'];
// Terminal is deliberately NOT here. It is readable while disconnected so you
// can sit on it and watch the device's boot record arrive when you connect --
// the firmware replays that after `hello`, and being forced to connect first
// and navigate second is exactly the moment you would miss it. Its controls
// are disabled instead of the whole page (updateTerminalAvailability).
//
// Firmware is not here either, for a stronger version of the same reason: a
// board that needs re-flashing is frequently a board that cannot be talked
// to, and gating the recovery tool on a working device would be exactly
// backwards.
const CONNECTION_REQUIRED_PAGES = new Set(['config', 'telemetry']);

function showPage(page) {
  for (const p of PAGES) el(`page-${p}`).classList.toggle('d-none', p !== page);
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

// Only the controls that actually talk to the device. Clear stays live -- it
// edits the local output buffer -- and Save is governed by setDirty(), which a
// disconnect already resets.
function updateTerminalAvailability() {
  el('term-input').disabled = !connected;
  el('term-send').disabled = !connected;
  el('term-restore').disabled = !connected;
  el('term-input').placeholder = connected
    ? 'type a command, e.g. get led.mode'
    : 'connect a device to send commands';
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

// --- terminal ----------------------------------------------------------------
const TERM_MAX_LINES = 500;   // caps growth when "show device traffic" streams tlm at up to 50Hz
const termHistory = [];
let termHistoryIdx = 0;

function termAppend(text) {
  const out = el('term-output');
  const lines = (out.value ? out.value.split('\n') : []).concat(text.split('\n'));
  out.value = lines.slice(-TERM_MAX_LINES).join('\n');
  out.scrollTop = out.scrollHeight;
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
    if (el('term-raw').checked) {
      if (r.raw_sent) termAppend(`  >> ${r.raw_sent}`);
      if (r.raw_recv) termAppend(`  << ${r.raw_recv}`);
    }
    if (r.ok) {
      // Unlike restore/defaults/revert, this command line can be anything --
      // `set rx.protocol elrs` changes which settings group and telemetry
      // fields are live. Without this, the form stays on the old protocol's
      // group and an edit there writes a parameter that has no effect.
      await loadDevice();
    }
  } catch (e) {
    termAppend(`ERROR: ${e.message}`);
  } finally {
    // Not an unconditional re-enable: the command may well have been what
    // revealed the device is gone.
    el('term-send').disabled = !connected;
  }
}

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

el('term-clear').addEventListener('click', () => { el('term-output').value = ''; });

// --- restore from INI --------------------------------------------------------
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
  }
});

// --- firmware page -----------------------------------------------------------
const FW_MAX_LINES = 400;
let dfuPollTimer = null;

function firmwareLog(text) {
  const out = el('fw-log');
  const lines = (out.value ? out.value.split('\n') : []).concat(text);
  out.value = lines.slice(-FW_MAX_LINES).join('\n');
  out.scrollTop = out.scrollHeight;
}

function setDfuPolling(on) {
  const store = window.Alpine?.store('firmware');
  if (on && store && !dfuPollTimer) {
    store.syncDevice(connected, deviceInfo);
    store.refresh();
    store.pollDfu();
    dfuPollTimer = setInterval(() => store.pollDfu(), 1500);
  } else if (!on && dfuPollTimer) {
    clearInterval(dfuPollTimer);
    dfuPollTimer = null;
  }
}

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
    await Api.flashBundled(img.id);
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
    await Api.flashUpload(await file.arrayBuffer(), file.name);
  } catch (e) {
    store.onFlashEvent({ phase: 'error', line: e.message });
    showError(e.message);
  }
});

// One handler for every frame the backend pushes. Nothing here knows there is
// a socket underneath -- Api.subscribe owns the transport and its reconnection
// across backend restarts -- which is what lets an Electron build swap it for
// IPC without this function changing at all.
function subscribeEvents() {
  Api.subscribe((msg) => {
    if (msg.type === 'tlm') Alpine.store('telemetry').render(msg.data);
    else if (msg.type === 'state') {
      const d = msg.data;
      setState(typeof d === 'string' ? d : d.state, typeof d === 'object' ? d : null);
    } else if (msg.type === 'flash') {
      // Its own frame type, not `log`: log frames are the DEVICE talking and
      // render in the Terminal as `[device] …`. This is dfu-util running on
      // the host, and putting it in the Terminal would misattribute it.
      Alpine.store('firmware').onFlashEvent(msg.data);
    } else if (msg.type === 'log') {
      // Device log lines are unprompted, so they are marked to distinguish
      // them from a reply to something the user typed. The firmware replays
      // its boot record after every `hello`, which is what puts boot health
      // here on connect — those lines were produced long before the browser
      // existed, and showing them only under "device traffic" buried them
      // among telemetry frames.
      termAppend(`[device] ${msg.data}`);
    }
    if (el('term-traffic').checked) {
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
    if (!connected) {
      // While disconnected, keep rescanning so a replugged board reappears.
      try { await refreshPorts(); } catch { /* ignore */ }
      return;
    }
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
  el('help-app-version').textContent = APP_VERSION;
  // Home is always the landing page, including on a reload while a device is
  // still connected server-side. Nothing navigates for you any more -- page
  // choice is the user's, and connecting only enables the gated nav items.
  showPage('home');
  await refreshPorts();
  const st = await Api.status();
  setState(st.state, st);
  if (st.state === 'connected') await loadDevice();
  subscribeEvents();
  startWatchdog();
})();
