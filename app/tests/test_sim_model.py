import json
from pathlib import Path

from backend import sim_model as m

PROFILE = Path(__file__).resolve().parents[1] / "backend" / "sim_profile.json"
_PROFILE = json.loads(PROFILE.read_text())
PARAMS = _PROFILE["params"]


def make_model():
    return m.SimModel(PARAMS)


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


def test_values_start_at_the_schema_defaults_except_rx_source():
    mod = make_model()
    assert mod.get("esc0.throttle_us") == 1500
    assert mod.get("device.name") == "betacrawler"
    # A simulated board has no UART to receive on, so `sim` is the honest
    # source for it; `uart` would report a link that cannot exist.
    assert mod.get("rx.source") == "sim"


def test_telemetry_covers_every_field_the_profile_advertises():
    tlm = make_model().telemetry(0)
    assert set(tlm) == {t["key"] for t in _PROFILE["tlm"]}


def test_rc_channels_sweep_and_respect_the_protocol_channel_count():
    mod = make_model()
    assert mod.telemetry(0)["ch1"] == 988
    assert mod.telemetry(2000)["ch1"] == 2012
    # elrs carries 16 channels, crossfire 12 -- the padding must stay 0
    # rather than be published as though it were a stick position.
    mod.set("rx.protocol", "crossfire", 0)
    assert mod.telemetry(0)["ch16"] == 0
    assert mod.telemetry(0)["ch12"] != 0


def test_selecting_the_uart_source_drops_the_link_and_the_channels():
    mod = make_model()
    mod.set("rx.source", "uart", 0)
    tlm = mod.telemetry(0)
    assert tlm["link"] == 0
    assert tlm["rate"] == 0
    assert tlm["ch1"] == 0
    assert tlm["drv_l"] == 1500 and tlm["drv_r"] == 1500


def test_drive_outputs_follow_the_mixer_at_a_known_instant():
    # At t=0 the sweep puts both sticks at 988; the proportional clamp then
    # gives left 1009 and right 1500.
    tlm = make_model().telemetry(0)
    assert (tlm["drv_l"], tlm["drv_r"]) == (1009, 1500)


def test_steer_ratio_changes_the_drive_outputs():
    mod = make_model()
    before = mod.telemetry(0)["drv_l"]
    mod.set("tank_drive.steer_ratio", 0, 0)
    assert mod.telemetry(0)["drv_l"] != before


def test_esc_holds_neutral_while_the_arm_switch_is_inactive():
    tlm = make_model().telemetry(0)
    assert tlm["arm0"] == m.ARM_ARMING
    assert tlm["esc0"] == 1500


def test_esc_arms_after_the_hold_once_the_arm_source_allows_it():
    mod = make_model()
    mod.set("tank_drive.arm_src", "none", 0)   # drive bus armed whenever rx is fresh
    mod.set("esc0.mode", "armed", 0)           # throttle_us 1500 == neutral, low
    for t in range(0, 2001, 100):
        tlm = mod.telemetry(t)
    assert tlm["arm0"] == m.ARM_ARMED
    assert tlm["esc0"] == 1500                 # armed, but commanded to neutral


def test_changing_the_esc_rate_demotes_an_armed_esc():
    mod = make_model()
    mod.set("tank_drive.arm_src", "none", 0)
    mod.set("esc0.mode", "armed", 0)
    for t in range(0, 3001, 100):
        mod.telemetry(t)
    assert mod.telemetry(3000)["arm0"] == m.ARM_ARMED
    mod.set("esc0.rate", "400", 3000)
    assert mod.telemetry(3000)["arm0"] == m.ARM_ARMING


def test_save_then_revert_reports_flash_and_restores():
    mod = make_model()
    mod.set("tlm.rate", 25, 0)
    mod.save()
    mod.set("tlm.rate", 40, 0)
    assert mod.revert(0) == "flash"
    assert mod.get("tlm.rate") == 25


def test_revert_with_nothing_saved_falls_back_to_defaults():
    mod = make_model()
    mod.set("tlm.rate", 40, 0)
    assert mod.revert(0) == "defaults"
    assert mod.get("tlm.rate") == 10


def test_load_defaults_resets_every_value():
    mod = make_model()
    mod.set("tlm.rate", 40, 0)
    mod.load_defaults(0)
    assert mod.get("tlm.rate") == 10
