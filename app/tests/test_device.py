import pytest

from backend.link import SerialLink
from backend.device import DeviceModel, ProtoMismatch, DeviceError
from tests.fake_serial import FakeSerial

SCHEMA = [
    {"key": "led.mode", "type": "enum", "options": ["off", "on", "blink"],
     "def": "blink", "label": "LED Mode"},
    {"key": "led.blink_hz", "type": "u8", "min": 1, "max": 20, "def": 2,
     "label": "Blink Rate", "unit": "Hz"},
    {"key": "device.name", "type": "str", "maxlen": 31, "def": "app-demo",
     "label": "Device Name"},
    {"key": "tlm.rate", "type": "u8", "min": 1, "max": 50, "def": 10,
     "label": "Telemetry Rate", "unit": "Hz"},
]
VALUES = {"led.mode": "blink", "led.blink_hz": 2,
          "device.name": "app-demo", "tlm.rate": 10}


def device_responder(proto=1):
    def responder(req, emit):
        op = req["op"]
        rid = req["id"]
        if op == "hello":
            emit({"id": rid, "ok": True, "fw": "app-demo 0.1.0",
                  "proto": proto, "board": "blackpill_f411ce"})
        elif op == "schema":
            emit({"id": rid, "ok": True, "params": SCHEMA})
        elif op == "getall":
            emit({"id": rid, "ok": True, "vals": dict(VALUES)})
        elif op == "set":
            emit({"id": rid, "ok": True})
        elif op in ("save", "defaults"):
            emit({"id": rid, "ok": True})
        else:
            emit({"id": rid, "ok": False, "err": "badop"})
    return responder


def make_device(proto=1):
    fake = FakeSerial(responder=device_responder(proto))
    return DeviceModel(SerialLink(open_port=lambda p: fake)), fake


def test_connect_caches_schema_and_values():
    dev, _ = make_device()
    dev.connect("/dev/fake")
    try:
        assert dev.status()["state"] == "connected"
        assert dev.status()["proto"] == 1
        assert len(dev.schema()) == 4
        assert dev.values()["led.blink_hz"] == 2
    finally:
        dev.disconnect()


def test_proto_mismatch_refuses_connection():
    dev, _ = make_device(proto=99)
    with pytest.raises(ProtoMismatch):
        dev.connect("/dev/fake")
    assert dev.status()["state"] == "disconnected"


def test_set_validates_against_cached_schema_before_sending():
    dev, fake = make_device()
    dev.connect("/dev/fake")
    try:
        before = len(fake.written)
        with pytest.raises(DeviceError) as exc:
            dev.set("led.blink_hz", 99)          # max is 20
        assert exc.value.code == "range"
        assert len(fake.written) == before        # nothing hit the wire
    finally:
        dev.disconnect()


def test_set_rejects_unknown_enum_option():
    dev, _ = make_device()
    dev.connect("/dev/fake")
    try:
        with pytest.raises(DeviceError) as exc:
            dev.set("led.mode", "purple")
        assert exc.value.code == "enum"
    finally:
        dev.disconnect()


def test_set_rejects_overlong_string():
    dev, _ = make_device()
    dev.connect("/dev/fake")
    try:
        with pytest.raises(DeviceError) as exc:
            dev.set("device.name", "x" * 40)
        assert exc.value.code == "toolong"
    finally:
        dev.disconnect()


def test_set_unknown_key_rejected():
    dev, _ = make_device()
    dev.connect("/dev/fake")
    try:
        with pytest.raises(DeviceError) as exc:
            dev.set("no.such.key", 1)
        assert exc.value.code == "nokey"
    finally:
        dev.disconnect()


def test_successful_set_updates_the_cache():
    dev, _ = make_device()
    dev.connect("/dev/fake")
    try:
        dev.set("led.blink_hz", 15)
        assert dev.values()["led.blink_hz"] == 15
    finally:
        dev.disconnect()


def test_handshake_timeout_raises_device_error():
    """Timeout during hello/schema/getall handshake wraps in DeviceError."""
    def silent_responder(req, emit):
        # Never reply — forces timeout
        pass

    fake = FakeSerial(responder=silent_responder)
    dev = DeviceModel(SerialLink(open_port=lambda p: fake))
    with pytest.raises(DeviceError) as exc:
        dev.connect("/dev/fake")
    assert exc.value.code == "timeout"
    assert dev.status()["state"] == "disconnected"


def test_connect_port_open_failure_raises_device_error():
    """Port-open failure wraps in DeviceError."""
    def bad_open(port):
        raise IOError(f"Port {port} not found")

    dev = DeviceModel(SerialLink(open_port=bad_open))
    with pytest.raises(DeviceError) as exc:
        dev.connect("/dev/nonexistent")
    assert exc.value.code == "connect_failed"
    assert "Port /dev/nonexistent not found" in str(exc.value)
    assert dev.status()["state"] == "disconnected"
