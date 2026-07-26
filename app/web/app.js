'use strict';

// The app (backend + this UI) is versioned independently of the firmware:
// they are separate projects that happen to live in one repo, and their
// numbers are not meant to track each other. app-demo is a template, so this
// stays 1.0.0 -- a fork bumps it. The firmware's own version lives in
// firmware/include/config.h and arrives over the wire in `hello`.
const APP_VERSION = '1.0.0';

const Api = {
  async get(path) {
    const r = await fetch(path);
    if (!r.ok) throw await Api._err(r);
    return r.json();
  },
  async send(method, path, body) {
    const r = await fetch(path, {
      method,
      headers: { 'Content-Type': 'application/json' },
      body: body === undefined ? undefined : JSON.stringify(body),
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
  sendTerminalCommand: (command) => Api.send('POST', '/api/terminal', { command }),
  restoreIni: (ini)      => Api.send('POST', '/api/params/restore', { ini }),
  socket:    ()          => new WebSocket(
    `${location.protocol === 'https:' ? 'wss:' : 'ws:'}//${location.host}/ws`),
};

const el = (id) => document.getElementById(id);
let connected = false;
let stale = false;

function showError(msg) {
  const a = el('alert');
  a.textContent = msg;
  a.classList.remove('d-none');
  setTimeout(() => a.classList.add('d-none'), 5000);
}

function setState(state, info) {
  connected = state === 'connected';
  stale = false;   // any ground-truth state transition supersedes staleness
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
const SLIDER_FIELDS = new Set(['led.blink_hz']);

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
function formatTelemetryValue(def, value) {
  if (typeof value !== 'number') return value;
  const scaled = def.div ? value / def.div : value;
  return scaled.toFixed(def.dec || 0);
}

// The config form and telemetry cards are the two most repetitive,
// DOM-construction-heavy regions of this file -- markup for both now lives in
// index.html as <template x-for> blocks. These two stores are the only
// bridge the surrounding plain JS (loadDevice/openSocket/watchdog) needs to
// push data in or read state back out: Alpine.store() is reachable from
// anywhere with no element handle, unlike Alpine.$data()/refs.
document.addEventListener('alpine:init', () => {
  Alpine.store('config', {
    schema: [],
    values: {},
    invalid: {},

    get groups() {
      return groupItems(this.schema, (p) => ({
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

    get groups() { return groupItems(this.schema); },

    load(tlmSchema) {
      this.schema = tlmSchema;
      this.data = {};
    },

    render(raw) {
      noteTelemetry();
      for (const def of this.schema) {
        if (raw[def.key] !== undefined) {
          this.data[def.key] = formatTelemetryValue(def, raw[def.key]);
        }
      }
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
    setState('connected');
    // The firmware's `defaults` op reloads RAM and re-notifies the modules but
    // never touches flash (core/dispatch.cpp, Op::Defaults), so the device is
    // now exactly as unsaved as after editing a field by hand.
    setDirty(true);
  } catch (e) { showError(e.message); }
});

// --- side-menu navigation ---------------------------------------------------
const PAGES = ['home', 'config', 'telemetry', 'terminal', 'help'];
const CONNECTION_REQUIRED_PAGES = new Set(['config', 'telemetry', 'terminal']);

function showPage(page) {
  for (const p of PAGES) el(`page-${p}`).classList.toggle('d-none', p !== page);
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
  } catch (e) {
    termAppend(`ERROR: ${e.message}`);
  } finally {
    el('term-send').disabled = false;
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

function openSocket() {
  const ws = Api.socket();
  ws.onmessage = (ev) => {
    const msg = JSON.parse(ev.data);
    if (msg.type === 'tlm') Alpine.store('telemetry').render(msg.data);
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
    if (el('term-traffic').checked) {
      termAppend(`<< [${msg.type}] ${JSON.stringify(msg.data)}`);
    }
  };
  ws.onclose = () => setTimeout(openSocket, 1000);   // survive backend restarts
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
  openSocket();
  startWatchdog();
})();
