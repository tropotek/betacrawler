'use strict';

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
  const badge = el('state');
  badge.textContent = state;
  badge.className = 'badge ' + (connected ? 'text-bg-success' : 'text-bg-secondary');
  el('connect').textContent = connected ? 'Disconnect' : 'Connect';
  el('fw').textContent = connected && info && info.fw ? `${info.fw} · proto ${info.proto}` : '';
  el('form').querySelectorAll('input,select').forEach((i) => { i.disabled = !connected; });
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

// --- schema-driven form ----------------------------------------------------
// Display-order preference only -- the firmware's schema order (its source
// of truth for the wire format and flash layout) is untouched. A key not
// listed here just keeps its original schema position, so new params need
// no update here to render correctly.
const FORM_ORDER = ['device.name', 'tlm.rate'];

function orderedForDisplay(schema) {
  return [...schema].sort((a, b) => {
    const ia = FORM_ORDER.indexOf(a.key);
    const ib = FORM_ORDER.indexOf(b.key);
    return (ia === -1 ? FORM_ORDER.length : ia) - (ib === -1 ? FORM_ORDER.length : ib);
  });
}

function buildForm(schema, values) {
  const form = el('form');
  form.innerHTML = '';
  for (const p of orderedForDisplay(schema)) {
    const col = document.createElement('div');
    col.className = 'col-md-6';

    const label = document.createElement('label');
    label.className = 'form-label';
    label.textContent = p.unit ? `${p.label} (${p.unit})` : p.label;
    label.htmlFor = `f-${p.key}`;

    let input;
    if (p.type === 'enum') {
      input = document.createElement('select');
      input.className = 'form-select';
      for (const opt of p.options) {
        const o = document.createElement('option');
        o.value = o.textContent = opt;
        input.appendChild(o);
      }
    } else if (p.type === 'str') {
      input = document.createElement('input');
      input.type = 'text';
      input.className = 'form-control';
      input.maxLength = p.maxlen;
    } else {
      input = document.createElement('input');
      input.type = 'number';
      input.className = 'form-control';
      input.min = p.min;
      input.max = p.max;
    }

    input.id = `f-${p.key}`;
    input.value = values[p.key];
    input.addEventListener('change', () => onFieldChange(p, input));

    const help = document.createElement('div');
    help.className = 'form-text';
    help.id = `h-${p.key}`;
    help.textContent = p.type === 'u8' ? `${p.min}–${p.max}`
                     : p.type === 'str' ? `max ${p.maxlen} chars` : '';

    col.append(label, input, help);
    form.appendChild(col);
  }
}

async function onFieldChange(spec, input) {
  const raw = input.value;
  const val = spec.type === 'u8' ? Number(raw) : raw;
  try {
    await Api.setParam(spec.key, val);
    input.classList.remove('is-invalid');
    el('dirty').classList.remove('d-none');
    if (spec.key === 'tlm.rate') tlmPeriodMs = 1000 / Number(val);
  } catch (e) {
    input.classList.add('is-invalid');
    showError(`${spec.label}: ${e.message}`);
  }
}

// --- telemetry -------------------------------------------------------------
const TLM_FIELDS = {
  up: 'Uptime (ms)', clk: 'Clock (MHz)', temp: 'Temp (°C)',
  vdd: 'VDD (V)', ram: 'Free RAM (B)', btn: 'Button',
};

// The wire protocol sends vdd as integer millivolts by design (see
// docs/api.md) -- only the display converts to Volts, so this never touches
// what's actually sent to/validated against the device.
function formatTelemetryValue(key, value) {
  if (typeof value !== 'number') return value;
  if (key === 'vdd') return (value / 1000).toFixed(2);
  return Math.round(value * 10) / 10;
}

function renderTelemetry(data) {
  noteTelemetry();
  const box = el('tlm');
  if (!box.children.length) {
    for (const [k, label] of Object.entries(TLM_FIELDS)) {
      const col = document.createElement('div');
      col.className = 'col-6 col-md-4';
      col.innerHTML =
        `<div class="card"><div class="card-body py-2">
           <div class="text-secondary small">${label}</div>
           <div class="fs-4" id="t-${k}">–</div>
         </div></div>`;
      box.appendChild(col);
    }
  }
  for (const k of Object.keys(TLM_FIELDS)) {
    if (data[k] !== undefined) {
      el(`t-${k}`).textContent = formatTelemetryValue(k, data[k]);
    }
  }
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
  buildForm(schema, values);
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
    }
  } catch (e) { showError(e.message); }
});

el('save').addEventListener('click', async () => {
  try {
    // Flash erase stalls the board ~1s; telemetry will gap. That is expected.
    await Api.save();
    el('dirty').classList.add('d-none');
  } catch (e) { showError(e.message); }
});

el('defaults').addEventListener('click', async () => {
  try {
    await Api.defaults();
    await loadDevice();
    setState('connected');
  } catch (e) { showError(e.message); }
});

document.querySelectorAll('[data-tab]').forEach((btn) => {
  btn.addEventListener('click', () => {
    document.querySelectorAll('[data-tab]').forEach((b) => b.classList.remove('active'));
    btn.classList.add('active');
    el('tab-config').classList.toggle('d-none', btn.dataset.tab !== 'config');
    el('tab-telemetry').classList.toggle('d-none', btn.dataset.tab !== 'telemetry');
  });
});

function openSocket() {
  const ws = Api.socket();
  ws.onmessage = (ev) => {
    const msg = JSON.parse(ev.data);
    if (msg.type === 'tlm') renderTelemetry(msg.data);
    else if (msg.type === 'state') {
      const d = msg.data;
      setState(typeof d === 'string' ? d : d.state, typeof d === 'object' ? d : null);
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
  await refreshPorts();
  const st = await Api.status();
  setState(st.state, st);
  if (st.state === 'connected') await loadDevice();
  openSocket();
  startWatchdog();
})();
