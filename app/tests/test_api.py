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
    fake = FakeSerial(responder=device_responder(caps=("dfu", "wifiscan")))
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
        "device.name", "tlm.rate",
        "esc0.direction", "esc0.rate", "esc0.mode", "esc0.throttle_us", "esc0.min_us", "esc0.max_us", "esc0.src",
        "esc1.direction", "esc1.rate", "esc1.mode", "esc1.throttle_us", "esc1.min_us", "esc1.max_us", "esc1.src",
        "rx.protocol", "rx.source", "crossfire.timeout_ms", "elrs.timeout_ms", "rx.deadband_us",
        "tank_drive.throttle_src", "tank_drive.steer_src",
        "tank_drive.forward_ratio", "tank_drive.reverse_ratio", "tank_drive.steer_ratio",
        "tank_drive.arm_src", "tank_drive.arm_min", "tank_drive.arm_max"}
    # Telemetry descriptor rides along in the same response, so the UI renders
    # its cards from the device rather than a hardcoded field list.
    assert {t["key"] for t in schema["tlm"]} == {
        "up", "clk", "ram", "temp", "vdd", "fault", "loop", "loopworst",
        "esc0", "arm0", "esc1", "arm1", "drv_l", "drv_r",
        "link", "lq", "rssi", "rate", "err", "rfrate", "pwr",
        "ch1", "ch2", "ch3", "ch4", "ch5", "ch6", "ch7", "ch8",
        "ch9", "ch10", "ch11", "ch12", "ch13", "ch14", "ch15", "ch16"}
    # Every item carries a group, so the form and the telemetry page can build
    # sections without inventing headings.
    assert all(p.get("group") for p in schema["params"])
    assert all(t.get("group") for t in schema["tlm"])

    assert client.get("/api/params").json()["rx.deadband_us"] == 2


def test_valid_set_returns_200_and_updates(client):
    client.post("/api/connect", json={"port": "/dev/fake"})
    assert client.put("/api/params/rx.deadband_us", json={"val": 15}).status_code == 200
    assert client.get("/api/params").json()["rx.deadband_us"] == 15


def test_a_show_if_hidden_param_is_still_settable():
    """showIf is a display hint, not an access rule.

    This test wires its own minimal fake device rather than leaning on the
    shipped schema, so the case stays pinned to one param whatever the board
    exposes: mode boots at "off", so hidden (showIf mode == on) is not drawn. It
    must still be settable -- an INI restore writes it regardless and a
    Terminal `set` knows nothing about what the browser is rendering.
    """
    schema = [
        {"key": "x.mode", "type": "enum", "options": ["off", "on"],
         "def": "off", "label": "Mode", "group": "X"},
        {"key": "x.hidden", "type": "u8", "min": 0, "max": 9, "def": 0,
         "label": "Hidden", "group": "X",
         "showIf": {"key": "x.mode", "val": "on"}},
    ]
    values = {"x.mode": "off", "x.hidden": 0}

    def responder(req, emit):
        op, rid = req["op"], req["id"]
        if op == "hello":
            emit({"id": rid, "ok": True, "fw": "betacrawler 1.0.0", "proto": 1,
                  "board": "blackpill_f411ce"})
        elif op == "schema":
            emit({"id": rid, "ok": True, "params": schema, "tlm": []})
        elif op == "getall":
            emit({"id": rid, "ok": True, "vals": dict(values)})
        elif op == "set":
            values[req["key"]] = req["val"]
            emit({"id": rid, "ok": True})
        else:
            emit({"id": rid, "ok": False, "err": "badop"})

    fake = FakeSerial(responder=responder)
    device = DeviceModel(SerialLink(open_port=lambda p: fake))
    app = create_app(device)
    with TestClient(app) as c:
        c.post("/api/connect", json={"port": "/dev/fake"})
        assert c.put("/api/params/x.hidden", json={"val": 4}).status_code == 200
        assert c.get("/api/params").json()["x.hidden"] == 4
    device.disconnect()


def test_out_of_range_set_returns_400_with_code(client):
    client.post("/api/connect", json={"port": "/dev/fake"})
    r = client.put("/api/params/rx.deadband_us", json={"val": 999})
    assert r.status_code == 400
    assert r.json()["err"] == "range"


def test_set_rejects_bool_and_float_coercion(client):
    """ValueBody uses StrictInt | StrictStr so `true` and `5.0` are rejected
    (422) rather than silently coerced to 1 / 5 -- see main.py's ValueBody."""
    client.post("/api/connect", json={"port": "/dev/fake"})
    assert client.put("/api/params/rx.deadband_us", json={"val": True}).status_code == 422
    assert client.put("/api/params/rx.deadband_us", json={"val": 5.0}).status_code == 422


def test_unknown_key_returns_400(client):
    client.post("/api/connect", json={"port": "/dev/fake"})
    r = client.put("/api/params/no.such", json={"val": 1})
    assert r.status_code == 400
    assert r.json()["err"] == "nokey"


def test_set_while_disconnected_returns_409(client):
    assert client.put("/api/params/rx.deadband_us", json={"val": 5}).status_code == 409


def test_save_and_defaults(client):
    client.post("/api/connect", json={"port": "/dev/fake"})
    assert client.post("/api/params/save").status_code == 200
    assert client.post("/api/params/defaults").status_code == 200


def test_revert_returns_the_source_and_values(client):
    client.post("/api/connect", json={"port": "/dev/fake"})
    resp = client.post("/api/params/revert")
    assert resp.status_code == 200
    body = resp.json()
    assert body["ok"] is True
    assert body["src"] == "flash"
    assert body["vals"]["rx.protocol"] == "elrs"


# --- restore from INI ---------------------------------------------------------

GOOD_INI = """
; a settings backup
[rx]
protocol = crossfire
deadband_us = 9

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
    assert set(body["applied"]) == {"rx.protocol", "rx.deadband_us", "device.name"}
    assert body["skipped"] == []
    # One `set` per key actually reached the device.
    assert wire_ops(fake)[before:] == ["set", "set", "set"]
    assert body["vals"]["rx.deadband_us"] == 9
    assert body["vals"]["device.name"] == "bench rig"


def test_restore_coerces_values_through_the_schema(client):
    """INI values are text; a u8 must arrive at the device as a number."""
    client.post("/api/connect", json={"port": "/dev/fake"})
    client.post("/api/params/restore", json={"ini": "[rx]\ndeadband_us = 9\n"})
    sent = [json.loads(line.decode()) for line in client.app.state.fake.written]
    last = sent[-1]
    assert last["op"] == "set"
    assert last["val"] == 9 and isinstance(last["val"], int)


def test_restore_skips_unknown_keys_but_applies_the_rest(client):
    """Restoring a dump from a board with more modules enabled is normal --
    the extra keys are reported, not fatal."""
    client.post("/api/connect", json={"port": "/dev/fake"})
    ini = "[nosuch]\nmode = armed\n\n[rx]\ndeadband_us = 9\n"
    body = client.post("/api/params/restore", json={"ini": ini}).json()
    assert body["ok"] is False
    assert body["applied"] == ["rx.deadband_us"]
    assert [s["key"] for s in body["skipped"]] == ["nosuch.mode"]
    assert "unknown parameter" in body["skipped"][0]["reason"]


def test_restore_skips_an_invalid_value_but_applies_the_rest(client):
    client.post("/api/connect", json={"port": "/dev/fake"})
    ini = "[rx]\ndeadband_us = 999\nprotocol = crossfire\n"  # deadband_us max is 200
    body = client.post("/api/params/restore", json={"ini": ini}).json()
    assert body["ok"] is False
    assert body["applied"] == ["rx.protocol"]
    assert body["skipped"][0]["key"] == "rx.deadband_us"
    assert "0..200" in body["skipped"][0]["reason"]


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
    assert len(body["applied"]) == 4


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


def test_device_log_line_reaches_the_websocket_as_a_log_frame(client):
    """Boot health depends on this whole path working.

    The firmware replays its boot record as unsolicited `{"log": ...}` lines
    after every `hello`, so a host that connects long after boot still sees
    what happened. That is only useful if the line survives the reader thread,
    the broadcaster bridge and the WS encoding as a distinct `log` frame --
    the UI shows those in the Terminal, separately from telemetry.
    """
    client.post("/api/connect", json={"port": "/dev/fake"})
    fake = client.app.state.fake

    with client.websocket_connect("/ws") as ws:
        assert ws.receive_json()["type"] == "state"
        fake.emit({"log": "boot: betacrawler 1.0.0 (blackpill_f411ce)"})

        # Same bounded-wait pattern as the broadcaster test above: a dropped
        # message must fail in 2s rather than hang the run.
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
            pytest.fail("device log line never arrived on the websocket")
        assert kind == "ok", f"receive_json raised: {payload!r}"
        assert payload == {
            "type": "log",
            "data": "boot: betacrawler 1.0.0 (blackpill_f411ce)",
        }


def test_wifi_scan_result_reaches_the_websocket_as_a_scan_frame(client):
    """Covers the other half of WiFi scan: the arming route is tested below,
    but nothing previously exercised the push side -- the firmware's
    unsolicited `{"scan": [...]}` line (wifi_driver.cpp's pollPush()) has to
    survive the same reader-thread -> broadcaster -> WS path as a `log` line
    above and come out the other end as a distinct `scan` frame, which is
    what app.js's onScanEvent() actually listens for.
    """
    client.post("/api/connect", json={"port": "/dev/fake"})
    fake = client.app.state.fake

    with client.websocket_connect("/ws") as ws:
        assert ws.receive_json()["type"] == "state"
        fake.emit({"scan": [{"ssid": "Home", "rssi": -52}]})

        # Same bounded-wait pattern as the log-frame test above: a dropped
        # message must fail in 2s rather than hang the run.
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
            pytest.fail("wifi scan result line never arrived on the websocket")
        assert kind == "ok", f"receive_json raised: {payload!r}"
        assert payload == {
            "type": "scan",
            "data": [{"ssid": "Home", "rssi": -52}],
        }


# --- WiFi scan -----------------------------------------------------------------

def test_wifi_scan_requires_a_connection(client):
    r = client.post("/api/wifi/scan")
    assert r.status_code == 409
    assert r.json()["err"] == "disconnected"


def test_wifi_scan_arms_a_scan(client):
    client.post("/api/connect", json={"port": "/dev/fake"})
    fake = client.app.state.fake
    r = client.post("/api/wifi/scan")
    assert r.status_code == 200
    assert r.json()["ok"] is True
    # Not just "the route answered ok" -- the `wifiscan` op must actually have
    # gone out over the wire, so a route that skipped device.start_wifi_scan()
    # and just returned {"ok": True} itself would fail this.
    last_req = json.loads(fake.written[-1].decode())
    assert last_req["op"] == "wifiscan"
