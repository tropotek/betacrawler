import json
import queue
import threading

import pytest
from fastapi.testclient import TestClient

from backend.link import SerialLink
from backend.device import DeviceModel
from backend.main import create_app
from tests.fake_serial import FakeSerial
from tests.test_device import device_responder


@pytest.fixture
def client():
    fake = FakeSerial(responder=device_responder())
    device = DeviceModel(SerialLink(open_port=lambda p: fake))
    app = create_app(device)
    app.state.fake = fake  # exposed so tests can drive unsolicited messages
    with TestClient(app) as c:
        yield c
    device.disconnect()


def test_status_starts_disconnected(client):
    assert client.get("/api/status").json()["state"] == "disconnected"


def test_ports_listing_is_a_list(client):
    assert isinstance(client.get("/api/ports").json(), list)


def test_connect_then_schema_and_params(client):
    assert client.post("/api/connect", json={"port": "/dev/fake"}).status_code == 200
    assert client.get("/api/status").json()["state"] == "connected"

    schema = client.get("/api/schema").json()
    assert {p["key"] for p in schema["params"]} == {
        "led.mode", "led.blink_hz", "device.name", "tlm.rate",
        "disp.mode", "disp.page", "disp.rate"}
    # Telemetry descriptor rides along in the same response, so the UI renders
    # its cards from the device rather than a hardcoded field list.
    assert {t["key"] for t in schema["tlm"]} == {
        "up", "clk", "ram", "temp", "vdd", "btn"}
    # Every item carries a group, so the form and the telemetry page can build
    # sections without inventing headings.
    assert all(p.get("group") for p in schema["params"])
    assert all(t.get("group") for t in schema["tlm"])

    assert client.get("/api/params").json()["led.blink_hz"] == 2


def test_valid_set_returns_200_and_updates(client):
    client.post("/api/connect", json={"port": "/dev/fake"})
    assert client.put("/api/params/led.blink_hz", json={"val": 15}).status_code == 200
    assert client.get("/api/params").json()["led.blink_hz"] == 15


def test_out_of_range_set_returns_400_with_code(client):
    client.post("/api/connect", json={"port": "/dev/fake"})
    r = client.put("/api/params/led.blink_hz", json={"val": 99})
    assert r.status_code == 400
    assert r.json()["err"] == "range"


def test_set_rejects_bool_and_float_coercion(client):
    """ValueBody uses StrictInt | StrictStr so `true` and `5.0` are rejected
    (422) rather than silently coerced to 1 / 5 -- see main.py's ValueBody."""
    client.post("/api/connect", json={"port": "/dev/fake"})
    assert client.put("/api/params/led.blink_hz", json={"val": True}).status_code == 422
    assert client.put("/api/params/led.blink_hz", json={"val": 5.0}).status_code == 422


def test_unknown_key_returns_400(client):
    client.post("/api/connect", json={"port": "/dev/fake"})
    r = client.put("/api/params/no.such", json={"val": 1})
    assert r.status_code == 400
    assert r.json()["err"] == "nokey"


def test_set_while_disconnected_returns_409(client):
    assert client.put("/api/params/led.blink_hz", json={"val": 5}).status_code == 409


def test_save_and_defaults(client):
    client.post("/api/connect", json={"port": "/dev/fake"})
    assert client.post("/api/params/save").status_code == 200
    assert client.post("/api/params/defaults").status_code == 200


# --- restore from INI ---------------------------------------------------------

GOOD_INI = """
; a settings backup
[led]
mode = on
blink_hz = 9

[device]
name = bench rig
"""


def wire_ops(fake):
    """Every op the fake device was asked to perform, in order."""
    return [json.loads(line.decode())["op"] for line in fake.written]


def test_restore_applies_every_key_and_reports_them(client):
    client.post("/api/connect", json={"port": "/dev/fake"})
    fake = client.app.state.fake
    before = len(fake.written)

    r = client.post("/api/params/restore", json={"ini": GOOD_INI})
    assert r.status_code == 200
    body = r.json()
    assert body["ok"] is True
    assert set(body["applied"]) == {"led.mode", "led.blink_hz", "device.name"}
    assert body["skipped"] == []
    # One `set` per key actually reached the device.
    assert wire_ops(fake)[before:] == ["set", "set", "set"]
    assert body["vals"]["led.blink_hz"] == 9
    assert body["vals"]["device.name"] == "bench rig"


def test_restore_coerces_values_through_the_schema(client):
    """INI values are text; a u8 must arrive at the device as a number."""
    client.post("/api/connect", json={"port": "/dev/fake"})
    client.post("/api/params/restore", json={"ini": "[led]\nblink_hz = 9\n"})
    sent = [json.loads(line.decode()) for line in client.app.state.fake.written]
    last = sent[-1]
    assert last["op"] == "set"
    assert last["val"] == 9 and isinstance(last["val"], int)


def test_restore_skips_unknown_keys_but_applies_the_rest(client):
    """Restoring a dump from a board with more modules enabled is normal --
    the extra keys are reported, not fatal."""
    client.post("/api/connect", json={"port": "/dev/fake"})
    ini = "[wifi]\nssid = home\n\n[led]\nblink_hz = 9\n"
    body = client.post("/api/params/restore", json={"ini": ini}).json()
    assert body["ok"] is False
    assert body["applied"] == ["led.blink_hz"]
    assert [s["key"] for s in body["skipped"]] == ["wifi.ssid"]
    assert "unknown parameter" in body["skipped"][0]["reason"]


def test_restore_skips_an_invalid_value_but_applies_the_rest(client):
    client.post("/api/connect", json={"port": "/dev/fake"})
    ini = "[led]\nblink_hz = 99\nmode = on\n"        # blink_hz max is 20
    body = client.post("/api/params/restore", json={"ini": ini}).json()
    assert body["ok"] is False
    assert body["applied"] == ["led.mode"]
    assert body["skipped"][0]["key"] == "led.blink_hz"
    assert "1..20" in body["skipped"][0]["reason"]


def test_restore_does_not_write_to_flash(client):
    """Restore applies to RAM only; persisting stays an explicit Save, exactly
    like editing a field in the config form."""
    client.post("/api/connect", json={"port": "/dev/fake"})
    fake = client.app.state.fake
    client.post("/api/params/restore", json={"ini": GOOD_INI})
    assert "save" not in wire_ops(fake)


def test_restore_of_a_dump_round_trips_cleanly(client):
    client.post("/api/connect", json={"port": "/dev/fake"})
    dump = client.post("/api/terminal", json={"command": "dump"}).json()["friendly"]
    body = client.post("/api/params/restore", json={"ini": dump}).json()
    assert body["ok"] is True
    assert body["skipped"] == []
    assert len(body["applied"]) == 7


def test_restore_of_malformed_ini_returns_400(client):
    client.post("/api/connect", json={"port": "/dev/fake"})
    fake = client.app.state.fake
    before = len(fake.written)
    r = client.post("/api/params/restore", json={"ini": "not an ini file"})
    assert r.status_code == 400
    assert r.json()["err"] == "badini"
    assert len(fake.written) == before          # nothing half-applied


def test_restore_while_disconnected_returns_409(client):
    r = client.post("/api/params/restore", json={"ini": GOOD_INI})
    assert r.status_code == 409
    assert r.json()["err"] == "disconnected"


def test_restore_of_an_empty_file_applies_nothing(client):
    client.post("/api/connect", json={"port": "/dev/fake"})
    body = client.post("/api/params/restore", json={"ini": "; empty\n"}).json()
    assert body["ok"] is False
    assert body["applied"] == []


def test_websocket_receives_telemetry(client):
    client.post("/api/connect", json={"port": "/dev/fake"})
    with client.websocket_connect("/ws") as ws:
        # the fake device only speaks when spoken to; nudge it
        client.get("/api/params")
        msg = ws.receive_json()
        assert msg["type"] in ("state", "tlm", "log")


def test_broadcaster_bridges_message_from_reader_thread(client):
    """Genuine test of the sync-thread -> asyncio bridge.

    The device_responder only replies to request/response traffic, which never
    touches Broadcaster.publish_threadsafe (that path is exercised only by
    unsolicited messages the reader thread hands to subscribers). Here we make
    the FakeSerial "volunteer" a line with no `id`, which SerialLink._read_loop
    treats as telemetry/log and publishes to subscribers *from the reader
    thread*. DeviceModel forwards that straight to
    Broadcaster.publish_threadsafe, which must cross into the FastAPI event
    loop via asyncio.run_coroutine_threadsafe using the loop captured at
    lifespan startup. If that loop reference were missing, stale, or captured
    before the loop existed, this message would silently vanish and the
    ws.receive_json() below would hang until the timeout fires.
    """
    client.post("/api/connect", json={"port": "/dev/fake"})
    fake = client.app.state.fake

    with client.websocket_connect("/ws") as ws:
        initial = ws.receive_json()
        assert initial["type"] == "state"

        # Emitted with no "id" -> the reader thread treats it as unsolicited
        # and calls subscribers directly from that (non-asyncio) thread.
        fake.emit({"tlm": {"uptime_ms": 42}})

        # ws.receive_json() has no timeout parameter and blocks forever on a
        # dropped message, so it's run on a *daemon* thread and joined via a
        # bounded queue.get() instead: if the bridge silently drops the
        # message, this test fails after 2s rather than hanging the run.
        # (A ThreadPoolExecutor was tried first and rejected: its shutdown
        # -- both the `with` block's and the interpreter's atexit hook --
        # joins worker threads unconditionally, so a stuck receive_json()
        # call hangs the whole test process even after the timeout fires.)
        result: "queue.Queue" = queue.Queue(maxsize=1)

        def _recv():
            try:
                result.put(("ok", ws.receive_json()))
            except Exception as exc:  # pragma: no cover - defensive
                result.put(("err", exc))

        threading.Thread(target=_recv, daemon=True).start()
        try:
            kind, payload = result.get(timeout=2.0)
        except queue.Empty:
            pytest.fail(
                "no message arrived on the websocket within 2s -- the "
                "reader-thread -> asyncio bridge (Broadcaster.publish_threadsafe "
                "/ run_coroutine_threadsafe) dropped the telemetry message"
            )
        assert kind == "ok", f"receive_json raised: {payload!r}"
        assert payload == {"type": "tlm", "data": {"uptime_ms": 42}}
