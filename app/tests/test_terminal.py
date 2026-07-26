import pytest
from fastapi.testclient import TestClient

from backend import terminal
from backend.device import DeviceModel
from backend.link import SerialLink
from backend.main import create_app
from backend.settings_ini import parse_ini
from tests.fake_serial import FakeSerial
from tests.test_device import SCHEMA, VALUES, device_responder, make_device


@pytest.fixture
def connected_device():
    dev, fake = make_device()
    dev.connect("/dev/fake")
    yield dev, fake
    dev.disconnect()


# --- help --------------------------------------------------------------

def test_help_lists_commands_and_touches_no_wire_traffic(connected_device):
    dev, fake = connected_device
    before = len(fake.written)
    result = terminal.run(dev, "help")
    assert result.ok
    for cmd in ("get", "set", "save", "defaults", "revert", "dump", "list", "help"):
        assert cmd in result.friendly
    assert result.raw_sent == ""
    assert result.raw_recv == ""
    assert len(fake.written) == before


def test_help_with_args_is_a_usage_error(connected_device):
    dev, _ = connected_device
    result = terminal.run(dev, "help now")
    assert not result.ok


# --- get -----------------------------------------------------------------

def test_get_returns_current_value_with_raw_wire_lines(connected_device):
    dev, _ = connected_device
    result = terminal.run(dev, "get led.blink_hz")
    assert result.ok
    assert result.friendly == f"led.blink_hz = {VALUES['led.blink_hz']}"
    assert result.raw_sent != ""
    assert "led.blink_hz" in result.raw_sent
    assert result.raw_recv != ""


def test_get_unknown_key_returns_error(connected_device):
    dev, fake = connected_device
    before = len(fake.written)
    result = terminal.run(dev, "get no.such")
    assert not result.ok
    assert "unknown parameter" in result.friendly
    assert len(fake.written) == before


def test_get_wrong_arg_count_returns_usage_error(connected_device):
    dev, fake = connected_device
    before = len(fake.written)
    assert not terminal.run(dev, "get").ok
    assert not terminal.run(dev, "get a b").ok
    assert len(fake.written) == before


# --- set -------------------------------------------------------------------

def test_set_updates_value_and_returns_ok_with_raw_wire_lines(connected_device):
    dev, _ = connected_device
    result = terminal.run(dev, "set led.blink_hz 15")
    assert result.ok
    assert result.friendly == "OK: led.blink_hz = 15"
    assert result.raw_sent != ""
    assert result.raw_recv != ""
    assert dev.values()["led.blink_hz"] == 15


def test_set_out_of_range_returns_validation_error(connected_device):
    dev, fake = connected_device
    before = len(fake.written)
    result = terminal.run(dev, "set led.blink_hz 99")
    assert not result.ok
    assert "1..20" in result.friendly
    assert len(fake.written) == before
    assert dev.values()["led.blink_hz"] == VALUES["led.blink_hz"]


def test_set_non_integer_for_u8_returns_error(connected_device):
    dev, fake = connected_device
    before = len(fake.written)
    result = terminal.run(dev, "set led.blink_hz abc")
    assert not result.ok
    assert "integer" in result.friendly
    assert len(fake.written) == before


def test_set_unknown_key_returns_error(connected_device):
    dev, _ = connected_device
    result = terminal.run(dev, "set no.such 1")
    assert not result.ok
    assert "unknown parameter" in result.friendly


def test_set_unknown_enum_option_returns_error(connected_device):
    dev, _ = connected_device
    result = terminal.run(dev, "set led.mode purple")
    assert not result.ok
    assert "must be one of" in result.friendly


def test_set_wrong_arg_count_returns_usage_error(connected_device):
    dev, fake = connected_device
    before = len(fake.written)
    assert not terminal.run(dev, "set led.blink_hz").ok
    assert not terminal.run(dev, "set led.blink_hz 1 2").ok
    assert len(fake.written) == before


# --- save / defaults ---------------------------------------------------------

def test_save_calls_device_and_returns_ok(connected_device):
    dev, fake = connected_device
    before = len(fake.written)
    result = terminal.run(dev, "save")
    assert result.ok
    assert result.friendly == "OK: saved to flash"
    assert len(fake.written) == before + 1


def test_defaults_resets_and_returns_ok(connected_device):
    dev, _ = connected_device
    terminal.run(dev, "set led.blink_hz 15")
    result = terminal.run(dev, "defaults")
    assert result.ok
    assert result.friendly == "OK: reset to defaults"
    assert dev.values() == VALUES


def test_save_and_defaults_reject_extra_args(connected_device):
    dev, _ = connected_device
    assert not terminal.run(dev, "save now").ok
    assert not terminal.run(dev, "defaults now").ok


def test_revert_reloads_from_flash(connected_device):
    dev, _ = connected_device
    terminal.run(dev, "set led.blink_hz 15")
    result = terminal.run(dev, "revert")
    assert result.ok
    assert result.friendly == "OK: reloaded settings from flash"
    assert dev.values() == VALUES


def test_revert_says_so_when_it_fell_back_to_defaults():
    fake = FakeSerial(responder=device_responder(revert_src="defaults"))
    dev = DeviceModel(SerialLink(open_port=lambda p: fake))
    dev.connect("/dev/fake")
    try:
        result = terminal.run(dev, "revert")
        assert result.ok
        assert result.friendly == (
            "OK: no saved settings on this board — loaded defaults instead")
    finally:
        dev.disconnect()


def test_revert_rejects_extra_args(connected_device):
    dev, _ = connected_device
    assert not terminal.run(dev, "revert now").ok


# --- dump ---------------------------------------------------------------------

def test_dump_returns_parseable_ini_covering_every_setting(connected_device):
    dev, _ = connected_device
    result = terminal.run(dev, "dump")
    assert result.ok
    assert dict(parse_ini(result.friendly)) == {
        k: str(v) for k, v in VALUES.items()}


def test_dump_reads_the_device_rather_than_the_cache(connected_device):
    """One `getall` on the wire: a dump is meant to be the device's truth, and
    the raw-JSON toggle needs real lines to show."""
    dev, fake = connected_device
    before = len(fake.written)
    result = terminal.run(dev, "dump")
    assert len(fake.written) == before + 1
    assert "getall" in result.raw_sent
    assert result.raw_recv != ""


def test_dump_rejects_extra_args(connected_device):
    dev, fake = connected_device
    before = len(fake.written)
    assert not terminal.run(dev, "dump now").ok
    assert len(fake.written) == before


# --- list ---------------------------------------------------------------------

def test_list_describes_every_setting_and_touches_no_wire_traffic(connected_device):
    dev, fake = connected_device
    before = len(fake.written)
    result = terminal.run(dev, "list")
    assert result.ok
    assert len(fake.written) == before        # schema is already cached
    assert result.raw_sent == ""

    for spec in SCHEMA:
        assert spec["key"] in result.friendly
        assert str(spec["def"]) in result.friendly
        if spec["type"] == "u8":
            assert f"{spec['min']}..{spec['max']}" in result.friendly
        elif spec["type"] == "enum":
            assert "|".join(spec["options"]) in result.friendly
        elif spec["type"] == "str":
            assert f"max {spec['maxlen']}" in result.friendly


def test_list_groups_settings_under_their_schema_group(connected_device):
    dev, _ = connected_device
    friendly = terminal.run(dev, "list").friendly
    for group in {spec["group"] for spec in SCHEMA}:
        assert group in friendly


def test_list_rejects_extra_args(connected_device):
    dev, _ = connected_device
    assert not terminal.run(dev, "list all").ok


# --- misc --------------------------------------------------------------------

def test_unknown_command_returns_error(connected_device):
    dev, _ = connected_device
    result = terminal.run(dev, "foo bar")
    assert result.friendly == "ERROR: unknown command 'foo'. Type 'help' for a list."


@pytest.mark.parametrize("command", ["", "   "])
def test_empty_command_returns_error(connected_device, command):
    dev, _ = connected_device
    result = terminal.run(dev, command)
    assert not result.ok


def test_command_while_disconnected_returns_friendly_error_not_exception():
    fake = FakeSerial(responder=device_responder())
    dev = DeviceModel(SerialLink(open_port=lambda p: fake))
    result = terminal.run(dev, "get led.blink_hz")
    assert not result.ok
    assert "ERROR" in result.friendly


# --- route wiring --------------------------------------------------------------

@pytest.fixture
def client():
    fake = FakeSerial(responder=device_responder())
    device = DeviceModel(SerialLink(open_port=lambda p: fake))
    app = create_app(device)
    with TestClient(app) as c:
        c.post("/api/connect", json={"port": "/dev/fake"})
        yield c
    device.disconnect()


def test_terminal_endpoint_help_returns_200(client):
    r = client.post("/api/terminal", json={"command": "help"})
    assert r.status_code == 200
    body = r.json()
    assert body["ok"] is True
    assert "help" in body["friendly"]


def test_terminal_endpoint_set_then_get_round_trip(client):
    r = client.post("/api/terminal", json={"command": "set led.blink_hz 7"})
    body = r.json()
    assert body["ok"] is True
    assert body["friendly"] == "OK: led.blink_hz = 7"
    assert body["raw_sent"] != ""
    assert body["raw_recv"] != ""

    r = client.post("/api/terminal", json={"command": "get led.blink_hz"})
    body = r.json()
    assert body["ok"] is True
    # The fake serial responder is stateless (always answers from its fixed
    # VALUES map), so a fresh wire "get" reflects the fixture's static value,
    # not the just-applied set -- this exercises the response shape and wire
    # round trip, not device-side persistence (already covered by
    # test_set_updates_value_and_returns_ok_with_raw_wire_lines's cache check).
    assert body["friendly"] == f"led.blink_hz = {VALUES['led.blink_hz']}"
    assert body["raw_sent"] != ""
    assert body["raw_recv"] != ""


def test_terminal_endpoint_command_error_still_returns_200(client):
    r = client.post("/api/terminal", json={"command": "foo"})
    assert r.status_code == 200
    body = r.json()
    assert body["ok"] is False
    assert "unknown command" in body["friendly"]


def test_commands_report_disconnection_rather_than_unknown_key():
    """A disconnected device has an empty schema cache, so DeviceModel's own
    checks would call every key 'unknown parameter' -- true of the cache, but a
    lie about what is actually wrong. Matters more now that the Terminal page is
    reachable while disconnected."""
    dev = DeviceModel(SerialLink(open_port=lambda p: None))
    assert dev.status()["state"] != "connected"

    for command in ("get led.mode", "set led.mode on", "save", "defaults",
                    "list", "dump"):
        result = terminal.run(dev, command)
        assert result.ok is False, command
        assert "not connected" in result.friendly, command
        assert result.raw_sent == "", command


def test_help_still_works_while_disconnected():
    """help is pure text and must not require a device."""
    dev = DeviceModel(SerialLink(open_port=lambda p: None))
    result = terminal.run(dev, "help")
    assert result.ok is True
    assert "get <key>" in result.friendly
