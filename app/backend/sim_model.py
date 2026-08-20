"""Reactive board simulation: what a simulated device reports.

Pure and deterministic -- no threads, no I/O, no wall clock. The math is
ported from the firmware (tank_drive_math.cpp, esc_math.cpp, RxDriver's sim
source) so a config change moves telemetry the way a real board would.
"""

CENTER_US = 1500
DRIVE_MIN_US = 1000
DRIVE_MAX_US = 2000
CH_LO_US = 988
CH_HI_US = 2012
WIRE_CHANNELS = 16
PROTO_CHANNELS = {"crossfire": 12, "elrs": 16}

MODE_OFF, MODE_ARMED, MODE_INPUT = 0, 1, 2
ARM_OFF, ARM_ARMING, ARM_ARMED = 0, 1, 2
ARM_HOLD_MS = 2000
ARM_LOW_MARGIN_US = 50
MIN_LOW_US = 125
FRAME_US = {"50": 20000, "100": 10000, "200": 5000, "400": 2500}

# esc<N>.src option indices 0..11 are ch1..ch12; 12 and 13 are the tank drive
# bus's left/right slots. Slot 2 of that bus is the shared ARM switch.
DRIVE_SRC_BASE = 12
DRIVE_ARM_SLOT = 2


def trunc_div(a: int, b: int) -> int:
    """C integer division: truncates toward zero, where Python's // floors."""
    q = abs(a) // abs(b)
    return -q if (a < 0) != (b < 0) else q


# Mirrors the firmware's vbat module. Cell detection divides by just above a
# full cell, so every charged pack resolves; a part-drained one reads low.
VBAT_SIM_LOW_MV = 13200
VBAT_SIM_HIGH_MV = 16800
VBAT_SIM_PERIOD_MS = 60000
VBAT_MIN_VALID_MV = 5000
VBAT_CELL_DETECT_MV = 4300


def detect_cells(pack_mv: int) -> int:
    if pack_mv < VBAT_MIN_VALID_MV:
        return 0
    return -(-pack_mv // VBAT_CELL_DETECT_MV)


def triangle_percent(phase_ms: int, period_ms: int) -> int:
    half = period_ms // 2
    if half == 0:
        return 0
    t = phase_ms % period_ms
    if t < half:
        return (t * 100) // half
    return 100 - ((t - half) * 100) // (period_ms - half)


def deadbanded(us: int, center_us: int, deadband_us: int) -> int:
    return center_us if abs(us - center_us) <= deadband_us else us


def _scale_num_for_offset(offset: int, min_offset: int, max_offset: int) -> int:
    if offset > max_offset and offset != 0:
        return trunc_div(max_offset * 100, offset)
    if offset < min_offset and offset != 0:
        return trunc_div(min_offset * 100, offset)
    return 100


def mix(throttle_us, steer_us, center_us, min_us, max_us,
        fwd_pct, rev_pct, steer_pct, deadband_us) -> tuple[int, int]:
    """Differential mix with a proportional clamp: when one track would exceed
    its range, both offsets scale by the same factor rather than saturating."""
    throttle = deadbanded(throttle_us, center_us, deadband_us)
    steer = deadbanded(steer_us, center_us, deadband_us)

    if throttle > center_us:
        throttle = center_us + trunc_div((throttle - center_us) * fwd_pct, 100)
    elif throttle < center_us:
        throttle = center_us + trunc_div((throttle - center_us) * rev_pct, 100)

    steer_offset = trunc_div((steer - center_us) * steer_pct, 100)
    left_offset = (throttle - center_us) + steer_offset
    right_offset = (throttle - center_us) - steer_offset

    max_offset = max_us - center_us
    min_offset = min_us - center_us

    scale = min(_scale_num_for_offset(left_offset, min_offset, max_offset),
                _scale_num_for_offset(right_offset, min_offset, max_offset))
    scale = max(0, min(100, scale))

    left = center_us + trunc_div(left_offset * scale, 100)
    right = center_us + trunc_div(right_offset * scale, 100)
    return (max(min_us, min(max_us, left)), max(min_us, min(max_us, right)))


def compute_armed(rx_fresh, arm_src_is_none, arm_src_us, arm_min_us, arm_max_us) -> bool:
    if not rx_fresh:
        return False
    if arm_src_is_none:
        return True
    return arm_min_us <= arm_src_us <= arm_max_us


def neutral_us(min_us: int, max_us: int, bidirectional: bool) -> int:
    return (min_us + max_us) // 2 if bidirectional else min_us


def is_commanded_low(mode, throttle_us, input_us, input_fresh, neutral,
                     low_margin_us, bidirectional) -> bool:
    if mode == MODE_ARMED:
        v = throttle_us
    elif mode == MODE_INPUT:
        if not input_fresh or input_us <= 0:
            return False
        v = input_us
    else:
        return False
    if bidirectional:
        return abs(v - neutral) <= low_margin_us
    return v <= neutral + low_margin_us


def next_arm_state(prev_state, mode_is_off, entering_from_off, now_ms,
                   arm_t0_ms, arm_hold_ms, commanded_low) -> int:
    if mode_is_off:
        return ARM_OFF
    if entering_from_off:
        return ARM_ARMING
    if (prev_state == ARM_ARMING and commanded_low
            and (now_ms - arm_t0_ms) >= arm_hold_ms):
        return ARM_ARMED
    return prev_state


def next_pulse_us(arm_state, mode, min_us, max_us, throttle_us, input_us,
                  input_stale, neutral) -> int:
    if arm_state != ARM_ARMED:
        return neutral
    if mode == MODE_ARMED:
        return max(min_us, min(max_us, throttle_us))
    if mode == MODE_INPUT:
        if input_stale:
            return neutral
        if input_us <= 0:
            return 0
        return max(min_us, min(max_us, input_us))
    return 0


def effective_max_us(max_us: int, frame_us: int) -> int:
    """The largest pulse that still leaves MIN_LOW_US of low time in a frame."""
    if frame_us <= MIN_LOW_US:
        return 0
    room = frame_us - MIN_LOW_US
    return room if max_us > room else max_us


# A simulated board has no UART to receive on, so `sim` is the only honest
# source for it -- `uart` would report a link that cannot exist. Selecting
# uart still correctly drops the link and zeroes the channels.
_BOOT_OVERRIDES = {"rx.source": "sim"}

_SIM_RF_MODE = 2
_SIM_TX_POWER_MW = 100
_SIM_FRAME_RATE_HZ = 143
_CROSSFIRE_RF_HZ = [4, 50, 150]
_ELRS_RF_HZ = [0, 0, 50, 0, 100, 150, 0, 250, 333, 500, 250, 500, 500, 1000]


class _Esc:
    """One ESC channel: arm state machine and last written pulse.

    update() detects mode/src/rate changes by comparing against the previous
    tick, so it carries the firmware's apply() and tick() behaviour in one
    call whether it runs from a parameter change or a plain tick.
    """

    def __init__(self, prefix: str):
        self.prefix = prefix
        self.arm_state = ARM_OFF
        self.arm_t0 = 0
        self.last_us = 0
        self._prev_mode = MODE_OFF
        self._prev_src = None
        self._prev_rate = None

    def update(self, now_ms, p, inputs, drive, rx_fresh, drive_ever_fresh):
        mode = p.enum_index(f"{self.prefix}.mode")
        throttle_us = p.num(f"{self.prefix}.throttle_us")
        min_us = p.num(f"{self.prefix}.min_us")
        max_us = p.num(f"{self.prefix}.max_us")
        bidirectional = p.text(f"{self.prefix}.direction") == "bidirectional"
        src_idx = p.enum_index(f"{self.prefix}.src")
        rate = p.text(f"{self.prefix}.rate")

        entering_from_off = self._prev_mode == MODE_OFF and mode != MODE_OFF
        src_changed = self._prev_src is not None and src_idx != self._prev_src
        rate_changed = self._prev_rate is not None and rate != self._prev_rate
        self._prev_mode, self._prev_src, self._prev_rate = mode, src_idx, rate

        neutral = neutral_us(min_us, max_us, bidirectional)
        if src_idx >= DRIVE_SRC_BASE:
            raw_input = drive[src_idx - DRIVE_SRC_BASE]
        else:
            raw_input = inputs[src_idx]
        input_fresh = mode == MODE_INPUT and rx_fresh
        input_stale = mode == MODE_INPUT and not input_fresh
        input_us = raw_input if mode == MODE_INPUT else 0

        armed_now = self.arm_state == ARM_ARMED
        if ((armed_now and mode == MODE_INPUT and not input_fresh)
                or (armed_now and mode == MODE_INPUT and src_changed)
                or (armed_now and rate_changed)):
            self.arm_state = ARM_ARMING
            self.arm_t0 = now_ms
        if entering_from_off:
            self.arm_t0 = now_ms

        commanded_low = is_commanded_low(mode, throttle_us, input_us, input_fresh,
                                         neutral, ARM_LOW_MARGIN_US, bidirectional)
        if self.arm_state == ARM_ARMING and not commanded_low:
            self.arm_t0 = now_ms
        self.arm_state = next_arm_state(self.arm_state, mode == MODE_OFF,
                                        entering_from_off, now_ms, self.arm_t0,
                                        ARM_HOLD_MS, commanded_low)
        if mode == MODE_OFF:
            return

        us = next_pulse_us(self.arm_state, mode, min_us, max_us, throttle_us,
                           input_us, input_stale, neutral)
        # The shared ARM switch is a pure output gate outside the hold state
        # machine: inactive forces neutral instantly, whatever the ESC's own
        # state says.
        if drive_ever_fresh and drive[DRIVE_ARM_SLOT] == 0:
            us = neutral
        eff_max = effective_max_us(max_us, FRAME_US[rate])
        if us > eff_max:
            us = eff_max
        if us > 0:
            self.last_us = us


class SimModel:
    """Parameter store plus the telemetry a simulated board would publish."""

    def __init__(self, params: list[dict]):
        self._specs = {p["key"]: p for p in params}
        self._defaults = {p["key"]: p["def"] for p in params}
        self._values = dict(self._defaults)
        self._values.update(_BOOT_OVERRIDES)
        self._stored: dict | None = None
        self._esc = {"esc0": _Esc("esc0"), "esc1": _Esc("esc1")}
        self._drive_ever_fresh = False
        self._vbat_cells = 0
        self._tlm: dict = {}
        self._tick(0)

    # --- parameter access ---------------------------------------------------
    def spec(self, key: str) -> dict | None:
        return self._specs.get(key)

    def values(self) -> dict:
        return dict(self._values)

    def get(self, key: str):
        return self._values[key]

    def num(self, key: str) -> int:
        return int(self._values[key])

    def text(self, key: str) -> str:
        return str(self._values[key])

    def enum_index(self, key: str) -> int:
        return self._specs[key]["options"].index(self._values[key])

    def set(self, key: str, val, now_ms: int) -> None:
        self._values[key] = val
        self._tick(now_ms)

    def load_defaults(self, now_ms: int) -> None:
        self._values = dict(self._defaults)
        self._values.update(_BOOT_OVERRIDES)
        self._tick(now_ms)

    def save(self) -> None:
        self._stored = dict(self._values)

    def revert(self, now_ms: int) -> str:
        if self._stored is None:
            self.load_defaults(now_ms)
            return "defaults"
        self._values = dict(self._stored)
        self._tick(now_ms)
        return "flash"

    # --- telemetry ----------------------------------------------------------
    def telemetry(self, now_ms: int) -> dict:
        self._tick(now_ms)
        return dict(self._tlm)

    def _tick(self, now_ms: int) -> None:
        rx_fresh = self._values["rx.source"] == "sim"
        channels = self._channels(now_ms, rx_fresh)
        inputs = [deadbanded(v, CENTER_US, self.num("rx.deadband_us"))
                  for v in channels]
        left, right, armed = self._tank(inputs, rx_fresh)
        drive = [left, right, 1 if armed else 0]
        if rx_fresh:
            self._drive_ever_fresh = True
        for esc in self._esc.values():
            esc.update(now_ms, self, inputs, drive, rx_fresh, self._drive_ever_fresh)

        tlm = {f"ch{i + 1}": channels[i] for i in range(WIRE_CHANNELS)}
        tlm.update(self._link(rx_fresh))
        tlm.update(self._system(now_ms))
        tlm.update(self._vbat(now_ms))
        tlm["drv_l"], tlm["drv_r"] = left, right
        tlm["esc0"] = self._esc["esc0"].last_us
        tlm["arm0"] = self._esc["esc0"].arm_state
        tlm["esc1"] = self._esc["esc1"].last_us
        tlm["arm1"] = self._esc["esc1"].arm_state
        self._tlm = tlm

    def _channels(self, now_ms: int, rx_fresh: bool) -> list[int]:
        us = [0] * WIRE_CHANNELS
        if not rx_fresh:
            return us
        n = PROTO_CHANNELS[self.text("rx.protocol")]
        span = CH_HI_US - CH_LO_US
        us[0] = CH_LO_US + triangle_percent(now_ms % 4000, 4000) * span // 100
        us[1] = CH_LO_US + triangle_percent(now_ms % 8000, 8000) * span // 100
        us[2] = CH_HI_US if (now_ms // 2000) % 2 else CH_LO_US
        for i in range(3, n):
            us[i] = CH_LO_US + span * (i - 2) // (n - 2)
        return us

    def _tank(self, inputs: list[int], rx_fresh: bool) -> tuple[int, int, bool]:
        if rx_fresh:
            left, right = mix(
                inputs[self.enum_index("tank_drive.throttle_src")],
                inputs[self.enum_index("tank_drive.steer_src")],
                CENTER_US, DRIVE_MIN_US, DRIVE_MAX_US,
                self.num("tank_drive.forward_ratio"),
                self.num("tank_drive.reverse_ratio"),
                self.num("tank_drive.steer_ratio"), 0)
        else:
            left = right = CENTER_US
        arm_src = self.text("tank_drive.arm_src")
        is_none = arm_src == "none"
        arm_us = 0 if is_none else inputs[int(arm_src[2:]) - 1]
        armed = compute_armed(rx_fresh, is_none, arm_us,
                              self.num("tank_drive.arm_min"),
                              self.num("tank_drive.arm_max"))
        return left, right, armed

    def _link(self, rx_fresh: bool) -> dict:
        if not rx_fresh:
            return {"link": 0, "lq": 0, "rssi": 0, "rate": 0, "err": 0,
                    "rfrate": 0, "pwr": 0}
        table = (_CROSSFIRE_RF_HZ if self.text("rx.protocol") == "crossfire"
                 else _ELRS_RF_HZ)
        return {"link": 1, "lq": 100, "rssi": -42, "rate": _SIM_FRAME_RATE_HZ,
                "err": 0, "rfrate": table[_SIM_RF_MODE], "pwr": _SIM_TX_POWER_MW}

    def _vbat(self, now_ms: int) -> dict:
        """Mirrors the firmware's vbat module: off publishes nothing, sim
        sweeps a synthetic pack, and the cell count latches once."""
        source = self.text("vbat.source")
        if source == "off":
            return {"vbat": 0, "cells": 0}
        if source == "sim":
            span = VBAT_SIM_HIGH_MV - VBAT_SIM_LOW_MV
            mv = VBAT_SIM_LOW_MV + triangle_percent(
                now_ms % VBAT_SIM_PERIOD_MS, VBAT_SIM_PERIOD_MS) * span // 100
        else:
            # No divider exists in the simulator, so adc reads nothing.
            mv = 0
        sel = self.text("vbat.cells")
        if sel != "auto":
            self._vbat_cells = int(sel)
        elif self._vbat_cells == 0:
            self._vbat_cells = detect_cells(mv)
        return {"vbat": mv, "cells": self._vbat_cells}

    def _system(self, now_ms: int) -> dict:
        # Deterministic triangles rather than a random walk: the whole model
        # stays reproducible and unit-testable.
        slow = triangle_percent(now_ms % 30000, 30000)
        fast = triangle_percent(now_ms % 6000, 6000)
        return {"up": now_ms,
                "clk": 100,
                "ram": 61440 + slow * 1024 // 100,
                "temp": 32.0 + slow * 4.0 / 100,
                "vdd": 3290 + slow * 20 // 100,
                "fault": 0,
                "loop": 8000 + fast * 400 // 100,
                "loopworst": 320 + fast * 60 // 100}
