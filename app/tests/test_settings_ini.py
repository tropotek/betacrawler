"""Tests for the INI codec behind the Terminal's `dump` and Restore buttons.

Pure: no device, no serial port. The schema used here is the real golden
fixture (via tests.test_device) rather than a hand-typed dict, for the same
reason the device tests use it -- a firmware schema change should surface as a
failure here, not drift silently.
"""
import pytest

from backend.settings_ini import dump_ini, parse_ini
from tests.test_device import SCHEMA, VALUES

INFO = {"fw": "betacrawler 1.0.0", "board": "blackpill_f411ce"}


def sections_in_order(text):
    return [line.strip()[1:-1] for line in text.splitlines()
            if line.strip().startswith("[")]


# --- dump ------------------------------------------------------------------

def test_dump_starts_with_identifying_comments():
    text = dump_ini(SCHEMA, VALUES, INFO)
    header = [l for l in text.splitlines() if l.startswith(";")]
    assert header, "dump must be self-identifying once pasted into a file"
    blob = "\n".join(header)
    assert "betacrawler 1.0.0" in blob
    assert "blackpill_f411ce" in blob
    # Every header line is a comment, so the file parses even with them.
    assert text.splitlines()[0].startswith(";")


def test_dump_sections_follow_schema_order():
    """Schema order is the firmware's module registration order -- the same
    rule the config form follows. A dump should read like the UI looks."""
    assert sections_in_order(dump_ini(SCHEMA, VALUES, INFO)) == [
        "device", "tlm", "led"]


def test_dump_includes_every_schema_key_that_has_a_value():
    pairs = dict(parse_ini(dump_ini(SCHEMA, VALUES, INFO)))
    assert set(pairs) == set(VALUES)


def test_dump_omits_keys_with_no_cached_value():
    partial = {"led.mode": "blink"}
    pairs = dict(parse_ini(dump_ini(SCHEMA, partial, INFO)))
    assert pairs == {"led.mode": "blink"}


def test_dump_omits_values_the_schema_does_not_describe():
    """The value cache is the device's, but the schema decides what is a
    setting. A stray key must not end up in a file we later feed back in."""
    values = dict(VALUES, **{"ghost.key": 1})
    pairs = dict(parse_ini(dump_ini(SCHEMA, values, INFO)))
    assert "ghost.key" not in pairs


def test_dump_survives_an_empty_info_block():
    """Nothing about the header should depend on a connected device's
    identity fields being populated."""
    text = dump_ini(SCHEMA, VALUES, {})
    assert dict(parse_ini(text)) == {k: str(v) for k, v in VALUES.items()}


# --- round trip ------------------------------------------------------------

def test_round_trip_preserves_keys_and_values_as_text():
    pairs = parse_ini(dump_ini(SCHEMA, VALUES, INFO))
    assert dict(pairs) == {k: str(v) for k, v in VALUES.items()}


def test_round_trip_preserves_awkward_string_values():
    """`%` would be eaten by configparser's default interpolation, and a
    trailing/leading space by naive splitting. device.name accepts both."""
    values = dict(VALUES, **{"device.name": "50% off  rig"})
    pairs = dict(parse_ini(dump_ini(SCHEMA, values, INFO)))
    assert pairs["device.name"] == "50% off  rig"


def test_dotless_key_round_trips_through_the_general_section():
    schema = [{"key": "legacy", "type": "u8", "min": 0, "max": 9, "def": 1}]
    text = dump_ini(schema, {"legacy": 5}, INFO)
    assert "[general]" in text
    assert parse_ini(text) == [("legacy", "5")]


def test_a_real_general_prefixed_key_beats_the_dotless_fallback():
    """`general.foo` and a dotless `foo` both live under [general]; a key the
    device actually advertises wins."""
    text = "[general]\nfoo = 3\n"
    assert parse_ini(text, known_keys={"general.foo"}) == [("general.foo", "3")]
    assert parse_ini(text, known_keys={"foo"}) == [("foo", "3")]


# --- parse -----------------------------------------------------------------

def test_parse_tolerates_comments_blank_lines_and_colon_separators():
    text = """
; a dump someone hand-edited
# hash comments too

[led]
mode: blink

   blink_hz   =   7
"""
    assert dict(parse_ini(text)) == {"led.mode": "blink", "led.blink_hz": "7"}


def test_parse_preserves_key_case():
    """configparser lowercases option names by default; our keys are
    lowercase today but the device, not configparser, defines them."""
    assert parse_ini("[Wifi]\nSSID = home\n") == [("Wifi.SSID", "home")]


def test_parse_returns_pairs_in_file_order():
    text = "[led]\nblink_hz = 7\nmode = on\n\n[tlm]\nrate = 20\n"
    assert [k for k, _ in parse_ini(text)] == [
        "led.blink_hz", "led.mode", "tlm.rate"]


def test_parse_rejects_garbage():
    with pytest.raises(ValueError):
        parse_ini("this is not an ini file at all\n")


def test_parse_rejects_a_key_outside_any_section():
    with pytest.raises(ValueError):
        parse_ini("mode = blink\n")


def test_parse_rejects_a_duplicated_key():
    """Two values for one setting is ambiguous -- refuse rather than pick."""
    with pytest.raises(ValueError):
        parse_ini("[led]\nmode = on\nmode = off\n")


def test_parse_of_an_empty_document_yields_nothing():
    assert parse_ini("; nothing here\n") == []
