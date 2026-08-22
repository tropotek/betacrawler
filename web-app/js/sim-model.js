// Reactive device simulation ported from firmware math (tank_drive_math.cpp,
// esc_math.cpp, RxDriver's sim source). Pure and deterministic: no timers, no I/O.

const CENTER_US = 1500;
const DRIVE_MIN_US = 1000;
const DRIVE_MAX_US = 2000;
const CH_LO_US = 988;
const CH_HI_US = 2012;
const WIRE_CHANNELS = 16;
const PROTO_CHANNELS = { crossfire: 12, elrs: 16 };

export const MODE_OFF = 0, MODE_ARMED = 1, MODE_INPUT = 2;
export const ARM_OFF = 0, ARM_ARMING = 1, ARM_ARMED = 2;
const ARM_HOLD_MS = 2000;
const ARM_LOW_MARGIN_US = 50;
const MIN_LOW_US = 125;
export const FRAME_US = { 50: 20000, 100: 10000, 200: 5000, 400: 2500 };

// esc<N>.src option indices 0..11 are ch1..ch12; 12 and 13 are the tank drive
// bus's left/right slots. Slot 2 of that bus is the shared ARM switch.
const DRIVE_SRC_BASE = 12;
const DRIVE_ARM_SLOT = 2;

// C integer division: truncates toward zero, where JS's Math.floor floors.
export function truncDiv(a, b) {
  const q = Math.floor(Math.abs(a) / Math.abs(b));
  if (q === 0) return 0;
  return (a < 0) !== (b < 0) ? -q : q;
}

const VBAT_SIM_LOW_MV = 13200;
const VBAT_SIM_HIGH_MV = 16800;
const VBAT_SIM_PERIOD_MS = 60000;
const VBAT_MIN_VALID_MV = 6000;
const VBAT_CELL_DETECT_MV = 4300;
const VBAT_CELL_EMPTY_MV = 3300;
const VBAT_CELL_FULL_MV = 4200;

function detectCells(packMv) {
  if (packMv < VBAT_MIN_VALID_MV) return 0;
  return -Math.floor(-packMv / VBAT_CELL_DETECT_MV);
}

function remainingPct(packMv, cells) {
  if (cells === 0) return 0;
  const perCell = truncDiv(packMv, cells);
  if (perCell <= VBAT_CELL_EMPTY_MV) return 0;
  if (perCell >= VBAT_CELL_FULL_MV) return 100;
  return truncDiv((perCell - VBAT_CELL_EMPTY_MV) * 100, VBAT_CELL_FULL_MV - VBAT_CELL_EMPTY_MV);
}

export function trianglePercent(phaseMs, periodMs) {
  const half = Math.floor(periodMs / 2);
  if (half === 0) return 0;
  const t = phaseMs % periodMs;
  if (t < half) return Math.floor((t * 100) / half);
  return 100 - Math.floor(((t - half) * 100) / (periodMs - half));
}

export function deadbanded(us, centerUs, deadbandUs) {
  return Math.abs(us - centerUs) <= deadbandUs ? centerUs : us;
}

function scaleNumForOffset(offset, minOffset, maxOffset) {
  if (offset > maxOffset && offset !== 0) return truncDiv(maxOffset * 100, offset);
  if (offset < minOffset && offset !== 0) return truncDiv(minOffset * 100, offset);
  return 100;
}

// Differential mix with a proportional clamp: when one track would exceed its
// range, both offsets scale by the same factor rather than saturating.
export function mix(throttleUs, steerUs, centerUs, minUs, maxUs, fwdPct, revPct, steerPct, deadbandUs) {
  let throttle = deadbanded(throttleUs, centerUs, deadbandUs);
  const steer = deadbanded(steerUs, centerUs, deadbandUs);

  if (throttle > centerUs) {
    throttle = centerUs + truncDiv((throttle - centerUs) * fwdPct, 100);
  } else if (throttle < centerUs) {
    throttle = centerUs + truncDiv((throttle - centerUs) * revPct, 100);
  }

  const steerOffset = truncDiv((steer - centerUs) * steerPct, 100);
  const leftOffset = (throttle - centerUs) + steerOffset;
  const rightOffset = (throttle - centerUs) - steerOffset;

  const maxOffset = maxUs - centerUs;
  const minOffset = minUs - centerUs;

  let scale = Math.min(
    scaleNumForOffset(leftOffset, minOffset, maxOffset),
    scaleNumForOffset(rightOffset, minOffset, maxOffset),
  );
  scale = Math.max(0, Math.min(100, scale));

  const left = centerUs + truncDiv(leftOffset * scale, 100);
  const right = centerUs + truncDiv(rightOffset * scale, 100);
  return [Math.max(minUs, Math.min(maxUs, left)), Math.max(minUs, Math.min(maxUs, right))];
}

export function computeArmed(rxFresh, armSrcIsNone, armSrcUs, armMinUs, armMaxUs) {
  if (!rxFresh) return false;
  if (armSrcIsNone) return true;
  return armMinUs <= armSrcUs && armSrcUs <= armMaxUs;
}

export function neutralUs(minUs, maxUs, bidirectional) {
  return bidirectional ? Math.floor((minUs + maxUs) / 2) : minUs;
}

function isCommandedLow(mode, throttleUs, inputUs, inputFresh, neutral, lowMarginUs, bidirectional) {
  let v;
  if (mode === MODE_ARMED) {
    v = throttleUs;
  } else if (mode === MODE_INPUT) {
    if (!inputFresh || inputUs <= 0) return false;
    v = inputUs;
  } else {
    return false;
  }
  if (bidirectional) return Math.abs(v - neutral) <= lowMarginUs;
  return v <= neutral + lowMarginUs;
}

export function nextArmState(prevState, modeIsOff, enteringFromOff, nowMs, armT0Ms, armHoldMs, commandedLow) {
  if (modeIsOff) return ARM_OFF;
  if (enteringFromOff) return ARM_ARMING;
  if (prevState === ARM_ARMING && commandedLow && (nowMs - armT0Ms) >= armHoldMs) return ARM_ARMED;
  return prevState;
}

export function nextPulseUs(armState, mode, minUs, maxUs, throttleUs, inputUs, inputStale, neutral) {
  if (armState !== ARM_ARMED) return neutral;
  if (mode === MODE_ARMED) return Math.max(minUs, Math.min(maxUs, throttleUs));
  if (mode === MODE_INPUT) {
    if (inputStale) return neutral;
    if (inputUs <= 0) return 0;
    return Math.max(minUs, Math.min(maxUs, inputUs));
  }
  return 0;
}

// The largest pulse that still leaves MIN_LOW_US of low time in a frame.
export function effectiveMaxUs(maxUs, frameUs) {
  if (frameUs <= MIN_LOW_US) return 0;
  const room = frameUs - MIN_LOW_US;
  return maxUs > room ? room : maxUs;
}

// A simulated board has no UART to receive on, so `sim` is the only honest
// source for it -- `uart` would report a link that cannot exist.
const BOOT_OVERRIDES = { 'rx.source': 'sim' };

const SIM_RF_MODE = 2;
const SIM_TX_POWER_MW = 100;
const SIM_FRAME_RATE_HZ = 143;
const CROSSFIRE_RF_HZ = [4, 50, 150];
const ELRS_RF_HZ = [0, 0, 50, 0, 100, 150, 0, 250, 333, 500, 250, 500, 500, 1000];

// One ESC channel: arm state machine and last written pulse. update() detects
// mode/src/rate changes by comparing against the previous tick, so it carries
// the firmware's apply() and tick() behaviour in one call.
class Esc {
  constructor(prefix) {
    this.prefix = prefix;
    this.armState = ARM_OFF;
    this.armT0 = 0;
    this.lastUs = 0;
    this._prevMode = MODE_OFF;
    this._prevSrc = null;
    this._prevRate = null;
  }

  update(nowMs, p, inputs, drive, rxFresh, driveEverFresh) {
    const mode = p.enumIndex(`${this.prefix}.mode`);
    const throttleUs = p.num(`${this.prefix}.throttle_us`);
    const minUs = p.num(`${this.prefix}.min_us`);
    const maxUs = p.num(`${this.prefix}.max_us`);
    const bidirectional = p.text(`${this.prefix}.direction`) === 'bidirectional';
    const srcIdx = p.enumIndex(`${this.prefix}.src`);
    const rate = p.text(`${this.prefix}.rate`);

    const enteringFromOff = this._prevMode === MODE_OFF && mode !== MODE_OFF;
    const srcChanged = this._prevSrc !== null && srcIdx !== this._prevSrc;
    const rateChanged = this._prevRate !== null && rate !== this._prevRate;
    this._prevMode = mode; this._prevSrc = srcIdx; this._prevRate = rate;

    const neutral = neutralUs(minUs, maxUs, bidirectional);
    const rawInput = srcIdx >= DRIVE_SRC_BASE ? drive[srcIdx - DRIVE_SRC_BASE] : inputs[srcIdx];
    const inputFresh = mode === MODE_INPUT && rxFresh;
    const inputStale = mode === MODE_INPUT && !inputFresh;
    const inputUs = mode === MODE_INPUT ? rawInput : 0;

    const armedNow = this.armState === ARM_ARMED;
    if ((armedNow && mode === MODE_INPUT && !inputFresh)
        || (armedNow && mode === MODE_INPUT && srcChanged)
        || (armedNow && rateChanged)) {
      this.armState = ARM_ARMING;
      this.armT0 = nowMs;
    }
    if (enteringFromOff) this.armT0 = nowMs;

    const commandedLow = isCommandedLow(
      mode, throttleUs, inputUs, inputFresh, neutral, ARM_LOW_MARGIN_US, bidirectional);
    if (this.armState === ARM_ARMING && !commandedLow) this.armT0 = nowMs;
    this.armState = nextArmState(
      this.armState, mode === MODE_OFF, enteringFromOff, nowMs, this.armT0, ARM_HOLD_MS, commandedLow);
    if (mode === MODE_OFF) return;

    let us = nextPulseUs(this.armState, mode, minUs, maxUs, throttleUs, inputUs, inputStale, neutral);
    // The shared ARM switch is a pure output gate outside the hold state
    // machine: inactive forces neutral instantly, whatever the ESC's own
    // state says.
    if (driveEverFresh && drive[DRIVE_ARM_SLOT] === 0) us = neutral;
    const effMax = effectiveMaxUs(maxUs, FRAME_US[rate]);
    if (us > effMax) us = effMax;
    if (us > 0) this.lastUs = us;
  }
}

// Parameter store plus the telemetry a simulated board would publish.
export class SimModel {
  constructor(params) {
    this._specs = new Map(params.map((p) => [p.key, p]));
    this._defaults = Object.fromEntries(params.map((p) => [p.key, p.def]));
    this._values = { ...this._defaults, ...BOOT_OVERRIDES };
    this._stored = null;
    this._esc = { esc0: new Esc('esc0'), esc1: new Esc('esc1') };
    this._driveEverFresh = false;
    this._vbatCells = 0;
    this._tlm = {};
    this._tick(0);
  }

  spec(key) { return this._specs.get(key) ?? null; }
  values() { return { ...this._values }; }
  get(key) { return this._values[key]; }
  num(key) { return Number(this._values[key]); }
  text(key) { return String(this._values[key]); }
  enumIndex(key) { return this._specs.get(key).options.indexOf(this._values[key]); }

  set(key, val, nowMs) {
    this._values[key] = val;
    this._tick(nowMs);
  }

  loadDefaults(nowMs) {
    this._values = { ...this._defaults, ...BOOT_OVERRIDES };
    this._tick(nowMs);
  }

  save() { this._stored = { ...this._values }; }

  revert(nowMs) {
    if (this._stored === null) {
      this.loadDefaults(nowMs);
      return 'defaults';
    }
    this._values = { ...this._stored };
    this._tick(nowMs);
    return 'flash';
  }

  telemetry(nowMs) {
    this._tick(nowMs);
    return { ...this._tlm };
  }

  _tick(nowMs) {
    const rxFresh = this._values['rx.source'] === 'sim';
    const channels = this._channels(nowMs, rxFresh);
    const inputs = channels.map((v) => deadbanded(v, CENTER_US, this.num('rx.deadband_us')));
    const [left, right, armed] = this._tank(inputs, rxFresh);
    const drive = [left, right, armed ? 1 : 0];
    if (rxFresh) this._driveEverFresh = true;
    for (const esc of Object.values(this._esc)) {
      esc.update(nowMs, this, inputs, drive, rxFresh, this._driveEverFresh);
    }

    const tlm = {};
    for (let i = 0; i < WIRE_CHANNELS; i += 1) tlm[`ch${i + 1}`] = channels[i];
    Object.assign(tlm, this._link(rxFresh));
    Object.assign(tlm, this._system(nowMs));
    Object.assign(tlm, this._vbat(nowMs));
    tlm.drv_l = left; tlm.drv_r = right;
    tlm.esc0 = this._esc.esc0.lastUs; tlm.arm0 = this._esc.esc0.armState;
    tlm.esc1 = this._esc.esc1.lastUs; tlm.arm1 = this._esc.esc1.armState;
    this._tlm = tlm;
  }

  _channels(nowMs, rxFresh) {
    const us = new Array(WIRE_CHANNELS).fill(0);
    if (!rxFresh) return us;
    const n = PROTO_CHANNELS[this.text('rx.protocol')];
    const span = CH_HI_US - CH_LO_US;
    us[0] = CH_LO_US + Math.floor((trianglePercent(nowMs % 4000, 4000) * span) / 100);
    us[1] = CH_LO_US + Math.floor((trianglePercent(nowMs % 8000, 8000) * span) / 100);
    us[2] = Math.floor(nowMs / 2000) % 2 ? CH_HI_US : CH_LO_US;
    for (let i = 3; i < n; i += 1) us[i] = CH_LO_US + Math.floor((span * (i - 2)) / (n - 2));
    return us;
  }

  _tank(inputs, rxFresh) {
    let left, right;
    if (rxFresh) {
      [left, right] = mix(
        inputs[this.enumIndex('tank_drive.throttle_src')],
        inputs[this.enumIndex('tank_drive.steer_src')],
        CENTER_US, DRIVE_MIN_US, DRIVE_MAX_US,
        this.num('tank_drive.forward_ratio'),
        this.num('tank_drive.reverse_ratio'),
        this.num('tank_drive.steer_ratio'), 0,
      );
    } else {
      left = right = CENTER_US;
    }
    const armSrc = this.text('tank_drive.arm_src');
    const isNone = armSrc === 'none';
    const armUs = isNone ? 0 : inputs[Number(armSrc.slice(2)) - 1];
    const armed = computeArmed(rxFresh, isNone, armUs,
      this.num('tank_drive.arm_min'), this.num('tank_drive.arm_max'));
    return [left, right, armed];
  }

  _link(rxFresh) {
    if (!rxFresh) return { link: 0, lq: 0, rssi: 0, rate: 0, err: 0, rfrate: 0, pwr: 0 };
    const table = this.text('rx.protocol') === 'crossfire' ? CROSSFIRE_RF_HZ : ELRS_RF_HZ;
    return {
      link: 1, lq: 100, rssi: -42, rate: SIM_FRAME_RATE_HZ,
      err: 0, rfrate: table[SIM_RF_MODE], pwr: SIM_TX_POWER_MW,
    };
  }

  // Mirrors the firmware's vbat module: off publishes nothing, sim sweeps a
  // synthetic pack, and the cell count latches once.
  _vbat(nowMs) {
    const source = this.text('vbat.source');
    if (source === 'off') return { vbat: 0, cells: 0, pct: 0 };
    const span = VBAT_SIM_HIGH_MV - VBAT_SIM_LOW_MV;
    const mv = VBAT_SIM_LOW_MV
      + Math.floor((trianglePercent(nowMs % VBAT_SIM_PERIOD_MS, VBAT_SIM_PERIOD_MS) * span) / 100);
    const sel = this.text('vbat.cells');
    if (sel !== 'auto') {
      this._vbatCells = Number(sel);
    } else if (mv < VBAT_MIN_VALID_MV) {
      this._vbatCells = 0;
    } else if (this._vbatCells === 0) {
      this._vbatCells = detectCells(mv);
    }
    return { vbat: mv, cells: this._vbatCells, pct: remainingPct(mv, this._vbatCells) };
  }

  _system(nowMs) {
    const slow = trianglePercent(nowMs % 30000, 30000);
    const fast = trianglePercent(nowMs % 6000, 6000);
    return {
      up: nowMs,
      clk: 100,
      ram: 61440 + Math.floor((slow * 1024) / 100),
      temp: 32.0 + (slow * 4.0) / 100,
      vdd: 3290 + Math.floor((slow * 20) / 100),
      fault: 0,
      loop: 8000 + Math.floor((fast * 400) / 100),
      loopworst: 320 + Math.floor((fast * 60) / 100),
    };
  }
}
