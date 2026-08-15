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
