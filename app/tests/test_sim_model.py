from backend import sim_model as m


def test_trunc_div_truncates_toward_zero_like_c():
    # Python's // floors: -30 // 100 == -1. The firmware's C integer division
    # truncates: -30 / 100 == 0. Every ported division must match C.
    assert m.trunc_div(-30, 100) == 0
    assert m.trunc_div(-49771, 100) == -497
    assert m.trunc_div(49771, 100) == 497
    assert m.trunc_div(-3300, 100) == -33


def test_triangle_percent_rises_then_falls():
    assert m.triangle_percent(0, 4000) == 0
    assert m.triangle_percent(2000, 4000) == 100
    assert m.triangle_percent(1000, 4000) == 50
    assert m.triangle_percent(3000, 4000) == 50


def test_deadband_snaps_to_centre_inside_the_band():
    assert m.deadbanded(1510, 1500, 20) == 1500
    assert m.deadbanded(1530, 1500, 20) == 1530
    assert m.deadbanded(1500, 1500, 0) == 1500


def test_mix_forward_drives_both_tracks_equally():
    assert m.mix(1600, 1500, 1500, 1000, 2000, 100, 100, 100, 0) == (1600, 1600)


def test_mix_steer_ratio_scales_the_turn_only():
    assert m.mix(1500, 2000, 1500, 1000, 2000, 100, 100, 50, 0) == (1750, 1250)


def test_mix_reverse_ratio_scales_reverse_only():
    assert m.mix(1000, 1500, 1500, 1000, 2000, 100, 50, 100, 0) == (1250, 1250)


def test_mix_clamps_proportionally_rather_than_saturating():
    # Full forward AND full right: the left track would need 2500us, so both
    # offsets scale by the same factor instead of one saturating alone.
    assert m.mix(2000, 2000, 1500, 1000, 2000, 100, 100, 100, 0) == (2000, 1500)


def test_mix_uses_c_truncation_for_the_ratio_scaling():
    # (1499-1500) * 30 / 100 truncates to 0 in C, so throttle stays at centre.
    # Python's // would floor it to -1 and produce 1499.
    assert m.mix(1499, 1500, 1500, 1000, 2000, 100, 30, 100, 0) == (1500, 1500)


def test_compute_armed_rules():
    assert m.compute_armed(False, True, 0, 1700, 2000) is False
    assert m.compute_armed(True, True, 0, 1700, 2000) is True
    assert m.compute_armed(True, False, 1800, 1700, 2000) is True
    assert m.compute_armed(True, False, 1500, 1700, 2000) is False


def test_neutral_us_depends_on_direction():
    assert m.neutral_us(1000, 2000, True) == 1500
    assert m.neutral_us(1000, 2000, False) == 1000


def test_arm_state_promotes_only_after_the_hold_with_throttle_low():
    s = m.next_arm_state(m.ARM_OFF, False, True, 0, 0, 2000, True)
    assert s == m.ARM_ARMING
    assert m.next_arm_state(s, False, False, 1999, 0, 2000, True) == m.ARM_ARMING
    assert m.next_arm_state(s, False, False, 2000, 0, 2000, True) == m.ARM_ARMED
    assert m.next_arm_state(s, False, False, 5000, 0, 2000, False) == m.ARM_ARMING
    assert m.next_arm_state(m.ARM_ARMED, True, False, 5000, 0, 2000, True) == m.ARM_OFF


def test_pulse_is_neutral_until_armed_then_follows_the_input():
    assert m.next_pulse_us(m.ARM_ARMING, m.MODE_INPUT, 1000, 2000, 1500, 1800,
                           False, 1500) == 1500
    assert m.next_pulse_us(m.ARM_ARMED, m.MODE_INPUT, 1000, 2000, 1500, 1800,
                           False, 1500) == 1800
    assert m.next_pulse_us(m.ARM_ARMED, m.MODE_INPUT, 1000, 2000, 1500, 1800,
                           True, 1500) == 1500
    assert m.next_pulse_us(m.ARM_ARMED, m.MODE_ARMED, 1000, 2000, 1700, 0,
                           False, 1500) == 1700


def test_effective_max_reserves_low_time_inside_the_frame():
    assert m.effective_max_us(2000, m.FRAME_US["50"]) == 2000
    assert m.effective_max_us(2500, m.FRAME_US["400"]) == 2375
